# 轮 43 · F1 定性：CBR 逐秒码率过冲（只读测量，不改产品、不改用例如判据）
# 轮 45 · 静态与活动两组固化进同一套脚本。
# 轮 46 · 判据按 `docs/21 §2` 改为「全片平均 ≤10% + 单秒 ≤2×」；逐秒/滑窗方差不作判据。
#         与 g5_format.ps1 **共用** `verify/lib/CbrCriteria.ps1` 的激励源、分桶与判据实现（F3）；
#         active 场景调用 `ActiveDesktop.ps1` 的**默认参数**（-Seconds/-Windowed/-BurstSec 都不传）。
#
# 场景：
#   static = 安静桌面（不做任何 UI 动作）
#   active = 桌面有活动（ActiveDesktop.ps1 默认参数：满屏滚动小字 + 突发整屏重排 + 光标移动）
#   motion = 全屏 testsrc2 运动源（轮 43 的历史场景，保留以便纵向对比）
#
# 每次运行输出：
#   - recorder stdout 汇总 + 全量 metrics 行（capture_fps / encode_fps / dropped）
#   - 逐秒表 persec.csv：second, bytes, kbps, packets, keyframes, max_packet
#   - 关键帧秒字节分解 nal-*.json：keyframe_bytes / filler_bytes / other_bytes
#   - 逐次 summary.json：全片平均/偏差、单秒峰值/2× 挡板、平均超还是单秒超（分开记）
#   - 汇总 probe-summary.txt：每次两条判据的实测值与 VERDICT
#
# Usage:
#   pwsh -File verify/cases/g5_cbr_probe.ps1 -Seconds 40 -Runs 5 -Mode both
#   pwsh -File verify/cases/g5_cbr_probe.ps1 -Seconds 40 -Runs 3 -Mode static,active -OutDir <dir>
#   pwsh -File verify/cases/g5_cbr_probe.ps1 -Seconds 40 -Runs 5 -Mode static -OutDir <dir>

param(
    [string]$BuildDir = 'build-msvc',
    [string]$Config = 'Release',
    [int]$Seconds = 40,
    [int]$Runs = 5,
    # 逗号分隔，可组合：static / active / motion / both(=static,motion) / all(=三者)
    [string]$Mode = 'both',
    [string]$OutDir = ''
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\lib\Cleanup.ps1')
# 轮 46 F3：与 g5_format.ps1 共用分桶 / 判据 / 激励源的调用封装。
. (Join-Path $PSScriptRoot '..\lib\CbrCriteria.ps1')
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$rec = Join-Path $root (Join-Path $BuildDir "bin\$Config\sr-recorder.exe")
if (-not (Test-Path $rec)) { $rec = Join-Path $root (Join-Path $BuildDir 'bin\sr-recorder.exe') }
if (-not (Test-Path $rec)) { Write-Host "FAIL: missing $rec"; exit 2 }
$ffbin = Split-Path -Parent (Get-Command ffprobe).Source
$ffprobe = Join-Path $ffbin 'ffprobe.exe'
$ffplay = Join-Path $ffbin 'ffplay.exe'
$nalTool = Join-Path $root 'verify/tools/cbr_nal_probe.py'

if (-not $OutDir) {
    $OutDir = Join-Path $root ('tasks/G6/V1-007/artifacts/cbr-probe-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

# 场景解析：both 保持轮 43 语义（static+motion），all 三场景，其余按逗号列表。
$modes = switch ($Mode.Trim().ToLowerInvariant()) {
    'both' { @('static', 'motion') }
    'all' { @('static', 'active', 'motion') }
    default {
        $list = @($Mode -split ',' | ForEach-Object { $_.Trim().ToLowerInvariant() } | Where-Object { $_ })
        $bad = @($list | Where-Object { $_ -notin @('static', 'active', 'motion') })
        if ($bad.Count -gt 0) { throw "未知场景：$($bad -join ',')（可选 static/active/motion）" }
        $list
    }
}
$activeDesktopScript = Join-Path $root 'verify/lib/ActiveDesktop.ps1'
if (($modes -contains 'active') -and -not (Test-Path $activeDesktopScript)) {
    Write-Host "FAIL: missing $activeDesktopScript"; exit 2
}
$summary = [System.Collections.Generic.List[string]]::new()
$summary.Add("F1_CBR_PROBE_START $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')")
$summary.Add("EXE=$rec")
$summary.Add("SECONDS=$Seconds RUNS_PER_MODE=$Runs MODE=$Mode")

# 逐秒表：分桶取自共用库（与 g5_format 同一份 ffprobe 取数），这里只写 CSV 与派生量。
function Get-PerSecond {
    param([string]$Mkv, [string]$CsvOut)
    $bucketed = Get-CbrSecondBuckets -Ffprobe $ffprobe -Path $Mkv
    $rows = [System.Collections.Generic.List[string]]::new()
    $rows.Add('second,bytes,kbps,packets,keyframes,max_packet')
    foreach ($r in @($bucketed.rows)) {
        $rows.Add(("{0},{1},{2:N0},{3},{4},{5}" -f $r.second, $r.bytes, $r.kbps,
                   $r.packets, $r.keyframes, $r.max_packet))
    }
    [System.IO.File]::WriteAllText($CsvOut, ($rows -join "`n") + "`n",
        [System.Text.UTF8Encoding]::new($false))

    $eff = @($bucketed.effective)
    $rates = @($bucketed.rates)
    if ($rates.Count -eq 0) {
        return [pscustomobject]@{ count = 0; min = 0; max = 0; avg = 0; spread = 1.0;
                                  worst_second = -1; worst_rate = 0; rates = @() }
    }
    $avg = ($rates | Measure-Object -Average).Average
    $max = ($rates | Measure-Object -Maximum).Maximum
    $min = ($rates | Measure-Object -Minimum).Minimum
    $worst = ($eff | Sort-Object { -$_.bytes } | Select-Object -First 1)
    return [pscustomobject]@{
        count = $rates.Count; min = $min; max = $max; avg = $avg
        # 逐秒 spread 只作历史纵向对照，docs/21 §2 明确「不作判据」。
        spread = $(if ($avg -gt 0) { ($max - $min) / $avg } else { 1.0 })
        worst_second = [int]$worst.second
        worst_rate = $worst.bytes * 8 / 1000.0
        rates = $rates
    }
}

foreach ($m in $modes) {
    foreach ($i in 1..$Runs) {
        $tag = ("{0}-{1:D2}" -f $m, $i)
        Write-Host "=== RUN $tag ($Seconds s CBR 6000kbps) ==="
        Clear-SrProcesses -IncludeNameFallback
        Get-Process ffplay -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
        Start-Sleep -Milliseconds 800
        $motion = $null
        $activity = $null
        if ($m -eq 'motion') {
            $motion = Start-Process -FilePath $ffplay -PassThru `
                -ArgumentList @('-fs', '-f', 'lavfi', '-i', 'testsrc2=size=1280x720:rate=30', '-an')
            Start-Sleep -Seconds 3
        } elseif ($m -eq 'active') {
            # 共用激励源：ActiveDesktop.ps1 **默认参数**（Seconds=120、全屏、BurstSec=2），
            # 不传 -Seconds / -Windowed / -BurstSec，不调弱（轮 46 指令 §注意事项 #2）。
            $activity = Start-CbrActiveDesktop -Root $root
        } else {
            # 静态桌面：不做任何 UI 动作，等桌面完全静止（轮 43 指令 §2.4）
            Start-Sleep -Seconds 2
        }
        $mkv = Join-Path $OutDir "$tag.mkv"
        $outFile = Join-Path $OutDir "$tag.out"
        $logFile = Join-Path $OutDir "$tag.log"
        $args = @('--record', '--seconds', "$Seconds", '--fps', '30', '--bitrate', '6000',
                  '--rate-mode', 'cbr', '--out', "`"$mkv`"", '--log-file', "`"$logFile`"",
                  '--log-level', 'info')
        try {
            $proc = Start-Process -FilePath $rec -PassThru -WindowStyle Hidden `
                -RedirectStandardOutput $outFile -ArgumentList $args
            Register-SrProcess $proc
            $exited = $proc.WaitForExit(($Seconds + 180) * 1000)
            Unregister-SrProcess $proc
            $exitCode = if ($exited) { $proc.ExitCode } else { -1 }
        } finally {
            if ($motion) { Stop-Process -Id $motion.Id -Force -ErrorAction SilentlyContinue }
            if ($activity) {
                # 共用收尾：按 PID + 命令行兜底清 pwsh 子进程，避免残留窗口影响下一次 static。
                Stop-CbrActiveDesktop -Process $activity
            }
        }
        $stdout = Read-TextSafe $outFile
        $recordExit = if ($stdout -match '(?m)^record_exit=(\S+)') { $Matches[1] } else { 'MISSING' }
        $filler = if ($stdout -match '(?m)^cbr_filler_bytes=(\S+)') { $Matches[1] } else { 'MISSING' }
        $encoded = if ($stdout -match '(?m)^frames_encoded=(\S+)') { $Matches[1] } else { 'MISSING' }
        $metrics = @()
        if (Test-Path $logFile) {
            $logText = Read-TextSafe $logFile
            $metrics = @(($logText -split "`n") | Where-Object { $_ -match 'metrics ' })
        }
        [System.IO.File]::WriteAllText((Join-Path $OutDir "$tag.metrics.txt"),
            ($metrics -join "`n") + "`n", [System.Text.UTF8Encoding]::new($false))

        $stats = Get-PerSecond -Mkv $mkv -CsvOut (Join-Path $OutDir "$tag.persec.csv")
        # 全片平均的唯一输入：recorder stdout 的 bytes_written / wall_seconds（不许逐秒再平均）。
        $bytesWritten = if ($stdout -match '(?m)^bytes_written=(\d+)') { [long]$Matches[1] } else { 0 }
        $wallSeconds = if ($stdout -match '(?m)^wall_seconds=([0-9.]+)') { [double]$Matches[1] } else { 0.0 }
        # docs/21 §2：全片平均偏差 ≤10% + 单秒 ≤2×；逐秒/滑窗方差不作判据。
        # 与 g5_format.ps1 共用 Get-CbrVerdict，两个工具不会出现两套结论。
        $verdict = Get-CbrVerdict -Rates @($stats.rates) -BytesWritten $bytesWritten `
            -WallSeconds $wallSeconds -NominalKbps 6000
        $verdictOk = $verdict.verdict_ok
        # 失败归因必须区分「平均超」与「单秒超」——两者处置不同（轮 46 指令 §注意事项 #3）。
        $failKind = if ($verdictOk) { '' }
            elseif (-not $verdict.avg_ok -and -not $verdict.cap_ok) { 'average+single-second' }
            elseif (-not $verdict.avg_ok) { 'average' }
            else { 'single-second' }
        $overTen = $stats.spread -gt 0.10   # 仅历史纵向对照，不参与判定
        $captureFps = ''
        if ($metrics.Count -gt 0) {
            $lastMetric = $metrics[-1]
            if ($lastMetric -match 'capture_fps=([0-9.]+)') { $captureFps = $Matches[1] }
        }
        # 关键帧秒字节分解：对最差的一秒 + 一个正常秒都做，便于对比
        $breakdowns = @()
        foreach ($sec in @($stats.worst_second, 2) | Select-Object -Unique) {
            if ($sec -lt 0) { continue }
            $jf = Join-Path $OutDir "$tag.nal-sec$sec.json"
            $py = & python $nalTool $mkv $sec --ffprobe $ffprobe --json $jf 2>&1 | Out-String
            if (Test-Path $jf) { $breakdowns += (Get-Content $jf -Raw -Encoding UTF8).Trim() }
            $summary.Add("NAL $tag sec=$sec :: $($py.Trim())")
        }
        $row = [pscustomobject]@{
            tag = $tag; mode = $m; record_exit = $recordExit; proc_exit = $exitCode
            frames_encoded = $encoded; cbr_filler_bytes = $filler
            metric_lines = $metrics.Count
            seconds_counted = $stats.count
            min_kbps = [Math]::Round($stats.min, 0); max_kbps = [Math]::Round($stats.max, 0)
            rates_avg_kbps = [Math]::Round($stats.avg, 0)
            persecond_spread = [Math]::Round($stats.spread, 4)   # 不作判据
            over_10pct_historical = $overTen                     # 不作判据
            worst_second = $stats.worst_second
            worst_kbps = [Math]::Round($stats.worst_rate, 0)
            capture_fps = $captureFps
            bytes_written = $bytesWritten; wall_seconds = $wallSeconds
            whole_avg_kbps = [Math]::Round($verdict.whole_avg_kbps, 1)
            avg_deviation_pct = [Math]::Round($verdict.avg_deviation * 100, 2)
            avg_ok = $verdict.avg_ok
            cap_kbps = $verdict.cap_kbps
            cap_ok = $verdict.cap_ok
            verdict_ok = $verdictOk
            fail_kind = $failKind
        }
        [System.IO.File]::WriteAllText((Join-Path $OutDir "$tag.summary.json"),
            ($row | ConvertTo-Json -Depth 3), [System.Text.UTF8Encoding]::new($false))
        $line = ("RUN {0} exit={1}/{2} frames={3} filler={4} metrics={5} cap_fps={6} | " +
                 "whole_avg={7:N0}kbps avg_dev={8:N2}% avg_ok={9} | " +
                 "peak={10:N0} vs cap={11:N0} cap_ok={12} VERDICT={13} fail_kind={14} | " +
                 "persecond_spread={15:P1}(历史对照,不作判据) worst_sec={16} ({17:N0} kbps)") -f `
            $tag, $recordExit, $exitCode, $encoded, $filler, $metrics.Count, $captureFps,
            $verdict.whole_avg_kbps, ($verdict.avg_deviation * 100), $verdict.avg_ok,
            $verdict.max_kbps, $verdict.cap_kbps, $verdict.cap_ok,
            $(if ($verdictOk) { 'PASS' } else { 'FAIL' }), $(if ($failKind) { $failKind } else { '-' }),
            $stats.spread, $stats.worst_second, [Math]::Round($stats.worst_rate, 0)
        $summary.Add($line)
        Write-Host $line
    }
}

foreach ($m in $modes) {
    $rows = @(Get-ChildItem $OutDir -Filter "$m-*.summary.json" |
              ForEach-Object { Get-Content $_.FullName -Raw -Encoding UTF8 | ConvertFrom-Json })
    $over = @($rows | Where-Object { $_.over_10pct_historical }).Count
    $verdictPass = @($rows | Where-Object { $_.verdict_ok }).Count
    $avgFail = @($rows | Where-Object { -not $_.avg_ok }).Count
    $capFail = @($rows | Where-Object { -not $_.cap_ok }).Count
    $summary.Add("MODE $m runs=$($rows.Count) verdict_pass=$verdictPass avg_fail=$avgFail cap_fail=$capFail " +
                 "persecond_spread_over10_historical=$over " +
                 "avg_dev_pct=$(($rows | ForEach-Object { $_.avg_deviation_pct }) -join ',') " +
                 "whole_avg_kbps=$(($rows | ForEach-Object { $_.whole_avg_kbps }) -join ',') " +
                 "peak_kbps=$(($rows | ForEach-Object { $_.max_kbps }) -join ',') " +
                 "fail_kind=$(($rows | ForEach-Object { if ($_.fail_kind) { $_.fail_kind } else { '-' } }) -join ',') " +
                 "capture_fps=$(($rows | ForEach-Object { $_.capture_fps }) -join ',')")
}
$summary.Add("F1_CBR_PROBE_DONE $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')")
[System.IO.File]::WriteAllText((Join-Path $OutDir 'probe-summary.txt'),
    ($summary -join "`n") + "`n", [System.Text.UTF8Encoding]::new($false))
Write-Host ("PROBE_SUMMARY " + (Join-Path $OutDir 'probe-summary.txt'))
