# 轮 41 · 无卡收口专属用例：M8-01#1 / M6-10#1 / M6-11#1 / M3-15#1 / M6-04#2
#
# 每条子句至少一条**行为断言**，一需求一证据；降级/代理验证在断言文案里显式标注，
# 不用文案冒充实测。所有取数失败一律 SKIP（与 R1 三档语义一致），不拿 0 算结论。
#
#   M8-01#1  强杀→scan→repair→恢复到崩溃前 ≤10s 且可播（12s 强杀代理 5min 长录）
#   M6-10#1  安装包无驱动组件（zip 条目 + install.ps1 + 源码三方）
#   M6-11#1  面板数值随录制状态更新（recorder 1Hz 指标 + UI 状态栏两态对照）
#   M3-15#1  定点丢帧：注入 45fps 时成片时长准确、无加速、音轨在（注入代理 GPU 压力）
#   M6-04#2  硬编均失败时落软编且录制成功（含全失败双分支）
#
# Usage: pwsh -File verify/cases/g5_closeout.ps1 [-BuildDir build-msvc] [-Config Release]

param(
    [string]$BuildDir = 'build-msvc',
    [string]$Config = 'Release'
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\lib\Cleanup.ps1')
. (Join-Path $PSScriptRoot '..\lib\Forensics.ps1')
trap {
    [void](Save-FailureContext -Tag 'g5_closeout' -WorkDirs @($script:work) -Note $_)
    Clear-SrProcesses -IncludeNameFallback
    Write-Host "FAIL: $_"
    exit 1
}

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$exe = Join-Path $root (Join-Path $BuildDir "bin\$Config\sr-recorder.exe")
$ui = Join-Path $root (Join-Path $BuildDir "bin\$Config\sr-ui.exe")
if (-not (Test-Path $exe)) { $exe = Join-Path $root (Join-Path $BuildDir 'bin\sr-recorder.exe') }
if (-not (Test-Path $ui)) { $ui = Join-Path $root (Join-Path $BuildDir 'bin\sr-ui.exe') }
if (-not (Test-Path $exe) -or -not (Test-Path $ui)) { Write-Host 'FAIL: binaries missing'; exit 2 }

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

$script:work = Join-Path $env:TEMP 'sr-closeout'
if (Test-Path $script:work) {
    Get-ChildItem $script:work -Recurse -Force | Remove-Item -Force -Recurse -ErrorAction SilentlyContinue
}
New-Item -ItemType Directory -Force -Path $script:work | Out-Null
$script:SrForensicsWorkDirs = @($script:work)

$script:pass = 0
$script:fail = 0
$script:skip = 0
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
function Read-Log {
    param([string]$Path)
    # UI 进程仍持有日志写句柄；ReadAllText 会抛共享冲突 → 必须用 FileShare.ReadWrite
    # （同 verify/lib/Cleanup.ps1 的 Read-TextSafe）。轮 41 首轮 SKIP 的根因。
    return (Read-TextSafe $Path)
}
function Wait-Log {
    param([string]$Path, [string]$Pattern, [int]$TimeoutMs = 8000)
    $deadline = (Get-Date).AddMilliseconds($TimeoutMs)
    while ((Get-Date) -lt $deadline) {
        $text = Read-Log $Path
        if ($text -match $Pattern) { return $true }
        Start-Sleep -Milliseconds 100
    }
    return $false
}
function Get-Probe {
    param([string]$Path)
    if (-not $ffbin -or -not (Test-Path $Path)) { return '' }
    return (& (Join-Path $ffbin 'ffprobe.exe') -v error -show_entries format=duration `
        -show_entries stream=codec_type,avg_frame_rate -of default=nw=1 $Path 2>&1 | Out-String)
}
function Test-Decodable {
    param([string]$Path)
    if (-not $ffbin -or -not (Test-Path $Path)) { return $false }
    $decode = & (Join-Path $ffbin 'ffmpeg.exe') -hide_banner -v error -i $Path -f null - 2>&1 | Out-String
    return [string]::IsNullOrWhiteSpace($decode)
}

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class SrCloseoutNative {
    [DllImport("user32.dll")] public static extern void keybd_event(byte bVk, byte bScan, uint dwFlags, UIntPtr dwExtraInfo);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "FindWindowW")]
    public static extern IntPtr FindWindowClass(string lpClassName, IntPtr lpWindowName);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr hDlg, int nIDDlgItem);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "GetWindowTextW")]
    public static extern int GetWindowTextW(IntPtr hWnd, System.Text.StringBuilder lpString, int nMaxCount);
}
'@
function Press-Combo {
    param([byte[]]$Keys)
    foreach ($key in $Keys) { [SrCloseoutNative]::keybd_event($key, 0, 0, [UIntPtr]::Zero) }
    Start-Sleep -Milliseconds 80
    for ($i = $Keys.Count - 1; $i -ge 0; $i--) {
        [SrCloseoutNative]::keybd_event($Keys[$i], 0, 2, [UIntPtr]::Zero)
    }
    Start-Sleep -Milliseconds 80
}
function Get-StatusText {
    $main = [SrCloseoutNative]::FindWindowClass('SrUiMainWindow', [IntPtr]::Zero)
    if ($main -eq [IntPtr]::Zero) { return '' }
    $sb = New-Object System.Text.StringBuilder 4096
    [void][SrCloseoutNative]::GetWindowTextW([SrCloseoutNative]::GetDlgItem($main, 200), $sb, 4096)
    return $sb.ToString()
}

# ================================================================ M8-01#1 中断恢复 ≤10s
Write-Host '--- M8-01#1 强制结束后恢复到崩溃前 ≤10s（12s 强杀代理 5min 长录）---'
$killAfter = 12
$crash = Join-Path $script:work 'm8-01.mkv'
$part = $crash + '.part'
$proc = Start-Process -FilePath $exe -PassThru -WindowStyle Hidden `
    -ArgumentList @('--record', '--seconds', '60', '--fps', '30', '--out', "`"$crash`"")
Register-SrProcess $proc
Start-Sleep -Seconds $killAfter
if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force }
Start-Sleep -Milliseconds 600
Assert 'm8-01-01-part-left' (Test-Path $part) "强杀后遗留 .part=$part"

$scanOut = & $exe --scan $script:work 2>&1 | Out-String
Assert 'm8-01-01-scan-repairable' ($scanOut -match 'repairable=1') "--scan 报告存在可修复分片"

$repairOut = & $exe --repair $part 2>&1 | Out-String
$recovered = Join-Path $script:work 'm8-01.mkv.recovered.mkv'
if (-not $ffbin) {
    Skip 'm8-01-01-loss-within-10s' 'ffprobe 不可用，时长无法取证'
    Skip 'm8-01-01-decodable' 'ffmpeg 不可用，解码无法取证'
} elseif (-not (Test-Path $recovered)) {
    Assert 'm8-01-01-repair-ok' $false ("repair 未产出成品；输出：{0}" -f $repairOut.Trim())
} else {
    Assert 'm8-01-01-repair-ok' ($repairOut -match 'repair=ok') "repair=ok，成品 $recovered"
    $probe = Get-Probe $recovered
    $duration = 0.0
    if ($probe -match 'duration=([\d\.]+)') { $duration = [double]$Matches[1] }
    $loss = $killAfter - $duration
    # 降级验证：需求原文是「录制 5 分钟后强杀」；这里用 12s 强杀代理，丢失上界判据不变。
    Assert 'm8-01-01-loss-within-10s' ($duration -gt 0 -and $loss -le 10.0) `
        ("killed_at={0}s recovered={1:N3}s loss={2:N3}s（判据 ≤10s；降级验证：12s 短录代理 5min 长录）" -f `
         $killAfter, $duration, $loss)
    $hasVideo = $probe -match 'codec_type=video'
    $hasAudio = $probe -match 'codec_type=audio'
    Assert 'm8-01-01-decodable' ((Test-Decodable $recovered) -and $hasVideo -and $hasAudio) `
        "ffmpeg 解码零错误，video=$hasVideo audio=$hasAudio"
}

# ================================================================ M6-10#1 安装包无驱动组件
Write-Host '--- M6-10#1 安装包无驱动组件（zip / 安装脚本 / 源码三方）---'
$zipPath = Join-Path $root 'dist\ScreenRecorder-0.1.0-win-x64.zip'
if (-not (Test-Path $zipPath)) {
    Skip 'm6-10-01' "发布包不存在：$zipPath（先跑 build_release.ps1）"
} else {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [System.IO.Compression.ZipFile]::OpenRead($zipPath)
    try { $entries = @($zip.Entries | ForEach-Object { $_.FullName }) } finally { $zip.Dispose() }
    $driverExt = @($entries | Where-Object { $_ -match '\.(sys|inf|cat|vxd|drv|dll)$' -or $_ -match '(^|/)(drivers|Drivers)/' })
    Assert 'm6-10-01-zip-no-driver' ($driverExt.Count -eq 0) `
        ("包内 {0} 条目，驱动类条目={1}；清单：{2}" -f $entries.Count, $driverExt.Count, ($entries -join ', '))

    $installText = [System.IO.File]::ReadAllText((Join-Path $root 'packaging\install.ps1'))
    $driverApi = @('pnputil', 'sc.exe', 'sc create', 'New-Service', 'NtLoadDriver', 'ZwLoadDriver',
                   'SetupCopyOEMInf', 'CurrentControlSet\Services', 'HKLM:\SYSTEM') |
        Where-Object { $installText -match [regex]::Escape($_) }
    Assert 'm6-10-01-install-userland-only' ($driverApi.Count -eq 0) `
        ("install.ps1 驱动/服务安装调用命中数={0}（期望 0）；命中：{1}" -f `
         $driverApi.Count, ($(if ($driverApi.Count) { $driverApi -join ',' } else { '无' })))

    $srcDriverFiles = @(Get-ChildItem (Join-Path $root 'src') -Recurse -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Extension -in @('.sys', '.inf', '.cat', '.vxd', '.drv') })
    $srcCode = @(Get-ChildItem (Join-Path $root 'src') -Recurse -File -Include '*.cpp', '*.h' -ErrorAction SilentlyContinue |
        Select-String -Pattern 'NtLoadDriver|ZwLoadDriver|SetupCopyOEMInf|IoRegisterDriver' -List)
    Assert 'm6-10-01-source-no-driver' ($srcDriverFiles.Count -eq 0 -and $srcCode.Count -eq 0) `
        ("src/ 驱动二进制={0}，驱动装载 API 命中文件={1}（期望均为 0）" -f $srcDriverFiles.Count, $srcCode.Count)
}

# ================================================================ M6-11#1 面板数值随录制状态更新
Write-Host '--- M6-11#1 面板数值随录制状态更新（1Hz 指标 + UI 状态栏两态）---'
$metricsFile = Join-Path $script:work 'm6-11.mkv'
$metricsLog = Join-Path $script:work 'm6-11.log'
$metricsOut = & $exe --record --seconds 5 --fps 30 --out $metricsFile --log-file $metricsLog 2>&1 | Out-String
$metricsLines = @((Read-Log $metricsLog) -split "`n" | Where-Object { $_ -match 'metrics .*capture_fps=' })
$metricsNumeric = @($metricsLines | Where-Object { $_ -match 'capture_fps=[1-9]' -and $_ -match 'encode_fps=[1-9]' })
Assert 'm6-11-01-metrics-1hz' ($metricsNumeric.Count -ge 2) `
    ("录制日志中 1Hz metrics 行={0}（含非零采集/编码 fps 的行={1}）；样例：{2}" -f `
     $metricsLines.Count, $metricsNumeric.Count, ($(if ($metricsLines.Count) { $metricsLines[0].Trim() } else { '无' })))

$ini = Join-Path $script:work 'm6-11.ini'
[System.IO.File]::WriteAllText($ini,
    "[hotkeys]`r`nstart=Ctrl+Alt+F9`r`npause=Ctrl+Alt+F10`r`nstop=Ctrl+Alt+F11`r`n",
    [System.Text.UTF8Encoding]::new($false))
$uiLog = Join-Path $script:work 'm6-11-ui.log'
$uiOut = Join-Path $script:work 'm6-11-ui.mkv'
$uiProc = Start-Process -FilePath $ui -PassThru -WindowStyle Hidden `
    -ArgumentList @('--hidden', '--out', $uiOut, '--config', $ini, '--log', $uiLog, '--exit-after-ms', '16000')
Register-SrProcess $uiProc
$ready = Wait-Log $uiLog 'app_ready' 8000
$idleText = Get-StatusText
$started = $false
if ($ready) {
    for ($i = 0; $i -lt 6 -and -not $started; $i++) {
        Press-Combo @(0x11, 0x12, 0x78)  # Ctrl+Alt+F9
        $started = Wait-Log $uiLog 'recording_indicator' 1200
        if ((Read-Log $uiLog) -match 'spawn pid=') { break }
    }
}
if (-not $started) {
    Skip 'm6-11-01-ui-status-refresh' "UI 录制未进入录制态（app_ready=$ready）；不拿文案冒充实测"
} else {
    Start-Sleep -Milliseconds 1200
    $recText1 = Get-StatusText
    Start-Sleep -Milliseconds 1200
    $recText2 = Get-StatusText
    $hasMetrics = ($recText1 -match '采集\s*[\d\.]+\s*/\s*编码\s*[\d\.]+\s*fps\s*·\s*丢帧\s*\d+')
    Assert 'm6-11-01-ui-status-refresh' ($hasMetrics -and ($recText1 -ne $idleText)) `
        ("就绪态不含指标、录制态含三项指标且数值随状态刷新：hasMetrics={0} idleChanged={1}；录制态首行：{2}" -f `
         $hasMetrics, ($recText1 -ne $idleText), (($recText1 -split "`n") | Select-Object -First 1))
    Assert 'm6-11-01-ui-timer-updates' ($recText1 -ne $recText2) `
        ("1.2s 间隔两次采样不同（计时/指标刷新）；t1={0} t2={1}" -f `
         (($recText1 -split "`n") | Select-Object -First 1), (($recText2 -split "`n") | Select-Object -First 1))
    Press-Combo @(0x11, 0x12, 0x7A)  # Ctrl+Alt+F11
}
try { if (-not $uiProc.HasExited) { [void]$uiProc.WaitForExit(20000) } } catch {}

# ================================================================ M3-15#1 定点丢帧策略
Write-Host '--- M3-15#1 定点丢帧：45fps 时成片时长准确、无加速、音轨在（注入代理）---'
$dropFile = Join-Path $script:work 'm3-15.mkv'
$dropOut = & $exe --record --seconds 6 --fps 60 --inject-drop-every 4 --out $dropFile 2>&1 | Out-String
$dropExit = [int](Field $dropOut 'record_exit')
$dropped = [double](Field $dropOut 'frames_dropped')
$encoded = [double](Field $dropOut 'frames_encoded')
$wall = [double](Field $dropOut 'wall_seconds')
Assert 'm3-15-01-drop-injected' ($dropExit -eq 0 -and $dropped -gt 0 -and $encoded -lt 360) `
    ("record_exit={0} frames_dropped={1} frames_encoded={2}（60fps×6s=360 帧，注入每 4 帧丢 1 → 实际约 45fps；降级验证：注入代理 GPU 压力）" -f `
     $dropExit, $dropped, $encoded)
if (-not $ffbin) {
    Skip 'm3-15-01-duration-accurate' 'ffprobe 不可用'
    Skip 'm3-15-01-no-speedup' 'ffprobe 不可用'
    Skip 'm3-15-01-av-sync' 'ffprobe/ffmpeg 不可用'
} else {
    $probe = Get-Probe $dropFile
    $duration = 0.0
    if ($probe -match 'duration=([\d\.]+)') { $duration = [double]$Matches[1] }
    $fps = ''
    if ($probe -match 'avg_frame_rate=([\d/]+)') { $fps = $Matches[1] }
    Assert 'm3-15-01-duration-accurate' ([Math]::Abs($duration - 6.0) -le 0.5) `
        ("成片 duration={0:N3}s vs 请求 6.000s（偏差 {1:N3}s ≤0.5s）；wall={2}s，无加速播放" -f `
         $duration, [Math]::Abs($duration - 6.0), $wall)
    Assert 'm3-15-01-no-speedup' ($fps -eq '60/1') `
        ("时间轴仍按配置 60fps 铺排（avg_frame_rate={0}），丢帧未压缩时间轴" -f $fps)
    Assert 'm3-15-01-av-sync' ((Test-Decodable $dropFile) -and ($probe -match 'codec_type=video') -and ($probe -match 'codec_type=audio')) `
        '视频轨 + 音轨俱在且 ffmpeg 全片解码零错误（音画同步代理判据，非逐帧 A/V 偏差实测）'
}

# ================================================================ M6-04#2 均失败落软编且录制成功
Write-Host '--- M6-04#2 硬编均不可用时落软编、录制成功（含全失败双分支）---'
$encList = & $exe --list-encoders 2>&1 | Out-String
# 轮 48（验收报告-轮47 §2 R1 / 轮48指令 §P0-1）：原断言把「不可用行 == 3」写死，
# 产品诚实新增第 4 行 `mf-h264-hw unavailable` 就红。改为**数量无关**的两条正向断言
# + 一条反向护栏；红线是不许改成「>=3」——那仍放得过「新加的项没写原因」。
# `encoders=[...]` 是汇总串（state=unavailable 挤在一行），不能拿它数条目，只看规范行。
$vendorIds = @('nvenc-h264', 'qsv-h264', 'amf-h264')
$unavailableRows = @($encList -split "`r?`n" | Where-Object {
    $_ -match '^encoder=id=[^\t]+\t' -and $_ -match 'available=0'
})
$unavailableIds = @($unavailableRows | ForEach-Object {
    if ($_ -match '^encoder=id=([^\t]+)\t') { $Matches[1] } else { '' }
})

# 反向用例共用的守卫：三厂商都在 + 每一行都有非空 reason。两个条件缺一不可。
function Test-UnavailableRows {
    param([string]$ListText)
    $rows = @($ListText -split "`r?`n" | Where-Object {
        $_ -match '^encoder=id=[^\t]+\t' -and $_ -match 'available=0'
    })
    # R48-GUARD-BEGIN
    if ($rows.Count -eq 0) { return $false }
    $ids = @($rows | ForEach-Object {
        if ($_ -match '^encoder=id=([^\t]+)\t') { $Matches[1] } else { '' }
    })
    foreach ($vendor in @('nvenc-h264', 'qsv-h264', 'amf-h264')) {
        if ($ids -notcontains $vendor) { return $false }
    }
    foreach ($row in $rows) {
        if ($row -notmatch 'reason=\S') { return $false }
    }
    return $true
    # R48-GUARD-END
}

$missingVendors = @($vendorIds | Where-Object { $unavailableIds -notcontains $_ })
Assert 'm6-04-02-vendor-ids-unavailable' ($unavailableRows.Count -gt 0 -and $missingVendors.Count -eq 0) `
    ("unavailable 行数={0}（数量无关），三厂商命中={1}/3，缺={2}；行={3}" -f `
     $unavailableRows.Count, (3 - $missingVendors.Count), ($missingVendors -join ','),
     ($unavailableIds -join ', '))

$rowsWithoutReason = @($unavailableRows | Where-Object { $_ -notmatch 'reason=\S' })
Assert 'm6-04-02-every-unavailable-has-reason' ($unavailableRows.Count -gt 0 -and
        $rowsWithoutReason.Count -eq 0 -and (Test-UnavailableRows $encList)) `
    ("unavailable 行={0}，缺 reason 的行={1}（期望 0）；守卫对真实列表=True" -f `
     $unavailableRows.Count, $rowsWithoutReason.Count)

# 反向：人为抹掉任意一行 reason → 守卫必须 FAIL（同时校验「确实抹掉了一行」，防止空跑）。
$blankReasonRx = [regex]'(?m)^(encoder=id=[^\t]+\t[^\r\n]*?available=0\t)reason=[^\r\n]*'
$tamperedList = $blankReasonRx.Replace($encList, '${1}reason=', 1)
$blanked = @($tamperedList -split "`r?`n" | Where-Object {
    $_ -match 'available=0' -and $_ -notmatch 'reason=\S'
}).Count
$guardOnTampered = Test-UnavailableRows $tamperedList
Assert 'm6-04-02-reverse-blank-reason-fails' ($blanked -eq 1 -and -not $guardOnTampered) `
    ("抹掉 reason 的行数={0}（期望 1），守卫对篡改列表=False（实际={1}）" -f `
     $blanked, (-not $guardOnTampered))

$fallbackFile = Join-Path $script:work 'm6-04.mkv'
$fallbackOut = & $exe --record --seconds 3 --fps 30 --out $fallbackFile 2>&1 | Out-String
$fbExit = [int](Field $fallbackOut 'record_exit')
$fbEncoder = Field $fallbackOut 'encoder'
$fbOk = ($fbExit -eq 0 -and $fbEncoder -eq 'mf-h264-sw' -and (Test-Path $fallbackFile))
if ($ffbin) { $fbOk = $fbOk -and (Test-Decodable $fallbackFile) }
Assert 'm6-04-02-fallback-recording-success' $fbOk `
    ("硬编全不可用 → encoder={0} record_exit={1} 成片可解码={2}（口径冲突：01 原文点名 x264，03 冻结选型为 MF 软编；本条按软编等价代理判定，裁定见 docs/15）" -f `
     $fbEncoder, $fbExit, $(if ($ffbin) { Test-Decodable $fallbackFile } else { '未测/缺 ffmpeg' }))

$allFailFile = Join-Path $script:work 'm6-04-allfail.mkv'
$allFailOut = & $exe --record --seconds 2 --out $allFailFile --fail-encoder 2>&1 | Out-String
$allFailExit = [int](Field $allFailOut 'record_exit')
Assert 'm6-04-02-allfail-dual-branch' ($allFailExit -eq 6 -and $allFailOut -match 'hardware' -and $allFailOut -match 'software') `
    ("连软编也失败时才报错：record_exit={0}（期望 6）且 hardware/software 双分支可见" -f $allFailExit)

Clear-SrProcesses -IncludeNameFallback
Write-Host ''
Write-Host ("RESULT: {0} PASS / {1} FAIL / {2} SKIP" -f $script:pass, $script:fail, $script:skip)
if ($script:fail -gt 0) { exit 1 }
exit 0
