# 轮 43 · F1 定性：CBR 逐秒码率过冲（只读测量，不改产品、不改用例如判据）
#
# 静态桌面 / 全屏运动源各 5 次 CBR 40s，输出：
#   - 每次的 recorder stdout 汇总 + 全量 metrics 行（capture_fps / encode_fps / dropped）
#   - 逐秒表 persec.csv：second, bytes, kbps, packets, keyframes, max_packet
#   - 关键帧秒字节分解 nal-*.json：keyframe_bytes / filler_bytes / other_bytes
#   - 汇总 probe-summary.txt：每次的 spread 与是否 >10%
#
# 判据口径与 g5_format.ps1::Get-PerSecondStats **完全一致**（按 pts_time 分桶，
# 跳过第 0 秒与最后一秒），便于和验收方的 58% 直接对照。
#
# Usage:
#   pwsh -File verify/cases/g5_cbr_probe.ps1 -Seconds 40 -Runs 5 -Mode both
#   pwsh -File verify/cases/g5_cbr_probe.ps1 -Seconds 40 -Runs 5 -Mode static -OutDir <dir>

param(
    [string]$BuildDir = 'build-msvc',
    [string]$Config = 'Release',
    [int]$Seconds = 40,
    [int]$Runs = 5,
    [ValidateSet('static', 'motion', 'both')][string]$Mode = 'both',
    [string]$OutDir = ''
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\lib\Cleanup.ps1')
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

$modes = if ($Mode -eq 'both') { @('static', 'motion') } else { @($Mode) }
$summary = [System.Collections.Generic.List[string]]::new()
$summary.Add("F1_CBR_PROBE_START $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')")
$summary.Add("EXE=$rec")
$summary.Add("SECONDS=$Seconds RUNS_PER_MODE=$Runs MODE=$Mode")

function Get-PerSecond {
    param([string]$Mkv, [string]$CsvOut)
    $packets = & $ffprobe -v error -select_streams v:0 `
        -show_entries packet=pts_time,size,flags -of csv=p=0 $Mkv 2>&1
    $buckets = @{}
    $counts = @{}
    $keys = @{}
    $maxes = @{}
    foreach ($line in @($packets)) {
        $a = ($line -split ',')
        if ($a.Count -lt 2) { continue }
        $t = 0.0; $s = 0
        if (-not [double]::TryParse($a[0], [ref]$t)) { continue }
        if (-not [int]::TryParse($a[1], [ref]$s)) { continue }
        $sec = [int][Math]::Floor($t)
        if (-not $buckets.ContainsKey($sec)) {
            $buckets[$sec] = 0; $counts[$sec] = 0; $keys[$sec] = 0; $maxes[$sec] = 0
        }
        $buckets[$sec] += $s
        $counts[$sec]++
        if ($a.Count -ge 3 -and $a[2] -match 'K') { $keys[$sec]++ }
        if ($s -gt $maxes[$sec]) { $maxes[$sec] = $s }
    }
    $rows = [System.Collections.Generic.List[string]]::new()
    $rows.Add('second,bytes,kbps,packets,keyframes,max_packet')
    foreach ($sec in ($buckets.Keys | Sort-Object)) {
        $rows.Add(("{0},{1},{2:N0},{3},{4},{5}" -f $sec, $buckets[$sec],
                   ($buckets[$sec] * 8 / 1000.0), $counts[$sec], $keys[$sec], $maxes[$sec]))
    }
    [System.IO.File]::WriteAllText($CsvOut, ($rows -join "`n") + "`n",
        [System.Text.UTF8Encoding]::new($false))

    # 与 g5_format 相同：跳过第 0 秒与最后一秒
    $last = if ($buckets.Count -gt 0) { ($buckets.Keys | Measure-Object -Maximum).Maximum } else { 0 }
    $rates = @($buckets.Keys | Sort-Object | Where-Object { $_ -ne 0 -and $_ -lt $last } |
               ForEach-Object { $buckets[$_] * 8 / 1000.0 })
    if ($rates.Count -eq 0) {
        return [pscustomobject]@{ count = 0; min = 0; max = 0; avg = 0; spread = 1.0;
                                  worst_second = -1; worst_rate = 0 }
    }
    $avg = ($rates | Measure-Object -Average).Average
    $max = ($rates | Measure-Object -Maximum).Maximum
    $min = ($rates | Measure-Object -Minimum).Minimum
    $worstSec = ($buckets.Keys | Where-Object { $_ -ne 0 -and $_ -lt $last } |
                 Sort-Object { -$buckets[$_] } | Select-Object -First 1)
    return [pscustomobject]@{
        count = $rates.Count; min = $min; max = $max; avg = $avg
        spread = $(if ($avg -gt 0) { ($max - $min) / $avg } else { 1.0 })
        worst_second = $worstSec
        worst_rate = $buckets[$worstSec] * 8 / 1000.0
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
        if ($m -eq 'motion') {
            $motion = Start-Process -FilePath $ffplay -PassThru `
                -ArgumentList @('-fs', '-f', 'lavfi', '-i', 'testsrc2=size=1280x720:rate=30', '-an')
            Start-Sleep -Seconds 3
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
        $overTen = $stats.spread -gt 0.10
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
            avg_kbps = [Math]::Round($stats.avg, 0); spread = [Math]::Round($stats.spread, 4)
            over_10pct = $overTen; worst_second = $stats.worst_second
            worst_kbps = [Math]::Round($stats.worst_rate, 0)
        }
        [System.IO.File]::WriteAllText((Join-Path $OutDir "$tag.summary.json"),
            ($row | ConvertTo-Json -Depth 3), [System.Text.UTF8Encoding]::new($false))
        $line = ("RUN {0} exit={1}/{2} frames={3} filler={4} metrics={5} | " +
                 "min={6} max={7} avg={8} spread={9:P1} over10={10} worst_sec={11} ({12} kbps)") -f `
            $tag, $recordExit, $exitCode, $encoded, $filler, $metrics.Count,
            [Math]::Round($stats.min, 0), [Math]::Round($stats.max, 0),
            [Math]::Round($stats.avg, 0), $stats.spread, $overTen,
            $stats.worst_second, [Math]::Round($stats.worst_rate, 0)
        $summary.Add($line)
        Write-Host $line
    }
}

foreach ($m in $modes) {
    $rows = @(Get-ChildItem $OutDir -Filter "$m-*.summary.json" |
              ForEach-Object { Get-Content $_.FullName -Raw -Encoding UTF8 | ConvertFrom-Json })
    $over = @($rows | Where-Object { $_.over_10pct }).Count
    $summary.Add("MODE $m runs=$($rows.Count) over_10pct=$over " +
                 "spreads=$(($rows | ForEach-Object { '{0:P1}' -f $_.spread }) -join ',')")
}
$summary.Add("F1_CBR_PROBE_DONE $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')")
[System.IO.File]::WriteAllText((Join-Path $OutDir 'probe-summary.txt'),
    ($summary -join "`n") + "`n", [System.Text.UTF8Encoding]::new($false))
Write-Host ("PROBE_SUMMARY " + (Join-Path $OutDir 'probe-summary.txt'))
