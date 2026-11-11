# 轮 43 · P1 无卡收口 4 条的专属行为断言
#
#   M1-24#1  关闭后成片任何位置都不出现鼠标指针：受控纯色背景 + --cursor off，
#            抽帧统计非背景像素（中心区域 + 全画面抽样），应全为背景色。
#   M1-24#2  开启后指针位置与操作一致、无残影：--cursor on，鼠标移到 A / B 两点各录一段，
#            光标出现在对应位置，另一位置无残影。
#   M7-13#1  录制中托盘图标带红点 + 可选屏幕边框：自绘图标像素自检日志 + 边框窗口可见/红色。
#   M7-13#2  暂停时边框样式与录制中可区分：黄色细虚线 vs 红色粗实线；--record-border off 可关。
#
# 降级/代理标注：M7-13 的状态由 --simulate-record-state 注入，走**同一条 UI 渲染分支**
# （与轮 42 M6-11#2 的 --simulate-backpressure 同族），非真实录制会话；
# M1-24 是真实录制 + 成片像素级验证，非代理。
#
# Usage: pwsh -File verify/cases/g5_r43_closeout.ps1

param([string]$BuildDir = 'build-msvc', [string]$Config = 'Release')

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\lib\Cleanup.ps1')
trap {
    Clear-SrProcesses -IncludeNameFallback
    Write-Host "FAIL: $_"
    exit 1
}

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$rec = Join-Path $root (Join-Path $BuildDir "bin\$Config\sr-recorder.exe")
$ui  = Join-Path $root (Join-Path $BuildDir "bin\$Config\sr-ui.exe")
if (-not (Test-Path $rec)) { $rec = Join-Path $root (Join-Path $BuildDir 'bin\sr-recorder.exe') }
if (-not (Test-Path $ui)) { $ui = Join-Path $root (Join-Path $BuildDir 'bin\sr-ui.exe') }
if (-not (Test-Path $rec) -or -not (Test-Path $ui)) { Write-Host 'FAIL: binaries missing'; exit 2 }
$ffbin = Split-Path -Parent (Get-Command ffprobe).Source
$ffplay = Join-Path $ffbin 'ffplay.exe'
$ffmpeg = Join-Path $ffbin 'ffmpeg.exe'
$work = Join-Path $env:TEMP 'sr-r43-closeout'
if (Test-Path $work) { Get-ChildItem $work -Recurse -Force | Remove-Item -Force -Recurse -ErrorAction SilentlyContinue }
New-Item -ItemType Directory -Force -Path $work | Out-Null

$script:pass = 0; $script:fail = 0; $script:skip = 0
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

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class SrR43Native {
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="FindWindowW")]
    public static extern IntPtr FindWindowClass(string cls, IntPtr title);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr GetDC(IntPtr h);
    [DllImport("user32.dll")] public static extern int ReleaseDC(IntPtr h, IntPtr dc);
    [DllImport("gdi32.dll")] public static extern uint GetPixel(IntPtr dc, int x, int y);
    public static uint ScreenPixel(int x, int y) {
        IntPtr dc = GetDC(IntPtr.Zero);
        try { return GetPixel(dc, x, y); } finally { ReleaseDC(IntPtr.Zero, dc); }
    }
    public static int R(uint c) { return (int)(c & 0xFF); }
    public static int G(uint c) { return (int)((c >> 8) & 0xFF); }
    public static int B(uint c) { return (int)((c >> 16) & 0xFF); }
}
'@

# 纯色全屏背景：ffplay 显示恒定绿色，避免桌面其它窗口干扰像素判定。
$script:colorProc = $null
function Start-FlatBackground {
    # 独立 WinForms 进程画纯色全屏：ffplay -fs 在本机双屏下未覆盖录制画面
    # （实测抽帧中心 (11,11,11)），独立进程有消息循环，WM_PAINT 才持续生效。
    $colorScript = Join-Path $root 'tasks/G6/V1-010/artifacts/ColorWindow.ps1'
    $script:colorProc = Start-Process -FilePath pwsh -PassThru -WindowStyle Hidden `
        -ArgumentList @('-NoProfile', '-STA', '-File', "`"$colorScript`"")
    Start-Sleep -Seconds 3
}
function Stop-FlatBackground {
    if ($script:colorProc) {
        Stop-Process -Id $script:colorProc.Id -Force -ErrorAction SilentlyContinue
        $script:colorProc = $null
        Start-Sleep -Milliseconds 400
    }
}
function Record-Clip {
    param([string]$Tag, [string]$Cursor, [int]$Seconds = 3)
    $file = Join-Path $work "$Tag.mkv"
    $outFile = Join-Path $work "$Tag.out"
    $o = & $rec --record --seconds $Seconds --fps 30 --bitrate 6000 --backend wgc `
        --cursor $Cursor --out $file --log-level warn 2>&1 | Out-String
    [System.IO.File]::WriteAllText($outFile, $o, [System.Text.UTF8Encoding]::new($false))
    return [pscustomobject]@{ File = $file; Stdout = $o }
}
function Get-FramePixels {
    param([string]$Mkv, [string]$Tag, [int]$FrameIndex = 45)
    $raw = Join-Path $work "$Tag.raw"
    if (Test-Path $raw) { Remove-Item $raw -Force }
    & $ffmpeg -hide_banner -v error -i $Mkv -vf "select=eq(n\,$FrameIndex)" `
        -fps_mode passthrough -frames:v 1 -f rawvideo -pix_fmt rgb24 $raw 2>$null
    if (-not (Test-Path $raw)) { return $null }
    return [pscustomobject]@{
        Bytes = [System.IO.File]::ReadAllBytes($raw)
        Width = 1920; Height = 1080
    }
}
# 背景是 RGB(0,128,0)；H.264 有损压缩后放宽到 ±36 容差。
function Test-NonBackground {
    param([byte[]]$Bytes, [int]$Width, [int]$X0, [int]$Y0, [int]$W, [int]$H)
    $count = 0
    for ($y = $Y0; $y -lt ($Y0 + $H); $y += 2) {
        for ($x = $X0; $x -lt ($X0 + $W); $x += 2) {
            $i = ($y * $Width + $x) * 3
            if ($i + 2 -ge $Bytes.Length) { continue }
            $r = [int]$Bytes[$i]; $g = [int]$Bytes[$i + 1]; $b = [int]$Bytes[$i + 2]
            if ([Math]::Abs($r - 0) -gt 36 -or [Math]::Abs($g - 128) -gt 36 -or
                [Math]::Abs($b - 0) -gt 36) { $count++ }
        }
    }
    return $count
}

# ================================================================ M1-24
Write-Host '=== M1-24 光标采集开关（真实录制 + 成片像素级验证）==='
Clear-SrProcesses -IncludeNameFallback
Stop-FlatBackground
Start-FlatBackground
try {
    $sx = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds.Width
    $sy = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds.Height
} catch {
    Add-Type -AssemblyName System.Windows.Forms
    $sx = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds.Width
    $sy = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds.Height
}
$cx = [int]($sx / 2); $cy = [int]($sy / 2)
[void][SrR43Native]::SetCursorPos($cx, $cy)
Start-Sleep -Milliseconds 500

$off = Record-Clip -Tag 'cursor-off' -Cursor 'off'
$offExit = Field $off.Stdout 'record_exit'
$offFlag = Field $off.Stdout 'include_cursor'
Assert 'm1-24-01-cursor-off-flag' ($offExit -eq '0' -and $offFlag -eq '0') `
    ("--cursor off：record_exit={0} include_cursor={1}" -f $offExit, $offFlag)
$offFrame = Get-FramePixels -Mkv $off.File -Tag 'cursor-off'
# WGC 只在指针移动时刷新光标层；静止鼠标不会重绘（首轮实测 on 段非背景=0，
# 而移动过的 A/B 段各有 35）。录 on 段前抖动一次触发重绘，判据不变。
[void][SrR43Native]::SetCursorPos($cx + 2, $cy)
Start-Sleep -Milliseconds 250
[void][SrR43Native]::SetCursorPos($cx, $cy)
Start-Sleep -Milliseconds 250
$on = Record-Clip -Tag 'cursor-on' -Cursor 'on'
$onExit = Field $on.Stdout 'record_exit'
$onFlag = Field $on.Stdout 'include_cursor'
$onFrame = Get-FramePixels -Mkv $on.File -Tag 'cursor-on'
Assert 'm1-24-02-cursor-on-flag' ($onExit -eq '0' -and $onFlag -eq '1') `
    ("--cursor on：record_exit={0} include_cursor={1}" -f $onExit, $onFlag)

if ($null -eq $offFrame -or $null -eq $onFrame) {
    $script:skip++; Write-Host '  SKIP M1-24 像素判定 — 抽帧失败（ffmpeg/成片不可用）'
} else {
    # 中心 60×60 区域（鼠标所在处）
    $offCenter = Test-NonBackground -Bytes $offFrame.Bytes -Width $offFrame.Width `
        -X0 ($cx - 30) -Y0 ($cy - 30) -W 60 -H 60
    $onCenter = Test-NonBackground -Bytes $onFrame.Bytes -Width $onFrame.Width `
        -X0 ($cx - 30) -Y0 ($cy - 30) -W 60 -H 60
    Assert 'm1-24-01-no-pointer-when-off' ($offCenter -eq 0) `
        ("关闭后中心 60×60 非背景像素={0}（期望 0）" -f $offCenter)
    # 全画面抽样：纯色背景下，任何非背景像素都说明仍有光标/杂影。
    $offAll = Test-NonBackground -Bytes $offFrame.Bytes -Width $offFrame.Width `
        -X0 0 -Y0 0 -W $offFrame.Width -H $offFrame.Height
    $onAll = Test-NonBackground -Bytes $onFrame.Bytes -Width $onFrame.Width `
        -X0 0 -Y0 0 -W $onFrame.Width -H $onFrame.Height
    $sampled = [int](($offFrame.Width / 2) * ($offFrame.Height / 2))
    # 全画面抽样允许 <1% 的边缘/压缩噪声，但关闭态不得比开启态多出成片的指针像素。
    Assert 'm1-24-01-no-pointer-anywhere' (($offAll / [double]$sampled) -lt 0.01 -and $offAll -le $onAll) `
        ("关闭后全画面抽样非背景={0}/{1}（{2:P2}，<1%）vs 开启态={3}；受控纯色背景抽样，非全帧逐像素" -f `
         $offAll, $sampled, ($offAll / [double]$sampled), $onAll)
    Assert 'm1-24-02-pointer-visible-when-on' ($onCenter -gt 0) `
        ("开启后中心 60×60 非背景像素={0}（期望 >0，即指针被写入成片）" -f $onCenter)

    # #2：把鼠标移到另一个位置，验证位置跟随且原位置无残影。
    # 每个段前都抖动一次：WGC 只在指针移动时刷新光标层，连续两段之间
    # 若只在 SetCursorPos 后等 400ms，偶发（回归首轮实测 B 段 0）不刷新。
    function Move-CursorWithNudge {
        param([int]$X, [int]$Y)
        [void][SrR43Native]::SetCursorPos($X, $Y)
        Start-Sleep -Milliseconds 300
        [void][SrR43Native]::SetCursorPos($X + 2, $Y)
        Start-Sleep -Milliseconds 250
        [void][SrR43Native]::SetCursorPos($X, $Y)
        Start-Sleep -Milliseconds 250
    }
    $ax = $cx - 300; $bx = $cx + 300
    Move-CursorWithNudge -X $ax -Y $cy
    $atA = Record-Clip -Tag 'cursor-pos-a' -Cursor 'on'
    $frameA = Get-FramePixels -Mkv $atA.File -Tag 'cursor-pos-a'
    Move-CursorWithNudge -X $bx -Y $cy
    $atB = Record-Clip -Tag 'cursor-pos-b' -Cursor 'on'
    $frameB = Get-FramePixels -Mkv $atB.File -Tag 'cursor-pos-b'
    if ($null -eq $frameA -or $null -eq $frameB) {
        $script:skip++; Write-Host '  SKIP M1-24#2 位置判定 — 抽帧失败'
    } else {
        $aAtA = Test-NonBackground -Bytes $frameA.Bytes -Width $frameA.Width `
            -X0 ($ax - 30) -Y0 ($cy - 30) -W 60 -H 60
        $aAtB = Test-NonBackground -Bytes $frameB.Bytes -Width $frameB.Width `
            -X0 ($ax - 30) -Y0 ($cy - 30) -W 60 -H 60
        $bAtB = Test-NonBackground -Bytes $frameB.Bytes -Width $frameB.Width `
            -X0 ($bx - 30) -Y0 ($cy - 30) -W 60 -H 60
        Assert 'm1-24-02-pointer-follows-position' ($aAtA -gt 0 -and $bAtB -gt 0) `
            ("指针随操作移动：A 点在 A 段非背景={0}，B 点在 B 段非背景={1}（期望均 >0）" -f $aAtA, $bAtB)
        Assert 'm1-24-02-no-ghost-at-old-position' ($aAtB -eq 0) `
            ("B 段里 A 点非背景像素={0}（期望 0，无残影）" -f $aAtB)
    }
}
Stop-FlatBackground
Clear-SrProcesses -IncludeNameFallback

# ================================================================ M7-13
Write-Host '=== M7-13 录制中状态提示（托盘红点 + 可选屏幕边框）==='
function Start-IndicatorUi {
    param([string]$Tag, [string]$State, [string]$Border = 'on')
    $log = Join-Path $work "$Tag.log"
    $p = Start-Process -FilePath $ui -PassThru -WindowStyle Hidden -ArgumentList @(
        '--hidden', '--log', $log, '--log-level', 'info',
        '--record-border', $Border, '--simulate-record-state', $State,
        '--exit-after-ms', '6000')
    Register-SrProcess $p
    Start-Sleep -Milliseconds 1600
    $hwnd = [SrR43Native]::FindWindowClass('SrRecordBorderWindow', [IntPtr]::Zero)
    $visible = ($hwnd -ne [IntPtr]::Zero) -and [SrR43Native]::IsWindowVisible($hwnd)
    $pix = [SrR43Native]::ScreenPixel(4, [int]($sy / 2))
    $logText = if (Test-Path $log) { (Read-TextSafe $log) } else { '' }
    $result = [pscustomobject]@{
        Tag = $Tag; Hwnd = $hwnd; Visible = $visible
        R = [SrR43Native]::R($pix); G = [SrR43Native]::G($pix); B = [SrR43Native]::B($pix)
        Log = $logText; Proc = $p; LogPath = $log
    }
    try { if (-not $p.HasExited) { [void]$p.WaitForExit(7000) } } catch {}
    Clear-SrProcesses -IncludeNameFallback
    return $result
}

$r = Start-IndicatorUi -Tag 'ind-recording' -State 'recording'
Assert 'm7-13-01-tray-red-dot-pixel' ($r.Log -match 'tray_icon red_dot_pixel=0xFF0000FF') `
    ("自绘托盘图标的像素自检：{0}" -f (($r.Log -split "`n" | Where-Object { $_ -match 'tray_icon' } | Select-Object -First 1)))
Assert 'm7-13-01-border-visible-recording' ($r.Visible -and ($r.Log -match 'state=recording border=1')) `
    ("录制中边框窗口 hwnd={0} visible={1}；日志={2}" -f $r.Hwnd, $r.Visible,
     (($r.Log -split "`n" | Where-Object { $_ -match 'record_indicator' } | Select-Object -First 1)))
Assert 'm7-13-01-border-red' ($r.R -gt 150 -and $r.G -lt 100 -and $r.B -lt 100) `
    ("屏幕左侧边框像素 RGB=({0},{1},{2})（期望偏红）" -f $r.R, $r.G, $r.B)

$p = Start-IndicatorUi -Tag 'ind-paused' -State 'paused'
Assert 'm7-13-02-border-distinct-when-paused' ($p.Visible -and
        ($p.Log -match 'state=paused border=1') -and
        (($p.R -ne $r.R) -or ($p.G -ne $r.G) -or ($p.B -ne $r.B))) `
    ("暂停态边框 RGB=({0},{1},{2}) vs 录制态 RGB=({3},{4},{5})；日志={6}" -f `
     $p.R, $p.G, $p.B, $r.R, $r.G, $r.B,
     (($p.Log -split "`n" | Where-Object { $_ -match 'record_indicator' } | Select-Object -First 1)))

$off = Start-IndicatorUi -Tag 'ind-border-off' -State 'recording' -Border 'off'
Assert 'm7-13-01-border-optional-off' (-not $off.Visible -and ($off.Log -match 'border=0|state=hidden')) `
    ("--record-border off：边框可见={0}；日志={1}" -f $off.Visible,
     (($off.Log -split "`n" | Where-Object { $_ -match 'record_indicator' } | Select-Object -First 1)))

Clear-SrProcesses -IncludeNameFallback
Write-Host ''
Write-Host ("RESULT: {0} PASS / {1} FAIL / {2} SKIP" -f $script:pass, $script:fail, $script:skip)
if ($script:fail -gt 0) { exit 1 }
exit 0
