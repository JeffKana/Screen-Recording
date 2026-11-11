# LT-01 真·单文件 8 小时（轮 33 P0-1）——不挂 UI、不分段、不拼接。
#
# 口径（写进用例，禁止目测）：
#   内存：每 60s 外部采样 WorkingSet64；基线取第 9~11 分钟样本均值；
#         峰值取全程序列最大值，并与 recorder 自报 mem_peak_mb 交叉核对；
#         「无持续爬升」= 8 个整点小时均值不是逐段上升，且末段 - 首段 ≤ 50MB。
#   句柄：同样 60s 采样；峰值相对 10 分钟基线 ≤ +10%。
#   漂移：每 30min 取该窗口最后 2s 内的末视频包 PTS 与末音频包 PTS，差值的
#         绝对值即该采样点漂移；16 个点取最差值，判据 ≤ 100ms。
#   丢帧：recorder 的 frames_dropped / frames_captured < 0.1%。
#
# 前置：磁盘 ≥ 35GB、关闭睡眠、不跑其它录制/构建（CPU 争用会污染丢帧与内存基线）。
# 长跑用独立镜像 `sr-recorder-lt01.exe`：并发用例按名字清理 `sr-recorder` 时不会误杀
# （D-60，曾在 7h22m 杀死一次 8h）。启动后写 `verify/artifacts/.lt01-running.json`；
# 看到该文件就不要开跑任何用例。
#
# Usage: pwsh -File verify/cases/g5_lt01_real.ps1 [-Seconds 28800] [-SampleSeconds 60]
#        [-MinFreeGB 35] [-SkipFullDecode] [-DriftIntervalSeconds 1800]

param(
    [int]$Seconds = 28800,
    [int]$SampleSeconds = 60,
    [int]$MinFreeGB = 35,
    [string]$BuildDir = 'build-msvc',
    [string]$Config = 'Release',
    [int]$DriftIntervalSeconds = 1800,
    [string]$DecodeOnlyPath = '',
    [switch]$SkipFullDecode
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot '..\lib\Cleanup.ps1')
. (Join-Path $PSScriptRoot '..\lib\Forensics.ps1')
trap {
    [void](Save-FailureContext -Tag 'g5_lt01_real' -WorkDirs $script:SrForensicsWorkDirs -Note $_)
    if ($script:LockFile -and (Test-Path $script:LockFile)) {
        Remove-Item $script:LockFile -Force -ErrorAction SilentlyContinue
    }
    Clear-SrProcesses -IncludeNameFallback
    Write-Host "FAIL: $_"
    exit 1
}

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$rec = Join-Path $root (Join-Path $BuildDir "bin\$Config\sr-recorder.exe")
if (-not (Test-Path $rec)) { $rec = Join-Path $root (Join-Path $BuildDir 'bin\sr-recorder.exe') }
if (-not (Test-Path $rec)) { Write-Host "FAIL: missing $rec"; exit 2 }
$ffbin = Split-Path -Parent (Get-Command ffprobe).Source
$ffprobe = Join-Path $ffbin 'ffprobe.exe'
$ffmpeg = Join-Path $ffbin 'ffmpeg.exe'

$work = Join-Path $root ('verify/artifacts/lt01-real-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Force -Path $work | Out-Null
$script:SrForensicsWorkDirs = @($work)
$output = Join-Path $work 'lt01-8h.mkv'
$logFile = Join-Path $work 'lt01-8h.log'
$stdoutFile = Join-Path $work 'lt01-8h.out'
$samplesFile = Join-Path $work 'samples.jsonl'

$script:pass = 0
$script:fail = 0
function Assert {
    param([string]$Name, [bool]$Ok, [string]$Detail)
    if ($Ok) {
        $script:pass++
        Write-Host ("  PASS {0} — {1}" -f $Name, $Detail)
    } else {
        $script:fail++
        Write-Host ("  FAIL {0} — {1}" -f $Name, $Detail)
    }
}
function Field {
    param([string]$Text, [string]$Key)
    if ($Text -match "(?m)^$Key=(.*)$") { return $Matches[1].Trim() }
    return ''
}

# ---- 前置检查 ----
$drive = [System.IO.DriveInfo]::new((Split-Path -Qualifier $work))
$freeGb = $drive.AvailableFreeSpace / 1GB
Write-Host ("前置：{0} 可用 {1:N1}GB（判据 ≥{2}GB）" -f $drive.Name, $freeGb, $MinFreeGB)
if ($freeGb -lt $MinFreeGB) { Write-Host 'BLOCKED: 磁盘不足'; exit 4 }
$existing = Get-Process sr-ui, sr-recorder -ErrorAction SilentlyContinue
if ($existing) {
    Write-Host ("BLOCKED: 已有进程 {0}（8h 必须独占）" -f (($existing | ForEach-Object { $_.Name }) -join ','))
    exit 4
}

# 复判模式（轮 36 R2）：只对**既有产物**跑「解码三件套 + 漂移」判据，不再录一遍。
# 用途：8h 产物要用新判据复判（验收方轮 35 指令 P0），复判不该再烧 8 小时。
$decodeOnly = -not [string]::IsNullOrWhiteSpace($DecodeOnlyPath)
if ($decodeOnly) {
    $output = (Resolve-Path $DecodeOnlyPath).Path
    Write-Host ("复判既有产物：{0}" -f $output)
    $samples = @()
    $spanS = 0.0
    $text = ''
    $logText = ''
    $recordExit = ''
    $aborted = $false
    $exitCode = 0
    $probePath = $output
    $began = Get-Date
    # 期望帧数从同目录的录制 stdout 里取（用例把 stdout 落在成片旁边）；
    # 取不到就只判 DTS 单调，不判帧数（并在 detail 里写明）。
    $siblingOut = [System.IO.Path]::ChangeExtension($output, '.out')
    if (Test-Path $siblingOut) {
        $text = Read-TextSafe $siblingOut
        $siblingLog = [System.IO.Path]::ChangeExtension($output, '.log')
        if (Test-Path $siblingLog) { $logText = Read-TextSafe $siblingLog }
    }
    $longExe = $output
} else {
# D-60（轮 34）：并发验收用例普遍以 `Get-Process sr-recorder | Stop-Process -Force` 开场，
# 曾把跑到 7h22m 的录制直接杀掉。长跑改用独立镜像名，按名字清理的用例不会再误伤。
$longExe = Join-Path $work 'sr-recorder-lt01.exe'
Copy-Item $rec $longExe -Force

$began = Get-Date
$proc = Start-Process -FilePath $longExe -PassThru -WindowStyle Hidden -RedirectStandardOutput $stdoutFile `
    -ArgumentList @('--record', '--seconds', "$Seconds", '--fps', '30', '--bitrate', '6000',
                    '--rate-mode', 'cbr', '--out', "`"$output`"", '--log-level', 'info',
                    '--log-file', "`"$logFile`"")
Register-SrProcess $proc
$script:LockFile = Join-Path $root 'verify/artifacts/.lt01-running.json'
$lock = [pscustomobject]@{
    pid = $proc.Id; started = $began.ToString('o'); seconds = $Seconds
    out = $output; exe = $longExe; image = 'sr-recorder-lt01'
}
[System.IO.File]::WriteAllText($script:LockFile, ($lock | ConvertTo-Json -Depth 3),
    [System.Text.UTF8Encoding]::new($false))
Write-Host ("启动：pid={0} seconds={1} out={2} image=sr-recorder-lt01" -f $proc.Id, $Seconds, $output)

# ---- 1 分钟粒度采样 ----
$samples = @()
$nextSample = 0
$timeoutMs = ($Seconds + 900) * 1000
while (-not $proc.HasExited -and ((Get-Date) - $began).TotalMilliseconds -lt $timeoutMs) {
    $elapsed = [int]((Get-Date) - $began).TotalMilliseconds
    if ($elapsed -ge $nextSample) {
        try {
            $p = Get-Process -Id $proc.Id -ErrorAction Stop
            $sample = [pscustomobject]@{
                t_ms     = $elapsed
                ws_mb    = [Math]::Round($p.WorkingSet64 / 1MB, 1)
                # 轮 34 硬要求②：50MB 判据以 PeakWorkingSet64 为准，基线取开录后 10 分钟。
                peak_ws_mb = [Math]::Round($p.PeakWorkingSet64 / 1MB, 1)
                private_mb = [Math]::Round($p.PrivateMemorySize64 / 1MB, 1)
                handles  = [int]$p.HandleCount
                cpu_s    = [Math]::Round($p.TotalProcessorTime.TotalSeconds, 1)
            }
            $samples += $sample
            ($sample | ConvertTo-Json -Compress) | Add-Content -Path $samplesFile -Encoding UTF8
        } catch {
            # 进程刚退出时 Get-Process 会失败，不算采样失败。
        }
        $nextSample += $SampleSeconds * 1000
    }
    Start-Sleep -Milliseconds 2000
}
$finished = $proc.WaitForExit(120000)
$exitCode = if ($finished) { $proc.ExitCode } else { -1 }
if (-not $finished) { Stop-SrProcess $proc }
Unregister-SrProcess $proc
$text = Read-TextSafe $stdoutFile
$logText = Read-TextSafe $logFile
$recordExit = Field $text 'record_exit'
# 被外部终止时 stdout 不会 flush：record_exit 缺失、.mkv 尚未由 .part 改名。
# 轮 34 的 D-59：旧版在这种情况下仍去 probe 不存在的 .mkv，产出 duration=0/captured=0 的误导性 summary。
$aborted = [string]::IsNullOrWhiteSpace($recordExit)
$probePath = if (Test-Path $output) { $output } elseif (Test-Path "$output.part") { "$output.part" } else { '' }
$spanS = if ($samples.Count -gt 0) { [Math]::Round($samples[-1].t_ms / 1000.0, 1) } else { 0.0 }
}

# ---- 2 成片与元数据 ----
$duration = 0.0
if ($probePath) {
    $out = & $ffprobe -v error -show_entries format=duration -of default=nw=1:nk=1 $probePath 2>&1 | Out-String
    [void][double]::TryParse($out.Trim(), [ref]$duration)
}
$captured = [long](Field $text 'frames_captured')
if ($captured -eq 0 -and $logText -match '(?s).*captured=(\d+)') { $captured = [long]$Matches[1] }
$dropped = [long](Field $text 'frames_dropped')
$memPeak = [double](Field $text 'mem_peak_mb')
$handlesEnd = [long](Field $text 'handles')
$mediaMs = [long](Field $text 'media_elapsed_ms')
if ($mediaMs -eq 0 -and $spanS -gt 0) { $mediaMs = [long]($spanS * 1000) }

if (-not $decodeOnly) {
Assert 'lt01-exit-zero' ((-not $aborted) -and $exitCode -eq 0) `
    ("record_exit={0} aborted={1} span={2:N0}s" -f $(if ($recordExit) { $recordExit } else { 'missing' }), $aborted, $spanS)
Assert 'lt01-duration' ((-not $aborted) -and $duration -ge ($Seconds - 10)) `
    ("成片 {0:N3}s（判据 ≥{1}s）" -f $duration, ($Seconds - 10))
Assert 'lt01-media-elapsed' ((-not $aborted) -and $mediaMs -ge (($Seconds - 10) * 1000)) `
    ("media_elapsed_ms={0} span={1:N0}s" -f $mediaMs, $spanS)
}

# 全解：8h/约 21GB，按 100x 实时约 5 分钟；SkipFullDecode 只用于调试。
#
# 判据三件套（轮 36 R2；验收方轮 35 裁定，见 .codebuddy/plans/验收报告-轮35.md §2）：
#   ① `ffmpeg -v error -i F -f null -vsync 0 -`：退出码 0，stderr 无解码错误；
#   ② `-f framecrc`：视频帧数 = frames_encoded，每条流的 DTS 严格递增（无重复、无倒序）；
#   ③ **默认 vsync 下的 `[null @ ...] non monotonically increasing dts to muxer` 不计**。
#      那是 null muxer 的 CFR 帧率转换产物（把平均 33.35ms 的帧流按 1/30 取整，边界处重复 DTS），
#      不是文件缺陷：轮 35 实测同一窗口 `-vsync 0` 解码 0 条警告、默认 vsync 6,667 条，
#      且包级 DTS dup=0 / backwards=0。这里用 `-vsync 0` 即为该判据的反证形式。
$decodePass = $true
$decodeDetail = 'SKIP'
$framecrcPass = $true
$framecrcDetail = 'SKIP'
if (-not $SkipFullDecode -and -not $aborted -and (Test-Path $output)) {
    # ffmpeg 9 移除了 -vsync（等价选项是 -fps_mode）；两者都探测一次再决定用哪个。
    $modeArgs = @()
    $modeProbe = & $ffmpeg -hide_banner -v error -f null -fps_mode passthrough - 2>&1 | Out-String
    if ($modeProbe -notmatch "Unrecognized option 'fps_mode'") {
        $modeArgs = @('-fps_mode', 'passthrough')   # ≡ 老版 -vsync 0
    } else {
        $modeArgs = @('-vsync', '0')
    }
    Write-Host ("全片解码中（ffmpeg -v error，null muxer，{0}）…" -f ($modeArgs -join ' '))
    $decode = & $ffmpeg -hide_banner -v error -i $output -f null @modeArgs - 2>&1 | Out-String
    $decodeExit = $LASTEXITCODE
    $decodeLines = @(($decode -split "`r?`n") | Where-Object { $_.Trim() })
    # 裁定③：null muxer 的「non monotonically increasing dts to muxer」是落盘端
    # （CFR 帧率转换）的行为，不是文件缺陷——轮 35 复现：默认 vsync 6,667 条，
    # 同窗口包级 DTS dup=0/backwards=0、framecrc DTS 严格递增。这类行剔除后，
    # 剩下的任何 stderr 行都算解码错误。h264 的「co located POCs unavailable」是
    # 解码器提示（不是错误），同样剔除并单独计数。
    $benignPattern = '(?i)non monotonically increasing dts to muxer|co located POCs unavailable'
    $benignCount = @($decodeLines | Where-Object { $_ -match $benignPattern }).Count
    $decodeErrors = @($decodeLines | Where-Object { $_ -notmatch $benignPattern })
    $decodePass = ($decodeExit -eq 0) -and ($decodeErrors.Count -eq 0)
    $decodeDetail = ("mode={0} exit={1} stderr_lines={2} benign={3} error_lines={4}" -f
        ($modeArgs -join ' '), $decodeExit, $decodeLines.Count, $benignCount, $decodeErrors.Count)

    # ② framecrc：逐帧 DTS 严格递增 + 帧数比对（frames_encoded）。
    Write-Host '全片 framecrc 校验（帧数 + DTS 严格递增）…'
    $crcPath = Join-Path $work 'framecrc.txt'
    & $ffmpeg -hide_banner -v error -i $output -f framecrc $crcPath 2>&1 | Out-Null
    $crcExit = $LASTEXITCODE
    $videoFrames = 0
    $audioFrames = 0
    $dtsViolations = 0
    $lastDts = @{}
    foreach ($line in [System.IO.File]::ReadLines($crcPath)) {
        if ($line.Length -eq 0 -or $line[0] -eq '#') {
            continue
        }
        $parts = $line.Split(',')
        if ($parts.Length -lt 3) {
            continue
        }
        $stream = $parts[0].Trim()
        $dts = 0L
        if (-not [long]::TryParse($parts[1].Trim(), [ref]$dts)) {
            continue
        }
        if ($stream -eq '0') {
            ++$videoFrames
        } else {
            ++$audioFrames
        }
        if ($lastDts.ContainsKey($stream)) {
            if ($dts -le $lastDts[$stream]) {
                ++$dtsViolations
            }
        }
        $lastDts[$stream] = $dts
    }
    $framesEncoded = [long](Field $text 'frames_encoded')
    $framecrcPass = ($crcExit -eq 0) -and ($dtsViolations -eq 0) -and
                    (($framesEncoded -le 0) -or ($videoFrames -eq $framesEncoded))
    $framecrcDetail = ("video_frames={0} frames_encoded={1} audio_frames={2} dts_violations={3}" -f
        $videoFrames, $framesEncoded, $audioFrames, $dtsViolations)
    # framecrc 原始文本很大（8h ≈ 1.1M 行 / >100MB），只留摘要。
    Remove-Item $crcPath -Force -ErrorAction SilentlyContinue
} else {
    Write-Host ("SKIP 全片解码（SkipFullDecode={0}, aborted={1}, file={2}）" -f $SkipFullDecode, $aborted, $probePath)
}
Assert 'lt01-full-decode' ($decodePass -and $framecrcPass) `
    ("{0}；{1}" -f $decodeDetail, $framecrcDetail)

if (-not $decodeOnly) {
# ---- 3 内存 / 句柄 ----
# 轮 34 硬要求②：PeakWorkingSet64 作为 50MB 判据；基线取开录后 9~11min（不是启动瞬间）。
$baselineSamples = @($samples | Where-Object { $_.t_ms -ge 9 * 60 * 1000 -and $_.t_ms -le 11 * 60 * 1000 })
$baseline = if ($baselineSamples.Count -gt 0) {
    ($baselineSamples | Measure-Object peak_ws_mb -Average).Average
} elseif ($samples.Count -gt 0) {
    $samples[[Math]::Min($samples.Count - 1, 10)].peak_ws_mb
} else { 0.0 }
$peak = if ($samples.Count -gt 0) { ($samples | Measure-Object peak_ws_mb -Maximum).Maximum } else { 0.0 }
$wsPeak = if ($samples.Count -gt 0) { ($samples | Measure-Object ws_mb -Maximum).Maximum } else { 0.0 }
Assert 'lt01-memory-delta' ($baseline -gt 0 -and ($peak - $baseline) -le 50) `
    ("10min PeakWS 基线 {0:N1}MB，全程序列峰值 {1:N1}MB，Δ{2:N1}MB（判据 ≤50MB；WS 峰值 {3:N1}MB）" -f `
        $baseline, $peak, ($peak - $baseline), $wsPeak)
Write-Host ("交叉核对：recorder mem_peak_mb={0:N1}，采样 PeakWS={1:N1}，采样 WS={2:N1}" -f $memPeak, $peak, $wsPeak)

$hourly = @()
# 分 8 段看趋势：8h 录制时每段正好 1 小时；短录制（60s/10min 验证）按实际跨度等分，
# 否则短跑的 hourly 段数不足 8，「无持续爬升」这一项无从判定。
$bucketS = [Math]::Max(1.0, ($spanS / 8.0))
for ($h = 1; $h -le 8; $h++) {
    $slice = @($samples | Where-Object { $_.t_ms -ge ($h - 1) * $bucketS * 1000 -and $_.t_ms -lt $h * $bucketS * 1000 })
    if ($slice.Count -gt 0) {
        $hourly += [Math]::Round(($slice | Measure-Object ws_mb -Average).Average, 1)
    }
}
$monotonic = $hourly.Count -ge 8
# 「无持续爬升」只比较暖机之后的桶：起录瞬间的内存爬升会把第一个桶的均值拉低，
# 拿它跟后面的桶比，短录制（60s/10min 验证）会误判成「单调上升」。
$trendBuckets = @()
if ($hourly.Count -gt 1) {
    $trendBuckets = @($hourly[1..($hourly.Count - 1)])
    $monotonic = $trendBuckets.Count -ge 2
    for ($i = 1; $i -lt $trendBuckets.Count; $i++) {
        if ($trendBuckets[$i] -le $trendBuckets[$i - 1]) { $monotonic = $false }
    }
}
$trendOk = ($hourly.Count -ge 8) -and (-not $monotonic) -and
           ($trendBuckets.Count -ge 2) -and (($trendBuckets[-1] - $trendBuckets[0]) -le 50)
Assert 'lt01-memory-no-trend' $trendOk `
    ("分段均值 [{0}] MB（比较用 {1}）；单调上升={2}" -f ($hourly -join ', '), ($trendBuckets -join ', '), $monotonic)

$handleBaselineSamples = @($samples | Where-Object { $_.t_ms -ge 9 * 60 * 1000 -and $_.t_ms -le 11 * 60 * 1000 })
$handleBaseline = if ($handleBaselineSamples.Count -gt 0) {
    ($handleBaselineSamples | Measure-Object handles -Average).Average
} elseif ($samples.Count -gt 0) { $samples[[Math]::Min($samples.Count - 1, 10)].handles } else { 0 }
$handlePeak = if ($samples.Count -gt 0) { ($samples | Measure-Object handles -Maximum).Maximum } else { 0 }
$handleGrowth = if ($handleBaseline -gt 0) { ($handlePeak - $handleBaseline) / $handleBaseline } else { 1.0 }
Assert 'lt01-handles' ($handleBaseline -gt 0 -and $handleGrowth -le 0.10) `
    ("基线 {0:N0}，峰值 {1:N0}，增幅 {2:P1}（判据 ≤10%）" -f $handleBaseline, $handlePeak, $handleGrowth)

# ---- 4 丢帧 ----
$dropRate = if ($captured -gt 0) { $dropped / $captured } else { 1.0 }
Assert 'lt01-dropped' ($captured -gt 0 -and $dropRate -lt 0.001) `
    ("dropped={0} / captured={1} = {2:P3}（判据 <0.1%）" -f $dropped, $captured, $dropRate)
}

# ---- 5 音画漂移：16 × 30min，末包 PTS 差 ----
# 坑（轮 33 实测）：ffprobe 的 `-read_intervals <start>%+<dur>` 把 dur 从**实际读到的
# 第一个包**算起；视频 seek 落在关键帧（可能早于 start 1~2s），音频没有关键帧，
# 于是两条流读到不同末端，末包差会假报 ~2s（在 1h 样片上实测 -1990ms，真值 0~14ms）。
# 这里统一读 [At-10, At+10]，再取 pts ≤ At+0.5 的最后一个包，保证同一截止时刻比较。
Write-Host '漂移采样（每 30min，截止 At+0.5s 的末包）…'
function Get-LastPts {
    param([string]$Path, [string]$Stream, [double]$At, [double]$Horizon)
    $start = [Math]::Max(0.0, $At - 10.0)
    $raw = & $ffprobe -v error -select_streams $Stream -read_intervals ("{0}%+20" -f $start) `
        -show_entries packet=pts_time -of csv=p=0 $Path 2>&1 | Out-String
    $values = @()
    foreach ($line in ($raw -split "`r?`n")) {
        $value = 0.0
        $token = ($line -split ',')[0].Trim()
        if ([double]::TryParse($token, [ref]$value) -and $value -le $Horizon) { $values += $value }
    }
    if ($values.Count -eq 0) { return -1.0 }
    return ($values | Measure-Object -Maximum).Maximum
}
$drifts = @()
$maxPoints = [int][Math]::Ceiling(($duration + 0.5) / [double]$DriftIntervalSeconds)
for ($i = 1; $i -le $maxPoints; $i++) {
    $at = $i * $DriftIntervalSeconds
    # D-61：文件 duration 通常略小于 28800（28,799.971），用 -ge 会把第 16 点 break 掉，
    # 只剩 15 点；改为超过 duration+0.5s 才 break，保证 16 点（R3：结尾点必须进明细数组）。
    if ($at -gt ($duration + 0.5) -or [string]::IsNullOrWhiteSpace($probePath)) { break }
    $horizon = $at + 0.5
    $v = Get-LastPts -Path $probePath -Stream 'v:0' -At $at -Horizon $horizon
    $a = Get-LastPts -Path $probePath -Stream 'a:0' -At $at -Horizon $horizon
    if ($v -lt 0 -or $a -lt 0) { continue }
    $driftMs = [Math]::Abs($v - $a) * 1000
    $drifts += [pscustomobject]@{ at_s = $at; video_pts = $v; audio_pts = $a; drift_ms = [Math]::Round($driftMs, 1) }
}
$worst = if ($drifts.Count -gt 0) { ($drifts | Measure-Object drift_ms -Maximum).Maximum } else { 99999 }
Assert 'lt01-drift' ($drifts.Count -ge 15 -and $worst -le 100) `
    ("{0} 个采样点，最差 {1:N1}ms（判据 ≤100ms）" -f $drifts.Count, $worst)

# R1（轮 36）：尾点偏差单独判定——「≤100ms 且 ≤ 内部点最大偏差×3」。
# 只看最差值会靠余量蒙混过关（轮 35 尾点 99ms 只差 1ms，重跑完全可能 120ms）。
# 内部点 = 除结尾点外的采样点；结尾点 = at_s 最大的那个点（文件末尾那一段）。
$internalMax = 0.0
$endDrift = -1.0
$endAt = 0
if ($drifts.Count -gt 0) {
    $lastPoint = $drifts[-1]
    $endDrift = $lastPoint.drift_ms
    $endAt = $lastPoint.at_s
    $internal = @($drifts | Where-Object { $_.at_s -lt $lastPoint.at_s })
    if ($internal.Count -gt 0) {
        $internalMax = ($internal | Measure-Object drift_ms -Maximum).Maximum
    }
}
Assert 'lt01-tail-drift' ($endDrift -ge 0 -and $endDrift -le 100 -and $endDrift -le (3 * $internalMax)) `
    ("尾点(at={0}s) 偏差 {1:N1}ms；内部 {2} 点最大 {3:N1}ms（判据 ≤100ms 且 ≤3×内部最大={4:N1}ms）" -f
        $endAt, $endDrift, ($drifts.Count - 1), $internalMax, (3 * $internalMax))

$summary = [pscustomobject]@{
    seconds        = $Seconds
    aborted        = $aborted
    record_exit    = $recordExit
    span_s         = $spanS
    probe_path     = $probePath
    recorder_sha256 = if ($longExe -and (Test-Path $longExe)) {
        (Get-FileHash $longExe -Algorithm SHA256).Hash
    } else { '' }
    image          = 'sr-recorder-lt01'
    duration_s     = $duration
    exit           = $exitCode
    mem_baseline_mb = [Math]::Round($baseline, 1)
    mem_peak_mb    = [Math]::Round($peak, 1)
    ws_peak_mb     = [Math]::Round($wsPeak, 1)
    mem_hourly_mb  = $hourly
    handles_baseline = [Math]::Round($handleBaseline, 0)
    handles_peak   = $handlePeak
    dropped        = $dropped
    captured       = $captured
    frames_encoded = [long](Field $text 'frames_encoded')
    audio_last_pts_ms = [long](Field $text 'audio_last_pts_ms')
    video_last_pts_ms = [long](Field $text 'video_last_pts_ms')
    audio_tail_fill_frames = [long](Field $text 'audio_tail_fill_frames')
    audio_tail_silence_frames = [long](Field $text 'audio_tail_silence_frames')
    decode_detail = $decodeDetail
    framecrc_detail = $framecrcDetail
    drift_interval_s = $DriftIntervalSeconds
    drift_points   = $drifts
    worst_drift_ms = $worst
    internal_max_drift_ms = [Math]::Round($internalMax, 1)
    end_drift_ms   = [Math]::Round($endDrift, 1)
    end_drift_at_s = $endAt
    pass           = $script:pass
    fail           = $script:fail
    work           = $work
}
$summary | ConvertTo-Json -Depth 6 | Set-Content -Path (Join-Path $work 'summary.json') -Encoding UTF8
if ($script:LockFile -and (Test-Path $script:LockFile)) { Remove-Item $script:LockFile -Force -ErrorAction SilentlyContinue }

Write-Host ''
Write-Host ("LT01-REAL: {0} PASS / {1} FAIL ; 产物 {2}" -f $script:pass, $script:fail, $work)
if ($script:fail -gt 0) { exit 1 }
exit 0
