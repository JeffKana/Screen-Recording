# G3 acceptance case: real playback / decode verification.
# Covers tasks/G3/S4-001 (M2-03 "停止后生成可播放文件", M3-07 "H.264 可解码").
#
# Unlike mkv_probe.py (which parses the container structurally), this case runs a
# real decoder: ffprobe reads the streams, ffmpeg decodes every frame, and the
# average color of sampled frames proves the picture is not blank.
#
# Usage: pwsh -File verify/cases/g3_playback.ps1 [-BuildDir build-mingw] [-Seconds 6]

param(
    [string]$BuildDir = 'build-mingw',
    [string]$Config = 'Release',
    [int]$Seconds = 6,
    [int]$Fps = 30
)

$ErrorActionPreference = 'Stop'

# 轮 30（F2）：统一异常清理——登记 spawn 进程 + trap 兜底，中途抛异常也不留孤儿。
. (Join-Path $PSScriptRoot '..\lib\Cleanup.ps1')
# trap 必须留在脚本自身作用域：dot-source 进来的 trap 不接管调用方的终止错误。
trap { Clear-SrProcesses -IncludeNameFallback; Write-Host "FAIL: $_"; exit 1 }

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$recorder = Join-Path $root (Join-Path $BuildDir "bin\$Config\sr-recorder.exe")
if (-not (Test-Path $recorder)) { $recorder = Join-Path $root (Join-Path $BuildDir 'bin\sr-recorder.exe') }
if (-not (Test-Path $recorder)) { Write-Host "FAIL: missing $recorder"; exit 2 }

function Resolve-FfmpegBin {
    $onPath = Get-Command ffprobe -ErrorAction SilentlyContinue
    if ($onPath) { return Split-Path -Parent $onPath.Source }

    $packages = Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Packages'
    if (Test-Path $packages) {
        $found = Get-ChildItem $packages -Recurse -Filter 'ffprobe.exe' -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($found) { return $found.DirectoryName }
    }
    return $null
}

$bin = Resolve-FfmpegBin
if (-not $bin) {
    Write-Host 'BLOCKED: ffmpeg/ffprobe not found. Install with: winget install Gyan.FFmpeg'
    exit 4
}
Write-Host "ffmpeg bin: $bin"

$out = Join-Path $root (Join-Path $BuildDir 'g3_playback.mkv')
# 录制器按 M5-15 会避让同名文件（改名为 _001），若上一轮产物还在，
# 本轮的成品会写到 _001，而脚本仍去读旧文件 → duration/frame-count 误判。
# 用例必须从干净目标开始。
Get-ChildItem (Split-Path -Parent $out) -Filter 'g3_playback*.mkv' -ErrorAction SilentlyContinue |
    Remove-Item -Force -ErrorAction SilentlyContinue
$avgFiles = @()

$script:pass = 0
$script:fail = 0

function Assert {
    param([string]$Name, [bool]$Ok, [string]$Detail)
    if ($Ok) {
        $script:pass++
        Write-Host ("  PASS {0} — {1}" -f $Name, $Detail)
    } else {
        $script:fail++
        Write-Host ("  FAIL {0} — {1}" -f $Name, $Detail)
    }
}

Write-Host "--- 录制 ${Seconds}s 样本 ---"
& $recorder --record --seconds $Seconds --fps $Fps --out $out | Select-Object -Last 3
if (-not (Test-Path $out)) { Write-Host 'FAIL: recorder produced no file'; exit 3 }

Write-Host '--- 容器与流信息 (ffprobe) ---'
$probe = & (Join-Path $bin 'ffprobe.exe') -v error -show_entries format=format_name,duration `
    -show_entries stream=codec_name,width,height,pix_fmt,r_frame_rate -of default=nw=1 $out 2>&1 | Out-String
Write-Host $probe.Trim()

function Field {
    param([string]$Text, [string]$Key)
    if ($Text -match "(?m)^$Key=(.*)$") { return $Matches[1].Trim() }
    return ''
}

$fmt = Field $probe 'format_name'
$duration = [double](Field $probe 'duration')
$codec = Field $probe 'codec_name'
$width = Field $probe 'width'
$height = Field $probe 'height'
$pixFmt = Field $probe 'pix_fmt'
$rate = Field $probe 'r_frame_rate'

Assert 'container' ($fmt -match 'matroska') "format_name=$fmt"
Assert 'codec' ($codec -eq 'h264') "codec_name=$codec"
Assert 'resolution' ($width -eq '1920' -and $height -eq '1080') "${width}x${height}"
Assert 'pixel-format' ($pixFmt -eq 'yuv420p') "pix_fmt=$pixFmt"
Assert 'frame-rate' ($rate -eq "$Fps/1") "r_frame_rate=$rate"
Assert 'duration' ([math]::Abs($duration - $Seconds) -lt 0.25) "duration=$duration (expect ~$Seconds)"

Write-Host '--- 全量解码 (ffmpeg -f null) ---'
$decode = & (Join-Path $bin 'ffmpeg.exe') -hide_banner -v error -i $out -f null - 2>&1 | Out-String
$decodeText = $decode.Trim()
Assert 'decode-clean' ([string]::IsNullOrEmpty($decodeText)) `
    $(if ($decodeText) { "decoder reported: $($decodeText.Substring(0, [Math]::Min(200, $decodeText.Length)))" } else { 'no decoder errors' })

Write-Host '--- 解码帧数 (ffprobe -count_frames) ---'
$countText = & (Join-Path $bin 'ffprobe.exe') -v error -count_frames `
    -select_streams v:0 -show_entries stream=nb_read_frames -of default=nw=1:nk=1 $out 2>&1 | Out-String
$frames = 0
[void][int]::TryParse($countText.Trim(), [ref]$frames)
$expected = $Seconds * $Fps
Assert 'frame-count' ([math]::Abs($frames - $expected) -le 2) "decoded=$frames expected=$expected"

Write-Host '--- 画面非空 (采样帧平均色) ---'
$samples = @(0, [int]($frames / 2), [Math]::Max(0, $frames - 1))
foreach ($index in $samples) {
    $avg = Join-Path $root (Join-Path $BuildDir "g3_avg_$index.rgb")
    & (Join-Path $bin 'ffmpeg.exe') -v error -i $out -vf "select=eq(n\,$index),scale=1:1" `
        -vframes 1 -y $avg -f rawvideo -pix_fmt rgb24 | Out-Null
    $avgFiles += $avg
    $bytes = [System.IO.File]::ReadAllBytes($avg)
    $sum = 0
    foreach ($b in $bytes) { $sum += $b }
    Assert "frame-$index-not-blank" ($sum -gt 30) "avg_rgb=$($bytes -join ',')"
}

Get-ChildItem $avgFiles -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue

Write-Host ''
Write-Host ("RESULT: {0} PASS / {1} FAIL" -f $script:pass, $script:fail)
if ($script:fail -gt 0) { exit 1 }
exit 0
