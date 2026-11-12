# 轮 33 · M3-03（帧率）与 M3-04（CBR / VBR）的引擎级核验。
# 轮 46 · M3-04#1 判据按 `docs/21 §2` 改为「全片平均 + 单秒峰值」，逐秒/滑窗方差不作判据。
#
# 判据：
#   M3-03#1  60fps 录制：encode_fps ≥ 58，帧数 ≈ 60×时长
#   M3-03#2  15fps 录制：encode_fps ≤ 15.5，帧数不超过配置值
#   M3-04#1  CBR 6000kbps（docs/21 §2 裁定，docs/01 变更记录 46 行）：
#            cbr-average-deviation    全片 bytes_written/wall_seconds 与标称偏差 ≤ 10%
#            cbr-single-second-cap    任意单秒 ≤ 2 × 标称（12000kbps）硬挡板
#   M3-04#2  VBR 6000/9000：avg 偏差 ≤ 20%，逐秒峰值 ≤ 上限（未改）
#   M3-04    「互不覆盖」：VBR 之后再跑 CBR，模式与 CBR 判据都不粘滞
#
# ===== CBR 口径（docs/21 §4；实现只在 verify/lib/CbrCriteria.ps1，两个用例共用）=====
# 1. 分桶：ffprobe `-select_streams v:0` 的 `packet.pts_time` 按 Floor 落整秒桶，
#    桶内累加 packet.size；跳过第 0 秒桶与最后一个桶；每桶 bytes*8/1000 → kbps。
# 2. 平均口径写死：**全片** `bytes_written / wall_seconds`（recorder stdout 字段），
#    与 `标称 kbps × 1000 / 8` 相比，**不允许**用「逐秒再平均」代替。
# 3. 单秒 2× 硬挡板：对全部有效桶逐个判，任一超标 FAIL；平均 PASS 也不许跳过。
# 4. 逐秒方差与滑窗方差受编码器实现限制，**不作判据**（docs/21 §2）。
# 5. 两条断言各自独立输出，互不短路。激励源（ActiveDesktop.ps1）默认参数，不调弱。
#
# VBR 的码率判据只在**能填满预算的运动画面**上有意义：静态桌面的 VBR 会显著低于
# 均值（实测 3,037kbps / 49%），这是 VBR 的正常行为，不是通过项。本用例启动
# ffplay 全屏 testsrc2 作为运动源，测试结束即关闭。
#
# Usage: pwsh -File verify/cases/g5_format.ps1 [-Seconds 15] [-VbrSeconds 20] [-CbrScene active]
# 反向护栏注入：-SimulateCbrRates '6000,...' [-SimulateCbrBytes N] [-SimulateCbrSecs S]
#   —— 不启动 recorder/ffplay，只跑上面两条 CBR 断言；判据阈值不因模拟开关改变。
# 同场景对照（F3）：-CbrOnly -CbrFile <mkv> -CbrStdout <recorder .out>
#   —— 不录制，直接用共用实现分析已有成片，输出 cbr-verdict.json 与两条断言。

param(
    [string]$BuildDir = 'build-msvc',
    [string]$Config = 'Release',
    [int]$Seconds = 15,
    [int]$VbrSeconds = 20,
    # 轮 43 F2：验收方指令 §1 P0 / §3 #2 要求的可重跑「取数失败」分支入口。
    # 只影响本用例的取数模拟，不改任何断言阈值与判据；不传时行为与轮 42 完全一致。
    [switch]$SimulateNoStart,
    [switch]$SimulateProductFailure,
    [switch]$SimulateMissingFields,
    # 反向护栏的伪造逐秒序列（csv，单位 kbps）；空 = 正常录制。
    [string]$SimulateCbrRates = '',
    # 轮 46：伪造 recorder 的全片取数（平均判据的唯一输入），仅模拟路径可读。
    [long]$SimulateCbrBytes = 0,
    [double]$SimulateCbrSecs = 40,
    # CBR 段激励场景：static（默认，安静桌面）/ active（ActiveDesktop.ps1 默认参数）。
    [ValidateSet('static', 'active')][string]$CbrScene = 'static',
    # F3 同场景对照：不录制，直接分析已有成片 + recorder stdout。
    [string]$CbrFile = '',
    [string]$CbrStdout = '',
    [switch]$CbrOnly
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot '..\lib\Cleanup.ps1')
. (Join-Path $PSScriptRoot '..\lib\Forensics.ps1')
# 轮 46 F3：CBR 分桶 / 判据 / 激励源的唯一实现，与 g5_cbr_probe.ps1 共用。
. (Join-Path $PSScriptRoot '..\lib\CbrCriteria.ps1')
trap {
    [void](Save-FailureContext -Tag 'g5_format' -WorkDirs $script:SrForensicsWorkDirs -Note $_)
    Clear-SrProcesses -IncludeNameFallback
    if ($script:MotionProcess) { Stop-Process -Id $script:MotionProcess.Id -Force -ErrorAction SilentlyContinue }
    Write-Host "FAIL: $_"
    exit 1
}

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$rec = Join-Path $root (Join-Path $BuildDir "bin\$Config\sr-recorder.exe")
if (-not (Test-Path $rec)) { $rec = Join-Path $root (Join-Path $BuildDir 'bin\sr-recorder.exe') }
if (-not (Test-Path $rec)) { Write-Host "FAIL: missing $rec"; exit 2 }
$ffbin = Split-Path -Parent (Get-Command ffprobe).Source
$ffprobe = Join-Path $ffbin 'ffprobe.exe'
$ffplay = Join-Path $ffbin 'ffplay.exe'

$work = Join-Path $root ('verify/artifacts/format-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
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

# 轮 43 F2：取数失败不进断言。输出 1 条 SKIP + stdout 尾 20 行后终止整个用例，
# SKIP 不计入 PASS/FAIL，退出码 0（与轮 41 R1 的三档取数语义一致）。
function Write-Summary {
    param([hashtable]$Extra = @{})
    $data = [ordered]@{
        pass          = $script:pass
        fail          = $script:fail
        skip          = $script:skip
        work          = $work
    }
    foreach ($k in $Extra.Keys) { $data[$k] = $Extra[$k] }
    [System.IO.File]::WriteAllText((Join-Path $work 'summary.json'),
        (($data | ConvertTo-Json -Depth 4)), [System.Text.UTF8Encoding]::new($false))
}
function Invoke-DataSkip {
    param([string]$Tag, [object]$Segment)
    $script:skip++
    Write-Host ("  SKIP 数据未取到 — {0}：{1}（attempts={2}，已重试一次仍缺，不进断言）" -f `
        $Tag, $Segment.Reason, $Segment.Attempts)
    Write-Host '  stdout 尾 20 行：'
    foreach ($line in @($Segment.Tail | Select-Object -Last 20)) { Write-Host ("    " + $line) }
    Write-Summary -Extra @{ data_not_acquired = $Tag; reason = $Segment.Reason; attempts = $Segment.Attempts }
    Write-Host ''
    Write-Host ("RESULT: {0} PASS / {1} FAIL / {2} SKIP ; 产物 {3}" -f $script:pass, $script:fail, $script:skip, $work)
    exit 0
}
function Confirm-Segment {
    param([string]$Tag, [object]$Segment)
    if ($Segment.ProductFailed) {
        # 数据齐但产品明确报错 → 真实失败，绝不伪装成 SKIP。
        Assert "$Tag-record-exit" $false ("record_exit={0}（产品明确失败，不判 SKIP）" -f $Segment.RecordExit)
        Write-Summary -Extra @{ product_failed = $Tag; record_exit = $Segment.RecordExit }
        Write-Host ''
        Write-Host ("RESULT: {0} PASS / {1} FAIL / {2} SKIP ; 产物 {3}" -f $script:pass, $script:fail, $script:skip, $work)
        exit 1
    }
    if (-not $Segment.Acquired) { Invoke-DataSkip -Tag $Tag -Segment $Segment }
}
function Field {
    param([string]$Text, [string]$Key)
    if ($Text -match "(?m)^$Key=(.*)$") { return $Matches[1].Trim() }
    return ''
}

# 轮 43 F2：取数前置。区分「数据未取到」（SKIP）与「产品明确失败」（FAIL），
# 关键字段缺失时重试一次；判据阈值与此函数内部的逐秒口径都未改动。
function Test-SegmentData {
    param([string]$Stdout, [string]$Path)
    if ([string]::IsNullOrWhiteSpace($Stdout)) {
        return [pscustomobject]@{ Acquired = $false; ProductFailed = $false; RecordExit = 'n/a'
            Reason = 'stdout 为空（recorder 未启动或输出未落盘）' }
    }
    $re = Field $Stdout 'record_exit'
    if (-not $re) {
        return [pscustomobject]@{ Acquired = $false; ProductFailed = $false; RecordExit = 'n/a'
            Reason = 'record_exit 字段缺失' }
    }
    if ($re -ne '0') {
        return [pscustomobject]@{ Acquired = $true; ProductFailed = $true; RecordExit = $re
            Reason = "产品明确失败 record_exit=$re" }
    }
    foreach ($k in @('frames_encoded', 'wall_seconds', 'bytes_written')) {
        if (-not (Field $Stdout $k)) {
            return [pscustomobject]@{ Acquired = $false; ProductFailed = $false; RecordExit = $re
                Reason = "$k 字段缺失" }
        }
    }
    if (-not (Test-Path $Path)) {
        return [pscustomobject]@{ Acquired = $false; ProductFailed = $false; RecordExit = $re
            Reason = '成片不存在' }
    }
    if ((Get-Item $Path).Length -lt 50KB) {
        return [pscustomobject]@{ Acquired = $false; ProductFailed = $false; RecordExit = $re
            Reason = ('成片过小 {0} B（疑似仅 .part 头）' -f (Get-Item $Path).Length) }
    }
    return [pscustomobject]@{ Acquired = $true; ProductFailed = $false; RecordExit = $re
        Reason = '' }
}

function New-SegmentResult {
    param([object]$Check, [string]$Stdout, [int]$Attempts)
    $tail = @(($Stdout -split "`n") | Where-Object { $_.Trim() } | Select-Object -Last 20)
    if ($tail.Count -eq 0) { $tail = @('(stdout 为空，无内容可展示)') }
    return [pscustomobject]@{
        Acquired = $Check.Acquired; ProductFailed = $Check.ProductFailed
        RecordExit = $Check.RecordExit; Reason = $Check.Reason
        Stdout = $Stdout; Tail = $tail; Attempts = $Attempts
    }
}

function Record-Segment {
    param(
        [string]$Path,
        [string]$Tag,
        [int]$Fps,
        [int]$BitrateKbps,
        [int]$Secs,
        [string]$RateMode = 'cbr',
        [int]$MaxBitrateKbps = 0,
        [int]$Quality = 0
    )
    $stdoutPath = Join-Path $work "$Tag.out"
    $args = @('--record', '--seconds', "$Secs", '--fps', "$Fps", '--bitrate', "$BitrateKbps",
              '--rate-mode', $RateMode, '--out', "`"$Path`"", '--log-level', 'warn')
    if ($MaxBitrateKbps -gt 0) { $args += @('--max-bitrate', "$MaxBitrateKbps") }
    if ($Quality -gt 0) { $args += @('--quality', "$Quality") }

    $attempts = 0
    for ($attempt = 1; $attempt -le 2; $attempt++) {
        $attempts = $attempt
        if ($SimulateNoStart) {
            $stdout = ''
        } elseif ($SimulateProductFailure) {
            $stdout = "record_exit=6`nencoder=mf-h264-sw`nframes_encoded=0`nwall_seconds=0.00`nbytes_written=0`nerror=all encoders unavailable"
        } elseif ($SimulateMissingFields) {
            $stdout = if ($attempt -eq 1) {
                "frames_encoded=900`nwall_seconds=15.0`nbytes_written=100"
            } else { '' }
        } else {
            $proc = Start-Process -FilePath $rec -PassThru -WindowStyle Hidden `
                -RedirectStandardOutput $stdoutPath -ArgumentList $args
            Register-SrProcess $proc
            [void]$proc.WaitForExit(($Secs + 180) * 1000)
            Unregister-SrProcess $proc
            $stdout = Read-TextSafe $stdoutPath
        }
        $check = Test-SegmentData -Stdout $stdout -Path $Path
        if ($check.Acquired -or $check.ProductFailed) {
            return (New-SegmentResult -Check $check -Stdout $stdout -Attempts $attempts)
        }
        # 产品没明确报错、只是取数失败 → 重试一次；仍失败就由 Confirm-Segment 判 SKIP。
        if ($attempt -eq 1) { Start-Sleep -Milliseconds 300 }
    }
    return (New-SegmentResult -Check $check -Stdout $stdout -Attempts $attempts)
}

# 逐秒派生量（VBR 段使用）：分桶由共用库 Get-CbrSecondBuckets 完成，
# 这里只保留 VBR 原有的 deviation / peak_ok / spread，不改变任何 VBR 判据。
function Get-PerSecondStats {
    param([string]$Path, [double]$NominalKbps, [double]$CapKbps = 0)
    $bucketed = Get-CbrSecondBuckets -Ffprobe $ffprobe -Path $Path
    $rates = @($bucketed.rates)
    $result = [pscustomobject]@{
        count = $rates.Count
        avg = 0.0; max = 0.0; min = 0.0; spread = 1.0
        deviation = 1.0; peak_ok = $false; rates = $rates
    }
    if ($rates.Count -gt 0) {
        $result.avg = ($rates | Measure-Object -Average).Average
        $result.max = ($rates | Measure-Object -Maximum).Maximum
        $result.min = ($rates | Measure-Object -Minimum).Minimum
        if ($result.avg -gt 0) {
            $result.spread = ($result.max - $result.min) / $result.avg
            $result.deviation = [Math]::Abs($result.avg - $NominalKbps) / $NominalKbps
        }
        # 峰值不超上限：留 2% 的容器/取整余量（VBR 原判据，未改）。
        $result.peak_ok = ($CapKbps -le 0) -or ($result.max -le $CapKbps * 1.02)
    }
    return $result
}

# 轮 46 新判据的两条独立断言。正常录制、模拟注入、-CbrOnly 对照共用同一份实现
# （Get-CbrVerdict 在 verify/lib/CbrCriteria.ps1），保证护栏验的就是真实判据。
function Assert-CbrCriteria {
    param([object]$Stats, [long]$BytesWritten, [double]$WallSeconds, [double]$NominalKbps = 6000)
    $v = Get-CbrVerdict -Rates @($Stats.rates) -BytesWritten $BytesWritten `
        -WallSeconds $WallSeconds -NominalKbps $NominalKbps
    $avgDetail = ("全片平均 {0:N0}kbps vs 标称 {1:N0}kbps，偏差 {2:P1}（判据 ≤10%）；bytes_written={3} / wall_seconds={4}，非逐秒再平均（逐秒均值 {5:N0}kbps 仅作对照）" -f `
        $v.whole_avg_kbps, $v.nominal_kbps, $v.avg_deviation, $v.bytes_written,
        $v.wall_seconds, $v.rates_avg_kbps)
    Assert 'cbr-average-deviation' ($v.avg_ok) $avgDetail
    $capDetail = ("单秒峰值 {0:N0}kbps vs 硬上限 {1:N0}kbps（2× 标称 {2:N0}；有效桶 {3} 个，平均 PASS 也不稀释）" -f `
        $v.max_kbps, $v.cap_kbps, $NominalKbps, $v.buckets)
    Assert 'cbr-single-second-cap' ($v.cap_ok) $capDetail
    return $v
}

function Start-MotionSource {
    # 不能用 -WindowStyle Hidden：ffplay 的 -fs 窗口就是被测画面，隐藏后 VBR 只能看到静态桌面。
    $script:MotionProcess = Start-Process -FilePath $ffplay -PassThru `
        -ArgumentList @('-fs', '-f', 'lavfi', '-i', 'testsrc2=size=1280x720:rate=30', '-an')
    Start-Sleep -Seconds 3
}

function Stop-MotionSource {
    if ($script:MotionProcess) {
        Stop-Process -Id $script:MotionProcess.Id -Force -ErrorAction SilentlyContinue
        $script:MotionProcess = $null
        Start-Sleep -Milliseconds 500
    }
}

# 模拟 / 同场景对照分支不启动 recorder，也不清理进程（避免误杀同机正在录制的其它用例）。
if (-not ($SimulateNoStart -or $SimulateProductFailure -or $SimulateMissingFields -or
          $SimulateCbrRates -or $CbrOnly)) {
    Get-Process sr-ui, sr-recorder -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
}

# F3 同场景对照（轮 46）：不录制，直接分析 g5_cbr_probe 产出的 active 成片与 recorder stdout。
# 两个工具走同一份分桶 / 判据实现，输出 cbr-verdict.json 供逐字段比对（差 ≤2%）。
if ($CbrOnly) {
    if (-not (Test-Path $CbrFile)) { Write-Host "FAIL: missing CbrFile $CbrFile"; exit 2 }
    if (-not (Test-Path $CbrStdout)) { Write-Host "FAIL: missing CbrStdout $CbrStdout"; exit 2 }
    $refOut = Read-TextSafe $CbrStdout
    $refBytes = [long](Field $refOut 'bytes_written')
    $refWall = [double](Field $refOut 'wall_seconds')
    if ($refBytes -le 0 -or $refWall -le 0) {
        Write-Host 'FAIL: CbrStdout 缺 bytes_written / wall_seconds（平均判据的唯一输入）'; exit 2
    }
    $refStats = Get-PerSecondStats -Path $CbrFile -NominalKbps 6000
    $refVerdict = Assert-CbrCriteria -Stats $refStats -BytesWritten $refBytes `
        -WallSeconds $refWall -NominalKbps 6000
    $verdictPath = Join-Path $work 'cbr-verdict.json'
    [System.IO.File]::WriteAllText($verdictPath,
        ($refVerdict | ConvertTo-Json -Depth 4), [System.Text.UTF8Encoding]::new($false))
    Write-Host ("CBR_VERDICT " + $verdictPath)
    Write-Host ''
    Write-Host ("RESULT: {0} PASS / {1} FAIL / {2} SKIP ; 产物 {3}" -f $script:pass, $script:fail, $script:skip, $work)
    if ($script:fail -gt 0) { exit 1 }
    exit 0
}

# 轮 46 反向护栏注入：不录、不启 ffplay，直接用伪造逐秒序列 + 伪造全片取数
# 跑真实的两条新断言。判据阈值（0.10 / 2×）与正常路径共用 Assert-CbrCriteria，
# 模拟开关不能改尺子。
if ($SimulateCbrRates) {
    $simRates = @($SimulateCbrRates -split ',' | ForEach-Object {
        $v = 0.0
        if ([double]::TryParse($_.Trim(), [ref]$v)) { $v } else { 0.0 }
    })
    $simStats = [pscustomobject]@{
        count = $simRates.Count
        avg = if ($simRates.Count) { ($simRates | Measure-Object -Average).Average } else { 0.0 }
        max = if ($simRates.Count) { ($simRates | Measure-Object -Maximum).Maximum } else { 0.0 }
        min = if ($simRates.Count) { ($simRates | Measure-Object -Minimum).Minimum } else { 0.0 }
        spread = 1.0; deviation = 1.0; peak_ok = $false; rates = $simRates
        simulated = $true
    }
    # 未显式注入全片取数时，按标称 6000kbps × SimulateCbrSecs 造一个「平均恰好达标」的基线。
    $simBytes = if ($SimulateCbrBytes -gt 0) { $SimulateCbrBytes } `
        else { [long](6000 * 1000 / 8 * $SimulateCbrSecs) }
    Write-Host ("=== 轮 46 反向护栏注入：{0} 个有效秒桶，逐秒 {1:N0}~{2:N0}kbps；bytes_written={3} / wall={4}s ===" -f `
        $simStats.count, $simStats.min, $simStats.max, $simBytes, $SimulateCbrSecs)
    $null = Assert-CbrCriteria -Stats $simStats -BytesWritten $simBytes `
        -WallSeconds $SimulateCbrSecs -NominalKbps 6000
    Write-Summary -Extra @{ simulated_cbr_rates = $SimulateCbrRates
        simulated_bytes_written = $simBytes; simulated_wall_seconds = $SimulateCbrSecs }
    Write-Host ''
    Write-Host ("RESULT: {0} PASS / {1} FAIL / {2} SKIP ; 产物 {3}" -f $script:pass, $script:fail, $script:skip, $work)
    if ($script:fail -gt 0) { exit 1 }
    exit 0
}

# ---- M3-03#1 60fps ----
Write-Host ("=== M3-03#1 60fps × {0}s ===" -f $Seconds)
$file60 = Join-Path $work 'fps60.mkv'
$seg60 = Record-Segment -Path $file60 -Tag 'fps60' -Fps 60 -BitrateKbps 12000 -Secs $Seconds
Confirm-Segment -Tag 'fps60' -Segment $seg60
$out60 = $seg60.Stdout
$enc60 = [double](Field $out60 'encode_fps')
$frames60 = [long](Field $out60 'frames_encoded')
$wall60 = [double](Field $out60 'wall_seconds')
Assert 'm3-03-60fps-rate' ($enc60 -ge 58.0) ("encode_fps={0} frames={1} wall={2}" -f $enc60, $frames60, $wall60)
Assert 'm3-03-60fps-frames' ($frames60 -ge [int]($Seconds * 58)) ("frames={0} (期望 ≥ {1})" -f $frames60, [int]($Seconds * 58))

# ---- M3-03#2 15fps ----
Write-Host ("=== M3-03#2 15fps × {0}s ===" -f $Seconds)
$file15 = Join-Path $work 'fps15.mkv'
$seg15 = Record-Segment -Path $file15 -Tag 'fps15' -Fps 15 -BitrateKbps 4000 -Secs $Seconds
Confirm-Segment -Tag 'fps15' -Segment $seg15
$out15 = $seg15.Stdout
$enc15 = [double](Field $out15 'encode_fps')
$frames15 = [long](Field $out15 'frames_encoded')
$wall15 = [double](Field $out15 'wall_seconds')
Assert 'm3-03-15fps-cap' ($enc15 -le 15.5) ("encode_fps={0} frames={1}" -f $enc15, $frames15)
Assert 'm3-03-15fps-frames' ($frames15 -le [int]([Math]::Ceiling(15 * $wall15) + 2)) `
    ("frames={0} (上限 {1})" -f $frames15, [int]([Math]::Ceiling(15 * $wall15) + 2))

# ---- M3-04#1 CBR 新判据（轮 46 / docs/21 §2）：全片平均 ≤10% + 单秒 ≤2× 标称 ----
Write-Host ("=== M3-04#1 CBR 6000kbps × 40s：全片平均 + 单秒 2× 硬挡板（场景 {0}）===" -f $CbrScene)
$fileCbr = Join-Path $work 'cbr.mkv'
$cbrActivity = $null
try {
    if ($CbrScene -eq 'active') {
        # 共用激励源：ActiveDesktop.ps1 默认参数，不调弱（轮 46 指令 §注意事项 #2）。
        $cbrActivity = Start-CbrActiveDesktop -Root $root
    }
    $segCbr = Record-Segment -Path $fileCbr -Tag 'cbr' -Fps 30 -BitrateKbps 6000 -Secs 40
} finally {
    if ($cbrActivity) { Stop-CbrActiveDesktop -Process $cbrActivity }
}
Confirm-Segment -Tag 'cbr' -Segment $segCbr
$outCbr = $segCbr.Stdout
# M3-04 修复：码控必须真的落到 ICodecAPI（rate_control=cbr），而不是只设 MF_MT_AVG_BITRATE。
$rateMode = Field $outCbr 'rate_control'
$fillerBytes = [long](Field $outCbr 'cbr_filler_bytes')
Assert 'm3-04-cbr-mode' ($rateMode -eq 'cbr') ("rate_control=$rateMode cbr_filler_bytes=$fillerBytes")
# 全片平均的唯一输入：recorder stdout 的 bytes_written / wall_seconds。
$bytesCbr = [long](Field $outCbr 'bytes_written')
$wallCbr = [double](Field $outCbr 'wall_seconds')
$cbrStats = Get-PerSecondStats -Path $fileCbr -NominalKbps 6000
# 两条断言各自独立输出；单秒 2× 是硬挡板，不因平均 PASS 而跳过。
$cbrVerdict = Assert-CbrCriteria -Stats $cbrStats -BytesWritten $bytesCbr `
    -WallSeconds $wallCbr -NominalKbps 6000
Assert 'cbr-average-source-wholefile' ($cbrVerdict.source -eq 'bytes_written/wall_seconds' -and
        $bytesCbr -gt 0 -and $wallCbr -gt 0) `
    ("全片平均取自 bytes_written={0} / wall_seconds={1}；逐秒均值 {2:N0}kbps 不参与判据" -f `
     $bytesCbr, $wallCbr, $cbrStats.avg)

# ---- M3-04#2 VBR：峰值约束，均值 ±20% ----
Write-Host ("=== M3-04#2 VBR 6000/9000kbps × {0}s（全屏运动源）===" -f $VbrSeconds)
Start-MotionSource
try {
    $fileVbr = Join-Path $work 'vbr.mkv'
    $segVbr = Record-Segment -Path $fileVbr -Tag 'vbr' -Fps 30 -BitrateKbps 6000 -Secs $VbrSeconds `
        -RateMode 'vbr' -MaxBitrateKbps 9000 -Quality 80
} finally {
    Stop-MotionSource
}
Confirm-Segment -Tag 'vbr' -Segment $segVbr
$outVbr = $segVbr.Stdout
$vbrMode = Field $outVbr 'rate_control'
$vbrQuality = [int](Field $outVbr 'rate_control_quality')
$vbrMax = [int](Field $outVbr 'rate_control_max_kbps')
$vbrFiller = [long](Field $outVbr 'cbr_filler_bytes')
Assert 'm3-04-vbr-mode' ($vbrMode -eq 'vbr' -and $vbrMax -eq 9000) `
    ("rate_control=$vbrMode max_kbps=$vbrMax quality=$vbrQuality")
Assert 'm3-04-vbr-no-filler' ($vbrFiller -eq 0) ("cbr_filler_bytes=$vbrFiller（VBR 不补齐）")
$vbrStats = Get-PerSecondStats -Path $fileVbr -NominalKbps 6000 -CapKbps 9000
# 裁定 ①（docs/05 §2.4）：只有指定运动源判均值偏差。
Assert 'm3-04-vbr-motion-nominal' ($vbrStats.count -gt 0 -and $vbrStats.deviation -le 0.20) `
    ("① 运动源：逐秒 {0:N0}~{1:N0}kbps avg={2:N0} 偏差={3:P1}（判据 ≤20%）" -f $vbrStats.min, $vbrStats.max, $vbrStats.avg, $vbrStats.deviation)

# 裁定 ③：静态桌面只记录实测值，不作偏差判定。
Write-Host '=== M3-04#2 VBR 静态桌面 × 15s（只记录，不判偏差）==='
$fileVbrStatic = Join-Path $work 'vbr-static.mkv'
$segVbrStatic = Record-Segment -Path $fileVbrStatic -Tag 'vbr_static' -Fps 30 -BitrateKbps 6000 -Secs 15 `
    -RateMode 'vbr' -MaxBitrateKbps 9000 -Quality 80
Confirm-Segment -Tag 'vbr_static' -Segment $segVbrStatic
$outVbrStatic = $segVbrStatic.Stdout
$vbrStaticMode = Field $outVbrStatic 'rate_control'
$vbrStaticFiller = [long](Field $outVbrStatic 'cbr_filler_bytes')
$vbrStaticStats = Get-PerSecondStats -Path $fileVbrStatic -NominalKbps 6000 -CapKbps 9000
Assert 'm3-04-vbr-static-recorded' ((Test-Path $fileVbrStatic) -and $vbrStaticMode -eq 'vbr' -and $vbrStaticFiller -eq 0) `
    ("③ 静态桌面：逐秒 {0:N0}~{1:N0}kbps avg={2:N0}（偏差 {3:P1}，只记录不判）" -f `
        $vbrStaticStats.min, $vbrStaticStats.max, $vbrStaticStats.avg, $vbrStaticStats.deviation)

# 裁定 ②：任意内容（运动源 + 静态桌面）单秒峰值都不得超上限。
Assert 'm3-04-vbr-peak-any-content' ($vbrStats.peak_ok -and $vbrStaticStats.peak_ok) `
    ("② 运动源峰值 {0:N0} / 静态峰值 {1:N0} vs 上限 9000kbps（判据 ≤102%）" -f $vbrStats.max, $vbrStaticStats.max)

# ---- M3-04 互不覆盖：VBR 之后 CBR 模式不粘滞，判据与正式 CBR 段同一套 ----
# 轮 46：正式 CBR 段改用「全片平均 ≤10% + 单秒 ≤2×」，本段必须同步，
# 否则同一需求在一次用例里出现两种判据。这里直接复用 Get-CbrVerdict，不再单独输出两条断言。
Write-Host '=== M3-04 互不覆盖：VBR 后复跑 CBR 12s（全片平均 + 单秒 2× 挡板）==='
$fileCbr2 = Join-Path $work 'cbr_after_vbr.mkv'
$segCbr2 = Record-Segment -Path $fileCbr2 -Tag 'cbr_after_vbr' -Fps 30 -BitrateKbps 6000 -Secs 12
Confirm-Segment -Tag 'cbr_after_vbr' -Segment $segCbr2
$outCbr2 = $segCbr2.Stdout
$cbr2Mode = Field $outCbr2 'rate_control'
$cbr2Filler = [long](Field $outCbr2 'cbr_filler_bytes')
$cbr2Stats = Get-PerSecondStats -Path $fileCbr2 -NominalKbps 6000
$cbr2Bytes = [long](Field $outCbr2 'bytes_written')
$cbr2Wall = [double](Field $outCbr2 'wall_seconds')
$cbr2Verdict = Get-CbrVerdict -Rates @($cbr2Stats.rates) -BytesWritten $cbr2Bytes `
    -WallSeconds $cbr2Wall -NominalKbps 6000
Assert 'm3-04-mode-isolation' ($cbr2Mode -eq 'cbr' -and $cbr2Filler -gt 0 -and
        $cbr2Verdict.verdict_ok) `
    ("第二次 CBR：mode={0} filler={1} 全片平均={2:N0}kbps 偏差={3:P1} 单秒峰值={4:N0} vs {5:N0}（2× 硬挡板）" -f `
     $cbr2Mode, $cbr2Filler, $cbr2Verdict.whole_avg_kbps, $cbr2Verdict.avg_deviation,
     $cbr2Verdict.max_kbps, $cbr2Verdict.cap_kbps)
Assert 'm3-04-no-overwrite' ((Test-Path $fileCbr) -and (Test-Path $fileVbr) -and
    (Test-Path $fileVbrStatic) -and (Test-Path $fileCbr2) -and
    ((Get-Item $fileVbr).Length -ne (Get-Item $fileCbr).Length)) `
    'CBR / VBR 运动 / VBR 静态 / CBR 四份文件并存且大小不同'

$summary = [pscustomobject]@{
    encode_fps_60 = $enc60
    frames_60     = $frames60
    encode_fps_15 = $enc15
    frames_15     = $frames15
    cbr_rates_avg_kbps = $cbrStats.avg
    cbr_whole_avg_kbps = $cbrVerdict.whole_avg_kbps
    cbr_avg_deviation  = $cbrVerdict.avg_deviation
    cbr_avg_ok         = $cbrVerdict.avg_ok
    cbr_max_kbps       = $cbrVerdict.max_kbps
    cbr_cap_ok         = $cbrVerdict.cap_ok
    cbr_scene          = $CbrScene
    cbr_rate_mode = $rateMode
    cbr_filler_bytes = $fillerBytes
    cbr_file_bytes = if (Test-Path $fileCbr) { (Get-Item $fileCbr).Length } else { 0 }
    cbr_bytes_written = $bytesCbr
    cbr_wall_seconds  = $wallCbr
    vbr_avg_kbps  = $vbrStats.avg
    vbr_min_kbps  = $vbrStats.min
    vbr_max_kbps  = $vbrStats.max
    vbr_deviation = $vbrStats.deviation
    vbr_rate_mode = $vbrMode
    vbr_max_cfg   = $vbrMax
    vbr_filler_bytes = $vbrFiller
    vbr_static_avg_kbps = $vbrStaticStats.avg
    vbr_static_min_kbps = $vbrStaticStats.min
    vbr_static_max_kbps = $vbrStaticStats.max
    vbr_static_mode = $vbrStaticMode
    cbr2_whole_avg_kbps = $cbr2Verdict.whole_avg_kbps
    cbr2_avg_deviation  = $cbr2Verdict.avg_deviation
    cbr2_mode     = $cbr2Mode
    pass          = $script:pass
    fail          = $script:fail
    skip          = $script:skip
    work          = $work
}
$summary | ConvertTo-Json -Depth 4 | Set-Content -Path (Join-Path $work 'summary.json') -Encoding UTF8
Write-Host ''
Write-Host ("RESULT: {0} PASS / {1} FAIL / {2} SKIP ; 产物 {3}" -f $script:pass, $script:fail, $script:skip, $work)
if ($script:fail -gt 0) { exit 1 }
exit 0
