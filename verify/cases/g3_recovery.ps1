# G3 acceptance case: crash recovery (M8-01 / M8-02) + normal-path temp file handling (M5-04)
#
# 1) 正常录制：产出成品 .mkv，且不残留 .part
# 2) 强制结束进程：残留 .part → --scan 判定可修复 → --repair 产出可解码文件
# 3) 丢失时长必须 <= 10 秒（M8-01 的验收要点）
#
# Usage: pwsh -File verify/cases/g3_recovery.ps1 [-BuildDir build-mingw] [-Config Release]

param(
    [string]$BuildDir = 'build-msvc',
    [string]$Config = 'Release',
    [int]$KillAfterSeconds = 6
)

$ErrorActionPreference = 'Stop'

# 轮 30（F2）：统一异常清理——登记 spawn 进程 + trap 兜底，中途抛异常也不留孤儿。
. (Join-Path $PSScriptRoot '..\lib\Cleanup.ps1')
# trap 必须留在脚本自身作用域：dot-source 进来的 trap 不接管调用方的终止错误。
trap { Clear-SrProcesses -IncludeNameFallback; Write-Host "FAIL: $_"; exit 1 }

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$exe = Join-Path $root (Join-Path $BuildDir "bin\$Config\sr-recorder.exe")
if (-not (Test-Path $exe)) {
    $exe = Join-Path $root (Join-Path $BuildDir 'bin\sr-recorder.exe')
}
if (-not (Test-Path $exe)) { Write-Host "FAIL: sr-recorder.exe not found"; exit 2 }

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
$ffbin = Resolve-FfmpegBin
if (-not $ffbin) { Write-Host 'BLOCKED: ffmpeg/ffprobe not found'; exit 4 }

$script:pass = 0
$script:fail = 0
function Assert {
    param([string]$Name, [bool]$Ok, [string]$Detail)
    if ($Ok) { $script:pass++; Write-Host ("  PASS {0} — {1}" -f $Name, $Detail) }
    else { $script:fail++; Write-Host ("  FAIL {0} — {1}" -f $Name, $Detail) }
}

$work = Join-Path $root (Join-Path $BuildDir 'recovery')
New-Item -ItemType Directory -Force -Path $work | Out-Null
Get-ChildItem $work -File | Remove-Item -Force -ErrorAction SilentlyContinue

Write-Host '--- 正常路径：成品生成且无 .part 残留 (M5-04) ---'
$normal = Join-Path $work 'normal.mkv'
& $exe --record --seconds 3 --fps 30 --out $normal | Out-Null
$partLeft = Test-Path ($normal + '.part')
Assert 'normal-output' (Test-Path $normal) "file=$normal"
Assert 'normal-no-part' (-not $partLeft) "part_exists=$partLeft"

Write-Host "--- 崩溃路径：录制 ${KillAfterSeconds}s 后强杀 (M8-01) ---"
$crash = Join-Path $work 'crash.mkv'
$proc = Start-Process -FilePath $exe -PassThru -WindowStyle Hidden `
    -ArgumentList @('--record', '--seconds', '60', '--fps', '30', '--out', "`"$crash`"")
Register-SrProcess $proc
Start-Sleep -Seconds $KillAfterSeconds
Stop-Process -Id $proc.Id -Force
Start-Sleep -Milliseconds 500

$part = $crash + '.part'
Assert 'crash-leftover-part' (Test-Path $part) "part=$part"

Write-Host '--- 扫描 (M8-02) ---'
$scan = & $exe --scan $work 2>&1 | Out-String
Write-Host $scan.Trim()
$repairable = $scan -match 'repairable=1'
Assert 'scan-repairable' $repairable 'scan 报告存在可修复文件'

Write-Host '--- 修复 ---'
$repair = & $exe --repair $part 2>&1 | Out-String
Write-Host $repair.Trim()
$recovered = Join-Path $work 'crash.mkv.recovered.mkv'
Assert 'repair-ok' (($repair -match 'repair=ok') -and (Test-Path $recovered)) "output=$recovered"

Write-Host '--- 恢复文件可解码且时长合理 ---'
if (Test-Path $recovered) {
    $probe = & (Join-Path $ffbin 'ffprobe.exe') -v error -show_entries format=duration `
        -show_entries stream=codec_type -of default=nw=1 $recovered 2>&1 | Out-String
    $duration = 0.0
    if ($probe -match 'duration=([\d\.]+)') { $duration = [double]$Matches[1] }
    $hasVideo = $probe -match 'codec_type=video'

    $decode = & (Join-Path $ffbin 'ffmpeg.exe') -hide_banner -v error -i $recovered -f null - 2>&1 | Out-String
    Assert 'recovered-decodable' ([string]::IsNullOrWhiteSpace($decode)) `
        $(if ($decode.Trim()) { $decode.Trim().Substring(0, [Math]::Min(160, $decode.Trim().Length)) } else { 'decode clean' })
    Assert 'recovered-video-track' $hasVideo "streams=$(($probe -split "`n" | Where-Object { $_ -match 'codec_type' }) -join ',')"
    Assert 'recovered-duration' ($duration -gt 1.0) "duration=$([Math]::Round($duration,3))s"
    $loss = $KillAfterSeconds - $duration
    Assert 'loss-within-10s' ($loss -le 10.0) "killed_at=${KillAfterSeconds}s recovered=$( [Math]::Round($duration,2) )s loss=$([Math]::Round($loss,2))s"
}

Write-Host ''
Write-Host ("RESULT: {0} PASS / {1} FAIL" -f $script:pass, $script:fail)
if ($script:fail -gt 0) { exit 1 }
exit 0
