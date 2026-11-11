# G4 门（UI-3 / 轮 29）：60s 全链路 + 停止到可播 ≤3s + 重复帧 ≤1%。
#
# 判据来自 docs/05 §7.2 与轮 29 指令：
#   - M2-01 spawn→Recording ≤1.5s
#   - M2-14 自检 ≤500ms（四项）
#   - 60s 素材全链路（UI → 管道 → Recorder → MKV）可解码
#   - 停止命令 → 成片可播 ≤3s
#   - 成片重复帧占比 ≤1%（运动源；静止桌面不算）
#
# Usage: pwsh -File verify/cases/g4_gate.ps1 [-BuildDir build-msvc] [-Seconds 60]

param(
    [string]$BuildDir = 'build-msvc',
    [string]$Config = 'Release',
    [int]$Seconds = 60,
    [int]$Fps = 30
)

$ErrorActionPreference = 'Stop'

# 轮 30（F2）：统一异常清理——登记 spawn 进程 + trap 兜底，中途抛异常也不留孤儿。
. (Join-Path $PSScriptRoot '..\lib\Cleanup.ps1')
# trap 必须留在脚本自身作用域：dot-source 进来的 trap 不接管调用方的终止错误。
trap { Clear-SrProcesses -IncludeNameFallback; Write-Host "FAIL: $_"; exit 1 }

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
function Resolve-Exe([string]$name) {
    $a = Join-Path $root (Join-Path $BuildDir "bin\$Config\$name")
    if (Test-Path $a) { return $a }
    return Join-Path $root (Join-Path $BuildDir "bin\$name")
}
$ui = Resolve-Exe 'sr-ui.exe'
if (-not (Test-Path $ui)) { Write-Host "FAIL: missing $ui"; exit 2 }

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
if (-not $bin) { Write-Host 'BLOCKED: ffmpeg/ffprobe not found'; exit 4 }

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
function Read-Log([string]$Path) {
    # UI 进程持有日志句柄；File.ReadAllText 会因共享冲突抛异常，
    # 这里必须用 Get-Content -Raw（共享读）并吞掉瞬时错误。
    try {
        if (-not (Test-Path $Path)) { return '' }
        return (Get-Content $Path -Raw -ErrorAction Stop)
    } catch {
        return ''
    }
}
function Field([string]$Text, [string]$Key) {
    if ($Text -match "(?m)^$Key=(.*)$") { return $Matches[1].Trim() }
    return ''
}
function Press-Combo([int[]]$Keys) {
    foreach ($k in $Keys) {
        [UiGateNative]::keybd_event([byte]$k, 0, 0, [UIntPtr]::Zero)
    }
    Start-Sleep -Milliseconds 70
    for ($i = $Keys.Count - 1; $i -ge 0; $i--) {
        [UiGateNative]::keybd_event([byte]$Keys[$i], 0, 2, [UIntPtr]::Zero)
    }
    Start-Sleep -Milliseconds 70
}
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class UiGateNative {
    [DllImport("user32.dll")] public static extern void keybd_event(byte bVk, byte bScan, uint dwFlags, UIntPtr dwExtraInfo);
}
'@

$uiProc = $null
$stim = $null

$work = Join-Path $env:TEMP ('sr-g4-gate-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $work | Out-Null
$out = Join-Path $work 'g4_gate.mkv'
$log = Join-Path $work 'ui.log'
$ini = Join-Path $work 'config.ini'
$stimLog = Join-Path $work 'stimulus.log'

Get-Process sr-ui, sr-recorder -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue

# WinForms 需要 STA；Windows PowerShell 5.1 的 -STA 最稳。
$stimPath = Join-Path $PSScriptRoot 'stimulus.ps1'
$stim = Start-Process -FilePath 'powershell.exe' -PassThru -WindowStyle Hidden `
    -ArgumentList @('-STA', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$stimPath`"") `
    -RedirectStandardOutput $stimLog
Register-SrProcess $stim
Start-Sleep -Milliseconds 900

$uiProc = Start-Process -FilePath $ui -PassThru -WindowStyle Hidden `
    -ArgumentList @('--hidden', '--config', $ini, '--out', $out, '--log', $log, '--exit-on-stop')
Register-SrProcess $uiProc

$ready = $false
for ($i = 0; $i -lt 100; $i++) {
    if ((Read-Log $log) -match 'app_ready') { $ready = $true; break }
    Start-Sleep -Milliseconds 100
}
Assert 'app-ready' $ready 'UI 启动'

Press-Combo @(0x11, 0x12, 0x78)  # Ctrl+Alt+F9
$recording = $false
for ($i = 0; $i -lt 100; $i++) {
    if ((Read-Log $log) -match 'recording_indicator') { $recording = $true; break }
    Start-Sleep -Milliseconds 100
}
$text = Read-Log $log
Assert 'recording-started' $recording '录制回执'
$latency = [int](Field $text 'recording_indicator latency_ms')
# Field() 只匹配行首；latency 在行中间，单独取
$m = [regex]::Match($text, 'recording_indicator latency_ms=(\d+)')
if ($m.Success) { $latency = [int]$m.Groups[1].Value }
Assert 'm2-01-under-1.5s' ($m.Success -and $latency -le 1500) "spawn→Recording=${latency}ms"

$pre = [regex]::Match($text, 'preflight_pass=1 ms=(\d+)')
Assert 'm2-14-preflight' ($pre.Success -and [int]$pre.Groups[1].Value -le 500) "preflight_ms=$($pre.Groups[1].Value)"

Write-Host ("--- 录制 {0}s ---" -f $Seconds)
Start-Sleep -Seconds $Seconds
Press-Combo @(0x11, 0x12, 0x7A)  # Ctrl+Alt+F11
$stopWatch = [System.Diagnostics.Stopwatch]::StartNew()

$playable = $false
while ($stopWatch.Elapsed.TotalSeconds -lt 15) {
    if (Test-Path $out) {
        $probe = & (Join-Path $bin 'ffprobe.exe') -v error -show_entries format=duration `
            -of default=nw=1 $out 2>&1 | Out-String
        if ($LASTEXITCODE -eq 0 -and $probe -match 'duration=') {
            $playable = $true
            break
        }
    }
    Start-Sleep -Milliseconds 100
}
$stopToPlayable = [int]$stopWatch.Elapsed.TotalMilliseconds
$uiProc.WaitForExit(20000) | Out-Null
if (-not $uiProc.HasExited) { Stop-Process -Id $uiProc.Id -Force -ErrorAction SilentlyContinue }
if (-not $stim.HasExited) { Stop-Process -Id $stim.Id -Force -ErrorAction SilentlyContinue }

$text = Read-Log $log
$session = [regex]::Match($text, 'session_end exit=(\d+) output_exists=(\d+)')
Assert 'session-end' ($session.Success -and $session.Groups[1].Value -eq '0' -and $session.Groups[2].Value -eq '1') `
    "session_end=$($session.Value)"
Assert 'stop-to-playable-3s' ($playable -and $stopToPlayable -le 3000) "stop→playable=${stopToPlayable}ms"
Assert 'output-exists' (Test-Path $out) "out=$out"

$probe = & (Join-Path $bin 'ffprobe.exe') -v error -show_entries format=duration `
    -show_entries stream=codec_name,width,height -of default=nw=1 $out 2>&1 | Out-String
$duration = [double](Field $probe 'duration')
Assert 'duration-target' ([Math]::Abs($duration - $Seconds) -le 1.0) "duration=$duration"

$decode = & (Join-Path $bin 'ffmpeg.exe') -hide_banner -v error -i $out -f null - 2>&1 | Out-String
Assert 'decode-clean' ([string]::IsNullOrEmpty($decode.Trim())) 'full decode'

$frameText = & (Join-Path $bin 'ffprobe.exe') -v error -count_frames -select_streams v:0 `
    -show_entries stream=nb_read_frames -of default=nw=1:nk=1 $out 2>&1 | Out-String
$frames = 0
[void][int]::TryParse($frameText.Trim(), [ref]$frames)
# 停止时刻相对 indicator 有 0.2-0.5s 偏差，按实测 duration 算期望帧数。
$expectedFrames = [int][Math]::Round($duration * $Fps)
Assert 'frame-count' ([Math]::Abs($frames - $expectedFrames) -le 2) "decoded=$frames expected=$expectedFrames"

# 产品级重复帧：ffmpeg 逐帧 md5，统计连续相同哈希。
$md5 = & (Join-Path $bin 'ffmpeg.exe') -v error -i $out -map 0:v:0 -f framemd5 - 2>&1 | Out-String
$hashes = @($md5 -split "`n" | Where-Object { $_ -match '^\s*\d+\s*,' } |
    ForEach-Object { ($_ -split ',')[-1].Trim() })
$dupes = 0
for ($i = 1; $i -lt $hashes.Count; $i++) {
    if ($hashes[$i] -eq $hashes[$i - 1]) { $dupes++ }
}
$ratio = if ($hashes.Count -gt 0) { $dupes / $hashes.Count } else { 1.0 }
Assert 'duplicate-frame-ratio' ($hashes.Count -gt 0 -and $ratio -le 0.01) `
    "dupes=$dupes/$($hashes.Count) ($([Math]::Round($ratio * 100, 2))%)"

$recMatch = [regex]::Match($text, 'spawn pid=(\d+)')
$leftover = $null
if ($recMatch.Success) {
    $leftover = Get-Process -Id ([int]$recMatch.Groups[1].Value) -ErrorAction SilentlyContinue
}
Assert 'no-leftover-recorder' ($recMatch.Success -and $null -eq $leftover) '无残留 Recorder'

Write-Host ''
Write-Host ("RESULT: {0} PASS / {1} FAIL" -f $script:pass, $script:fail)
if ($script:fail -gt 0) { exit 1 }
exit 0
