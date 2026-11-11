# 轮 33 · M3-03 帧率入口（产品级）：
#   A 六档可设置并持久化：15 / 24 / 25 / 30 / 60 / 自定义 45，重启后仍生效
#   B UI 驱动 60fps 录制：成片 avg_frame_rate ∈ [59, 60.5]
#   C UI 驱动 15fps 录制：成片 avg_frame_rate ≤ 15.2，帧数不超过配置值
#
# 断言链：`--set-fps` 写 INI（与设置面板同一个 SaveVideoSettings）
#   → 重启 UI 读回（app_start fps=N）→ UI 下发 recorder（recorder_cmd fps=N）
#   → 成片帧率（ffprobe）。三段都过才说明「用户能选、能保留、能生效」。
#
# Usage: pwsh -File verify/cases/g5_fps.ps1

param(
    [string]$BuildDir = 'build-msvc',
    [string]$Config = 'Release'
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot '..\lib\Cleanup.ps1')
. (Join-Path $PSScriptRoot '..\lib\Forensics.ps1')
trap {
    [void](Save-FailureContext -Tag 'g5_fps' -WorkDirs $script:SrForensicsWorkDirs -Note $_)
    Clear-SrProcesses -IncludeNameFallback
    Write-Host "FAIL: $_"
    exit 1
}

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$ui = Join-Path $root (Join-Path $BuildDir "bin\$Config\sr-ui.exe")
if (-not (Test-Path $ui)) { $ui = Join-Path $root (Join-Path $BuildDir 'bin\sr-ui.exe') }
if (-not (Test-Path $ui)) { Write-Host 'FAIL: sr-ui.exe not found'; exit 2 }
$ffbin = Split-Path -Parent (Get-Command ffprobe).Source
$ffprobe = Join-Path $ffbin 'ffprobe.exe'

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class SrFpsNative {
    [DllImport("user32.dll")] public static extern void keybd_event(byte bVk, byte bScan, uint dwFlags, UIntPtr dwExtraInfo);
}
'@

$work = Join-Path $env:TEMP 'sr-fps'
if (Test-Path $work) { Get-ChildItem $work -Recurse -Force | Remove-Item -Force -Recurse -ErrorAction SilentlyContinue }
New-Item -ItemType Directory -Force -Path $work | Out-Null
$script:SrForensicsWorkDirs = @($work)
$ini = Join-Path $work 'config.ini'
[System.IO.File]::WriteAllText($ini,
    "[hotkeys]`r`nstart=Ctrl+Alt+F9`r`npause=Ctrl+Alt+F10`r`nstop=Ctrl+Alt+F11`r`n",
    [System.Text.UTF8Encoding]::new($false))

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
        [void](Save-FailureContext -Tag $Name -WorkDirs @($work) -Note $Detail)
    }
}

function Read-Text {
    param([string]$Path)
    # UI 进程仍持有日志写句柄，必须按共享读打开（同 lib/Cleanup.ps1 的 Read-TextSafe）。
    return (Read-TextSafe $Path)
}

function Wait-Log {
    param([string]$Path, [string]$Pattern, [int]$TimeoutMs = 10000)
    $deadline = (Get-Date).AddMilliseconds($TimeoutMs)
    while ((Get-Date) -lt $deadline) {
        if ((Read-Text $Path) -match $Pattern) { return $true }
        Start-Sleep -Milliseconds 100
    }
    return $false
}

function Set-Fps {
    param([int]$Fps, [string]$Tag)
    $log = Join-Path $work "$Tag-set.log"
    $proc = Start-Process -FilePath $ui -PassThru -Wait -WindowStyle Hidden `
        -ArgumentList @('--set-fps', "$Fps", '--config', $ini, '--log', $log)
    Register-SrProcess $proc
    return $proc.ExitCode
}

function Restart-ReadBack {
    param([int]$Fps, [string]$Tag)
    $log = Join-Path $work "$Tag-restart.log"
    $proc = Start-Process -FilePath $ui -PassThru -WindowStyle Hidden `
        -ArgumentList @('--hidden', '--exit-after-ms', '2500', '--config', $ini, '--log', $log)
    Register-SrProcess $proc
    $ok = Wait-Log $log 'app_ready' 8000
    $exited = $proc.WaitForExit(10000)
    $text = Read-Text $log
    $match = [regex]::Match($text, 'app_start .*fps=(\d+) rate_mode=(\w+)')
    $read_fps = if ($match.Success) { [int]$match.Groups[1].Value } else { -1 }
    return [pscustomobject]@{ Ok = $ok -and $exited -and $read_fps -eq $Fps; Fps = $read_fps; Text = $text }
}

function Press-Combo {
    param([byte[]]$Keys)
    foreach ($key in $Keys) { [SrFpsNative]::keybd_event($key, 0, 0, [UIntPtr]::Zero) }
    Start-Sleep -Milliseconds 80
    for ($i = $Keys.Count - 1; $i -ge 0; $i--) {
        [SrFpsNative]::keybd_event($Keys[$i], 0, 2, [UIntPtr]::Zero)
    }
    Start-Sleep -Milliseconds 80
}

function Get-AvgFps {
    param([string]$Path)
    $raw = & $ffprobe -v error -select_streams v:0 -show_entries stream=avg_frame_rate `
        -of default=nw=1:nk=1 $Path 2>&1 | Out-String
    $text = $raw.Trim()
    if ($text -match '^(\d+)/(\d+)$') {
        $den = [double]$Matches[2]
        if ($den -gt 0) { return ([double]$Matches[1] / $den) }
    }
    return 0.0
}

function Record-ViaUi {
    param([int]$Fps, [string]$Tag, [int]$HoldMs = 4000)
    Get-Process sr-ui, sr-recorder -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    $out = Join-Path $work "$Tag.mkv"
    $log = Join-Path $work "$Tag.log"
    $exitMs = $HoldMs + 9000
    $proc = Start-Process -FilePath $ui -PassThru -WindowStyle Hidden `
        -ArgumentList @('--hidden', '--out', $out, '--config', $ini, '--log', $log,
                        '--exit-after-ms', "$exitMs")
    Register-SrProcess $proc
    $ready = Wait-Log $log 'app_ready' 8000
    Press-Combo @(0x11, 0x12, 0x78)  # Ctrl+Alt+F9
    $started = Wait-Log $log 'recording_indicator' 8000
    Start-Sleep -Milliseconds $HoldMs
    Press-Combo @(0x11, 0x12, 0x7A)  # Ctrl+Alt+F11
    $exited = $proc.WaitForExit($exitMs + 15000)
    $text = Read-Text $log
    return [pscustomobject]@{
        Ready = $ready; Started = $started; Exited = $exited; Text = $text; Out = $out
        CommandFps = if ($text -match 'recorder_cmd fps=(\d+)') { [int]$Matches[1] } else { -1 }
    }
}

Get-Process sr-ui, sr-recorder -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue

# ---------------- A 六档持久化 ----------------
Write-Host '--- A 六档帧率：设置 + 重启读回 ---'
$levels = @(15, 24, 25, 30, 60, 45)
foreach ($level in $levels) {
    $tag = "fps$level"
    $exit = Set-Fps -Fps $level -Tag $tag
    $iniText = Read-Text $ini
    # INI 是 CRLF：`$` 只到 \n 之前，必须显式吃掉 \r。
    $saved = $iniText -match "(?m)^fps=$level\r?$"
    $readback = Restart-ReadBack -Fps $level -Tag $tag
    Assert "fps-$level-persist" ($exit -eq 0 -and $saved -and $readback.Ok) `
        ("exit={0} ini_fps={1} restart_fps={2}" -f $exit, $saved, $readback.Fps)
}

# ---------------- B 60fps 端到端 ----------------
Write-Host '--- B UI 驱动 60fps 录制 ---'
[void](Set-Fps -Fps 60 -Tag 'e2e60')
$b = Record-ViaUi -Fps 60 -Tag 'ui60' -HoldMs 4000
$fps60 = if (Test-Path $b.Out) { Get-AvgFps $b.Out } else { 0.0 }
$bMode = if ($b.Text -match 'recorder_cmd fps=(\d+) rate_mode=(\w+)') { $Matches[2] } else { '' }
Assert 'm3-03-ui-60-cmd' ($b.CommandFps -eq 60) ("recorder_cmd fps={0}" -f $b.CommandFps)
Assert 'm3-03-ui-60-record' ($b.Exited -and (Test-Path $b.Out)) 'UI 录制正常收尾且产物存在'
Assert 'm3-03-ui-60-fps' ($fps60 -ge 59.0 -and $fps60 -le 60.5) ("avg_frame_rate={0:N2}" -f $fps60)

# ---------------- C 15fps 端到端 ----------------
Write-Host '--- C UI 驱动 15fps 录制 ---'
[void](Set-Fps -Fps 15 -Tag 'e2e15')
$c = Record-ViaUi -Fps 15 -Tag 'ui15' -HoldMs 4000
$fps15 = if (Test-Path $c.Out) { Get-AvgFps $c.Out } else { 0.0 }
Assert 'm3-03-ui-15-cmd' ($c.CommandFps -eq 15) ("recorder_cmd fps={0}" -f $c.CommandFps)
Assert 'm3-03-ui-15-fps' ($fps15 -gt 0 -and $fps15 -le 15.2) ("avg_frame_rate={0:N2}" -f $fps15)

$summary = [pscustomobject]@{
    levels        = $levels
    fps_60        = $fps60
    fps_15        = $fps15
    rate_mode_60  = $bMode
    pass          = $script:pass
    fail          = $script:fail
    work          = $work
}
$summary | ConvertTo-Json -Depth 4 | Set-Content -Path (Join-Path $work 'summary.json') -Encoding UTF8
Write-Host ''
Write-Host ("RESULT: {0} PASS / {1} FAIL ; 产物 {2}" -f $script:pass, $script:fail, $work)
if ($script:fail -gt 0) { exit 1 }
exit 0
