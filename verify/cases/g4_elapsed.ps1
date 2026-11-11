# G4 acceptance case: media-time elapsed (D-39 / M2-09)
#
# 计时必须跟随成片时间轴，而不是墙钟：
#   A 计时暂停 3s 期间，两次快照的 elapsed 不变（±50ms）
#   B 停止时快照 elapsed 与成品 duration 差 ≤0.5s
#   C 恢复后 elapsed 继续增长，且状态序列正确
#
# Usage: pwsh -File verify/cases/g4_elapsed.ps1 [-BuildDir build-msvc] [-Config Release]

param(
    [string]$BuildDir = 'build-msvc',
    [string]$Config = 'Release'
)

$ErrorActionPreference = 'Stop'

# 轮 30（F2）：统一异常清理——登记 spawn 进程 + trap 兜底，中途抛异常也不留孤儿。
. (Join-Path $PSScriptRoot '..\lib\Cleanup.ps1')
# trap 必须留在脚本自身作用域：dot-source 进来的 trap 不接管调用方的终止错误。
trap { Clear-SrProcesses -IncludeNameFallback; Write-Host "FAIL: $_"; exit 1 }

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$exe = Join-Path $root (Join-Path $BuildDir "bin\$Config\sr-recorder.exe")
$ctl = Join-Path $root (Join-Path $BuildDir "bin\$Config\srctl.exe")
if (-not (Test-Path $exe)) { $exe = Join-Path $root (Join-Path $BuildDir 'bin\sr-recorder.exe') }
if (-not (Test-Path $ctl)) { $ctl = Join-Path $root (Join-Path $BuildDir 'bin\srctl.exe') }
if (-not (Test-Path $exe) -or -not (Test-Path $ctl)) { Write-Host 'FAIL: binaries not found'; exit 2 }

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
if (-not $ffbin) { Write-Host 'BLOCKED: ffprobe not found'; exit 4 }

$script:pass = 0
$script:fail = 0
function Assert {
    param([string]$Name, [bool]$Ok, [string]$Detail)
    if ($Ok) { $script:pass++; Write-Host ("  PASS {0} — {1}" -f $Name, $Detail) }
    else { $script:fail++; Write-Host ("  FAIL {0} — {1}" -f $Name, $Detail) }
}
function Field {
    param([string]$Text, [string]$Key)
    if ($Text -match "(?m)^$Key=(.*)$") { return $Matches[1].Trim() }
    return ''
}
function LastElapsed {
    param([string]$Text)
    $values = [regex]::Matches($Text, 'elapsed_ms=(\d+)') | ForEach-Object { [long]$_.Groups[1].Value }
    if ($values.Count -eq 0) { return -1 }
    return $values[$values.Count - 1]
}
function Get-Duration {
    param([string]$Path)
    if (-not (Test-Path $Path)) { return 0.0 }
    $out = & (Join-Path $ffbin 'ffprobe.exe') -v error -show_entries format=duration -of default=nw=1 $Path 2>&1 | Out-String
    if ($out -match 'duration=([\d\.]+)') { return [double]$Matches[1] }
    return 0.0
}
function Test-Decodable {
    param([string]$Path)
    if (-not (Test-Path $Path)) { return $false }
    $decode = & (Join-Path $ffbin 'ffmpeg.exe') -hide_banner -v error -i $Path -f null - 2>&1 | Out-String
    return [string]::IsNullOrWhiteSpace($decode)
}

$work = Join-Path $root (Join-Path $BuildDir 'elapsed')
New-Item -ItemType Directory -Force -Path $work | Out-Null
Get-ChildItem $work -Recurse | Remove-Item -Force -Recurse -ErrorAction SilentlyContinue

$pipe = "\\.\pipe\sr.g4.elapsed.$PID"
$file = Join-Path $work 'elapsed.mkv'
$log = Join-Path $work 'elapsed.log'

Write-Host '--- 录制 20s 请求：2s 处计时暂停 3s ---'
$proc = Start-Process -FilePath $exe -PassThru -WindowStyle Hidden -RedirectStandardOutput $log `
    -ArgumentList @('--record', '--seconds', '20', '--fps', '30', '--out', "`"$file`"",
                    '--control-pipe', $pipe, '--control-wait', '300',
                    '--pause-at', '2', '--pause-duration', '3')
Register-SrProcess $proc

Start-Sleep -Milliseconds 2600
$paused1 = & $ctl --pipe $pipe state 2>&1 | Out-String
$e1 = LastElapsed $paused1
Start-Sleep -Milliseconds 2400
$paused2 = & $ctl --pipe $pipe state 2>&1 | Out-String
$e2 = LastElapsed $paused2

Assert 'paused-snapshot' ($paused1 -match 'state=Paused' -and $paused2 -match 'state=Paused') '两次查询都在 Paused'
Assert 'paused-elapsed-frozen' ([Math]::Abs($e2 - $e1) -le 50) "e1=${e1}ms e2=${e2}ms（暂停 2.4s，差 $([Math]::Abs($e2-$e1))ms）"
Assert 'paused-elapsed-near-pause-point' ($e1 -ge 1800 -and $e1 -le 2400) "e1=${e1}ms（2s 处暂停，含控制等待）"

Start-Sleep -Milliseconds 900
$stopOut = & $ctl --pipe $pipe stop 2>&1 | Out-String
$e3 = LastElapsed $stopOut
$proc.WaitForExit(40000) | Out-Null
$logText = Get-Content $log -Raw
$duration = Get-Duration $file
$mediaFinal = [long](Field $logText 'media_elapsed_ms')

Assert 'stop-ack-observed' ($e3 -gt $e1) "stop ack elapsed=${e3}ms > paused ${e1}ms（恢复后继续走）"
Assert 'stop-elapsed-vs-duration' ([Math]::Abs($e3 - ($duration * 1000)) -le 500) "elapsed=${e3}ms duration=$([Math]::Round($duration*1000))ms"
Assert 'final-media-vs-duration' ([Math]::Abs($mediaFinal - ($duration * 1000)) -le 500) "media_elapsed_ms=${mediaFinal} duration=$([Math]::Round($duration*1000))ms"
Assert 'state-sequence' ((Field $logText 'state_sequence') -eq 'Idle,Preflight,Countdown,Recording,Paused,Recording,Stopping,Idle') "state_sequence=$(Field $logText 'state_sequence')"
Assert 'record-exit' ([int](Field $logText 'record_exit') -eq 0) "record_exit=$(Field $logText 'record_exit')"
Assert 'playable' (Test-Decodable $file) 'decode clean'

Write-Host '--- B 计时暂停不受 --pause-timeout 截断 (F3) ---'
$fileB = Join-Path $work 'timed.mkv'
$outB = & $exe --record --seconds 3 --fps 30 --out $fileB --pause-at 1 --pause-duration 4 --pause-timeout 2 2>&1 | Out-String
$durB = Get-Duration $fileB
$wallB = [double](Field $outB 'wall_seconds')
Assert 'timed-pause-no-timeout' ([int](Field $outB 'pause_timeout_hit') -eq 0) "pause_timeout_hit=$(Field $outB 'pause_timeout_hit')"
Assert 'timed-pause-content' ([Math]::Abs($durB - 3.0) -le 0.1) "duration=$durB（内容 3s，计划暂停 4s）"
Assert 'timed-pause-wall' ($wallB -ge 6.5) "wall_seconds=$wallB（暂停 4s 必须走完）"
Assert 'timed-pause-exit' ([int](Field $outB 'record_exit') -eq 0) "record_exit=$(Field $outB 'record_exit')"

Write-Host ''
Write-Host ("RESULT: {0} PASS / {1} FAIL" -f $script:pass, $script:fail)
if ($script:fail -gt 0) { exit 1 }
exit 0
