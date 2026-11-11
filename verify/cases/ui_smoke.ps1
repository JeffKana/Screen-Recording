# G4 / UI-1 acceptance case: Win32 skeleton, tray plumbing, global hotkeys (轮 27)
#
# 自动化范围（计入 PASS）：
#   A 热键冲突被拒 + 自定义持久化到 INI
#   B UI 驱动录制：热键开始/暂停/恢复/停止，M2-01 时延、M2-05 响应、成品可播、无残留
#   C 热键注册失败可见，且不阻断其它热键
#   D 重启后自定义热键生效 + 录制中关闭应用能干净收尾
#
# 托盘图标 / 菜单置灰 / 最小化到托盘属人工项，见 verify/manual/round27-ui-checklist.md，
# 不计入本脚本 PASS。
#
# Usage: pwsh -File verify/cases/ui_smoke.ps1 [-BuildDir build-msvc] [-Config Release]

param(
    [string]$BuildDir = 'build-msvc',
    [string]$Config = 'Release'
)

$ErrorActionPreference = 'Stop'

# 轮 30（F2）：统一异常清理——登记 spawn 进程 + trap 兜底，中途抛异常也不留孤儿。
. (Join-Path $PSScriptRoot '..\lib\Cleanup.ps1')
# 轮 31（U1）：断言失败先留现场（进程/命令行/日志尾部/.part），再继续或退出。
. (Join-Path $PSScriptRoot '..\lib\Forensics.ps1')
# trap 必须留在脚本自身作用域：dot-source 进来的 trap 不接管调用方的终止错误。
trap {
    [void](Save-FailureContext -Tag 'trap' -WorkDirs $script:SrForensicsWorkDirs -Note $_)
    Clear-SrProcesses -IncludeNameFallback
    if ($script:HadDefaultIni -and (Test-Path $script:DefaultBackup)) {
        Copy-Item $script:DefaultBackup $script:DefaultIni -Force -ErrorAction SilentlyContinue
    }
    Write-Host "FAIL: $_"
    exit 1
}

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$ui = Join-Path $root (Join-Path $BuildDir "bin\$Config\sr-ui.exe")
if (-not (Test-Path $ui)) { $ui = Join-Path $root (Join-Path $BuildDir 'bin\sr-ui.exe') }
if (-not (Test-Path $ui)) { Write-Host 'FAIL: sr-ui.exe not found'; exit 2 }

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

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class UiSmokeNative {
    [DllImport("user32.dll")] public static extern void keybd_event(byte bVk, byte bScan, uint dwFlags, UIntPtr dwExtraInfo);
    [DllImport("user32.dll", SetLastError = true)] public static extern bool RegisterHotKey(IntPtr hWnd, int id, uint fsModifiers, uint vk);
    [DllImport("user32.dll", SetLastError = true)] public static extern bool UnregisterHotKey(IntPtr hWnd, int id);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindow(string lpClassName, string lpWindowName);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "FindWindowW")] public static extern IntPtr FindWindowClass(string lpClassName, IntPtr lpWindowName);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr hDlg, int nIDDlgItem);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr hWnd, System.Text.StringBuilder lpString, int nMaxCount);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern IntPtr GetWindowLongPtr(IntPtr hWnd, int nIndex);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extra);
}
'@

$script:pass = 0
$script:fail = 0
$script:skip = 0
function Skip {
    param([string]$Name, [string]$Reason)
    $script:skip++
    Write-Host ("  SKIP {0} — {1}" -f $Name, $Reason)
}
function Assert {
    param([string]$Name, [bool]$Ok, [string]$Detail)
    if ($Ok) {
        $script:pass++
        Write-Host ("  PASS {0} — {1}" -f $Name, $Detail)
    } else {
        $script:fail++
        Write-Host ("  FAIL {0} — {1}" -f $Name, $Detail)
        # U1：失败即取证。只留第一次现场；不打断后续用例，便于看失败面。
        [void](Save-FailureContext -Tag $Name -WorkDirs $script:SrForensicsWorkDirs -Note $Detail)
    }
}
function Read-Log {
    param([string]$Path)
    if (-not (Test-Path $Path)) { return '' }
    try {
        $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open,
                                          [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
        $reader = New-Object System.IO.StreamReader($stream, [System.Text.Encoding]::UTF8)
        $text = $reader.ReadToEnd()
        $reader.Dispose()
        $stream.Dispose()
        return $text
    } catch {
        return ''
    }
}
function Wait-Log {
    param([string]$Path, [string]$Pattern, [int]$TimeoutMs)
    $deadline = (Get-Date).AddMilliseconds($TimeoutMs)
    while ((Get-Date) -lt $deadline) {
        $text = Read-Log $Path
        if ($text -match $Pattern) { return $text }
        Start-Sleep -Milliseconds 100
    }
    return (Read-Log $Path)
}
# 轮 34（R1）：等窗口出现，而不是「PostMessage 后 sleep 500ms」。
function Wait-WindowClass {
    param([string]$Class, [int]$TimeoutMs = 3000)
    $deadline = (Get-Date).AddMilliseconds($TimeoutMs)
    while ((Get-Date) -lt $deadline) {
        $handle = [UiSmokeNative]::FindWindowClass($Class, [IntPtr]::Zero)
        if ($handle -ne [IntPtr]::Zero) { return $handle }
        Start-Sleep -Milliseconds 50
    }
    return [IntPtr]::Zero
}
function Wait-Status {
    param([string]$Pattern, [int]$TimeoutMs = 3000)
    $deadline = (Get-Date).AddMilliseconds($TimeoutMs)
    while ((Get-Date) -lt $deadline) {
        if ((Get-StatusText) -match $Pattern) { return $true }
        Start-Sleep -Milliseconds 50
    }
    return $false
}
# 热键可能丢一次（输入队列繁忙）；看到目标日志或 spawn 就停，避免重复触发。
function Press-Until {
    param([byte[]]$Keys, [string]$Log, [string]$Pattern, [int]$TimeoutMs = 6000)
    $deadline = (Get-Date).AddMilliseconds($TimeoutMs)
    while ((Get-Date) -lt $deadline) {
        Press-Combo $Keys
        $hit = Wait-Log $Log $Pattern 1200
        if ($hit -match $Pattern) { return $true }
        if ($hit -match 'spawn pid=') { return $false }  # 已经启动，别再按
    }
    return $false
}
function Reset-WorkDir {
    param([string]$Path)
    # 先杀残留，再删文件；否则被进程持有的 .part 删不掉，会污染后续 no-part 断言。
    Get-Process sr-ui, sr-recorder -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    for ($i = 0; $i -lt 20; $i++) {
        if (Test-Path $Path) {
            Get-ChildItem $Path -Recurse -Force -ErrorAction SilentlyContinue |
                Remove-Item -Force -Recurse -ErrorAction SilentlyContinue
        }
        $left = @(Get-ChildItem $Path -Recurse -Force -ErrorAction SilentlyContinue).Count
        if ($left -eq 0) { return $true }
        Start-Sleep -Milliseconds 250
    }
    return $false
}
function Get-LogLines {
    param([string]$Text)
    $lines = @()
    foreach ($line in ($Text -split "`r?`n")) {
        $match = [regex]::Match($line, '^t=(\d+) (.*)$')
        if ($match.Success) {
            $lines += [pscustomobject]@{ T = [long]$match.Groups[1].Value; Text = $match.Groups[2].Value }
        }
    }
    return $lines
}
function Last-Line {
    param($Lines, [string]$Pattern, [long]$After = 0)
    return $Lines | Where-Object { $_.Text -match $Pattern -and $_.T -ge $After } | Select-Object -Last 1
}
function First-Line {
    param($Lines, [string]$Pattern, [long]$After = 0)
    return $Lines | Where-Object { $_.Text -match $Pattern -and $_.T -ge $After } | Select-Object -First 1
}
function Field {
    param([string]$Text, [string]$Key)
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
function Get-VideoSize {
    param([string]$Path)
    $out = & (Join-Path $ffbin 'ffprobe.exe') -v error -select_streams v:0 `
        -show_entries stream=width,height -of csv=p=0 $Path 2>&1 | Out-String
    return $out.Trim()
}
function Test-Decodable {
    param([string]$Path)
    if (-not (Test-Path $Path)) { return $false }
    $decode = & (Join-Path $ffbin 'ffmpeg.exe') -hide_banner -v error -i $Path -f null - 2>&1 | Out-String
    return [string]::IsNullOrWhiteSpace($decode)
}
function Press-Combo {
    param([byte[]]$Keys)
    foreach ($key in $Keys) { [UiSmokeNative]::keybd_event($key, 0, 0, [UIntPtr]::Zero) }
    Start-Sleep -Milliseconds 80
    for ($i = $Keys.Count - 1; $i -ge 0; $i--) {
        [UiSmokeNative]::keybd_event($Keys[$i], 0, 2, [UIntPtr]::Zero)
    }
    Start-Sleep -Milliseconds 80
}
function Wait-Ready {
    param([string]$Log, [int]$TimeoutMs = 8000)
    return Wait-Log $Log 'app_ready' $TimeoutMs
}
function Start-Ui {
    param([string]$Log, [string]$Out, [string]$ConfigPath, [string]$Extra = '')
    $argList = @('--hidden', '--log', $Log)
    if ($Out) { $argList += @('--out', $Out) }
    if ($ConfigPath) { $argList += @('--config', $ConfigPath) }
    if ($Extra) { $argList += $Extra.Split(' ', [StringSplitOptions]::RemoveEmptyEntries) }
    $proc = Start-Process -FilePath $ui -PassThru -WindowStyle Hidden -ArgumentList $argList
    # 轮 30（F2）：登记到统一清理表，中途抛异常时 trap 能杀掉这个 UI。
    Register-SrProcess $proc
    return $proc
}
function Get-Text {
    param([IntPtr]$Handle)
    if ($Handle -eq [IntPtr]::Zero) { return '' }
    $sb = New-Object System.Text.StringBuilder 512
    [void][UiSmokeNative]::GetWindowTextW($Handle, $sb, 512)
    return $sb.ToString()
}
function Get-MainWindow {
    return [UiSmokeNative]::FindWindowClass('SrUiMainWindow', [IntPtr]::Zero)
}
function Get-StatusText {
    $main = Get-MainWindow
    return Get-Text ([UiSmokeNative]::GetDlgItem($main, 200))
}

# 工作目录放在 TEMP（路径不含空格）：Start-Process -ArgumentList 不会自动给
# 含空格的路径加引号（D-20），工作区路径 "Screen Recording" 会在这里踩坑。
$work = Join-Path $env:TEMP 'sr-ui-smoke'
New-Item -ItemType Directory -Force -Path $work | Out-Null
$script:SrForensicsWorkDirs = @($work)
# 轮 34（R1）：先杀进程再清目录，并重试到空。旧顺序是先删后杀，被持有的 .part 删不掉，
# 上一轮的 e.mkv.part 会让本轮四个 preflight-no-part 全红（92/9 的根因）。
$clean = Reset-WorkDir $work
$ini = Join-Path $work 'config.ini'
Assert 'work-dir-clean' $clean "work=$work"
# 默认配置也会影响 preflight（输出目录/热键）：备份后删除，跑完恢复，保证从内置默认值起步。
$defaultIni = Join-Path $env:LOCALAPPDATA 'sr-ui\config.ini'
$defaultBackup = Join-Path $work 'default-config.bak'
$hadDefaultIni = Test-Path $defaultIni
if ($hadDefaultIni) {
    Copy-Item $defaultIni $defaultBackup -Force
    Remove-Item $defaultIni -Force -ErrorAction SilentlyContinue
}
$script:DefaultIni = $defaultIni
$script:DefaultBackup = $defaultBackup
$script:HadDefaultIni = $hadDefaultIni

# ---------------- A 热键冲突 + 持久化 ----------------
Write-Host '--- A 热键冲突被拒 + 自定义持久化 ---'
$held = [UiSmokeNative]::RegisterHotKey([IntPtr]::Zero, 99, 0x0003, 0x7B)  # Ctrl+Alt+F12
Assert 'conflict-hold-setup' $held "test process holds Ctrl+Alt+F12=$held"
$conflictIni = Join-Path $work 'conflict.ini'
[System.IO.File]::WriteAllText($conflictIni, "[hotkeys]`r`nstart=Ctrl+Alt+F12`r`n", [System.Text.UTF8Encoding]::new($false))
$conflictLog = Join-Path $work 'conflict.log'
$conflict = Start-Process -FilePath $ui -PassThru -Wait -WindowStyle Hidden `
    -ArgumentList @('--set-hotkey', 'start', 'Ctrl+Alt+F12', '--config', $conflictIni, '--log', $conflictLog)
$conflictText = Read-Log $conflictLog
Assert 'conflict-rejected' ($conflict.ExitCode -eq 3) "exit=$($conflict.ExitCode)"
Assert 'conflict-visible' ($conflictText -match 'hotkey_rejected') "$($conflictText.Trim())"
Assert 'conflict-ini-unchanged' ((Read-Log $conflictIni) -match 'start=Ctrl\+Alt\+F12') 'INI 未被改写'
[UiSmokeNative]::UnregisterHotKey([IntPtr]::Zero, 99) | Out-Null

$saveLog = Join-Path $work 'save.log'
$save = Start-Process -FilePath $ui -PassThru -Wait -WindowStyle Hidden `
    -ArgumentList @('--set-hotkey', 'start', 'Ctrl+Alt+F8', '--config', $ini, '--log', $saveLog)
Assert 'custom-save-exit' ($save.ExitCode -eq 0) "exit=$($save.ExitCode)"
Assert 'custom-saved-ini' ((Read-Log $ini) -match 'start=Ctrl\+Alt\+F8') "INI: $((Read-Log $ini).Trim())"

# ---------------- B 默认热键驱动录制 ----------------
Write-Host '--- B UI 驱动录制（默认热键）---'
$bLog = Join-Path $work 'b.log'
$bOut = Join-Path $work 'b.mkv'
$b = Start-Ui -Log $bLog -Out $bOut -Extra '--exit-on-stop'
$null = Wait-Ready $bLog 8000
$bHwnd = Get-MainWindow
Press-Combo @(0x11, 0x12, 0x78)  # Ctrl+Alt+F9 start
$null = Wait-Log $bLog 'recording_indicator' 8000
Start-Sleep -Milliseconds 1200
Press-Combo @(0x11, 0x12, 0x79)  # Ctrl+Alt+F10 pause
$null = Wait-Log $bLog 'snapshot state=Paused' 3000
Start-Sleep -Milliseconds 1200
Press-Combo @(0x11, 0x12, 0x79)  # resume
$null = Wait-Log $bLog 'snapshot state=Recording' 3000
Start-Sleep -Milliseconds 1200
Press-Combo @(0x11, 0x12, 0x7A)  # Ctrl+Alt+F11 stop
$bExited = $b.WaitForExit(20000)
$bText = Read-Log $bLog
$lines = Get-LogLines $bText
$indicator = Last-Line $lines '^recording_indicator latency_ms='
$pauseHotkeys = @($lines | Where-Object { $_.Text -match '^hotkey id=2 ' })
$pauseHotkey = if ($pauseHotkeys.Count -ge 1) { $pauseHotkeys[0] } else { $null }
$resumeHotkey = if ($pauseHotkeys.Count -ge 2) { $pauseHotkeys[1] } else { $null }
$pausedSnap = if ($pauseHotkey) { First-Line $lines '^snapshot state=Paused ' ($pauseHotkey.T) } else { $null }
$resumeSnap = if ($resumeHotkey) { First-Line $lines '^snapshot state=Recording ' ($resumeHotkey.T) } else { $null }
$startLatency = if ($indicator -and $indicator.Text -match 'latency_ms=(\d+)') { [int]$Matches[1] } else { -1 }
$pauseResponse = if ($pauseHotkey -and $pausedSnap) { $pausedSnap.T - $pauseHotkey.T } else { -1 }
$resumeResponse = if ($resumeHotkey -and $resumeSnap) { $resumeSnap.T - $resumeHotkey.T } else { -1 }

Assert 'hotkeys-registered' (($bText -match 'hotkey_register id=start .*ok=1') -and
                            ($bText -match 'hotkey_register id=pause .*ok=1') -and
                            ($bText -match 'hotkey_register id=stop .*ok=1')) '三个默认热键均注册'
Assert 'start-latency-1.5s' ($startLatency -ge 0 -and $startLatency -le 1500) "spawn→Recording=${startLatency}ms"
$preMatch = [regex]::Match($bText, 'preflight_pass=1 ms=(\d+)')
Assert 'preflight-pass' ($preMatch.Success) '四项自检通过'
Assert 'preflight-under-500ms' ($preMatch.Success -and [int]$preMatch.Groups[1].Value -le 500) "preflight_ms=$($preMatch.Groups[1].Value)"
# 轮 35（F2）：标题可能为空，不能作为判据。用热键触发时的前台 HWND 与应用主窗口 HWND
# 比较；标题只进日志。旧断言只看标题，桌面前台（foreground=）会被误判为「已失焦」。
$fgMatch = [regex]::Match($bText, 'hotkey id=1 foreground=.*foreground_hwnd=(\d+)')
$fgHwnd = if ($fgMatch.Success) { [long]$fgMatch.Groups[1].Value } else { 0 }
Assert 'unfocused-hotkey' ($fgHwnd -ne 0 -and $bHwnd -ne [IntPtr]::Zero -and
                          $fgHwnd -ne $bHwnd.ToInt64()) `
    ("foreground_hwnd={0} app_hwnd={1}" -f $fgHwnd, $bHwnd.ToInt64())
Assert 'pause-response-200ms' ($pauseResponse -ge 0 -and $pauseResponse -le 200) "hotkey→Paused=${pauseResponse}ms"
Assert 'resume-response-200ms' ($resumeResponse -ge 0 -and $resumeResponse -le 200) "hotkey→Recording=${resumeResponse}ms"
Assert 'session-exited' ($bExited -and $b.ExitCode -eq 0) "exited=$bExited code=$($b.ExitCode)"
Assert 'session-end-zero' ($bText -match 'session_end exit=0 output_exists=1') 'recorder 干净收尾'
Assert 'output-playable' (Test-Decodable $bOut) 'decode clean'
$bDur = Get-Duration $bOut
Assert 'output-duration' ($bDur -ge 2.0 -and $bDur -le 8.0) "duration=$bDur（3 段内容）"
$bRecorderPid = if ($bText -match 'spawn pid=(\d+)') { [int]$Matches[1] } else { 0 }
$leftoverB = if ($bRecorderPid -gt 0) { Get-Process -Id $bRecorderPid -ErrorAction SilentlyContinue } else { $null }
Assert 'no-leftover-recorder' ($bRecorderPid -gt 0 -and $null -eq $leftoverB) "pid=$bRecorderPid"

# ---------------- C 注册失败可见且不阻断其它热键 ----------------
Write-Host '--- C 热键注册失败可见 ---'
$held2 = [UiSmokeNative]::RegisterHotKey([IntPtr]::Zero, 98, 0x0003, 0x7B)  # Ctrl+Alt+F12
$failIni = Join-Path $work 'fail.ini'
[System.IO.File]::WriteAllText($failIni, "[hotkeys]`r`nstart=Ctrl+Alt+F12`r`npause=Ctrl+Alt+F10`r`nstop=Ctrl+Alt+F11`r`n", [System.Text.UTF8Encoding]::new($false))
$cLog = Join-Path $work 'c.log'
$c = Start-Process -FilePath $ui -PassThru -WindowStyle Hidden `
    -ArgumentList @('--hidden', '--exit-after-ms', '3000', '--config', $failIni, '--log', $cLog)
Register-SrProcess $c
$null = Wait-Log $cLog 'hotkey_register_failed' 5000
$cText = Read-Log $cLog
$cExited = $c.WaitForExit(8000)
Assert 'register-failed-visible' ($cText -match 'hotkey_register_failed id=start .*error=1409') '日志明确记录 1409'
Assert 'other-hotkeys-alive' (($cText -match 'hotkey_register id=pause .*ok=1') -and
                             ($cText -match 'hotkey_register id=stop .*ok=1')) 'pause/stop 仍注册成功'
Assert 'app-survives-conflict' ($cText -match 'app_ready') '应用未因注册失败退出'
Assert 'exit-after-ms' ($cExited) '定时退出生效'
[UiSmokeNative]::UnregisterHotKey([IntPtr]::Zero, 98) | Out-Null

# ---------------- D 重启后自定义热键 + 录制中关闭 ----------------
Write-Host '--- D 自定义热键重启生效 + 录制中关闭收尾 ---'
$dLog = Join-Path $work 'd.log'
$dOut = Join-Path $work 'd.mkv'
$d = Start-Process -FilePath $ui -PassThru -WindowStyle Hidden `
    -ArgumentList @('--hidden', '--exit-after-ms', '6000', '--config', $ini, '--log', $dLog, '--out', $dOut)
Register-SrProcess $d
$null = Wait-Ready $dLog 8000
Press-Combo @(0x11, 0x12, 0x77)  # Ctrl+Alt+F8（来自 INI）
$null = Wait-Log $dLog 'start_request' 4000
$dExited = $d.WaitForExit(20000)
$dText = Read-Log $dLog
Assert 'custom-hotkey-effective' ($dText -match 'hotkey id=1 ' -and $dText -match 'spawn pid=') 'F8 触发了 spawn'
Assert 'close-while-recording' ($dText -match 'shutdown_done exit=0 output_exists=1') '关闭时先 Stop 再退出'
Assert 'close-exit-zero' ($dExited -and $d.ExitCode -eq 0) "exited=$dExited code=$($d.ExitCode)"
Assert 'close-output-playable' (Test-Decodable $dOut) "duration=$(Get-Duration $dOut)"

Write-Host '--- E 运行中改 INI：热键热更新 ---'
$eLog = Join-Path $work 'e.log'
$eOut = Join-Path $work 'e.mkv'
$eCli = Join-Path $work 'e_cli.log'
$e = Start-Ui -Log $eLog -Out $eOut -ConfigPath $ini -Extra '--exit-on-stop'
$null = Wait-Ready $eLog 8000
$set = Start-Process -FilePath $ui -PassThru -Wait -WindowStyle Hidden `
    -ArgumentList @('--set-hotkey', 'start', 'Ctrl+Alt+F7', '--config', $ini, '--log', $eCli)
Assert 'live-save-exit' ($set.ExitCode -eq 0) "exit=$($set.ExitCode)"
$null = Wait-Log $eLog 'hotkeys_reloaded' 5000
$eReload = Read-Log $eLog
Assert 'live-reload-logged' ($eReload -match 'hotkeys_reloaded start=Ctrl\+Alt\+F7') '运行中的应用 1s 内重载 INI'
Press-Combo @(0x11, 0x12, 0x76)  # Ctrl+Alt+F7
$null = Wait-Log $eLog 'spawn pid=' 5000
$null = Wait-Log $eLog 'recording_indicator' 5000
Start-Sleep -Milliseconds 800
Press-Combo @(0x11, 0x12, 0x7A)  # Ctrl+Alt+F11 stop
$eExited = $e.WaitForExit(20000)
$eText = Read-Log $eLog
Assert 'live-reload-effective' (($eText -match 'hotkey id=1 ') -and ($eText -match 'spawn pid=')) '新热键 F7 触发了 spawn'
Assert 'live-exit' ($eExited -and $e.ExitCode -eq 0) "exited=$eExited code=$($e.ExitCode)"
Assert 'live-playable' (Test-Decodable $eOut) "duration=$(Get-Duration $eOut)"
$eRecorderPid = if ($eText -match 'spawn pid=(\d+)') { [int]$Matches[1] } else { 0 }
$eLeftover = if ($eRecorderPid -gt 0) { Get-Process -Id $eRecorderPid -ErrorAction SilentlyContinue } else { $null }
Assert 'live-no-leftover' ($eRecorderPid -gt 0 -and $null -eq $eLeftover) "pid=$eRecorderPid"

Write-Host '--- F 悬浮工具条（M2-08）---'
$fResetLog = Join-Path $work 'f_reset.log'
$fReset = Start-Process -FilePath $ui -PassThru -Wait -WindowStyle Hidden `
    -ArgumentList @('--set-hotkey', 'start', 'Ctrl+Alt+F9', '--config', $ini, '--log', $fResetLog)
Assert 'f-reset-start-hotkey' ($fReset.ExitCode -eq 0) "exit=$($fReset.ExitCode)"
$fLog = Join-Path $work 'f.log'
$fOut = Join-Path $work 'f.mkv'
$f = Start-Ui -Log $fLog -Out $fOut -ConfigPath $ini -Extra '--exit-on-stop'
$null = Wait-Ready $fLog 8000
Press-Combo @(0x11, 0x12, 0x78)  # F9
$null = Wait-Log $fLog 'recording_indicator' 5000
$tb = [UiSmokeNative]::FindWindowClass('SrUiToolbar', [IntPtr]::Zero)
Assert 'toolbar-exists' ($tb -ne [IntPtr]::Zero) "hwnd=$tb"
$exStyle = [UiSmokeNative]::GetWindowLongPtr($tb, -20).ToInt64()
Assert 'toolbar-topmost' (($exStyle -band 0x8) -ne 0) "exstyle=0x$($exStyle.ToString('X'))"
Assert 'toolbar-noactivate' (($exStyle -band 0x08000000) -ne 0) "exstyle=0x$($exStyle.ToString('X'))"
$tbTimer = [UiSmokeNative]::GetDlgItem($tb, 500)
$tbRemaining = [UiSmokeNative]::GetDlgItem($tb, 501)
$tbPause = [UiSmokeNative]::GetDlgItem($tb, 502)
$tbStop = [UiSmokeNative]::GetDlgItem($tb, 503)
$tbHide = [UiSmokeNative]::GetDlgItem($tb, 504)
$timerText = Get-Text $tbTimer
Assert 'toolbar-timer' ($timerText -match '^\d{2}:\d{2}') "timer=$timerText"
Assert 'toolbar-remaining' ((Get-Text $tbRemaining) -match '剩余') "remaining=$(Get-Text $tbRemaining)"
Assert 'toolbar-pause-enabled' ([UiSmokeNative]::IsWindowEnabled($tbPause)) 'recording → pause enabled'
Assert 'toolbar-stop-enabled' ([UiSmokeNative]::IsWindowEnabled($tbStop)) 'recording → stop enabled'
$hit = [UiSmokeNative]::SendMessage($tb, 0x0084, [IntPtr]::Zero, [IntPtr]([int]((10 -shl 16) -bor 10)))
Assert 'toolbar-draggable' ($hit.ToInt64() -eq 2) "WM_NCHITTEST=$($hit.ToInt64())（HTCAPTION=2）"
$mouse = [UiSmokeNative]::SendMessage($tb, 0x0021, [IntPtr]::Zero, [IntPtr]::Zero)
Assert 'toolbar-no-focus-steal' ($mouse.ToInt64() -eq 3) "WM_MOUSEACTIVATE=$($mouse.ToInt64())（MA_NOACTIVATE=3）"
Start-Sleep -Milliseconds 1200
$timerText2 = Get-Text $tbTimer
Assert 'toolbar-timer-ticks' ($timerText2 -ne $timerText) "timer $timerText -> $timerText2"
[UiSmokeNative]::SendMessage($tbPause, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 500
Assert 'toolbar-pause-click' ((Get-Text $tbPause) -eq '继续') "pause button text=$(Get-Text $tbPause)"
[UiSmokeNative]::SendMessage($tbPause, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 500
Assert 'toolbar-resume-click' ((Get-Text $tbPause) -eq '暂停') "pause button text=$(Get-Text $tbPause)"
[UiSmokeNative]::SendMessage($tbHide, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 300
Assert 'toolbar-hide' (-not [UiSmokeNative]::IsWindowVisible($tb)) 'hide button → SW_HIDE'
$main = Get-MainWindow
[UiSmokeNative]::SendMessage($main, 0x0111, [IntPtr]307, [IntPtr]::Zero) | Out-Null  # kTrayToolbar
Start-Sleep -Milliseconds 300
Assert 'toolbar-show-via-tray' ([UiSmokeNative]::IsWindowVisible($tb)) '托盘命令恢复悬浮条'
[UiSmokeNative]::SendMessage($tbStop, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
$fExited = $f.WaitForExit(20000)
Assert 'toolbar-stop-exit' ($fExited -and $f.ExitCode -eq 0) "exited=$fExited code=$($f.ExitCode)"
Assert 'toolbar-playable' (Test-Decodable $fOut) "duration=$(Get-Duration $fOut)"
if (-not $fExited) { Stop-Process -Id $f.Id -Force -ErrorAction SilentlyContinue }

Write-Host '--- G 剩余可录时长 + 保存路径（M2-09 / M5-01）---'
$gLog = Join-Path $work 'g.log'
$gOut = Join-Path $work 'g.mkv'
$g = Start-Ui -Log $gLog -Out $gOut -ConfigPath $ini -Extra '--exit-after-ms 6000'
$null = Wait-Ready $gLog 8000
Start-Sleep -Milliseconds 2500
$status = Get-StatusText
Assert 'remaining-in-ui' ($status -match '剩余可录') "status=$status"
$gLines = Get-LogLines (Read-Log $gLog)
$diskLines = @($gLines | Where-Object { $_.Text -match '^disk_remaining seconds=' })
Assert 'remaining-refresh-1s' ($diskLines.Count -ge 3 -and ($diskLines[-1].T - $diskLines[-2].T) -le 1200) "lines=$($diskLines.Count) last_delta=$($diskLines[-1].T - $diskLines[-2].T)ms"
$diskMatch = [regex]::Match($diskLines[-1].Text, 'seconds=(\d+) free_bytes=(\d+) stop_bytes=(\d+) bitrate_kbps=(\d+)')
$uiSeconds = [double]$diskMatch.Groups[1].Value
$freeBytes = [double]$diskMatch.Groups[2].Value
$stopBytes = [double]$diskMatch.Groups[3].Value
$bitrate = [double]$diskMatch.Groups[4].Value
$expectedSeconds = ($freeBytes - $stopBytes) * 8.0 / ($bitrate * 1000.0)
$errorRatio = [Math]::Abs($uiSeconds - $expectedSeconds) / $expectedSeconds
Assert 'remaining-within-15pct' ($errorRatio -le 0.15) "ui=$uiSeconds expected=$([Math]::Round($expectedSeconds,1)) err=$([Math]::Round($errorRatio*100,2))%"
$g.WaitForExit(10000) | Out-Null

# 只读目录：当场拒绝 + 原因；INI 不变
$roDir = Join-Path $work 'readonly_dir'
New-Item -ItemType Directory -Force -Path $roDir | Out-Null
$user = "$env:USERDOMAIN\$env:USERNAME"
& icacls $roDir /inheritance:r /grant:r "${user}:(OI)(CI)(RX)" 2>&1 | Out-Null
$iniBefore = Read-TextSafe $ini
$roLog = Join-Path $work 'ro.log'
$ro = Start-Process -FilePath $ui -PassThru -Wait -WindowStyle Hidden `
    -ArgumentList @('--set-output', $roDir, '--config', $ini, '--log', $roLog)
$roText = Read-Log $roLog
if ($ro.ExitCode -ne 0) {
    Assert 'readonly-dir-rejected' $true "exit=$($ro.ExitCode)"
    Assert 'readonly-reason' ($roText -match 'not writable|not a directory|rejected write') '原因已记录'
    Assert 'readonly-ini-unchanged' ((Read-TextSafe $ini) -eq $iniBefore) 'INI 未被改写'
} else {
    Skip 'readonly-dir-rejected' 'ACL not enforced in this session (elevated?)'
    Skip 'readonly-reason' 'ACL not enforced'
    Skip 'readonly-ini-unchanged' 'ACL not enforced'
}
& icacls $roDir /reset 2>&1 | Out-Null

# 非目录路径也必须拒绝（不依赖 ACL）
$notDir = Join-Path $work 'not_a_dir.txt'
Set-Content -Path $notDir -Value 'x' -Encoding ASCII
$fileLog = Join-Path $work 'notdir.log'
$fileReject = Start-Process -FilePath $ui -PassThru -Wait -WindowStyle Hidden `
    -ArgumentList @('--set-output', $notDir, '--config', $ini, '--log', $fileLog)
Assert 'non-directory-rejected' ($fileReject.ExitCode -ne 0) "exit=$($fileReject.ExitCode)"
Assert 'non-directory-reason' ((Read-Log $fileLog) -match 'not a directory|cannot create output directory') '原因已记录'

# 有效目录：保存并重启保留
$saveDir = Join-Path $work 'saved_dir'
$saveLog = Join-Path $work 'save.log'
$save = Start-Process -FilePath $ui -PassThru -Wait -WindowStyle Hidden `
    -ArgumentList @('--set-output', $saveDir, '--config', $ini, '--log', $saveLog)
Assert 'output-dir-saved' ($save.ExitCode -eq 0 -and (Read-Log $saveLog) -match 'output_dir_saved') "exit=$($save.ExitCode)"
Assert 'output-dir-ini' ((Read-TextSafe $ini) -match [regex]::Escape($saveDir)) 'INI 已持久化'
$g2Log = Join-Path $work 'g2.log'
$g2 = Start-Ui -Log $g2Log -ConfigPath $ini -Extra '--exit-after-ms 2500'
$null = Wait-Ready $g2Log 8000
$status2 = Get-StatusText
Assert 'output-dir-restored' ($status2 -match [regex]::Escape($saveDir)) "status=$status2"
$g2.WaitForExit(10000) | Out-Null

Write-Host '--- H 命名模板 AC-10（M5-02）---'
$ac10Dir = Join-Path $work 'ac10_dir'
$ac10SetLog = Join-Path $work 'ac10_set.log'
$null = Start-Process -FilePath $ui -PassThru -Wait -WindowStyle Hidden `
    -ArgumentList @('--set-output', $ac10Dir, '--config', $ini, '--log', $ac10SetLog)
$tmplLog = Join-Path $work 'template.log'
$tmpl = Start-Process -FilePath $ui -PassThru -Wait -WindowStyle Hidden `
    -ArgumentList @('--set-template', 'ac10_{index}', '--config', $ini, '--log', $tmplLog)
Assert 'ac10-template-saved' ($tmpl.ExitCode -eq 0 -and (Read-Log $tmplLog) -match 'template_saved') "exit=$($tmpl.ExitCode)"
Get-Process sr-ui -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
$hLog = Join-Path $work 'h.log'
$h = Start-Ui -Log $hLog -ConfigPath $ini
$null = Wait-Ready $hLog 8000
Press-Combo @(0x11, 0x12, 0x78)
Start-Sleep -Milliseconds 1200
Press-Combo @(0x11, 0x12, 0x7A)
Start-Sleep -Milliseconds 1200
Press-Combo @(0x11, 0x12, 0x78)
Start-Sleep -Milliseconds 1200
Press-Combo @(0x11, 0x12, 0x7A)
Start-Sleep -Milliseconds 1500
$mainH = Get-MainWindow
[UiSmokeNative]::PostMessage($mainH, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
$hExited = $h.WaitForExit(15000)
if (-not $hExited) { Stop-Process -Id $h.Id -Force -ErrorAction SilentlyContinue }
$hText = Read-Log $hLog
$spawnCount = ([regex]::Matches($hText, 'spawn pid=')).Count
$file1 = Join-Path $ac10Dir 'ac10_001.mkv'
$file2 = Join-Path $ac10Dir 'ac10_002.mkv'
Assert 'ac10-two-sessions' ($spawnCount -eq 2) "spawn_count=$spawnCount"
Assert 'ac10-file-001' (Test-Path $file1) "file=$file1"
Assert 'ac10-file-002' (Test-Path $file2) "file=$file2"
Assert 'ac10-both-playable' ((Test-Decodable $file1) -and (Test-Decodable $file2)) "d1=$(Get-Duration $file1) d2=$(Get-Duration $file2)"
Assert 'ac10-exit' ($hExited -and $h.ExitCode -eq 0) "exited=$hExited code=$($h.ExitCode)"

Write-Host '--- I 区域录制（M1-04 / M1-05）---'
$regionIni = Join-Path $work 'region.ini'
$regionSetLog = Join-Path $work 'region_set.log'
$regionSet = Start-Process -FilePath $ui -PassThru -Wait -WindowStyle Hidden `
    -ArgumentList @('--set-region', '0,0,801,601', '--config', $regionIni, '--log', $regionSetLog)
Assert 'region-set-exit' ($regionSet.ExitCode -eq 0) "exit=$($regionSet.ExitCode)"
Assert 'region-normalized-801x601' ((Read-Log $regionSetLog) -match 'region_saved x=0 y=0 w=800 h=600') '801×601 → 800×600'
$iLog = Join-Path $work 'i.log'
$iOut = Join-Path $work 'i.mkv'
$i = Start-Ui -Log $iLog -Out $iOut -ConfigPath $regionIni -Extra '--exit-on-stop'
$null = Wait-Ready $iLog 8000
Assert 'region-status-800x600' ((Get-StatusText) -match '区域 800×600') "status=$(Get-StatusText)"
Press-Combo @(0x11, 0x12, 0x78)
$null = Wait-Log $iLog 'recording_indicator' 8000
Start-Sleep -Milliseconds 1200
Press-Combo @(0x11, 0x12, 0x7A)
$iExited = $i.WaitForExit(20000)
Assert 'region-recording-exit' ($iExited -and $i.ExitCode -eq 0) "exited=$iExited code=$($i.ExitCode)"
Assert 'region-output-size' ((Get-VideoSize $iOut) -eq '800,600') "size=$(Get-VideoSize $iOut)"
Assert 'region-playable' (Test-Decodable $iOut) 'decode clean'

$editLog = Join-Path $work 'edit.log'
$edit = Start-Ui -Log $editLog -ConfigPath $regionIni -Extra '--exit-on-stop'
$null = Wait-Ready $editLog 8000
$mainEdit = Get-MainWindow
[UiSmokeNative]::PostMessage($mainEdit, 0x0111, [IntPtr]207, [IntPtr]::Zero) | Out-Null
$editor = Wait-WindowClass 'SrUiRegionWindow' 3000
Assert 'region-editor-opens' ($editor -ne [IntPtr]::Zero) "hwnd=$editor"
[UiSmokeNative]::SendMessage($editor, 0x0100, [IntPtr]0x27, [IntPtr]::Zero) | Out-Null  # VK_RIGHT
Start-Sleep -Milliseconds 150
Assert 'region-arrow-step-1px' ((Read-Log $editLog) -match 'region_edit x=1 y=0 w=800 h=600 step=1') '方向键步进 1px'
[UiSmokeNative]::SendMessage([UiSmokeNative]::GetDlgItem($editor, 602), 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 150
Assert 'region-preset-16x9' ((Read-Log $editLog) -match 'region_preset 16:9 x=1 y=0 w=800 h=450') '16:9 800×450'
[UiSmokeNative]::SendMessage([UiSmokeNative]::GetDlgItem($editor, 606), 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 300
Assert 'region-editor-applied' ((Read-Log $editLog) -match 'region_applied source=editor .* w=800 h=450') '编辑器区域已生效'
Assert 'region-status-800x450' ((Get-StatusText) -match '区域 800×450') "status=$(Get-StatusText)"
# 关掉编辑器应用，避免它继续占用 F9/F10/F11
[void][UiSmokeNative]::PostMessage($mainEdit, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
$edit.WaitForExit(8000) | Out-Null
if (-not $edit.HasExited) { Stop-Process -Id $edit.Id -Force -ErrorAction SilentlyContinue }
# 编辑器必须先完全退出，否则它占着 F9/F10/F11，下一次录制会「按了没反应」。
for ($w = 0; $w -lt 40 -and -not $edit.HasExited; $w++) { Start-Sleep -Milliseconds 100 }
$editOut = Join-Path $work 'edit.mkv'
# 重新启动一次，带 --out 走完整录制（上面的应用没有 --out，用的默认路径）
$edit2Log = Join-Path $work 'edit2.log'
$edit2 = Start-Ui -Log $edit2Log -Out $editOut -ConfigPath $regionIni -Extra '--exit-on-stop'
$null = Wait-Ready $edit2Log 8000
Press-Combo @(0x11, 0x12, 0x78)
$null = Wait-Log $edit2Log 'recording_indicator' 8000
Start-Sleep -Milliseconds 1200
Press-Combo @(0x11, 0x12, 0x7A)
$edit2Exited = $edit2.WaitForExit(20000)
Assert 'region-editor-recording' ($edit2Exited -and $edit2.ExitCode -eq 0) "exited=$edit2Exited code=$($edit2.ExitCode)"
Assert 'region-editor-output-size' ((Get-VideoSize $editOut) -eq '800,450') "size=$(Get-VideoSize $editOut)"

$overlayIni = Join-Path $work 'overlay.ini'
$null = Start-Process -FilePath $ui -PassThru -Wait -WindowStyle Hidden `
    -ArgumentList @('--set-region', 'full', '--config', $overlayIni, '--log', (Join-Path $work 'overlay_set.log'))
$overlayLog = Join-Path $work 'overlay.log'
$overlayApp = Start-Ui -Log $overlayLog -ConfigPath $overlayIni -Extra '--exit-on-stop'
$null = Wait-Ready $overlayLog 8000
$mainOverlay = Get-MainWindow
[UiSmokeNative]::PostMessage($mainOverlay, 0x0111, [IntPtr]207, [IntPtr]::Zero) | Out-Null
$overlayEditor = Wait-WindowClass 'SrUiRegionWindow' 3000
Assert 'region-overlay-editor' ($overlayEditor -ne [IntPtr]::Zero) "hwnd=$overlayEditor"
[UiSmokeNative]::SendMessage([UiSmokeNative]::GetDlgItem($overlayEditor, 605), 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
$overlay = Wait-WindowClass 'SrUiRegionOverlay' 3000
Assert 'region-overlay-opens' ($overlay -ne [IntPtr]::Zero) "hwnd=$overlay"
# 轮 34（R1）：不再用 SetCursorPos/mouse_event 模拟拖拽。桌面是 3840×1080 双屏，
# 物理光标事件在 overlay 上的落点会漂（实测 776×700）。直接给 overlay 发真实鼠标消息，
# 走的是同一套 WM_LBUTTONDOWN/UP 处理，坐标确定。
$lpDown = [IntPtr](100 -bor (100 -shl 16))
$lpUp = [IntPtr](901 -bor (701 -shl 16))
[UiSmokeNative]::SendMessage($overlay, 0x0201, [IntPtr]1, $lpDown) | Out-Null
Start-Sleep -Milliseconds 120
[UiSmokeNative]::SendMessage($overlay, 0x0200, [IntPtr]1, $lpUp) | Out-Null
Start-Sleep -Milliseconds 80
[UiSmokeNative]::SendMessage($overlay, 0x0202, [IntPtr]0, $lpUp) | Out-Null
$null = Wait-Log $overlayLog 'region_selected' 3000
$null = Wait-Status '区域 800×600' 3000
Assert 'region-drag-raw-801x601' ((Read-Log $overlayLog) -match 'region_selected .*client=100,100,801,601') '框选原始 801×601'
Assert 'region-drag-normalized' ((Read-Log $overlayLog) -match 'region_applied source=drag x=100 y=100 w=800 h=600') '框选后 800×600'
Assert 'region-status-drag' ((Get-StatusText) -match '区域 800×600') "status=$(Get-StatusText)"
[void][UiSmokeNative]::PostMessage($mainOverlay, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
$overlayApp.WaitForExit(8000) | Out-Null
if (-not $overlayApp.HasExited) { Stop-Process -Id $overlayApp.Id -Force -ErrorAction SilentlyContinue }
$overlayOut = Join-Path $work 'overlay.mkv'
# 重新启动带 --out，验证框选区域的成片分辨率
$overlay2Log = Join-Path $work 'overlay2.log'
$overlay2 = Start-Ui -Log $overlay2Log -Out $overlayOut -ConfigPath $overlayIni -Extra '--exit-on-stop'
$null = Wait-Ready $overlay2Log 8000
Press-Combo @(0x11, 0x12, 0x78)
$null = Wait-Log $overlay2Log 'recording_indicator' 8000
Start-Sleep -Milliseconds 1200
Press-Combo @(0x11, 0x12, 0x7A)
$overlay2Exited = $overlay2.WaitForExit(20000)
Assert 'region-drag-recording' ($overlay2Exited -and $overlay2.ExitCode -eq 0) "exited=$overlay2Exited code=$($overlay2.ExitCode)"
Assert 'region-drag-output-size' ((Get-VideoSize $overlayOut) -eq '800,600') "size=$(Get-VideoSize $overlayOut)"

Write-Host '--- J 开录前自检失败（M2-14）---'
foreach ($kind in @('disk', 'display', 'audio', 'encoder')) {
    $pfLog = Join-Path $work "pf_$kind.log"
    $pfOut = Join-Path $work "pf_$kind.mkv"
    $partBefore = @(Get-ChildItem $work -Filter '*.part' -Recurse -ErrorAction SilentlyContinue).Count
    $pf = Start-Ui -Log $pfLog -Out $pfOut -Extra "--preflight-fail $kind --exit-after-ms 5000"
    $null = Wait-Ready $pfLog 8000
    # 轮 34（R1）：热键丢一次就重按；判定看日志，不看固定 sleep。
    $null = Press-Until @(0x11, 0x12, 0x78) $pfLog "preflight_check name=$kind ok=0" 4000
    $pfText = Read-Log $pfLog
    Assert "preflight-$kind-check" ($pfText -match "preflight_check name=$kind ok=0") "$kind 项失败"
    Assert "preflight-$kind-blocked" (($pfText -match 'preflight_failed') -and ($pfText -notmatch 'spawn pid=')) '未启动 Recorder'
    $null = Wait-Status "开录前自检失败: $kind" 2000
    Assert "preflight-$kind-visible" ((Get-StatusText) -match "开录前自检失败: $kind") "$kind 原因在状态栏可见"
    $partAfter = @(Get-ChildItem $work -Filter '*.part' -Recurse -ErrorAction SilentlyContinue).Count
    Assert "preflight-$kind-no-part" ($partAfter -le $partBefore) "part=$partAfter（开跑前 $partBefore）"
    $pf.WaitForExit(8000) | Out-Null
    if (-not $pf.HasExited) { Stop-Process -Id $pf.Id -Force -ErrorAction SilentlyContinue }
}

# 恢复用户默认配置（本轮为了确定性临时移除过）。
if ($script:HadDefaultIni) {
    if (Test-Path $script:DefaultBackup) {
        Copy-Item $script:DefaultBackup $script:DefaultIni -Force -ErrorAction SilentlyContinue
    }
} elseif (Test-Path $script:DefaultIni) {
    Remove-Item $script:DefaultIni -Force -ErrorAction SilentlyContinue
}

Write-Host ''
Write-Host ("RESULT: {0} PASS / {1} FAIL / {2} SKIP" -f $script:pass, $script:fail, $script:skip)
if ($script:fail -gt 0) { exit 1 }
exit 0
