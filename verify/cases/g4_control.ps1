# G4 acceptance case: recording control loop
# 锚点（2026-10-21 裁定）：M2-02 暂停/恢复、M2-04 倒计时（含取消）、M7-02 自动停止
#
# A 取消：倒计时内取消 → exit 14、cancelled=1、目录不存在、无 .part
# B 暂停：时长/帧数不计暂停、暂停期间不写盘、恢复无长空洞
# C 音轨：暂停与非暂停两种情况下，视频末 PTS 与音频末 PTS 差 ≤ 300ms
# D 倒计时：等待生效且不进成片
# E 自动停止：按时长到达即停（按结束时间属 M7-02 另一半，本轮未实现）
# F 双后端：WGC / DXGI 各一遍，且不得后端漂移
# G 错误传播（D-28）：--fail-resume → exit 15，成品可播且内容不丢
# H 状态机：state_sequence 精确匹配；Stop 后 Pause 必须 Rejected；支持多段暂停
#
# Usage: pwsh -File verify/cases/g4_control.ps1 [-BuildDir build-msvc] [-Config Release]

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
if (-not (Test-Path $exe)) { $exe = Join-Path $root (Join-Path $BuildDir 'bin\sr-recorder.exe') }
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
if (-not $ffbin) { Write-Host 'BLOCKED: ffprobe not found'; exit 4 }

$script:pass = 0
$script:fail = 0
function Assert {
    param([string]$Name, [bool]$Ok, [string]$Detail)
    if ($Ok) { $script:pass++; Write-Host ("  PASS {0} — {1}" -f $Name, $Detail) }
    else { $script:fail++; Write-Host ("  FAIL {0} — {1}" -f $Name, $Detail) }
}
function Field {
    param([string]$Text, [String]$Key)
    if ($Text -match "(?m)^$Key=(.*)$") { return $Matches[1].Trim() }
    return ''
}
function Get-Duration {
    param([string]$Path)
    if (-not (Test-Path $Path)) { return 0.0 }
    $out = & (Join-Path $ffbin 'ffprobe.exe') -v error -show_entries format=duration -of default=nw=1 $Path 2>&1 | Out-String
    if ($out -match 'duration=([\d\.]+)') { return [double]$Matches[1] }
    return 0.0
}
function Get-MaxFrameGapMs {
    param([string]$Path)
    $pts = & (Join-Path $ffbin 'ffprobe.exe') -v error -select_streams v:0 `
        -show_entries frame=pts_time -of csv=p=0 $Path 2>$null |
        ForEach-Object { $_.Trim().TrimEnd(',') } |
        Where-Object { $_ -match '^\d+(\.\d+)?$' } |
        ForEach-Object { [double]$_ }
    $max = 0.0
    for ($i = 1; $i -lt $pts.Count; $i++) {
        $delta = $pts[$i] - $pts[$i - 1]
        if ($delta -gt $max) { $max = $delta }
    }
    return [Math]::Round($max * 1000, 1)
}

$work = Join-Path $root (Join-Path $BuildDir 'control')
New-Item -ItemType Directory -Force -Path $work | Out-Null
Get-ChildItem $work -Recurse | Remove-Item -Force -Recurse -ErrorAction SilentlyContinue

Write-Host '--- A 倒计时取消 (M2-04) ---'
$cancelDir = Join-Path $work 'cancel_target'
$cancelFile = Join-Path $cancelDir 'x.mkv'
$outA = & $exe --record --seconds 5 --out $cancelFile --countdown 5 --cancel-at 2 2>&1 | Out-String
$codeA = [int](Field $outA 'record_exit')
$cancelA = [int](Field $outA 'cancelled')
$stateA = Field $outA 'state_sequence'
Assert 'cancel-exit-code' ($codeA -eq 14) "record_exit=$codeA"
Assert 'cancel-flag' ($cancelA -eq 1) "cancelled=$cancelA"
Assert 'cancel-no-directory' (-not (Test-Path $cancelDir)) "dir_exists=$(Test-Path $cancelDir)"
Assert 'cancel-no-part' (-not (Test-Path ($cancelFile + '.part'))) 'no .part left behind'
Assert 'cancel-state-ends-idle' ($stateA -match 'Countdown,Idle$') "state_sequence=$stateA"

Write-Host '--- B 暂停账目 (M2-02) ---'
$pauseFile = Join-Path $work 'pause.mkv'
$outB = & $exe --record --seconds 6 --fps 30 --out $pauseFile --pause-at 3 --pause-duration 4 2>&1 | Out-String
$durB = Get-Duration $pauseFile
$wallB = [double](Field $outB 'wall_seconds')
$pausedMs = [long](Field $outB 'paused_ms')
$framesB = [int](Field $outB 'frames_encoded')
$bytesPause = [long](Field $outB 'part_bytes_at_pause')
$bytesResume = [long](Field $outB 'part_bytes_at_resume')
Assert 'pause-observed' ($pausedMs -ge 3900) "paused_ms=$pausedMs"
Assert 'wall-includes-pause' ($wallB -ge 9.5) "wall_seconds=$wallB"
Assert 'duration-excludes-pause' ([Math]::Abs($durB - 6.0) -le 0.05) "duration=$durB (expect 6s)"
Assert 'frame-count-exact' ($framesB -eq 180) "frames_encoded=$framesB"
Assert 'pause-wrote-data' ($bytesPause -gt 0) "part_bytes_at_pause=$bytesPause"
Assert 'no-growth-during-pause' ($bytesResume -eq $bytesPause) "growth=$($bytesResume - $bytesPause)B"
$gap = Get-MaxFrameGapMs $pauseFile
Assert 'no-long-gap' ($gap -le 100) "max_frame_gap=${gap}ms"

Write-Host '--- C 音轨对齐 (M2-02) ---'
$vB = [long](Field $outB 'video_last_pts_ms')
$aB = [long](Field $outB 'audio_last_pts_ms')
Assert 'paused-av-tail' ([Math]::Abs($vB - $aB) -le 300) "video=$vB audio=$aB diff=$($vB - $aB)ms"
Assert 'paused-audio-present' ($aB -gt 0) "audio_last_pts_ms=$aB"

$plainFile = Join-Path $work 'plain.mkv'
$outC = & $exe --record --seconds 5 --fps 30 --out $plainFile 2>&1 | Out-String
$vC = [long](Field $outC 'video_last_pts_ms')
$aC = [long](Field $outC 'audio_last_pts_ms')
Assert 'plain-av-tail' ([Math]::Abs($vC - $aC) -le 300) "video=$vC audio=$aC diff=$($vC - $aC)ms"

Write-Host '--- D 倒计时 (M2-04) ---'
$cdFile = Join-Path $work 'countdown.mkv'
$outD = & $exe --record --seconds 3 --fps 30 --out $cdFile --countdown 3 2>&1 | Out-String
$cdMs = [long](Field $outD 'countdown_ms')
$cdDur = Get-Duration $cdFile
Assert 'countdown-waited' ($cdMs -ge 2900) "countdown_ms=$cdMs"
Assert 'countdown-not-in-output' ([Math]::Abs($cdDur - 3.0) -le 0.05) "duration=$cdDur"

Write-Host '--- E 自动停止 (M7-02 前半) ---'
$fixedFile = Join-Path $work 'fixed.mkv'
$outE = & $exe --record --seconds 8 --fps 30 --out $fixedFile 2>&1 | Out-String
$eDur = Get-Duration $fixedFile
$eFrames = [int](Field $outE 'frames_encoded')
Assert 'auto-stop-duration' ([Math]::Abs($eDur - 8.0) -le 0.05) "duration=$eDur"
Assert 'auto-stop-frames' ($eFrames -eq 240) "frames_encoded=$eFrames"

Write-Host '--- F 双后端 (M2-02) ---'
foreach ($backend in @('wgc', 'dxgi')) {
    $file = Join-Path $work "pause_$backend.mkv"
    $out = & $exe --record --seconds 4 --fps 30 --out $file --pause-at 2 --pause-duration 3 --backend $backend 2>&1 | Out-String
    $active = Field $out 'backend'
    $changed = [int](Field $out 'backend_changed')
    $d = Get-Duration $file
    Assert "backend-$backend-active" ($active -eq $backend) "backend=$active"
    Assert "backend-$backend-stable" ($changed -eq 0) "backend_changed=$changed"
    Assert "backend-$backend-duration" ([Math]::Abs($d - 4.0) -le 0.05) "duration=$d"
}

Write-Host '--- G 恢复失败必须传播 (D-28) ---'
$failFile = Join-Path $work 'failresume.mkv'
$outG = & $exe --record --seconds 8 --fps 30 --out $failFile --pause-at 2 --pause-duration 1 --fail-resume 2>&1 | Out-String
$codeG = [int](Field $outG 'record_exit')
$gDur = Get-Duration $failFile
Assert 'fail-resume-exit-code' ($codeG -eq 15) "record_exit=$codeG (不得静默成功)"
Assert 'fail-resume-file-playable' ((Test-Path $failFile) -and $gDur -gt 1.0) "exists=$(Test-Path $failFile) duration=$gDur"
$probeG = & python (Join-Path $root 'verify\tools\mkv_probe.py') $failFile 2>&1 | Out-String
Assert 'fail-resume-container-valid' ($probeG -match 'RESULT: 0 failure') 'container valid'
$decodeG = & (Join-Path $ffbin 'ffmpeg.exe') -hide_banner -v error -i $failFile -f null - 2>&1 | Out-String
Assert 'fail-resume-decodable' ([string]::IsNullOrWhiteSpace($decodeG)) 'decode clean'

Write-Host '--- H 状态机与多段暂停 (M2-02) ---'
$multiFile = Join-Path $work 'multi.mkv'
$outH = & $exe --record --seconds 8 --fps 30 --out $multiFile --pause-at 2,5 --pause-duration 1,1 2>&1 | Out-String
$stateH = Field $outH 'state_sequence'
$pauseCount = [int](Field $outH 'pause_count')
$rejected = [int](Field $outH 'pause_after_stop_rejected')
$expected = 'Idle,Preflight,Countdown,Recording,Paused,Recording,Paused,Recording,Stopping,Idle'
Assert 'state-sequence-exact' ($stateH -eq $expected) "state_sequence=$stateH"
Assert 'multi-pause-count' ($pauseCount -eq 2) "pause_count=$pauseCount"
Assert 'pause-after-stop-rejected' ($rejected -eq 1) "pause_after_stop_rejected=$rejected"

Write-Host ''
Write-Host ("RESULT: {0} PASS / {1} FAIL" -f $script:pass, $script:fail)
if ($script:fail -gt 0) { exit 1 }
exit 0
