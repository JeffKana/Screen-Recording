# 轮 45 · 「桌面有活动」场景的固化激励源（供 g5_cbr_probe.ps1 的 active 模式调用）
#
# 为什么需要它：轮 44 验收方实测证明 M3-04#1 的结果随「桌面活动量」数量级变化——
# 安静桌面 0.0%，终端/IDE 在动时 13.0% / 44.6%、单秒曾 13,234kbps。
# 只测静态桌面的数据不再作为依据，所以静态/活动两组必须在同一套脚本里各自被测到。
#
# 本脚本只做一件事：让桌面上持续有大面积、**高细节**的像素变化（等价于终端/IDE 滚动输出），
# 且不改任何被测产品行为。它不参与断言、不产生判定值。
#
# 为什么不用色块动画：实测大面积色块几乎不占比特（编码器 147KB/s 就能装下），
# 6 次全 6,000kbps 恒平，证明不了任何东西；真实桌面（终端/IDE）是**抗锯齿小字**，
# 这才是把编码器顶过秒预算的内容，所以这里按「满屏等宽小字持续滚动」来激励。
#
# Usage: pwsh -File verify/lib/ActiveDesktop.ps1 -Seconds 120 [-Windowed]
param(
    [int]$Seconds = 120,
    [switch]$Windowed,
    # 每隔 N 秒把整屏内容整体换掉一次（等价于真实桌面上的窗口布局变化/大面积重绘）。
    # 实测：渐变小字只占 143KB/s，撑不破秒预算；**突发大变化**才是把编码器顶过预算的形态。
    [int]$BurstSec = 2
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class SrActiveDesktopNative {
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int X, int Y);
}
'@

$screen = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
# 默认铺满主屏（等价于全屏 IDE/终端）；-Windowed 时用 60% 区域。
$w = if ($Windowed) { [int]($screen.Width * 0.6) } else { $screen.Width }
$h = if ($Windowed) { [int]($screen.Height * 0.6) } else { $screen.Height }

$form = New-Object System.Windows.Forms.Form
$form.FormBorderStyle = [System.Windows.Forms.FormBorderStyle]::None
$form.StartPosition = [System.Windows.Forms.FormStartPosition]::Manual
$form.Bounds = New-Object System.Drawing.Rectangle($screen.X, $screen.Y, $w, $h)
$form.TopMost = $true
$form.ShowInTaskbar = $false
$form.BackColor = [System.Drawing.Color]::FromArgb(12, 12, 12)

$script:frame = 0
$script:scroll = 0
$script:cursorAngle = 0.0
$script:deadline = (Get-Date).AddSeconds($Seconds)
$script:font = New-Object System.Drawing.Font('Consolas', 12)
$script:lineH = 16
$script:lines = [int][Math]::Floor($h / $script:lineH) + 2
$script:rng = New-Object System.Random(20261111)
# 满屏随机字符矩阵：滚动时每行整体位移，产生与真实日志一致的「大块小字变化」。
$script:glyphs = New-Object 'char[][]' $script:lines
for ($r = 0; $r -lt $script:lines; $r++) {
    $script:glyphs[$r] = New-Object 'char[]' ([int]($w / 8) + 8)
    for ($c = 0; $c -lt $script:glyphs[$r].Length; $c++) {
        $script:glyphs[$r][$c] = [char](32 + $script:rng.Next(0, 94))
    }
}

$panel = New-Object System.Windows.Forms.Panel
$panel.Dock = [System.Windows.Forms.DockStyle]::Fill
$form.Controls.Add($panel)

$timer = New-Object System.Windows.Forms.Timer
$timer.Interval = 16   # ≈60Hz 重绘
$timer.Add_Tick({
    if ((Get-Date) -gt $script:deadline) {
        $timer.Stop()
        $form.Close()
        return
    }
    $script:frame++
    $script:scroll = ($script:scroll + 3) % ($script:lineH * 8)
    # 每秒把约 1/6 的行换成新内容：模拟日志持续输出（不是整屏闪变，接近真实终端）。
    if ($script:frame % 10 -eq 0) {
        for ($n = 0; $n -lt [Math]::Max(2, [int]($script:lines / 6)); $n++) {
            $r = $script:rng.Next(0, $script:lines)
            $len = $script:glyphs[$r].Length
            for ($c = 0; $c -lt $len; $c++) {
                $script:glyphs[$r][$c] = [char](32 + $script:rng.Next(0, 94))
            }
        }
    }
    # 突发整屏变化：模拟窗口布局变化/大面积重绘（真实场景里撑破秒预算的就是这种形态）。
    if ($BurstSec -gt 0 -and $script:frame % (60 * $BurstSec) -eq 0) {
        for ($r = 0; $r -lt $script:lines; $r++) {
            $len = $script:glyphs[$r].Length
            for ($c = 0; $c -lt $len; $c++) {
                $script:glyphs[$r][$c] = [char](32 + $script:rng.Next(0, 94))
            }
        }
    }
    # 每 250ms 整屏重排一次：等价于「窗口切换 / 面板展开 / 大段输出刷屏」。
    if ($script:frame % 15 -eq 0) {
        $script:scroll = 0
        for ($r = 0; $r -lt $script:lines; $r++) {
            $len = $script:glyphs[$r].Length
            for ($c = 0; $c -lt $len; $c++) {
                $script:glyphs[$r][$c] = [char](32 + $script:rng.Next(0, 94))
            }
        }
    }
    $panel.Invalidate()
    # 光标小圆：桌面合成也要更新（真实场景里光标一直在动）。
    $script:cursorAngle += 0.12
    $cx = [int]($screen.X + $screen.Width / 2 + 260 * [Math]::Cos($script:cursorAngle))
    $cy = [int]($screen.Y + $screen.Height / 2 + 200 * [Math]::Sin($script:cursorAngle))
    [void][SrActiveDesktopNative]::SetCursorPos($cx, $cy)
})

$panel.Add_Paint({
    param($sender, $e)
    $g = $e.Graphics
    # 浅色底 + 深色小字：等价于 IDE/文档（大块浅色区域 + 密集抗锯齿字形），
    # 比黑底纯文字更接近真实桌面的比特开销。
    $g.Clear([System.Drawing.Color]::FromArgb(246, 246, 240))
    $brush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(30, 32, 38))
    $offsetY = -($script:scroll)
    for ($r = 0; $r -lt $script:lines; $r++) {
        $y = $offsetY + $r * $script:lineH
        if ($y -lt -$script:lineH -or $y -gt $h) { continue }
        $text = New-Object string($script:glyphs[$r])
        $g.DrawString($text, $script:font, $brush, 0, $y)
    }
    $brush.Dispose()
})

$form.Add_Shown({ $timer.Start() })
[void]$form.ShowDialog()
