# 轮 40 · 存储守护收口：M8-03#1 / M8-03#2 / M8-04#1 / M8-05#1
#
# 这四条此前是「无卡」（已实现但缺 TASK 追溯/缺断言）。本轮给每条补**一条行为断言**，
# 再把覆盖表转「已实现」。
#
#   M8-03#1  剩余 < 2GB 时非阻塞提示、录制继续
#   M8-03#2  剩余 < 500MB 升级为强提示
#   M8-04#1  写满磁盘时空间耗尽前自动停止、成品完整无 0 字节
#   M8-05#1  按码率与剩余空间估算可录时长，误差 ≤ 20%
#
# 阈值说明：磁盘提示线是可配置项（[output] disk_warn_mb / disk_critical_mb，
# 默认 2048 / 500）。验收用「把阈值调到极大/极小」来触发与不触发，
# **不真的把磁盘写满**（写满会破坏机器且不可重复）。
#
# 轮 41 R1（验收报告-轮40 §2）：M8-05 段旧实现在 $estOut 为空时把四个字段取 0，
# 用「没取到数据」算出 100% 误差（假 FAIL，3 次跑 1 次）。现在取数三档语义：
#   ① 开跑前清残留 + 等 ≤2s 确认无残留（单实例锁）；
#   ② 字段齐全 → 正常算误差判 ≤20%；
#   ③ 字段缺失 → 重试一次 → 仍缺 → SKIP「数据未取到」，不参与 PASS 统计，不用 0 算误差。
# -SimulateMissingFields 人为造字段缺失，专测第 ③ 档；-SelfTestEstimate 只跑 M8-05 段。
#
# 轮 42（验收报告-轮41 §2 裁定 A + 长窗口护栏）：
#   ★ 主判据口径改为**配置码率**：expected = (free − stop) / (configured_kbps×1000/8)，
#     ±20% 不变。依据 01 原文「依据当前码率与剩余空间估算」与 ER-05 的 8Mbps→2.7h 示例。
#   ★ 新增**独立**断言 m8-05-01-longwindow-bitrate-20pct：≥60s 真实录制，
#     用文件系统字节数 / 墙钟时长算平均写入速率，与配置码率比 ≤20%。
#     它不消费 estimated_record_seconds，不是产品公式的复读机（轮42指令 §2 头号风险）。
#   ★ 两条断言必须同时存在；护栏只在「数据未取到」时 SKIP，真实超标一律 FAIL
#     （验收方拒 C：SKIP 不得用来藏真超标）。
#
# Usage: pwsh -File verify/cases/g5_storage.ps1 [-BuildDir build-msvc] [-Config Release]
#        pwsh -File verify/cases/g5_storage.ps1 -SelfTestEstimate -SimulateMissingFields
#        pwsh -File verify/cases/g5_storage.ps1 -SelfTestEstimate -LongWindowPayload '...'

param(
    [string]$BuildDir = 'build-msvc',
    [string]$Config = 'Release',
    [switch]$SelfTestEstimate,
    [switch]$SimulateMissingFields,
    [string]$EstimatePayload = '',
    # 自测注入：合成长窗口实测值（\n 为换行占位），专测护栏断言，不启动 60s 录制。
    [string]$LongWindowPayload = 'long_file_bytes=46440000\nlong_wall_seconds=60.03\n'
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot '..\lib\Cleanup.ps1')
. (Join-Path $PSScriptRoot '..\lib\Forensics.ps1')
. (Join-Path $PSScriptRoot '..\lib\LongWindow.ps1')
trap {
    [void](Save-FailureContext -Tag 'g5_storage' -WorkDirs $script:SrForensicsWorkDirs -Note $_)
    Clear-SrProcesses -IncludeNameFallback
    Write-Host "FAIL: $_"
    exit 1
}

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$ui = Join-Path $root (Join-Path $BuildDir "bin\$Config\sr-ui.exe")
$rec = Join-Path $root (Join-Path $BuildDir "bin\$Config\sr-recorder.exe")
if (-not (Test-Path $ui)) { $ui = Join-Path $root (Join-Path $BuildDir 'bin\sr-ui.exe') }
if (-not (Test-Path $rec)) { $rec = Join-Path $root (Join-Path $BuildDir 'bin\sr-recorder.exe') }
if (-not (Test-Path $ui) -or -not (Test-Path $rec)) { Write-Host 'FAIL: binaries missing'; exit 2 }

$work = Join-Path $env:TEMP 'sr-storage'
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
function Skip { param([string]$Name, [string]$Reason) $script:skip++; Write-Host ("  SKIP {0} — {1}" -f $Name, $Reason) }
function Field {
    param([string]$Text, [string]$Name)
    if ($Text -match "(?m)^$Name=([^`r`n]*)") { return $Matches[1].Trim() }
    return ''
}
function Read-Log {
    param([string]$Path)
    if (-not (Test-Path $Path)) { return '' }
    try {
        $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open,
                                          [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
        $reader = New-Object System.IO.StreamReader($stream, [System.Text.Encoding]::UTF8)
        $text = $reader.ReadToEnd(); $reader.Dispose(); $stream.Dispose(); return $text
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

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class SrStorageNative {
    [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "FindWindowW")]
    public static extern IntPtr FindWindowClass(string lpClassName, IntPtr lpWindowName);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr hDlg, int nIDDlgItem);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "GetWindowTextW")]
    public static extern int GetWindowTextW(IntPtr hWnd, System.Text.StringBuilder lpString, int nMaxCount);
}
'@
function Get-MainWindow { return [SrStorageNative]::FindWindowClass('SrUiMainWindow', [IntPtr]::Zero) }
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
function Get-StatusText {
    param([IntPtr]$Main)
    if ($Main -eq [IntPtr]::Zero) { return '' }
    $sb = New-Object System.Text.StringBuilder 2048
    [void][SrStorageNative]::GetWindowTextW([SrStorageNative]::GetDlgItem($Main, 200), $sb, 2048)
    return $sb.ToString()
}

# 让 UI 用一组受控阈值启动，返回日志文本、状态栏文本。
function Start-UiWithThresholds {
    param([string]$Tag, [string]$WarnMb, [string]$CriticalMb)
    $ini = Join-Path $work "$Tag.ini"
    [System.IO.File]::WriteAllText($ini,
        "[output]`r`ndir=$work`r`ndisk_warn_mb=$WarnMb`r`ndisk_critical_mb=$CriticalMb`r`n",
        [System.Text.UTF8Encoding]::new($false))
    $log = Join-Path $work "$Tag.log"
    $proc = Start-Process -FilePath $ui -PassThru `
        -ArgumentList @('--exit-after-ms', '12000', '--config', $ini, '--log', $log)
    Register-SrProcess $proc
    $null = Wait-Log $log 'app_ready' 8000
    $main = Wait-MainWindow
    $text = Wait-Log $log 'disk_(warn|critical)=' 4000
    $status = Get-StatusText $main
    $null = $proc.WaitForExit(15000)
    return [pscustomobject]@{ log = $text; status = $status; main = $main }
}

# -SelfTestEstimate：只跑 M8-05 段（字段缺失分支用例用，跳过前 12 条断言）。
if (-not $SelfTestEstimate) {
# ---------------------------------------------------------------- M8-03#1 非阻塞提示，录制继续
Write-Host '--- M8-03#1 剩余不足时非阻塞提示、录制继续 ---'
$warnFile = Join-Path $work 'warn.mkv'
$warnOut = & $rec --record --seconds 4 --fps 30 --out $warnFile `
    --disk-warn-mb 999999999 --disk-stop-mb 1 2>&1 | Out-String
$warnFlag = [int](Field $warnOut 'disk_warn')
$warnStop = [int](Field $warnOut 'disk_stop')
$warnExit = [int](Field $warnOut 'record_exit')
$warnWall = [double](Field $warnOut 'wall_seconds')
Assert 'm8-03-01-warn-fired' ($warnFlag -eq 1) "disk_warn=$warnFlag（提示线触发）"
Assert 'm8-03-01-not-blocking' ($warnStop -eq 0 -and $warnExit -eq 0) `
    "disk_stop=$warnStop record_exit=$warnExit（提示不阻断，录制继续）"
Assert 'm8-03-01-recording-continued' (($warnWall -ge 3.5) -and (Test-Path $warnFile) -and
                                     ((Get-Item $warnFile).Length -gt 50KB)) `
    "wall=${warnWall}s size=$((Get-Item $warnFile -ErrorAction SilentlyContinue).Length)"

$uiWarn = Start-UiWithThresholds -Tag 'warn' -WarnMb '999999999' -CriticalMb '1'
Assert 'm8-03-01-ui-warn-logged' ($uiWarn.log -match 'disk_warn=1') 'UI 日志记录了非阻塞提示'
Assert 'm8-03-01-ui-warn-visible' ($uiWarn.status -match '磁盘剩余不足') `
    "状态栏首行：$(($uiWarn.status -split "`n") | Select-Object -First 1)"

# ---------------------------------------------------------------- M8-03#2 强提示（默认 500MB 线）
Write-Host '--- M8-03#2 剩余不足 500MB 升级为强提示 ---'
$uiCritical = Start-UiWithThresholds -Tag 'critical' -WarnMb '999999999' -CriticalMb '999999999'
Assert 'm8-03-02-critical-logged' ($uiCritical.log -match 'disk_critical=1') 'UI 日志记录了强提示'
Assert 'm8-03-02-critical-visible' (($uiCritical.status -match '磁盘剩余不足') -and
                                   ($uiCritical.status -match '录制将随时停止')) `
    "状态栏首行：$(($uiCritical.status -split "`n") | Select-Object -First 1)"
$uiNormal = Start-UiWithThresholds -Tag 'normal' -WarnMb '1' -CriticalMb '1'
Assert 'm8-03-02-no-false-critical' ($uiNormal.log -notmatch 'disk_critical=1') '空间充足时不误报强提示'
Assert 'm8-03-02-no-false-warn' ($uiNormal.log -notmatch 'disk_warn=1') '空间充足时不误报非阻塞提示'

# ---------------------------------------------------------------- M8-04#1 空间耗尽前自动停止
Write-Host '--- M8-04#1 空间耗尽前自动停止、成品完整 ---'
$drive = (Get-Item $work).PSDrive.Name
$free = (Get-PSDrive $drive).Free
$stopMb = [math]::Floor(($free - 2MB) / 1MB)
$stopFile = Join-Path $work 'stop.mkv'
$stopOut = & $rec --record --seconds 30 --fps 30 --out $stopFile --disk-stop-mb $stopMb 2>&1 | Out-String
$stopFlag = [int](Field $stopOut 'disk_stop')
$stopWall = [double](Field $stopOut 'wall_seconds')
$stopSize = if (Test-Path $stopFile) { (Get-Item $stopFile).Length } else { 0 }
Assert 'm8-04-01-stopped-early' ($stopFlag -eq 1 -and $stopWall -lt 25) `
    "disk_stop=$stopFlag wall=${stopWall}s（请求 30s）"
Assert 'm8-04-01-output-complete' (($stopSize -gt 100KB) -and (-not (Test-Path ($stopFile + '.part')))) `
    "size=$stopSize part_left=$(Test-Path ($stopFile + '.part'))"
$probeOut = & python (Join-Path $root 'verify\tools\mkv_probe.py') $stopFile 2>&1 | Out-String
Assert 'm8-04-01-container-valid' ($probeOut -match 'RESULT: 0 failure') `
    (($probeOut -split "`n" | Where-Object { $_ -match 'FAIL' }) -join '; ')
}

# ---------------------------------------------------------------- M8-05#1 可录时长估算误差 ≤ 20%
# 轮 41 R1：三档语义。取数失败不再用 0 参与误差计算（D-58 同族：拿缺失输入算结论）。
# 轮 42 裁定 A：主判据口径 = 配置码率；另加独立 60s 长窗口护栏。
Write-Host '--- M8-05#1 配置码率主判据 + 60s 长窗口护栏（双断言）---'

# 配置码率由**用例自己传给产品的 --bitrate 6000** 加上音频常量 192kbps 得到，
# 不读产品的任何估算字段，因此不是「产品公式的复读机」。
# 192 出处：src/core/util/DiskEstimate.h:8、src/recorder/RecordingSession.cpp:275。
$videoBitrateKbps = 6000
$configuredBps = Get-ConfiguredBytesPerSec -VideoBitrateKbps $videoBitrateKbps

# ① 开跑前清残留并等 ≤2s 确认无残留（单实例锁）。M8-04 的录制进程可能在
#    --record 返回后仍持有锁数十毫秒，本次启动会被以 exit=2/3 拒绝、输出为空。
Clear-SrProcesses -IncludeNameFallback
$lockDeadline = (Get-Date).AddSeconds(2)
$stale = 0
while ((Get-Date) -lt $lockDeadline) {
    $stale = @(Get-Process sr-recorder, sr-ui -ErrorAction SilentlyContinue).Count
    if ($stale -eq 0) { break }
    Start-Sleep -Milliseconds 100
}

# 取数：返回字段字典与缺失清单；字段值必须是非负十进制数，空串/非数值都算「缺失」。
function Read-EstimateFields {
    param([string]$Text)
    $spec = [ordered]@{
        est     = 'estimated_record_seconds'
        free    = 'disk_free_bytes'
        written = 'bytes_written'
        wall    = 'wall_seconds'
    }
    $values = @{}
    $missing = @()
    foreach ($key in $spec.Keys) {
        $raw = (Field $Text $spec[$key]).Trim()
        if ($raw -match '^\d+(\.\d+)?$') { $values[$key] = [double]$raw }
        else { $missing += $spec[$key] }
    }
    return [pscustomobject]@{ Values = $values; Missing = $missing; Exit = (Field $Text 'record_exit') }
}

function Invoke-EstimateRecord {
    param([string]$OutFile, [switch]$ForceMissing)
    if ($ForceMissing) {
        # 人为造一次「输出里没有字段」：等价于单实例锁拒绝 / 输出被截断，
        # 专测第三档语义，不启动录制进程（不污染单实例状态）。
        return ''
    }
    if ($EstimatePayload -ne '') {
        # 自测注入：用合成输出验证「字段齐全」两分支（达标 PASS / 真实超标 FAIL），
        # 证明三档语义没有把判据改软。\n 是换行占位，便于命令行传参。
        return ($EstimatePayload -replace '\\n', "`n")
    }
    return (& $rec --record --seconds 10 --fps 30 --out $OutFile --bitrate 6000 2>&1 | Out-String)
}

$estFile = Join-Path $work 'est.mkv'
$estOut = Invoke-EstimateRecord -OutFile $estFile -ForceMissing:$SimulateMissingFields
$snapshot = Read-EstimateFields $estOut
$attempts = 1
# ② 字段缺失 → 重试一次（快照/输出偶发截断）。重试前再清一次残留。
#    -SimulateMissingFields 下两次都强制缺失，用来完整演示「重试一次→仍缺→SKIP」。
if ($snapshot.Missing.Count -gt 0) {
    Clear-SrProcesses -IncludeNameFallback
    Start-Sleep -Milliseconds 300
    $estOut = Invoke-EstimateRecord -OutFile $estFile -ForceMissing:$SimulateMissingFields
    $snapshot = Read-EstimateFields $estOut
    $attempts = 2
}

$exitLabel = if ($snapshot.Exit -ne '') { $snapshot.Exit } else { 'n/a' }
$preface = "record_exit=$exitLabel attempts=$attempts stale_after_clear=$stale"

# ③ 仍缺失 → SKIP「数据未取到」，不参与 PASS 统计，不用 0 算误差。
if ($snapshot.Missing.Count -gt 0) {
    Skip 'm8-05-01-estimate-error-20pct' `
        ("数据未取到：{0} 缺失（{1}）→ 标 SKIP，不计入 PASS/FAIL，不用 0 算误差" -f `
         ($snapshot.Missing -join ', '), $preface)
} else {
    $v = $snapshot.Values
    $stopBytes = 128MB
    # 判据 A（验收报告-轮41 §2）：expected = (free − stop) / (configured_kbps×1000/8)。
    # 10s 短窗口实测速率不再当基准（run3 的 954KB/s 含首帧 IDR + 容器开销）。
    $expectedSeconds = ($v.free - $stopBytes) / $configuredBps
    $shortWindowBps = if ($v.wall -gt 0) { $v.written / $v.wall } else { 0 }
    if ($expectedSeconds -le 0) {
        Skip 'm8-05-01-estimate-error-20pct' `
            ("数据未取到：分母不可用（free={0:N0} B, configured_bps={1:N0}；{2}）→ 标 SKIP，不用 0 算误差" -f `
             $v.free, $configuredBps, $preface)
    } else {
        $errorRatio = [Math]::Abs($v.est - $expectedSeconds) / $expectedSeconds
        Assert 'm8-05-01-estimate-error-20pct' ($errorRatio -le 0.20) `
            ("配置码率口径：估算={0:N0}s 期望=(free−stop)/{1:N0}Bps={2:N0}s 误差={3:P1}（free={4:N0} B, configured_kbps={5}, 10s窗口实测={6:N0} B/s 仅记录不参与判定；{7}）" -f `
             $v.est, $configuredBps, $expectedSeconds, $errorRatio, $v.free, `
             ($videoBitrateKbps + 192), $shortWindowBps, $preface)
    }
}

# ---------------------------------------------------------------- 独立护栏：≥60s 长窗口平均写入速率
# 主判据几乎等于产品公式（只防读错 free / 漏减 stop）。这条才是真信息：
# 用**文件系统字节数 / 独立墙钟**算平均写入速率，与配置码率比 ≤20%。
# 数据缺失 / 窗口不足 60s 一律 FAIL，不 SKIP（验收方拒 C：SKIP 会藏真超标）。
if ($SimulateMissingFields) {
    Skip 'm8-05-longwindow-bitrate-20pct' '数据未取到（自测模拟取数失败，长窗口同源不可用）'
} else {
    if ($SelfTestEstimate) {
        $lwText = $LongWindowPayload -replace '\\n', "`n"
        $lwBytes = [double](Field $lwText 'long_file_bytes')
        $lwWall = [double](Field $lwText 'long_wall_seconds')
        $lwSource = 'self-test injected payload'
    } else {
        $longFile = Join-Path $work 'long.mkv'
        if (Test-Path $longFile) { Remove-Item $longFile -Force -ErrorAction SilentlyContinue }
        Clear-SrProcesses -IncludeNameFallback
        Start-Sleep -Milliseconds 300
        $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
        $longOut = & $rec --record --seconds 62 --fps 30 --out $longFile --bitrate $videoBitrateKbps 2>&1 | Out-String
        $stopwatch.Stop()
        $lwBytes = if (Test-Path $longFile) { [double](Get-Item $longFile).Length } else { 0 }
        $lwWall = $stopwatch.Elapsed.TotalSeconds
        $lwSource = ("filesystem size + Stopwatch; record_exit={0} product_wall={1}" -f `
                     (Field $longOut 'record_exit'), (Field $longOut 'wall_seconds'))
    }
    $lw = Get-LongWindowDeviation -FileBytes $lwBytes -WallSeconds $lwWall -ConfiguredBps $configuredBps
    Assert 'm8-05-longwindow-bitrate-20pct' ($lw.Valid -and $lw.Pass) `
        ("长窗口：wall={0:N2}s file_bytes={1:N0} 平均={2:N0} B/s vs 配置={3:N0} B/s 偏差={4:P2}（判据 ≥60s 且 ≤20%；{5}）{6}" -f `
         $lwWall, $lwBytes, $lw.AvgBps, $configuredBps, $lw.Deviation, $lwSource, `
         $(if ($lw.Reason) { ' INVALID: ' + $lw.Reason } else { '' }))
}

Clear-SrProcesses -IncludeNameFallback
Write-Host ''
Write-Host ("RESULT: {0} PASS / {1} FAIL / {2} SKIP" -f $script:pass, $script:fail, $script:skip)
if ($script:fail -gt 0) { exit 1 }
exit 0
