# 轮 55（0.2-1）：UI 主题与工作台骨架的**像素级**验收用例。
#
# 判据来源：docs/29 §4（视觉规范）+ .codebuddy/plans/轮55指令.md P0-4。
# 证据口径：所有颜色/尺寸都由**截图取像素**得到，日志只用来定位采样坐标与锚点，
#          「日志里写了主题名」不算证据（指令明确要求）。
#
# Usage: pwsh -File verify/cases/ui_theme.ps1 [-BuildDir build-msvc] [-Config Release]
#
# 断言清单（≥10）：
#   1 base-color            窗口底色 ≈ #0F1115
#   2 card-color            卡片底色 ≈ #161A20
#   3 primary-fill          主按钮填充 ≈ #3B82F6
#   4 primary-hover         悬停后主按钮变亮（≥3 级，且 ≈ #5B9CF8）
#   5 text-color            标题文本最亮像素 ≈ #E6EAF0
#   6 header-height-96dpi   Header 高度 = 48±1px（像素边界）
#   7 statusbar-height      状态栏高度 = 32±1px（像素边界）
#   8 dpi-scale-125         --dpi-scale 1.25 后 Header/状态栏 ×1.25 ±2px
#   9 panel-auto-collapse   最小窗口 860×560 时右侧面板自动收起（该区域回到底色）
#   10 theme-light-base     --theme light 底色 ≈ #F7F8FA
#   11 no-system-gray       三处描边采样均为 token 描边色（无系统默认边框色）
#   12 radius-card          卡片四角是圆角（角像素≠卡片色，内缩后=卡片色）
param(
    [string]$BuildDir = 'build-msvc',
    [string]$Config = 'Release'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
# -BuildDir 可以是相对仓库根，也可以是绝对路径（「先红」时用旧版本包内的 exe）。
$base = if ([System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $root $BuildDir }
$exe = Join-Path $base "bin/$Config/sr-ui.exe"
if (-not (Test-Path $exe)) { $exe = Join-Path $base 'bin/sr-ui.exe' }
if (-not (Test-Path $exe)) { $exe = Join-Path $base 'sr-ui.exe' }
if (-not (Test-Path $exe)) {
    Write-Host "FAIL: sr-ui.exe not found under $base"
    exit 1
}

$work = Join-Path $env:TEMP 'sr-ui-theme'
New-Item -ItemType Directory -Force -Path $work | Out-Null
$artifacts = Join-Path $root 'verify/artifacts/ui-theme'
New-Item -ItemType Directory -Force -Path $artifacts | Out-Null

$script:pass = 0
$script:fail = 0
$script:skip = 0
$script:processes = @()

function Assert {
    param([string]$Name, [bool]$Condition, [string]$Detail)
    if ($Condition) {
        $script:pass++
        Write-Host ("  PASS {0} — {1}" -f $Name, $Detail)
    } else {
        $script:fail++
        Write-Host ("  FAIL {0} — {1}" -f $Name, $Detail)
    }
}
function Skip {
    param([string]$Name, [string]$Reason)
    $script:skip++
    Write-Host ("  SKIP {0} — {1}" -f $Name, $Reason)
}

Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public class UiThemeNative {
  [DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="FindWindowW")]
  public static extern IntPtr FindWindowClass(string cls, IntPtr title);
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr hDlg, int nID);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, System.Text.StringBuilder s, int max);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint msg, IntPtr wparam, IntPtr lparam);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr wparam, IntPtr lparam);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent, EnumProc cb, IntPtr param);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool SetForegroundWindowAlt(IntPtr h);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, IntPtr pid);
  [DllImport("user32.dll")] public static extern bool GetGUIThreadInfo(uint tid, ref GUITHREADINFO info);
  [StructLayout(LayoutKind.Sequential)] public struct GUITHREADINFO {
    public int cbSize; public int flags; public IntPtr hwndActive; public IntPtr hwndFocus;
    public IntPtr hwndCapture; public IntPtr hwndMenuOwner; public IntPtr hwndMoveSize;
    public IntPtr hwndCaret; public RECT rcCaret;
  }
  public delegate bool EnumProc(IntPtr hwnd, IntPtr param);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int left, top, right, bottom; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int x, y; }
}
'@

function Stop-UiProcesses {
    foreach ($p in $script:processes) {
        try { if (-not $p.HasExited) { $p.Kill(); $p.WaitForExit(2000) } } catch {}
    }
    $script:processes = @()
    Get-Process sr-ui -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
}
trap {
    Write-Host "FAIL(trap): $_"
    Write-Host ("  栈: {0}" -f $_.ScriptStackTrace)
    Stop-UiProcesses
    exit 1
}

function Read-Log {
    # 共享读：sr-ui 用 FILE_SHARE_READ 打开日志，读取端必须允许对方继续写。
    param([string]$Path)
    if (-not (Test-Path $Path)) { return '' }
    for ($i = 0; $i -lt 20; $i++) {
        try {
            $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open,
                                             [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
            $reader = New-Object System.IO.StreamReader($stream, [System.Text.Encoding]::UTF8)
            $text = $reader.ReadToEnd()
            $reader.Dispose()
            $stream.Dispose()
            return $text
        } catch { Start-Sleep -Milliseconds 100 }
    }
    return ''
}

function Start-Ui {
    param([string]$Tag, [string[]]$Extra = @(), [string]$IniOverride)
    $ini = if ($IniOverride) { $IniOverride } else { Join-Path $work "$Tag.ini" }
    if (-not (Test-Path $ini)) {
        [System.IO.File]::WriteAllText($ini, "[output]`r`ndir=$work`r`n", [System.Text.UTF8Encoding]::new($false))
    }
    $log = Join-Path $work "$Tag.log"
    Remove-Item $log -Force -ErrorAction SilentlyContinue
    $args = @('--config', $ini, '--log', $log, '--exit-after-ms', '60000') + $Extra
    $proc = Start-Process -FilePath $exe -PassThru -ArgumentList $args
    $script:processes += $proc
    $hwnd = [IntPtr]::Zero
    for ($i = 0; $i -lt 100 -and $hwnd -eq [IntPtr]::Zero; $i++) {
        Start-Sleep -Milliseconds 100
        $hwnd = [UiThemeNative]::FindWindowClass('SrUiMainWindow', [IntPtr]::Zero)
    }
    return [pscustomobject]@{ Process = $proc; Hwnd = $hwnd; Log = $log }
}

# 截取客户区（不含标题栏），返回 Bitmap；像素坐标即客户区坐标。
function Get-ClientBitmap {
    param([IntPtr]$Hwnd, [string]$Name)
    # 截图前把窗口临时置顶：CopyFromScreen 取的是屏幕像素，被别的窗口挡住就取错了。
    [void][UiThemeNative]::ShowWindow($Hwnd, 5)
    [void][UiThemeNative]::SetWindowPos($Hwnd, [IntPtr](-1), 40, 40, 0, 0, 0x0001 -bor 0x0040)  # HWND_TOPMOST|SWP_SHOWWINDOW
    [void][UiThemeNative]::SetForegroundWindow($Hwnd)
    Start-Sleep -Milliseconds 450
    $rc = New-Object UiThemeNative+RECT
    [void][UiThemeNative]::GetClientRect($Hwnd, [ref]$rc)
    $pt = New-Object UiThemeNative+POINT
    [void][UiThemeNative]::ClientToScreen($Hwnd, [ref]$pt)
    $w = $rc.right - $rc.left
    $h = $rc.bottom - $rc.top
    $bmp = New-Object System.Drawing.Bitmap($w, $h)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($pt.x, $pt.y, 0, 0, (New-Object System.Drawing.Size($w, $h)))
    $g.Dispose()
    $bmp.Save((Join-Path $artifacts "$Name.png"), [System.Drawing.Imaging.ImageFormat]::Png)
    return $bmp
}

function Hex($c) { return ('#{0:X2}{1:X2}{2:X2}' -f $c.R, $c.G, $c.B) }
function Px($bmp, $x, $y) {
    $x = [Math]::Max(0, [Math]::Min($bmp.Width - 1, [int]$x))
    $y = [Math]::Max(0, [Math]::Min($bmp.Height - 1, [int]$y))
    return $bmp.GetPixel($x, $y)
}
# 与 token 比：每通道 ±tol
function Near($color, [string]$hex, [int]$tol = 4) {
    $r = [Convert]::ToInt32($hex.Substring(1, 2), 16)
    $g = [Convert]::ToInt32($hex.Substring(3, 2), 16)
    $b = [Convert]::ToInt32($hex.Substring(5, 2), 16)
    return ([Math]::Abs($color.R - $r) -le $tol -and [Math]::Abs($color.G - $g) -le $tol -and
            [Math]::Abs($color.B - $b) -le $tol)
}
# 区域内最亮像素（文本色：抗锯齿边缘会偏暗，取笔画内部最亮值）
function BrightestPixel($bmp, $x, $y, $w, $h) {
    $best = $null; $bestSum = -1
    for ($yy = $y; $yy -lt ($y + $h); $yy++) {
        for ($xx = $x; $xx -lt ($x + $w); $xx++) {
            $c = Px $bmp $xx $yy
            $sum = [int]$c.R + [int]$c.G + [int]$c.B
            if ($sum -gt $bestSum) { $bestSum = $sum; $best = $c }
        }
    }
    return $best
}
# 纵向扫描第一行「不再是 header 色」的位置 → 段高
function Measure-VerticalBand($bmp, [int]$x, [string]$hex, [int]$fromTop = 1, [int]$tol = 3) {
    $h = $bmp.Height
    if ($fromTop -eq 1) {
        for ($y = 0; $y -lt $h; $y++) {
            if (-not (Near (Px $bmp $x $y) $hex $tol)) { return $y }
        }
    } else {
        # 客户区最底一行会被窗口边框混色，从 h-2 起算（差 1px 落在 ±1 容差内）。
        for ($y = $h - 2; $y -ge 0; $y--) {
            if (-not (Near (Px $bmp $x $y) $hex $tol)) { return ($h - 2 - $y) }
        }
    }
    return -1
}
# 从 ui_probe 行里取坐标
function Get-Probe {
    param([string]$Text)
    $line = ($Text -split "`n" | Where-Object { $_ -match 'ui_probe ' } | Select-Object -Last 1)
    if (-not $line) { return $null }
    $probe = @{}
    foreach ($m in [regex]::Matches($line, '(\w+)=([0-9,\-]+)')) {
        $probe[$m.Groups[1].Value] = $m.Groups[2].Value
    }
    return $probe
}
function Get-LayoutValue {
    param([string]$Text, [string]$Key)
    $line = ($Text -split "`n" | Where-Object { $_ -match 'ui_layout ' } | Select-Object -Last 1)
    if (-not $line) { return $null }
    $m = [regex]::Match($line, "$Key=([0-9]+)")
    if ($m.Success) { return [int]$m.Groups[1].Value }
    return $null
}
function Wait-LogMatch {
    param([string]$Path, [string]$Pattern, [int]$TimeoutMs = 8000)
    $deadline = (Get-Date).AddMilliseconds($TimeoutMs)
    while ((Get-Date) -lt $deadline) {
        if ((Read-Log $Path) -match $Pattern) { return $true }
        Start-Sleep -Milliseconds 100
    }
    return $false
}

Write-Host '--- 0 启动深色工作台 ---'
$dark = Start-Ui -Tag 'dark'
Assert 'ui-window-created' ($dark.Hwnd -ne [IntPtr]::Zero) "hwnd=$($dark.Hwnd.ToInt64())"
if ($dark.Hwnd -eq [IntPtr]::Zero) {
    Write-Host ("RESULT: {0} PASS / {1} FAIL / {2} SKIP" -f $script:pass, $script:fail, $script:skip)
    Stop-UiProcesses
    exit 1
}
$logText = Read-Log $dark.Log
Assert 'layout-logged' ($logText -match 'ui_layout client=') '启动后输出 ui_layout 锚点'
$probe = Get-Probe $logText
Assert 'probe-logged' ($null -ne $probe) '输出 ui_probe 采样坐标'
if ($null -eq $probe) {
    Write-Host '无法取得采样坐标，后续像素断言无法进行'
    Write-Host ("RESULT: {0} PASS / {1} FAIL / {2} SKIP" -f $script:pass, $script:fail, $script:skip)
    Stop-UiProcesses
    exit 1
}

$bmp = Get-ClientBitmap -Hwnd $dark.Hwnd -Name 'dark'
Write-Host ("  [client] {0}x{1}" -f $bmp.Width, $bmp.Height)

# ---- 1 底色 ----
$baseAt = $probe['base'] -split ','
$basePixel = Px $bmp $baseAt[0] $baseAt[1]
Assert 'base-color' (Near $basePixel '#0F1115') "实测 $(Hex $basePixel) @($($baseAt -join ','))，期望 #0F1115"

# ---- 2 卡片底 ----
$cardAt = $probe['card'] -split ','
$cardPixel = Px $bmp $cardAt[0] $cardAt[1]
Assert 'card-color' (Near $cardPixel '#161A20') "实测 $(Hex $cardPixel) @($($cardAt -join ','))，期望 #161A20"

# ---- 3 主按钮填充 ----
$primaryAt = $probe['primary'] -split ','
$primaryPixel = Px $bmp $primaryAt[0] $primaryAt[1]
Assert 'primary-fill' (Near $primaryPixel '#3B82F6') "实测 $(Hex $primaryPixel) @($($primaryAt -join ','))，期望 #3B82F6"

# ---- 5 文本色（标题笔画最亮像素）----
$textAt = $probe['text'] -split ','
$textPixel = BrightestPixel $bmp ([int]$textAt[0]) ([int]$textAt[1]) ([int]$textAt[2]) ([int]$textAt[3])
Assert 'text-color' (Near $textPixel '#E6EAF0') "实测 $(Hex $textPixel)，期望 #E6EAF0"

# ---- 6/7 Header 与状态栏高度（像素边界）----
$headerPx = Measure-VerticalBand $bmp 4 '#161A20' 1
$statusPx = Measure-VerticalBand $bmp 4 '#161A20' 0
Assert 'header-height-96dpi' ([Math]::Abs($headerPx - 48) -le 1) "像素边界 header=$headerPx px（期望 48±1）"
Assert 'statusbar-height' ([Math]::Abs($statusPx - 32) -le 1) "像素边界 status=$statusPx px（期望 32±1）"

# ---- 11 三处描边采样：必须是 token 描边色，不是系统默认灰 ----
$borderOk = $true
$borderDetail = @()
foreach ($key in 'border1', 'border2', 'border3') {
    $at = $probe[$key] -split ','
    $c = Px $bmp $at[0] $at[1]
    $isToken = Near $c '#2A313B' 4
    # 系统默认边框色家族（#F0F0F0 / #D4D0C8 / #B4B4B4 / #A0A0A0 / #696969）任一个都不许出现
    $isSystemGray = ($c.R -eq $c.G -and $c.G -eq $c.B -and $c.R -ge 0x69)
    if (-not $isToken -or $isSystemGray) { $borderOk = $false }
    $borderDetail += "$key=$(Hex $c)"
}
Assert 'no-system-gray' $borderOk ("描边采样 " + ($borderDetail -join ' / ') + '，期望 #2A313B（±4）')

# ---- 12 圆角：卡片左上角像素不是卡片色，内缩后是卡片色 ----
$border1At = $probe['border1'] -split ','
$border2At = $probe['border2'] -split ','
$cornerX = [int]$border1At[0]
$cornerY = [int]$border2At[1]
$corner = Px $bmp ($cornerX + 1) ($cornerY + 1)
$inside = Px $bmp ($cornerX + 12) ($cornerY + 12)
Assert 'radius-card' ((-not (Near $corner '#161A20' 3)) -and (Near $inside '#161A20' 4)) `
    "左上角($($cornerX + 1),$($cornerY + 1))=$(Hex $corner)，内缩 12px=$(Hex $inside)"

# ---- 面板默认展开：名义面板位置的字段框是底色 base（与收起态 card 可区分）----
$panelAt = $probe['panel'] -split ','
$panelOpenPixel = Px $bmp $panelAt[0] $panelAt[1]
Assert 'panel-open-visible' (Near $panelOpenPixel '#0F1115' 4) `
    "面板字段框内实测 $(Hex $panelOpenPixel) @($($panelAt -join ','))，期望 #0F1115（值框底色）"

# ---- 4 悬停变亮（真实光标移入按钮，走控件自己的 hover 分支）----
$startButton = [UiThemeNative]::GetDlgItem($dark.Hwnd, 201)
$hoverOk = $false
$hoverDetail = 'button hwnd=0'
if ($startButton -ne [IntPtr]::Zero) {
    $btnRc = New-Object UiThemeNative+RECT
    [void][UiThemeNative]::GetWindowRect($startButton, [ref]$btnRc)
    [void][UiThemeNative]::SetCursorPos([int](($btnRc.left + $btnRc.right) / 2), [int](($btnRc.top + $btnRc.bottom) / 2))
    Start-Sleep -Milliseconds 400
    $bmpHover = Get-ClientBitmap -Hwnd $dark.Hwnd -Name 'hover'
    $hoverPixel = Px $bmpHover $primaryAt[0] $primaryAt[1]
    $bmpHover.Dispose()
    $delta = ([int]$hoverPixel.R - [int]$primaryPixel.R) + ([int]$hoverPixel.G - [int]$primaryPixel.G) +
             ([int]$hoverPixel.B - [int]$primaryPixel.B)
    $hoverOk = ($delta -ge 3) -and (Near $hoverPixel '#5B9CF8' 6)
    $hoverDetail = "常态=$(Hex $primaryPixel) → hover=$(Hex $hoverPixel)，亮度增量和=${delta}（期望 #5B9CF8，≥3 级）"
    [void][UiThemeNative]::SetCursorPos(2, 2)
    Start-Sleep -Milliseconds 200
}
Assert 'primary-hover' $hoverOk $hoverDetail

# ---- 13 键盘可达：Tab 焦点落在自绘控件上，且焦点矩形可见（客户区内可见像素）----
$focusOk = $false
$focusDetail = 'window not focused'
if ($startButton -ne [IntPtr]::Zero) {
    Add-Type -AssemblyName Microsoft.VisualBasic
    try { [Microsoft.VisualBasic.Interaction]::AppActivate($dark.Process.Id) | Out-Null } catch {}
    Start-Sleep -Milliseconds 300
    $tid = [UiThemeNative]::GetWindowThreadProcessId($dark.Hwnd, [IntPtr]::Zero)
    $focusHwnd = [IntPtr]::Zero
    for ($i = 0; $i -lt 12; $i++) {
        $gi = New-Object UiThemeNative+GUITHREADINFO
        $gi.cbSize = [System.Runtime.InteropServices.Marshal]::SizeOf($gi)
        [void][UiThemeNative]::GetGUIThreadInfo($tid, [ref]$gi)
        if ($gi.hwndFocus -eq $startButton) { $focusHwnd = $gi.hwndFocus; break }
        [void][UiThemeNative]::PostMessage($dark.Hwnd, 0x0100, [IntPtr]0x09, [IntPtr]::Zero)  # WM_KEYDOWN VK_TAB
        Start-Sleep -Milliseconds 120
    }
    if ($focusHwnd -ne [IntPtr]::Zero) {
        $bmpFocus = Get-ClientBitmap -Hwnd $dark.Hwnd -Name 'focus'
        # 主按钮 132×36（96dpi，中心点来自 ui_probe）：焦点矩形画在客户区内侧 1px。
        $cx = [int]$primaryAt[0]
        $cy = [int]$primaryAt[1]
        $ringY = @(($cy - 17), ($cy - 16), ($cy + 16), ($cy + 17))
        $ring = 0
        for ($x = ($cx - 60); $x -le ($cx + 60); $x++) {
            foreach ($y in $ringY) {
                $c = Px $bmpFocus $x $y
                if ($c.R -gt 230 -and $c.G -gt 230 -and $c.B -gt 230) { $ring++ }
            }
        }
        $bmpFocus.Dispose()
        $focusOk = $ring -ge 40
        $focusDetail = "Tab 焦点落到主按钮（hwnd=$($startButton.ToInt64())），边缘近白像素=$ring（焦点矩形 2px）"
    } else {
        $focusDetail = '12 次 Tab 后焦点未落到自绘控件（窗口可能未被激活）'
    }
}
Assert 'focus-ring-visible' $focusOk $focusDetail

# ---- 9 最小窗口 860×560：右侧面板自动收起（该处由字段框底色变回卡片色）----
[void][UiThemeNative]::SetWindowPos($dark.Hwnd, [IntPtr]::Zero, 40, 40, 860, 560, 0x0004)
Start-Sleep -Milliseconds 500
$smallText = Read-Log $dark.Log
$panelOpen = Get-LayoutValue $smallText 'panel_open'
$panelW = Get-LayoutValue $smallText 'panel_w'
$bmpSmall = Get-ClientBitmap -Hwnd $dark.Hwnd -Name 'min-window'
$smallProbe = Get-Probe $smallText
$smallPanelAt = if ($null -ne $smallProbe) { $smallProbe['panel'] -split ',' } else { $panelAt }
$panelCollapsedPixel = Px $bmpSmall $smallPanelAt[0] $smallPanelAt[1]
$layoutCount = (($smallText -split "`n") | Where-Object { $_ -match 'ui_layout ' }).Count
Assert 'panel-auto-collapse' (($panelOpen -eq 0 -and $panelW -eq 0) -and (Near $panelCollapsedPixel '#161A20' 4)) `
    "ui_layout panel_open=$panelOpen panel_w=$panelW（共 $layoutCount 次布局），面板位置实测 $(Hex $panelCollapsedPixel)（收起后为卡片色 #161A20）"
$bmpSmall.Dispose()
$bmp.Dispose()
Stop-UiProcesses

# ---- 8 DPI 125% ----
Write-Host '--- 1 DPI 125% ---'
$dpi = Start-Ui -Tag 'dpi125' -Extra @('--dpi-scale', '1.25')
$dpiOk = $false
$dpiDetail = 'window not created'
if ($dpi.Hwnd -ne [IntPtr]::Zero) {
    [void](Wait-LogMatch -Path $dpi.Log -Pattern 'ui_layout client=')
    $bmpDpi = Get-ClientBitmap -Hwnd $dpi.Hwnd -Name 'dpi125'
    $headerDpi = Measure-VerticalBand $bmpDpi 4 '#161A20' 1
    $statusDpi = Measure-VerticalBand $bmpDpi 4 '#161A20' 0
    $dpiOk = ([Math]::Abs($headerDpi - 60) -le 2) -and ([Math]::Abs($statusDpi - 40) -le 2)
    $dpiDetail = "header=$headerDpi px（期望 60±2，= 48×1.25）/ status=$statusDpi px（期望 40±2，= 32×1.25）"
    $bmpDpi.Dispose()
}
Assert 'dpi-scale-125' $dpiOk $dpiDetail
Stop-UiProcesses

# ---- 10 --theme light ----
Write-Host '--- 2 浅色主题 ---'
$light = Start-Ui -Tag 'light' -Extra @('--theme', 'light')
$lightOk = $false
$lightDetail = 'window not created'
if ($light.Hwnd -ne [IntPtr]::Zero) {
    [void](Wait-LogMatch -Path $light.Log -Pattern 'ui_probe ')
    $lightProbe = Get-Probe (Read-Log $light.Log)
    $bmpLight = Get-ClientBitmap -Hwnd $light.Hwnd -Name 'light'
    if ($null -ne $lightProbe) {
        $at = $lightProbe['base'] -split ','
        $lightBase = Px $bmpLight $at[0] $at[1]
        $lightCardAt = $lightProbe['card'] -split ','
        $lightCard = Px $bmpLight $lightCardAt[0] $lightCardAt[1]
        $lightOk = (Near $lightBase '#F7F8FA' 4)
        $lightDetail = "底色=$(Hex $lightBase) 卡片=$(Hex $lightCard)（期望 #F7F8FA / #FFFFFF）"
    }
    $bmpLight.Dispose()
}
Assert 'theme-light-base' $lightOk $lightDetail

Stop-UiProcesses
Write-Host ''
Write-Host ("RESULT: {0} PASS / {1} FAIL / {2} SKIP" -f $script:pass, $script:fail, $script:skip)
Write-Host "artifacts: $artifacts"
if ($script:fail -gt 0) { exit 1 }
exit 0
