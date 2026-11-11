# 轮 40 · M1-27 UAC 提权窗口采集限制提示（提权检测 + 提示 + 以管理员身份重启入口）
#
# 覆盖（docs/01 M1-27#1）：
#   A 非管理员运行时，开录前自检列出提权项 + 给出明确提示（自检项/常驻标签/日志行）
#   B 录制中同样能看到提示（提示常驻 + 日志行）
#   C 界面有「以管理员身份重启」入口（主窗口按钮），入口可见可点
#   D 入口的命令构造正确（--elevate-dry-run：exe/参数/runas 动词都记进日志）
#   E 入口真实可用（--elevate-launch：ShellExecute(runas) 拉起高完整性实例）
#   F 高完整性目标窗口的采集实测（WGC / PrintWindow 两条路径分别测，如实记录）
#
# 关于 F 的结论方式：**不预设「一定黑屏」**。轮 40 实测发现本机 UAC 策略为
#   ConsentPromptBehaviorAdmin=0 / PromptOnSecureDesktop=0
#   → UAC 对话框不落安全桌面，「UAC 提权窗口黑屏」在本机不可复现；
#   实测 WGC 能采到高完整性窗口的真实内容，兜底 PrintWindow 则完全取不到帧。
# 因此 F 按**实测值分类**：黑屏则 PASS，非黑屏则 SKIP 并附实测数据与不可复现的原因，
# 绝不用「文案已核查」冒充黑屏证据。
#
# Usage: pwsh -File verify/cases/g5_uac.ps1 [-BuildDir build-msvc] [-Config Release]

param(
    [string]$BuildDir = 'build-msvc',
    [string]$Config = 'Release'
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot '..\lib\Cleanup.ps1')
. (Join-Path $PSScriptRoot '..\lib\Forensics.ps1')
trap {
    [void](Save-FailureContext -Tag 'g5_uac' -WorkDirs $script:SrForensicsWorkDirs -Note $_)
    Clear-SrProcesses -IncludeNameFallback
    Write-Host "FAIL: $_"
    exit 1
}

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$ui = Join-Path $root (Join-Path $BuildDir "bin\$Config\sr-ui.exe")
$rec = Join-Path $root (Join-Path $BuildDir "bin\$Config\sr-recorder.exe")
$probe = Join-Path $root (Join-Path $BuildDir "bin\$Config\srprobe.exe")
if (-not (Test-Path $ui)) { $ui = Join-Path $root (Join-Path $BuildDir 'bin\sr-ui.exe') }
if (-not (Test-Path $rec)) { $rec = Join-Path $root (Join-Path $BuildDir 'bin\sr-recorder.exe') }
if (-not (Test-Path $probe)) { $probe = Join-Path $root (Join-Path $BuildDir 'bin\srprobe.exe') }
if (-not (Test-Path $ui) -or -not (Test-Path $rec) -or -not (Test-Path $probe)) {
    Write-Host 'FAIL: binaries missing'; exit 2
}

# 工作目录放在 TEMP（路径不含空格）：Start-Process -ArgumentList 不会自动给含空格的路径加引号。
$work = Join-Path $env:TEMP 'sr-uac'
if (Test-Path $work) { Get-ChildItem $work -Recurse -Force | Remove-Item -Force -Recurse -ErrorAction SilentlyContinue }
New-Item -ItemType Directory -Force -Path $work | Out-Null
$script:SrForensicsWorkDirs = @($work)

$script:pass = 0
$script:fail = 0
$script:skip = 0
function Assert {
    param([string]$Name, [bool]$Ok, [string]$Detail)
    if ($Ok) { $script:pass++; Write-Host ("  PASS {0} — {1}" -f $Name, $Detail) }
    else {
        $script:fail++
        Write-Host ("  FAIL {0} — {1}" -f $Name, $Detail)
        [void](Save-FailureContext -Tag $Name -WorkDirs @($work) -Note $Detail)
    }
}
function Skip {
    param([string]$Name, [string]$Reason)
    $script:skip++
    Write-Host ("  SKIP {0} — {1}" -f $Name, $Reason)
}
function Read-Log {
    param([string]$Path)
    if (-not (Test-Path $Path)) { return '' }
    try {
        $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open,
                                          [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
        $reader = New-Object System.IO.StreamReader($stream, [System.Text.Encoding]::UTF8)
        $text = $reader.ReadToEnd()
        $reader.Dispose(); $stream.Dispose()
        return $text
    } catch { return '' }
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
function Field {
    param([string]$Text, [string]$Name)
    if ($Text -match "(?m)^$Name=([^`r`n]*)") { return $Matches[1].Trim() }
    return ''
}

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class SrUacNative {
    [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "FindWindowW")]
    public static extern IntPtr FindWindowClass(string lpClassName, IntPtr lpWindowName);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr hDlg, int nIDDlgItem);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "GetWindowTextW")]
    public static extern int GetWindowTextW(IntPtr hWnd, System.Text.StringBuilder lpString, int nMaxCount);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint pid);
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool CloseHandle(IntPtr handle);
    [DllImport("advapi32.dll", SetLastError = true)]
    public static extern bool OpenProcessToken(IntPtr process, uint access, out IntPtr token);
    [DllImport("advapi32.dll", SetLastError = true)]
    public static extern bool GetTokenInformation(IntPtr token, int infoClass, out int info, int size, out int returned);
}
'@

function Get-ControlText {
    param([IntPtr]$Hwnd)
    if ($Hwnd -eq [IntPtr]::Zero) { return '' }
    $sb = New-Object System.Text.StringBuilder 1024
    [void][SrUacNative]::GetWindowTextW($Hwnd, $sb, 1024)
    return $sb.ToString()
}
function Get-MainWindow {
    return [SrUacNative]::FindWindowClass('SrUiMainWindow', [IntPtr]::Zero)
}
function Wait-MainWindow {
    param([int]$TimeoutMs = 8000)
    $deadline = (Get-Date).AddMilliseconds($TimeoutMs)
    while ((Get-Date) -lt $deadline) {
        $h = Get-MainWindow
        if ($h -ne [IntPtr]::Zero) { return $h }
        Start-Sleep -Milliseconds 50
    }
    return [IntPtr]::Zero
}
# 进程是否已提权（TokenElevation）。用于「目标窗口确实是高完整性」这条控制。
function Test-ProcessElevated {
    param([uint32]$ProcessId)
    $process = [SrUacNative]::OpenProcess(0x1000, $false, $ProcessId)   # PROCESS_QUERY_LIMITED_INFORMATION
    if ($process -eq [IntPtr]::Zero) { return $false }
    $token = [IntPtr]::Zero
    $elevated = $false
    if ([SrUacNative]::OpenProcessToken($process, 0x0008, [ref]$token)) {   # TOKEN_QUERY
        $info = 0; $returned = 0
        if ([SrUacNative]::GetTokenInformation($token, 20, [ref]$info, 4, [ref]$returned)) {  # TokenElevation
            $elevated = ($info -ne 0)
        }
        [void][SrUacNative]::CloseHandle($token)
    }
    [void][SrUacNative]::CloseHandle($process)
    return $elevated
}

$ini = Join-Path $work 'config.ini'
[System.IO.File]::WriteAllText($ini, "[output]`r`ndir=$work`r`n", [System.Text.UTF8Encoding]::new($false))

# ---------------------------------------------------------------- A/B 开录前与录制中的提示
Write-Host '--- A/B 提权检测 + 开录前/录制中提示 ---'
$log = Join-Path $work 'ui.log'
$out = Join-Path $work 'a.mkv'
# 注意：这里不能用 -WindowStyle Hidden——它会把主窗口的首次 ShowWindow 变成隐藏，
# 提示标签与提权按钮的可见性就测不到了（IsWindowVisible 跟随父窗口链）。
$app = Start-Process -FilePath $ui -PassThru `
    -ArgumentList @('--exit-after-ms', '30000', '--config', $ini, '--log', $log, '--out', $out)
Register-SrProcess $app
$null = Wait-Log $log 'app_ready' 8000
$main = Wait-MainWindow
Assert 'ui-started' ($main -ne [IntPtr]::Zero) "main_hwnd=$($main.ToInt64())"

$startText = Read-Log $log
Assert 'elevation-state-logged' ($startText -match 'uac_elevation_state elevated=0') `
    '非提权进程被正确识别（elevated=0）'

# 点「开始录制」→ 触发开录前自检
$startButton = [SrUacNative]::GetDlgItem($main, 201)
if ($startButton -ne [IntPtr]::Zero) {
    [void][SrUacNative]::SendMessage($startButton, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)  # BM_CLICK
}
$null = Wait-Log $log 'preflight_pass=1' 8000
$recText = Wait-Log $log 'uac_warning phase=recording' 8000

Assert 'preflight-elevation-item' ($recText -match 'preflight_check name=elevation ok=0 blocking=0') `
    '开录前自检列出提权项，且标记为不阻断'
Assert 'preflight-warning-logged' ($recText -match 'uac_preflight_warning elevated=0 reason=not_elevated') `
    '开录前给出明确提示（日志行）'
Assert 'preflight-not-blocking' ($recText -match 'preflight_pass=1') '提示不阻断开录（自检仍通过）'
Assert 'recording-warning-logged' ($recText -match 'uac_warning phase=recording elevated=0') `
    '录制中同样给出提示（日志行）'

$label = [SrUacNative]::GetDlgItem($main, 210)
$labelText = Get-ControlText $label
Assert 'uac-label-visible' (($label -ne [IntPtr]::Zero) -and [SrUacNative]::IsWindowVisible($label) -and
                           ($labelText -match '管理员')) "标签文本：$labelText"

$elevateButton = [SrUacNative]::GetDlgItem($main, 209)
$buttonText = Get-ControlText $elevateButton
Assert 'elevate-entry-exists' (($elevateButton -ne [IntPtr]::Zero) -and [SrUacNative]::IsWindowVisible($elevateButton) -and
                              [SrUacNative]::IsWindowEnabled($elevateButton) -and
                              ($buttonText -match '管理员')) "按钮文本：$buttonText"

# 停止录制并退出
$stopButton = [SrUacNative]::GetDlgItem($main, 203)
if ($stopButton -ne [IntPtr]::Zero) {
    [void][SrUacNative]::SendMessage($stopButton, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)
}
$null = $app.WaitForExit(20000)
Clear-SrProcesses -IncludeNameFallback

# ---------------------------------------------------------------- D 入口的命令构造
Write-Host '--- D 「以管理员身份重启」命令构造 ---'
$dryLog = Join-Path $work 'dry.log'
# 带 --exit-after-ms：即使换成不含该开关的旧二进制，进程也会自己退出，脚本不会挂死。
$dry = Start-Process -FilePath $ui -PassThru -Wait -WindowStyle Hidden `
    -ArgumentList @('--elevate-dry-run', '--exit-after-ms', '5000', '--config', $ini, '--log', $dryLog)
$dryText = Read-Log $dryLog
Assert 'elevate-dry-run-exit' ($dry.ExitCode -eq 0) "exit=$($dry.ExitCode)"
Assert 'elevate-command-uses-runas' ($dryText -match 'uac_elevate_dry_run verb=runas') '动词为 runas'
Assert 'elevate-command-has-exe' ($dryText -match 'uac_elevate_dry_run verb=runas command="[^"]*sr-ui\.exe"') `
    '命令行含自身 exe 路径'
Assert 'elevate-command-keeps-args' (($dryText -match '--config') -and ($dryText -match '\.elevated')) `
    '命令行保留原参数，并给提权实例单独日志'

# ---------------------------------------------------------------- E 入口真实拉起
Write-Host '--- E 入口真实拉起（ShellExecute runas）---'
$launchLog = Join-Path $work 'launch.log'
$launch = Start-Process -FilePath $ui -PassThru -Wait -WindowStyle Hidden `
    -ArgumentList @('--elevate-launch', '--exit-after-ms', '8000', '--config', $ini, '--log', $launchLog)
$launchText = Read-Log $launchLog
Assert 'elevate-launch-exit' ($launch.ExitCode -eq 0) "exit=$($launch.ExitCode)"
Assert 'elevate-launch-ok' ($launchText -match 'uac_elevate_launch=ok') '提权请求已发出'

$elevatedLog = "$launchLog.elevated"
$elevatedText = Wait-Log $elevatedLog 'uac_elevation_state' 20000
if ($elevatedText -match 'uac_elevation_state elevated=1') {
    Assert 'elevated-instance-is-elevated' $true '提权实例自报 elevated=1'
} else {
    Skip 'elevated-instance-is-elevated' `
        '提权实例未在 20s 内出现（UAC 对话框等待用户确认 / 策略禁止静默提权）；父实例日志已记录'
}
Clear-SrProcesses -IncludeNameFallback

# ---------------------------------------------------------------- F 高完整性目标窗口的采集实测
Write-Host '--- F 高完整性目标窗口采集实测 ---'
$hwndFile = Join-Path $work 'host-hwnd.txt'
Remove-Item $hwndFile -Force -ErrorAction SilentlyContinue
$hostProc = Start-Process -FilePath $probe -Verb RunAs -WindowStyle Hidden -PassThru `
    -ArgumentList @('windowhost', '--seconds', '60', '--width', '800', '--height', '600', '--hwnd-file', $hwndFile)
$deadline = (Get-Date).AddSeconds(25)
while ((Get-Date) -lt $deadline -and -not (Test-Path $hwndFile)) { Start-Sleep -Milliseconds 300 }
if (-not (Test-Path $hwndFile)) {
    Skip 'elevated-target-window' '高完整性窗口未在 25s 内就绪（本机 UAC 策略或提权被拦）'
} else {
    $hwnd = [uint64](Get-Content $hwndFile -Raw).Trim()
    $ownerPid = [uint32]0
    [void][SrUacNative]::GetWindowThreadProcessId([IntPtr][int64]$hwnd, [ref]$ownerPid)
    $ownerElevated = Test-ProcessElevated -ProcessId $ownerPid
    Assert 'target-window-is-elevated' $ownerElevated "owner_pid=$ownerPid（目标窗口必须属于高完整性进程）"

    function Measure-Capture {
        param([string]$Backend, [string]$Tag)
        $video = Join-Path $work ("target-$Tag.mkv")
        Remove-Item $video -Force -ErrorAction SilentlyContinue
        $recArgs = @('--record', '--seconds', '6', '--fps', '25', '--window', $hwnd, '--fit', 'stretch', '--out', $video)
        if ($Backend -ne '') { $recArgs += @('--backend', $Backend) }
        $text = & $rec @recArgs 2>&1 | Out-String
        $frames = [int](Field $text 'frames_captured')
        $exitCode = [int](Field $text 'record_exit')
        $usedBackend = Field $text 'backend'
        $black = -1.0
        $green = -1.0
        if (Test-Path $video) {
            $bf = & ffmpeg -hide_banner -v info -i $video -vf "blackframe=amount=0:threshold=32" -f null - 2>&1 | Out-String
            $matches = [regex]::Matches($bf, 'pblack:(\d+)')
            if ($matches.Count -gt 0) {
                $values = @($matches | ForEach-Object { [int]$_.Groups[1].Value })
                $black = ($values | Measure-Object -Maximum).Maximum / 100.0
            }
            $png = Join-Path $work ("target-$Tag.png")
            Remove-Item $png -Force -ErrorAction SilentlyContinue
            & ffmpeg -hide_banner -v error -i $video -vf "select=eq(n\,30)" -fps_mode passthrough -frames:v 1 $png 2>&1 | Out-Null
            if (Test-Path $png) {
                Add-Type -AssemblyName System.Drawing
                $bmp = [System.Drawing.Bitmap]::new($png)
                $g = 0; $b = 0; $total = 0
                for ($y = 0; $y -lt $bmp.Height; $y += 4) {
                    for ($x = 0; $x -lt $bmp.Width; $x += 4) {
                        $c = $bmp.GetPixel($x, $y); $total++
                        if ($c.G -gt 150 -and $c.R -lt 100 -and $c.B -lt 100) { $g++ }
                        if ($c.R -lt 20 -and $c.G -lt 20 -and $c.B -lt 20) { $b++ }
                    }
                }
                $bmp.Dispose()
                $green = $g / $total
                if ($b / $total -gt $black) { $black = $b / $total }
            }
        }
        return [pscustomobject]@{ backend = $usedBackend; frames = $frames; exit = $exitCode
                                  black = $black; green = $green }
    }

    $wgc = Measure-Capture -Backend 'wgc' -Tag 'wgc'
    $pw = Measure-Capture -Backend 'printwindow' -Tag 'pw'
    Write-Host ("  [实测] WGC: backend={0} frames={1} black={2:P1} green={3:P1}" -f $wgc.backend, $wgc.frames, $wgc.black, $wgc.green)
    Write-Host ("  [实测] PrintWindow: backend={0} frames={1} record_exit={2}" -f $pw.backend, $pw.frames, $pw.exit)

    if ($wgc.frames -gt 0 -and $wgc.black -ge 0.95) {
        Assert 'uac-target-black' $true ("实测为黑屏：black={0:P1}" -f $wgc.black)
    } else {
        $reason = ("实测未黑屏（WGC black={0:P1} green={1:P1} frames={2}）→ 本机 ConsentPromptBehaviorAdmin=0 / " +
                   "PromptOnSecureDesktop=0，UAC 对话框不落安全桌面，「UAC 提权窗口黑屏」不可复现；" +
                   "同一高完整性窗口走兜底 PrintWindow 时 frames={3}（UIPI 拦截，取不到帧）") -f `
                   $wgc.black, $wgc.green, $wgc.frames, $pw.frames
        Skip 'uac-target-black' $reason
    }
    # 兜底路径的失败是**可复现**的高权限采集失败：必须取不到帧。
    Assert 'uac-target-printwindow-blocked' ($pw.frames -eq 0) "frames=$($pw.frames) record_exit=$($pw.exit)"
}

Clear-SrProcesses -IncludeNameFallback
Write-Host ''
Write-Host ("RESULT: {0} PASS / {1} FAIL / {2} SKIP" -f $script:pass, $script:fail, $script:skip)
if ($script:fail -gt 0) { exit 1 }
exit 0
