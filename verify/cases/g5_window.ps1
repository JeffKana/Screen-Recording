# 轮 39 · M1-08 窗口尺寸跟随 + M1-09 被遮挡采集
#
#   M1-08#1 拖动窗口 800×600 → 1280×720，成片画面始终完整包含窗口内容
#   M1-08#2 宽高比变化时按「填充 + 黑边」处理，且可配置为「拉伸」
#   M1-09#1 被遮挡时录到的仍是目标窗口内容（不是遮挡物）
#
# 关键设计：
#   * 目标窗口由 `srprobe windowhost` 自建（纯绿 + 蓝色边框 + 移动黑块）。
#     不用记事本/浏览器：Win11 的记事本是打包应用，MainWindowHandle 取不到
#     （g2_walk.ps1:151 已实测失败）。
#   * 窗口必须**持续变化**：WGC 只在内容变化时产帧，静止窗口会让时间轴停住。
#   * 「完整包含」用**蓝色边框的包围盒**判定：边框四边都贴住内容包围盒 = 整幅内容都在；
#     只画源纹理左上角时右边/下边的边框会消失 → 可判定「被裁剪」。
#   * 两策略用黑边占比区分：pad 等比缩放后上下留黑边；stretch 拉伸铺满无黑边。
#
# 落点（产品代码，不在用例里实现）：
#   src/capture/WgcCaptureSource.cpp：按帧内容尺寸重建帧池与拷贝纹理
#   src/compositor/Nv12Converter.cpp：ComputeDestRect + VideoProcessor 的源/目标矩形
#   src/recorder/RecordingSession.cpp：--window/--fit 入口、上一帧纹理用 ComPtr 持有
#   src/tools/srprobe/window_host.cpp：自建目标窗口 + 遮挡窗口
#
# Usage: pwsh -File verify/cases/g5_window.ps1 [-BuildDir build-msvc] [-Config Release]

param(
    [string]$BuildDir = 'build-msvc',
    [string]$Config = 'Release'
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot '..\lib\Cleanup.ps1')
. (Join-Path $PSScriptRoot '..\lib\Forensics.ps1')
trap {
    [void](Save-FailureContext -Tag 'g5_window' -WorkDirs $script:SrForensicsWorkDirs -Note $_)
    Clear-SrProcesses -IncludeNameFallback
    Write-Host "FAIL: $_"
    exit 1
}

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$rec = Join-Path $root (Join-Path $BuildDir "bin\$Config\sr-recorder.exe")
$probe = Join-Path $root (Join-Path $BuildDir "bin\$Config\srprobe.exe")
if (-not (Test-Path $rec)) { $rec = Join-Path $root (Join-Path $BuildDir 'bin\sr-recorder.exe') }
if (-not (Test-Path $probe)) { $probe = Join-Path $root (Join-Path $BuildDir 'bin\srprobe.exe') }
if (-not (Test-Path $rec) -or -not (Test-Path $probe)) { Write-Host 'FAIL: binaries missing'; exit 2 }

$work = Join-Path $env:TEMP 'sr-window'
if (Test-Path $work) { Get-ChildItem $work -Recurse -Force | Remove-Item -Force -Recurse -ErrorAction SilentlyContinue }
New-Item -ItemType Directory -Force -Path $work | Out-Null
$script:SrForensicsWorkDirs = @($work)

$script:pass = 0
$script:fail = 0
$script:skip = 0
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
function Skip {
    param([string]$Name, [string]$Reason)
    $script:skip++
    Write-Host ("  SKIP {0} — {1}" -f $Name, $Reason)
}

function Field {
    param([string]$Text, [string]$Name)
    if ($Text -match "(?m)^$Name=([^`r`n]*)") { return $Matches[1].Trim() }
    return ''
}

function Get-VideoSize {
    param([string]$Video)
    $out = & ffprobe -v error -select_streams v:0 -show_entries stream=width,height `
        -of csv=p=0 $Video 2>$null | Out-String
    $parts = ($out -split ',') | Where-Object { $_.Trim() -ne '' }
    if ($parts.Count -lt 2) { return $null }
    return [pscustomobject]@{ width = [int]$parts[0]; height = [int]$parts[1] }
}

# 抽一帧原始 RGB，统计各色占比与包围盒（绿色=窗口内容、蓝色=窗口边框、红色=遮挡物、黑色=黑边）。
function Get-FrameStats {
    param([string]$Video, [int]$Index, [int]$Width, [int]$Height)
    # 中间文件按「视频名+帧号」命名并先删：同名会复用上一次抽帧的旧文件，
    # 导致 pad 的测量结果被 stretch 读到（真的发生过：stretch 误报 25% 黑边）。
    $stem = [System.IO.Path]::GetFileNameWithoutExtension($Video)
    $raw = Join-Path $work ('frame-' + $stem + '-' + $Index + '.raw')
    if (Test-Path $raw) { Remove-Item $raw -Force }
    # ffmpeg 9 已移除 -vsync（等价 -fps_mode passthrough）。
    & ffmpeg -hide_banner -v error -i $Video -vf "select=eq(n\,$Index)" -fps_mode passthrough `
        -frames:v 1 -f rawvideo -pix_fmt rgb24 $raw 2>$null
    if (-not (Test-Path $raw)) { return $null }
    $bytes = [System.IO.File]::ReadAllBytes($raw)
    if ($bytes.Length -lt ($Width * $Height * 3)) { return $null }
    $green = 0; $black = 0; $red = 0; $blue = 0
    $minX = $Width; $maxX = -1; $minY = $Height; $maxY = -1
    $blueMinX = $Width; $blueMaxX = -1; $blueMinY = $Height; $blueMaxY = -1
    for ($y = 0; $y -lt $Height; $y++) {
        $rowBase = $y * $Width * 3
        for ($x = 0; $x -lt $Width; $x++) {
            $i = $rowBase + $x * 3
            $r = $bytes[$i]; $g = $bytes[$i + 1]; $b = $bytes[$i + 2]
            if ($b -gt 120 -and $r -lt 90 -and $g -lt 90) {
                # 蓝色边框：记录包围盒。画面被裁剪时右/下边框消失 → 包围盒缩到左上角。
                $blue++
                if ($x -lt $blueMinX) { $blueMinX = $x }
                if ($x -gt $blueMaxX) { $blueMaxX = $x }
                if ($y -lt $blueMinY) { $blueMinY = $y }
                if ($y -gt $blueMaxY) { $blueMaxY = $y }
                if ($x -lt $minX) { $minX = $x }
                if ($x -gt $maxX) { $maxX = $x }
                if ($y -lt $minY) { $minY = $y }
                if ($y -gt $maxY) { $maxY = $y }
            } elseif ($g -gt 120 -and $r -lt 90 -and $b -lt 90) {
                $green++
                if ($x -lt $minX) { $minX = $x }
                if ($x -gt $maxX) { $maxX = $x }
                if ($y -lt $minY) { $minY = $y }
                if ($y -gt $maxY) { $maxY = $y }
            } elseif ($r -gt 120 -and $g -lt 90 -and $b -lt 90) {
                $red++
            } elseif ($r -lt 40 -and $g -lt 40 -and $b -lt 40) {
                $black++
            }
        }
    }
    $total = $Width * $Height
    return [pscustomobject]@{
        green_ratio = $green / $total
        black_ratio = $black / $total
        red_ratio = $red / $total
        blue_ratio = $blue / $total
        blue_min_x = $blueMinX; blue_max_x = $blueMaxX
        blue_min_y = $blueMinY; blue_max_y = $blueMaxY
        min_x = $minX; max_x = $maxX; min_y = $minY; max_y = $maxY
    }
}

# 边框是否贴合内容包围盒（四边 ≤8px）。
function Test-BorderComplete {
    param($Frame)
    return (([Math]::Abs($Frame.blue_min_x - $Frame.min_x) -le 8) -and
            ([Math]::Abs($Frame.blue_max_x - $Frame.max_x) -le 8) -and
            ([Math]::Abs($Frame.blue_min_y - $Frame.min_y) -le 8) -and
            ([Math]::Abs($Frame.blue_max_y - $Frame.max_y) -le 8))
}

function Start-WindowHost {
    param([int]$Width, [int]$Height, [int]$Seconds, [string]$ResizeTo, [switch]$Occluder, [int]$Cycle = 0)
    $tag = [Guid]::NewGuid().ToString('N').Substring(0, 6)
    $out = Join-Path $work "host-$tag.out"
    $args = @('windowhost', '--width', "$Width", '--height', "$Height", '--seconds', "$Seconds")
    if ($ResizeTo) { $args += @('--resize-to', $ResizeTo) }
    if ($Occluder) { $args += @('--occluder') }
    if ($Cycle -gt 0) { $args += @('--resize-cycle', "$Cycle") }
    $proc = Start-Process -FilePath $probe -PassThru -WindowStyle Hidden -RedirectStandardOutput $out `
        -RedirectStandardError (Join-Path $work "host-$tag.err") -ArgumentList $args
    Register-SrProcess $proc
    $deadline = (Get-Date).AddSeconds(10)
    $hwnd = 0
    while ((Get-Date) -lt $deadline) {
        if (Test-Path $out) {
            $text = Get-Content $out -Encoding UTF8 -Raw -ErrorAction SilentlyContinue
            if ($text -and $text -match 'windowhost_hwnd=0x([0-9a-fA-F]+)') {
                $hwnd = [Convert]::ToInt64($Matches[1], 16)
                break
            }
        }
        Start-Sleep -Milliseconds 100
    }
    return [pscustomobject]@{ proc = $proc; hwnd = $hwnd }
}

function Record-Window {
    param([long]$Hwnd, [string]$Fit, [int]$Seconds, [string]$Tag)
    $out = Join-Path $work "$Tag.out"
    $log = Join-Path $work "$Tag.log"
    $clip = Join-Path $work "$Tag.mkv"
    $proc = Start-Process -FilePath $rec -PassThru -WindowStyle Hidden -RedirectStandardOutput $out `
        -RedirectStandardError (Join-Path $work "$Tag.err") `
        -ArgumentList @('--record', '--seconds', "$Seconds", '--fps', '30', '--bitrate', '4000',
                        '--window', "$Hwnd", '--fit', $Fit, '--out', "`"$clip`"",
                        '--log-level', 'info', '--log-file', "`"$log`"")
    Register-SrProcess $proc
    $finished = $proc.WaitForExit(120000)
    $exit = if ($finished) { $proc.ExitCode } else { -1 }
    Unregister-SrProcess $proc
    $text = if (Test-Path $out) { Get-Content $out -Encoding UTF8 -Raw } else { '' }
    return [pscustomobject]@{
        exit = $exit
        text = $text
        clip = $clip
        fit_mode = Field $text 'fit_mode'
        source_size_changes = [int](Field $text 'source_size_changes')
        source_size = Field $text 'source_size'
        dropped = [int](Field $text 'frames_dropped')
        captured = [int](Field $text 'frames_captured')
        frames_encoded = [int](Field $text 'frames_encoded')
    }
}

# ---------------- 1 被遮挡采集（M1-09#1） ----------------
Write-Host '--- 1 目标窗口被另一个窗口遮挡（fit=pad）---'
$hostWindow = Start-WindowHost -Width 800 -Height 600 -Seconds 40 -ResizeTo '' -Occluder
Assert 'm1-09-host-window' ($hostWindow.hwnd -ne 0) ("自建窗口 hwnd={0}" -f $hostWindow.hwnd)
if ($hostWindow.hwnd -eq 0) { Write-Host 'RESULT: 0 PASS / 1 FAIL'; exit 1 }

$occluded = Record-Window -Hwnd $hostWindow.hwnd -Fit 'pad' -Seconds 8 -Tag 'occluded'
Assert 'm1-09-record-exit' ($occluded.exit -eq 0) ("录制退出码={0}" -f $occluded.exit)
Assert 'm1-09-no-dropped' ($occluded.dropped -eq 0) ("丢帧={0} / 采集={1}" -f $occluded.dropped, $occluded.captured)

$size = Get-VideoSize -Video $occluded.clip
if ($size) {
    $frame = Get-FrameStats -Video $occluded.clip -Index 120 -Width $size.width -Height $size.height
    Assert 'm1-09-frame-read' ($null -ne $frame) `
        ("成片尺寸 {0}x{1}，抽帧结果：{2}" -f $size.width, $size.height, $(if ($frame) { 'ok' } else { 'null' }))
    if ($frame) {
        Assert 'm1-09-content-still-visible' ($frame.green_ratio -gt 0.5) `
            ("被遮挡时绿色占比={0:P1}（期望 >50%，即录到的是目标窗口内容）" -f $frame.green_ratio)
        Assert 'm1-09-occluder-not-captured' ($frame.red_ratio -lt 0.02) `
            ("成片里遮挡物（红）占比={0:P1}（期望 <2%）" -f $frame.red_ratio)
        Assert 'm1-09-content-complete' (Test-BorderComplete $frame) `
            ("蓝色边框包围盒 x=[{0},{1}] y=[{2},{3}]；内容包围盒 x=[{4},{5}] y=[{6},{7}]（两者应贴合）" -f `
                $frame.blue_min_x, $frame.blue_max_x, $frame.blue_min_y, $frame.blue_max_y,
                $frame.min_x, $frame.max_x, $frame.min_y, $frame.max_y)
    }
} else {
    Assert 'm1-09-video-readable' $false 'ffprobe 读不到成片尺寸'
}

# ---------------- 2 窗口尺寸跟随（M1-08#1/#2） ----------------
Write-Host '--- 2 窗口 800x600 → 1280x720（fit=pad）---'
$hostResize = Start-WindowHost -Width 800 -Height 600 -Seconds 40 -ResizeTo '1280x720@6'
Assert 'm1-08-host-window' ($hostResize.hwnd -ne 0) ("自建窗口 hwnd={0}" -f $hostResize.hwnd)
$pad = Record-Window -Hwnd $hostResize.hwnd -Fit 'pad' -Seconds 11 -Tag 'pad'
Assert 'm1-08-record-exit' ($pad.exit -eq 0) ("录制退出码={0}" -f $pad.exit)
Assert 'm1-08-fit-mode-applied' ($pad.fit_mode -eq 'pad') ("fit_mode={0}（期望 pad）" -f $pad.fit_mode)
Assert 'm1-08-source-follows' ($pad.source_size_changes -ge 1 -and $pad.source_size -eq '1280x720') `
    ("源尺寸变化次数={0}，最终源尺寸={1}（期望 ≥1 次、1280x720）" -f $pad.source_size_changes, $pad.source_size)
Assert 'm1-08-no-dropped' ($pad.dropped -eq 0) ("丢帧={0} / 采集={1}" -f $pad.dropped, $pad.captured)

$padSize = Get-VideoSize -Video $pad.clip
if ($padSize) {
    $after = Get-FrameStats -Video $pad.clip -Index 240 -Width $padSize.width -Height $padSize.height
    Assert 'm1-08-frame-read' ($null -ne $after) `
        ("成片 {0}x{1}，抽帧结果：{2}" -f $padSize.width, $padSize.height, $(if ($after) { 'ok' } else { 'null' }))
    if ($after) {
        Assert 'm1-08-content-complete' (Test-BorderComplete $after) `
            ("蓝色边框包围盒 x=[{0},{1}] y=[{2},{3}]；内容包围盒 x=[{4},{5}] y=[{6},{7}]（两者应贴合，偏差 ≤8px）" -f `
                $after.blue_min_x, $after.blue_max_x, $after.blue_min_y, $after.blue_max_y,
                $after.min_x, $after.max_x, $after.min_y, $after.max_y)
        # pad 策略：1280x720 等比缩到 800x450，上下各留 75px 黑边（±2px）。
        $topBar = $after.min_y
        $bottomBar = $padSize.height - 1 - $after.max_y
        Assert 'm1-08-fit-pad-black-bars' `
            ([Math]::Abs($topBar - 75) -le 2 -and [Math]::Abs($bottomBar - 75) -le 2) `
            ("pad 黑边 上={0}px 下={1}px（期望 75±2px；黑边占比={2:P1}）" -f $topBar, $bottomBar, $after.black_ratio)
    }
    # 切换瞬间 5 帧内不能出现全黑帧。
    $blackFrames = @()
    for ($i = 177; $i -le 183; $i++) {
        $f = Get-FrameStats -Video $pad.clip -Index $i -Width $padSize.width -Height $padSize.height
        if ($f -and $f.green_ratio -lt 0.02 -and $f.blue_ratio -lt 0.02) { $blackFrames += $i }
    }
    Assert 'm1-08-no-black-frame-on-switch' ($blackFrames.Count -eq 0) `
        ("切换点 177~183 帧里的全黑帧：{0}（期望 0）" -f `
            ($(if ($blackFrames.Count -gt 0) { $blackFrames -join ',' } else { '无' })))
}

Write-Host '--- 3 同一场景（fit=stretch）---'
$hostStretch = Start-WindowHost -Width 800 -Height 600 -Seconds 40 -ResizeTo '1280x720@6'
$stretch = Record-Window -Hwnd $hostStretch.hwnd -Fit 'stretch' -Seconds 11 -Tag 'stretch'
Assert 'm1-08-stretch-exit' ($stretch.exit -eq 0) ("录制退出码={0}" -f $stretch.exit)
Assert 'm1-08-stretch-fit-mode-applied' ($stretch.fit_mode -eq 'stretch') `
    ("fit_mode={0}（期望 stretch）" -f $stretch.fit_mode)
Assert 'm1-08-stretch-source-follows' ($stretch.source_size_changes -ge 1) `
    ("源尺寸变化次数={0}" -f $stretch.source_size_changes)
$stretchSize = Get-VideoSize -Video $stretch.clip
if ($stretchSize) {
    $stretchAfter = Get-FrameStats -Video $stretch.clip -Index 240 -Width $stretchSize.width -Height $stretchSize.height
    if ($stretchAfter) {
        Assert 'm1-08-fit-stretch-no-bars' ($stretchAfter.black_ratio -lt 0.02) `
            ("stretch 黑边占比={0:P1}（期望 <2%，即拉伸铺满无黑边）" -f $stretchAfter.black_ratio)
        Assert 'm1-08-stretch-content-complete' (Test-BorderComplete $stretchAfter) `
            ("stretch 蓝色边框包围盒 x=[{0},{1}] y=[{2},{3}]；内容包围盒 x=[{4},{5}] y=[{6},{7}]" -f `
                $stretchAfter.blue_min_x, $stretchAfter.blue_max_x, $stretchAfter.blue_min_y, $stretchAfter.blue_max_y,
                $stretchAfter.min_x, $stretchAfter.max_x, $stretchAfter.min_y, $stretchAfter.max_y)
    }
}

# ---------------- 4 反复 resize 不崩溃 ----------------
Write-Host '--- 4 每秒切换一次尺寸（压「反复 resize」路径）---'
$hostCycle = Start-WindowHost -Width 800 -Height 600 -Seconds 45 -ResizeTo '1280x720@1' -Cycle 1
$cycle = Record-Window -Hwnd $hostCycle.hwnd -Fit 'pad' -Seconds 24 -Tag 'cycle'
Assert 'm1-08-cycle-exit' ($cycle.exit -eq 0) `
    ("录制退出码={0}（期望 0：反复 resize 不得崩）" -f $cycle.exit)
Assert 'm1-08-cycle-resizes' ($cycle.source_size_changes -ge 20) `
    ("尺寸变化次数={0}（期望 ≥20 次）" -f $cycle.source_size_changes)
Assert 'm1-08-cycle-no-dropped' ($cycle.dropped -eq 0) `
    ("丢帧={0} / 采集={1}" -f $cycle.dropped, $cycle.captured)

foreach ($h in @($hostWindow, $hostResize, $hostStretch, $hostCycle)) {
    if ($h -and $h.proc -and -not $h.proc.HasExited) { Stop-SrProcess $h.proc }
}

Write-Host ''
Write-Host ("RESULT: {0} PASS / {1} FAIL / {2} SKIP ; 产物 {3}" -f `
    $script:pass, $script:fail, $script:skip, $work)
if ($script:fail -gt 0) { exit 1 }
exit 0
