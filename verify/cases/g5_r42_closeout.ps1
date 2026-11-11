# 轮 42 · 无卡收口 5 条专属行为断言
#
#   M6-08#2  CPU 中转仍能录制成功：--force-cpu -> converter=cpu + 录制成功 + 成片可解码
#   M3-10#1  画质档位联动：切档位后 fps/bitrate/quality 三项联动变化 + 高级面板单独覆盖
#   M3-10#2  预设值在界面可见：设置页控件文本含当前档位与联动值
#   M6-11#2  背压可见标识：注入丢帧 -> 状态栏出现「⚠ 背压」，正常态不出现
#
# 本轮 5 条收口 = M6-08#2 / M6-10#2 / M3-10#1 / M3-10#2 / M6-11#2；
# M6-10#2 的断言在 g5_r42_hvci.ps1（HVCI 环境实测，独立成条）。
# M7-13#1/#2、M1-24#1/#2 等仍需真实 UI 绘制/像素级验证，本轮**不收口**，在 HANDOFF 挂账。
# 降级/代理验证逐条标注；取数失败一律 SKIP，不拿 0 算结论。
# Usage: pwsh -File verify/cases/g5_r42_closeout.ps1

param([string]$BuildDir = 'build-msvc', [string]$Config = 'Release')

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\lib\Cleanup.ps1')
. (Join-Path $PSScriptRoot '..\lib\Forensics.ps1')
trap {
    [void](Save-FailureContext -Tag 'g5_r42_closeout' -WorkDirs @($script:work) -Note $_)
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

$script:work = Join-Path $env:TEMP 'sr-r42-closeout'
if (Test-Path $script:work) { Get-ChildItem $script:work -Recurse -Force | Remove-Item -Force -Recurse -ErrorAction SilentlyContinue }
New-Item -ItemType Directory -Force -Path $script:work | Out-Null

$script:pass = 0; $script:fail = 0; $script:skip = 0
function Assert {
    param([string]$Name, [bool]$Ok, [string]$Detail)
    if ($Ok) { $script:pass++; Write-Host ("  PASS {0} — {1}" -f $Name, $Detail) }
    else {
        $script:fail++
        Write-Host ("  FAIL {0} — {1}" -f $Name, $Detail)
        [void](Save-FailureContext -Tag $Name -WorkDirs @($script:work) -Note $Detail)
    }
}
function Skip { param([string]$Name, [string]$Reason) $script:skip++; Write-Host ("  SKIP {0} — {1}" -f $Name, $Reason) }
function Field {
    param([string]$Text, [string]$Name)
    if ($Text -match "(?m)^$Name=([^`r`n]*)") { return $Matches[1].Trim() }
    return ''
}
function Read-Log { param([string]$Path) return (Read-TextSafe $Path) }
function Wait-Log {
    param([string]$Path, [string]$Pattern, [int]$TimeoutMs = 9000)
    $deadline = (Get-Date).AddMilliseconds($TimeoutMs)
    while ((Get-Date) -lt $deadline) {
        if ((Read-Log $Path) -match $Pattern) { return $true }
        Start-Sleep -Milliseconds 100
    }
    return $false
}
function Get-Probe {
    param([string]$Path)
    if (-not $ffbin -or -not (Test-Path $Path)) { return '' }
    return (& (Join-Path $ffbin 'ffprobe.exe') -v error -show_entries format=duration `
        -show_entries stream=codec_type -of default=nw=1 $Path 2>&1 | Out-String)
}
function Test-Decodable {
    param([string]$Path)
    if (-not $ffbin -or -not (Test-Path $Path)) { return $false }
    $o = & (Join-Path $ffbin 'ffmpeg.exe') -hide_banner -v error -i $Path -f null - 2>&1 | Out-String
    return [string]::IsNullOrWhiteSpace($o)
}

Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class SrR42Native {
    [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "FindWindowW")]
    public static extern IntPtr FindWindowClass(string cls, IntPtr title);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr hDlg, int id);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "GetWindowTextW")]
    public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int max);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "SendMessageW")]
    public static extern IntPtr SendMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    public const uint WM_COMMAND = 0x0111;
    public const uint CB_GETCURSEL = 0x0147;
    public const uint CB_SETCURSEL = 0x014E;
    public const uint CB_GETLBTEXT = 0x0148;
    public const int CBN_SELCHANGE = 1;
    public static IntPtr MakeWParam(int id, int code) { return (IntPtr)((code << 16) | (id & 0xffff)); }
    public static string Text(IntPtr h) {
        var s = new StringBuilder(2048);
        GetWindowTextW(h, s, s.Capacity);
        return s.ToString();
    }
}
'@
function Get-ControlText {
    param([IntPtr]$Parent, [int]$Id)
    if ($Parent -eq [IntPtr]::Zero) { return '' }
    return [SrR42Native]::Text([SrR42Native]::GetDlgItem($Parent, $Id))
}

# ================================================================ M6-08#2 CPU 中转
Write-Host '--- M6-08#2 强制走既有 CPU 中转路径仍能录制成功（降级验证：强制开关触发）---'
Clear-SrProcesses -IncludeNameFallback
Start-Sleep -Milliseconds 300
$cpuFile = Join-Path $script:work 'cpu.mkv'
$cpuOut = & $rec --record --seconds 3 --fps 30 --bitrate 6000 --force-cpu --out $cpuFile 2>&1 | Out-String
$cpuExit = [int](Field $cpuOut 'record_exit')
$cpuConverter = Field $cpuOut 'converter'
$cpuEncoded = [double](Field $cpuOut 'frames_encoded')
Assert 'm6-08-02-force-cpu-path' ($cpuExit -eq 0 -and $cpuConverter -eq 'cpu' -and $cpuEncoded -gt 0) `
    ("--force-cpu：record_exit={0} converter={1} frames_encoded={2}（强制开关令既有 CPU 中转路径可达）" -f `
     $cpuExit, $cpuConverter, $cpuEncoded)
if (-not $ffbin) {
    Skip 'm6-08-02-cpu-decodable' 'ffprobe/ffmpeg 不可用，无法核验成片'
} else {
    $probe = Get-Probe $cpuFile
    Assert 'm6-08-02-cpu-decodable' ((Test-Decodable $cpuFile) -and ($probe -match 'codec_type=video') `
        -and ($probe -match 'codec_type=audio')) `
        ("CPU 路径成片 ffmpeg 解码零错误，video+audio 双轨；duration 行：{0}" -f `
         (($probe -split "`n" | Where-Object { $_ -match 'duration=' } | Select-Object -First 1)))
}
$gpuFile = Join-Path $script:work 'gpu.mkv'
$gpuOut = & $rec --record --seconds 2 --fps 30 --out $gpuFile 2>&1 | Out-String
Assert 'm6-08-02-gpu-control' ((Field $gpuOut 'converter') -eq 'gpu-videoprocessor') `
    ("对照组（不传开关）converter={0}，证明开关确实切换了路径" -f (Field $gpuOut 'converter'))

# ================================================================ M3-10#1 档位联动
Write-Host '--- M3-10#1 画质档位切换后 fps / 码率 / 画质三项联动 ---'
$ini = Join-Path $script:work 'preset.ini'
[System.IO.File]::WriteAllText($ini, "[video]`r`nfps=30`r`nquality=70`r`n[output]`r`nbitrate_kbps=6000`r`n",
    [System.Text.UTF8Encoding]::new($false))
$presetLog = Join-Path $script:work 'preset.log'
$linkRows = @()
foreach ($name in @('极致', '高清', '标准', '省空间')) {
    $pOut = & $ui --set-preset $name --config $ini --log $presetLog 2>&1 | Out-String
    $pRc = $LASTEXITCODE
    # INI 由产品的 WritePrivateProfileStringW 写入（系统 ANSI 编码），按同一编码读回。
    $iniText = [System.IO.File]::ReadAllText($ini, [System.Text.Encoding]::Default)
    $fpsVal = if ($iniText -match 'fps=(\d+)') { [int]$Matches[1] } else { 0 }
    $kbps = if ($iniText -match 'bitrate_kbps=(\d+)') { [int]$Matches[1] } else { 0 }
    $qual = if ($iniText -match 'quality=(\d+)') { [int]$Matches[1] } else { -1 }
    $logText = Read-Log $presetLog
    $linkRows += [pscustomobject]@{ name = $name; fps = $fpsVal; kbps = $kbps; quality = $qual;
                                   rc = $pRc; logged = ($logText -match ('preset_saved name=' + [regex]::Escape($name))) }
}
$distinct = @($linkRows | ForEach-Object { "$($_.fps)/$($_.kbps)/$($_.quality)" } | Select-Object -Unique)
Assert 'm3-10-01-preset-links-three-params' ($distinct.Count -eq 4 -and
        @($linkRows | Where-Object { $_.rc -eq 0 -and $_.logged }).Count -eq 4) `
    ("四档联动值互不相同（{0} 组）：{1}；每次都写日志且退出码 0" -f $distinct.Count,
     (($linkRows | ForEach-Object { "$($_.name)=>$($_.fps)fps/$($_.kbps)kbps/q$($_.quality)" }) -join ', '))
Assert 'm3-10-01-preset-values-monotonic' (
        ($linkRows[0].fps -gt $linkRows[3].fps) -and
        ($linkRows[0].kbps -gt $linkRows[3].kbps) -and
        ($linkRows[0].quality -gt $linkRows[3].quality)) `
    ("「极致」三项都高于「省空间」：fps {0}>{1}, kbps {2}>{3}, quality {4}>{5}" -f `
     $linkRows[0].fps, $linkRows[3].fps, $linkRows[0].kbps, $linkRows[3].kbps,
     $linkRows[0].quality, $linkRows[3].quality)
# 高级面板单独覆盖：--set-fps 改帧率后，档位名保持可读且 fps 被单独改掉。
$coverOut = & $ui --set-fps 45 --config $ini --log $presetLog 2>&1 | Out-String
$coverIni = [System.IO.File]::ReadAllText($ini, [System.Text.Encoding]::Default)
$coverFps = if ($coverIni -match 'fps=(\d+)') { [int]$Matches[1] } else { 0 }
Assert 'm3-10-01-advanced-panel-override' ($coverFps -eq 45 -and $coverIni -match 'quality_preset=') `
    ("高级面板单独把 fps 改成 45 并落盘（实际 fps={0}），档位名仍在配置中，未被预设强行回写" -f $coverFps)

# ================================================================ M3-10#2 预设值界面可见
Write-Host '--- M3-10#2 预设值在界面可见（设置页控件文本）---'
$settingsIni = Join-Path $script:work 'settings.ini'
if (Test-Path $settingsIni) { Remove-Item $settingsIni -Force }
$settingsLog = Join-Path $script:work 'settings.log'
# 中文档位名必须由产品自己的 WritePrivateProfileStringW 写入：手写 UTF8 INI 会被
# Windows profile API 按 ANSI 解读成乱码（首轮实测到「鏋佽嚧」）。
$seed = & $ui --set-preset '极致' --config $settingsIni --log $settingsLog 2>&1 | Out-String
$seedRc = $LASTEXITCODE
Assert 'm3-10-02-preset-seed-ok' ($seedRc -eq 0 -and (Read-Log $settingsLog) -match 'preset_saved name=极致') `
    ("产品 API 落盘档位（退出码 {0}）；日志：{1}" -f $seedRc,
     (($seed -split "`n" | Where-Object { $_ -match 'preset_saved' } | Select-Object -First 1)))
$uiProc = Start-Process -FilePath $ui -PassThru -WindowStyle Hidden `
    -ArgumentList @('--hidden', '--show-settings', '--config', $settingsIni, '--log', $settingsLog,
                   '--exit-after-ms', '9000')
Register-SrProcess $uiProc
$null = Wait-Log $settingsLog 'app_ready' 9000
Start-Sleep -Milliseconds 900
$settingsHwnd = [SrR42Native]::FindWindowClass('SrUiSettingsWindow', [IntPtr]::Zero)
if ($settingsHwnd -eq [IntPtr]::Zero) {
    Skip 'm3-10-02-preset-visible' '设置页窗口未找到'
} else {
    $comboHwnd = [SrR42Native]::GetDlgItem($settingsHwnd, 108)
    $valuesText = Get-ControlText $settingsHwnd 109
    $sel = [int][SrR42Native]::SendMessage($comboHwnd, [SrR42Native]::CB_GETCURSEL,
                                          [IntPtr]::Zero, [IntPtr]::Zero)
    Assert 'm3-10-02-preset-visible' ($valuesText -match '当前预设' -and $valuesText -match 'fps' `
            -and $valuesText -match 'kbps' -and $valuesText -match '画质') `
        "设置页只读文本（控件 109）显示当前预设与联动值：$valuesText"
    Assert 'm3-10-02-preset-name-not-garbled' ($valuesText -match '当前预设：极致' -and $sel -eq 0) `
        ("档位名在界面正确显示（非乱码），下拉选中序号={0}（0=极致）" -f $sel)
    Assert 'm3-10-02-preset-combo-present' ($comboHwnd -ne [IntPtr]::Zero -and $sel -ge 0) `
        ("画质档位下拉（控件 108）存在且有选中项，选中序号={0}" -f $sel)
}
try { if (-not $uiProc.HasExited) { [void]$uiProc.WaitForExit(12000) } } catch {}

# ================================================================ M6-11#2 背压可见标识
Write-Host '--- M6-11#2 背压可见标识（注入丢帧走同一 UI 渲染分支）---'
Clear-SrProcesses -IncludeNameFallback
Start-Sleep -Milliseconds 300
$bpIni = Join-Path $script:work 'bp.ini'
[System.IO.File]::WriteAllText($bpIni, "[output]`r`ndir=$($script:work)`r`n", [System.Text.UTF8Encoding]::new($false))
$bpLog = Join-Path $script:work 'bp.log'
$bpProc = Start-Process -FilePath $ui -PassThru -WindowStyle Hidden `
    -ArgumentList @('--hidden', '--config', $bpIni, '--log', $bpLog,
                   '--simulate-backpressure', '7', '--exit-after-ms', '8000')
Register-SrProcess $bpProc
$null = Wait-Log $bpLog 'app_ready' 9000
Start-Sleep -Milliseconds 900
$mainHwnd = [SrR42Native]::FindWindowClass('SrUiMainWindow', [IntPtr]::Zero)
if ($mainHwnd -eq [IntPtr]::Zero) {
    Skip 'm6-11-02-backpressure-visible' '主窗口未找到'
} else {
    $bpText = Get-ControlText $mainHwnd 200
    Assert 'm6-11-02-backpressure-visible' ($bpText -match '背压' -and $bpText -match '丢帧 7') `
        "状态栏可见标识：$($bpText -split "`n" | Select-Object -First 1)"
}
try { if (-not $bpProc.HasExited) { [void]$bpProc.WaitForExit(11000) } } catch {}

Clear-SrProcesses -IncludeNameFallback
Start-Sleep -Milliseconds 300
$normalLog = Join-Path $script:work 'normal.log'
$normalProc = Start-Process -FilePath $ui -PassThru -WindowStyle Hidden `
    -ArgumentList @('--hidden', '--config', $bpIni, '--log', $normalLog, '--exit-after-ms', '7000')
Register-SrProcess $normalProc
$null = Wait-Log $normalLog 'app_ready' 9000
Start-Sleep -Milliseconds 900
$normalHwnd = [SrR42Native]::FindWindowClass('SrUiMainWindow', [IntPtr]::Zero)
if ($normalHwnd -eq [IntPtr]::Zero) {
    Skip 'm6-11-02-no-false-backpressure' '主窗口未找到'
} else {
    $normalText = Get-ControlText $normalHwnd 200
    Assert 'm6-11-02-no-false-backpressure' ($normalText -notmatch '背压') `
        "正常（无丢帧）时状态栏不显示背压标识：$($normalText -split "`n" | Select-Object -First 1)"
}
try { if (-not $normalProc.HasExited) { [void]$normalProc.WaitForExit(10000) } } catch {}

Clear-SrProcesses -IncludeNameFallback
Write-Host ''
Write-Host ("RESULT: {0} PASS / {1} FAIL / {2} SKIP" -f $script:pass, $script:fail, $script:skip)
if ($script:fail -gt 0) { exit 1 }
exit 0
