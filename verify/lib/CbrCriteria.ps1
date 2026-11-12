# 轮 46 · M3-04#1 共用判据实现（docs/21 §2 / §4，F3 口径合一）
#
# 判据原文（照抄 docs/21 §2）：
#   CBR 模式下**全片平均码率**与标称偏差 ≤10%，且**任意单秒**码率不超过标称的 2 倍。
#   （逐秒方差与滑窗方差受编码器实现限制，**不作判据**；单秒 2× 为硬挡板，不可放宽。）
#
# 口径（docs/21 §4，两个用例共用，不允许各自实现一套）：
#   ① 分桶：ffprobe `-select_streams v:0 -show_entries packet=pts_time,size,flags -of csv=p=0`，
#      `pts_time` 按 [int][Math]::Floor() 落整秒桶，桶内累加 packet.size（字节）；
#      跳过第 0 秒桶与最后一个桶（编码器预热/收尾），每桶 bytes*8/1000 → kbps。
#   ② 平均：**全片** `bytes_written / wall_seconds`（recorder stdout 字段）换算 kbps，
#      与 `NominalKbps` 比 `|avg-nominal|/nominal ≤ 10%`；**不得**用逐秒 rates 再平均代替。
#   ③ 单秒：对**全部有效桶**逐个判 `rate ≤ 2 × NominalKbps`，任一超标即 FAIL；
#      平均 PASS 也不许跳过此判定。
#
# 使用方：`verify/cases/g5_format.ps1`（正式用例）与 `verify/cases/g5_cbr_probe.ps1`（6 次实测），
#         以及 `-CbrOnly` 的同一场景对照。激励源统一由本文件的 Start-/Stop- 函数启动
#         `verify/lib/ActiveDesktop.ps1` 的**默认参数**（不传 -Seconds / -Windowed / -BurstSec）。

# 分桶：两个用例唯一的一份 ffprobe 取数与逐秒桶实现。
function Get-CbrSecondBuckets {
    param(
        [Parameter(Mandatory = $true)][string]$Ffprobe,
        [Parameter(Mandatory = $true)][string]$Path
    )
    $packetLines = @(& $Ffprobe -v error -select_streams v:0 `
        -show_entries packet=pts_time,size,flags -of csv=p=0 $Path 2>&1)
    $buckets = @{}
    $packets = @{}
    $keys = @{}
    $maxes = @{}
    foreach ($line in $packetLines) {
        $a = ([string]$line) -split ','
        if ($a.Count -lt 2) { continue }
        $time = 0.0
        $size = 0
        if (-not [double]::TryParse($a[0], [ref]$time)) { continue }
        if (-not [int]::TryParse($a[1], [ref]$size)) { continue }
        $second = [int][Math]::Floor($time)
        if (-not $buckets.ContainsKey($second)) {
            $buckets[$second] = 0
            $packets[$second] = 0
            $keys[$second] = 0
            $maxes[$second] = 0
        }
        $buckets[$second] += $size
        $packets[$second]++
        if ($a.Count -ge 3 -and $a[2] -match 'K') { $keys[$second]++ }
        if ($size -gt $maxes[$second]) { $maxes[$second] = $size }
    }
    $sorted = @($buckets.Keys | Sort-Object)
    $last = if ($sorted.Count -gt 0) { [int]$sorted[-1] } else { 0 }
    $rows = @()
    foreach ($sec in $sorted) {
        $rows += [pscustomobject]@{
            second     = [int]$sec
            bytes      = [long]$buckets[$sec]
            kbps       = ($buckets[$sec] * 8 / 1000.0)
            packets    = [int]$packets[$sec]
            keyframes  = [int]$keys[$sec]
            max_packet = [long]$maxes[$sec]
        }
    }
    $effective = @($rows | Where-Object { $_.second -ne 0 -and $_.second -lt $last })
    $rates = @($effective | ForEach-Object { [double]$_.kbps })
    return [pscustomobject]@{
        rows         = $rows
        effective    = $effective
        rates        = $rates
        last_second  = $last
    }
}

# 判据：全片平均 + 单秒硬挡板。两个用例、模拟注入、-CbrOnly 对照全部走这一份实现。
function Get-CbrVerdict {
    param(
        [double[]]$Rates,
        [long]$BytesWritten,
        [double]$WallSeconds,
        [double]$NominalKbps = 6000
    )
    $capKbps = 2 * $NominalKbps
    $wholeAvg = if ($WallSeconds -gt 0) { ($BytesWritten * 8 / 1000.0) / $WallSeconds } else { 0.0 }
    $deviation = if ($NominalKbps -gt 0) { [Math]::Abs($wholeAvg - $NominalKbps) / $NominalKbps } else { 1.0 }
    $avgOk = ($WallSeconds -gt 0 -and $BytesWritten -gt 0 -and $deviation -le 0.10)
    $ratesAvg = if ($Rates.Count -gt 0) { ($Rates | Measure-Object -Average).Average } else { 0.0 }
    $maxRate = if ($Rates.Count -gt 0) { ($Rates | Measure-Object -Maximum).Maximum } else { 0.0 }
    # 全部有效桶逐个判；不设 2% 余量（硬挡板，docs/21 §4 #1）。
    $capOk = ($Rates.Count -gt 0 -and $maxRate -le $capKbps)
    return [pscustomobject]@{
        source            = 'bytes_written/wall_seconds'
        bytes_written     = $BytesWritten
        wall_seconds      = $WallSeconds
        nominal_kbps      = $NominalKbps
        whole_avg_kbps    = $wholeAvg
        avg_deviation     = $deviation
        avg_ok            = $avgOk
        cap_kbps          = $capKbps
        max_kbps          = $maxRate
        cap_ok            = $capOk
        verdict_ok        = ($avgOk -and $capOk)
        buckets           = $Rates.Count
        rates_avg_kbps    = $ratesAvg   # 仅作对照，绝不用于平均判据
    }
}

# 激励源：ActiveDesktop.ps1 默认参数（-Seconds/-Windowed/-BurstSec 一律不传）。
function Start-CbrActiveDesktop {
    param([Parameter(Mandatory = $true)][string]$Root)
    $scriptPath = Join-Path $Root 'verify/lib/ActiveDesktop.ps1'
    if (-not (Test-Path $scriptPath)) { throw "missing $scriptPath" }
    $p = Start-Process -FilePath 'pwsh' -PassThru -WindowStyle Hidden `
        -ArgumentList @('-NoProfile', '-File', "`"$scriptPath`"")
    Start-Sleep -Seconds 3
    return $p
}

function Stop-CbrActiveDesktop {
    param([object]$Process)
    if ($Process) { Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue }
    Get-CimInstance Win32_Process -Filter "Name='pwsh.exe'" -ErrorAction SilentlyContinue |
        Where-Object { $_.CommandLine -like '*ActiveDesktop.ps1*' } |
        ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
    Start-Sleep -Milliseconds 500
}
