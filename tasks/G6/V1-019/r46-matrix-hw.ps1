# 轮 46 · P1 矩阵补 1 格：Win11 24H2（及之后）× 单屏 100% × 硬编 mf-h264-hw
#
# 三件套沿用 docs/11b §6 / verify/cases/g5_r42_matrix.ps1 的口径：
#   ① 录 60s（--display 0 只采主屏，--encoder mf-h264-hw 强制硬编）
#   ② 抽帧比对（n=30/900/1770 三帧非全黑 + 原始尺寸稳定）
#   ③ 音画尾偏差 + 容器可解码（duration / 双轨 / ffmpeg -f null 零错误）
# 额外硬编判据：recorder stdout 的 encoder 必须是 mf-h264-hw，不是静默回退到软编。
#
# Usage: pwsh -File tasks/G6/V1-019/r46-matrix-hw.ps1

param([string]$BuildDir = 'build-msvc', [string]$Config = 'Release')

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\..\..\verify\lib\Cleanup.ps1')
$root = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$rec = Join-Path $root (Join-Path $BuildDir "bin\$Config\sr-recorder.exe")
if (-not (Test-Path $rec)) { Write-Host "FAIL: recorder missing: $rec"; exit 2 }
$work = Join-Path $env:TEMP 'sr-r46-hw-matrix'
if (Test-Path $work) { Get-ChildItem $work -Recurse -Force | Remove-Item -Force -Recurse -ErrorAction SilentlyContinue }
New-Item -ItemType Directory -Force -Path $work | Out-Null

$script:pass = 0; $script:fail = 0; $script:skip = 0
function Assert {
    param([string]$Name, [bool]$Ok, [string]$Detail)
    if ($Ok) { $script:pass++; Write-Host ("  PASS {0} — {1}" -f $Name, $Detail) }
    else { $script:fail++; Write-Host ("  FAIL {0} — {1}" -f $Name, $Detail) }
}
function Field {
    param([string]$Text, [string]$Name)
    if ($Text -match "(?m)^$Name=([^`r`n]*)") { return $Matches[1].Trim() }
    return ''
}
$ffbin = Split-Path -Parent (Get-Command ffprobe).Source
$ffprobe = Join-Path $ffbin 'ffprobe.exe'
$ffmpeg = Join-Path $ffbin 'ffmpeg.exe'

Write-Host '--- 环境（真实读取，不推断）---'
$os = Get-CimInstance Win32_OperatingSystem
Add-Type -AssemblyName System.Windows.Forms
$screens = @([System.Windows.Forms.Screen]::AllScreens)
$logPixels = (Get-ItemProperty 'HKCU:\Control Panel\Desktop' -Name LogPixels -ErrorAction SilentlyContinue).LogPixels
$scale = if ($logPixels -and $logPixels -gt 0) { [Math]::Round($logPixels / 96 * 100) } else { 100 }
Assert 'r46-hw-env-win11-100pct' ($os.Caption -match 'Windows 11' -and $scale -eq 100) `
    ("OS={0} build={1}；主屏={2}×{3}，物理屏数={4}；DPI 缩放={5}%（降级验证：物理双屏用 --display 0 只采主屏）" -f `
     $os.Caption, $os.BuildNumber, $screens[0].Bounds.Width, $screens[0].Bounds.Height, $screens.Count, $scale)

Write-Host '--- 三件套 1/3：录 60s（--display 0 单屏 + --encoder mf-h264-hw）---'
Clear-SrProcesses -IncludeNameFallback
Start-Sleep -Milliseconds 300
$clip = Join-Path $work 'matrix-hw-60s.mkv'
$out = & $rec --record --seconds 60 --fps 30 --bitrate 6000 --display 0 `
    --encoder mf-h264-hw --out $clip 2>&1 | Out-String
$exit = [int](Field $out 'record_exit')
$wall = [double](Field $out 'wall_seconds')
$encoded = [double](Field $out 'frames_encoded')
$dropped = [double](Field $out 'frames_dropped')
$vpts = [double](Field $out 'video_last_pts_ms')
$apts = [double](Field $out 'audio_last_pts_ms')
$encoder = Field $out 'encoder'
Assert 'r46-hw-record-60s' ($exit -eq 0 -and $wall -ge 60 -and $encoded -ge 1800 -and (Test-Path $clip)) `
    ("record_exit={0} wall={1}s frames_encoded={2} frames_dropped={3} size={4} B" -f `
     $exit, $wall, $encoded, $dropped, $(if (Test-Path $clip) { (Get-Item $clip).Length } else { 0 }))
Assert 'r46-hw-encoder-really-hardware' ($encoder -eq 'mf-h264-hw') `
    ("encoder={0}（必须真命中硬编，不是静默回退 mf-h264-sw）" -f $encoder)

Write-Host '--- 三件套 2/3：抽帧比对（首/中/末三帧，非全黑 + 尺寸一致）---'
$frameInfo = @()
foreach ($idx in @(30, 900, 1770)) {
    $raw = Join-Path $work "frame-$idx.raw"
    if (Test-Path $raw) { Remove-Item $raw -Force }
    & $ffmpeg -hide_banner -v error -i $clip -vf "select=eq(n\,$idx)" -fps_mode passthrough `
        -frames:v 1 -f rawvideo -pix_fmt rgb24 $raw 2>$null
    if (-not (Test-Path $raw)) { $frameInfo += [pscustomobject]@{ idx = $idx; ok = $false; nonblack = 0; bytes = 0 }; continue }
    $bytes = [System.IO.File]::ReadAllBytes($raw)
    $nonblack = 0
    $step = 97 * 3
    for ($i = 0; $i -lt $bytes.Length - $step; $i += $step) {
        if ($bytes[$i] -gt 20 -or $bytes[$i + 1] -gt 20 -or $bytes[$i + 2] -gt 20) { $nonblack++ }
    }
    $frameInfo += [pscustomobject]@{ idx = $idx; ok = $true; nonblack = $nonblack; bytes = $bytes.Length }
}
$frameOk = @($frameInfo | Where-Object { $_.ok -and $_.nonblack -gt 0 }).Count
Assert 'r46-hw-frame-nonblack' ($frameOk -eq 3) `
    ("三帧都非全黑且可抽取（{0}/3）：{1}" -f $frameOk,
     (($frameInfo | ForEach-Object { "n=$($_.idx):nonblack=$($_.nonblack)" }) -join ', '))
$sizes = @($frameInfo | ForEach-Object { $_.bytes } | Select-Object -Unique)
Assert 'r46-hw-frame-size-stable' ($sizes.Count -eq 1 -and $sizes[0] -gt 0) `
    ("三帧原始字节数一致（唯一尺寸数={0}，bytes={1}）" -f $sizes.Count, $sizes[0])

Write-Host '--- 三件套 3/3：音画偏差 + 容器可解码 ---'
$avDelta = [Math]::Abs($vpts - $apts)
Assert 'r46-hw-av-sync' ($vpts -gt 0 -and $apts -gt 0 -and $avDelta -le 100) `
    ("video_last_pts_ms={0} audio_last_pts_ms={1} 音画尾偏差={2}ms（≤100ms）" -f $vpts, $apts, $avDelta)
$probe = & $ffprobe -v error -show_entries format=duration -show_entries stream=codec_type,width,height,avg_frame_rate `
    -of default=nw=1 $clip 2>&1 | Out-String
$decode = & $ffmpeg -hide_banner -v error -i $clip -f null - 2>&1 | Out-String
$duration = 0.0
if ($probe -match 'duration=([\d\.]+)') { $duration = [double]$Matches[1] }
Assert 'r46-hw-decodable' ([string]::IsNullOrWhiteSpace($decode) -and
        $probe -match 'codec_type=video' -and $probe -match 'codec_type=audio' -and
        [Math]::Abs($duration - 60) -le 0.5) `
    ("ffmpeg 全片解码零错误；duration={0}s；video+audio 双轨" -f $duration)

Clear-SrProcesses -IncludeNameFallback
Write-Host ''
Write-Host ("MATRIX_DATE={0}" -f (Get-Date -Format 'yyyy-MM-dd'))
Write-Host ("MATRIX_CELL=Win11 24H2 × 单屏 100% × 硬编 mf-h264-hw（本机 build {0}，降级验证）" -f $os.BuildNumber)
Write-Host ("MATRIX_VALUES=record_exit={0} encoder={1} wall={2}s frames={3} dropped={4} av_delta_ms={5} duration={6}s nonblack_frames={7}/3" -f `
    $exit, $encoder, $wall, $encoded, $dropped, $avDelta, $duration, $frameOk)
Write-Host ("RESULT: {0} PASS / {1} FAIL / {2} SKIP" -f $script:pass, $script:fail, $script:skip)
if ($script:fail -gt 0) { exit 1 }
exit 0
