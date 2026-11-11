# LT-01 代理：4 段 × 30min × N 轮，段间进程退出再起（模拟用户行为）。
#
# ⚠ 这是**代理**测试，不是「单文件 8 小时」证明。本脚本只覆盖「连续多段 30 分钟」的
#   内存/句柄/丢帧/可播性，8 小时单文件仍在 BLOCKED（见 docs/10 第一行）。
#
# 采集口径：
#   - recorder 直接跑（--seconds 1800），不挂 UI：UI 的 1Hz 快照和托盘刷新会污染内存基线；
#   - 内存取 recorder 自己打印的 mem_peak_mb（PeakWorkingSet64），不是外部采样；
#   - 每段结束进程退出，下一段新建进程 → 测的是「用户连续录 4 段」而不是「同进程多段」。
#
# 判据：段间内存增幅 ≤5%、句柄增幅 ≤10%、每段可播、时长 1800±1s。
#
# Usage: pwsh -File verify/cases/g5_lt01.ps1 [-Rounds 3] [-Segments 4] [-Seconds 1800]

param(
    [int]$Rounds = 3,
    [int]$Segments = 4,
    [int]$Seconds = 1800,
    [string]$BuildDir = 'build-msvc',
    [string]$Config = 'Release',
    [switch]$FullDecode
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot '..\lib\Cleanup.ps1')
. (Join-Path $PSScriptRoot '..\lib\Forensics.ps1')
trap { Clear-SrProcesses -IncludeNameFallback; Write-Host "FAIL: $_"; exit 1 }

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$rec = Join-Path $root (Join-Path $BuildDir "bin\$Config\sr-recorder.exe")
if (-not (Test-Path $rec)) { $rec = Join-Path $root (Join-Path $BuildDir 'bin\sr-recorder.exe') }
if (-not (Test-Path $rec)) { Write-Host "FAIL: missing $rec"; exit 2 }
$ffbin = Split-Path -Parent (Get-Command ffprobe).Source
$ffprobe = Join-Path $ffbin 'ffprobe.exe'
$ffmpeg = Join-Path $ffbin 'ffmpeg.exe'

$work = Join-Path $root ('verify/artifacts/lt01-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Force -Path $work | Out-Null
$script:SrForensicsWorkDirs = @($work)

Get-Process sr-ui, sr-recorder -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue

function Field {
    param([string]$Text, [string]$Key)
    if ($Text -match "(?m)^$Key=(.*)$") { return $Matches[1].Trim() }
    return ''
}

function Get-MediaSeconds {
    param([string]$Path)
    $out = & $ffprobe -v error -show_entries format=duration -of default=nw=1:nk=1 $Path 2>&1 | Out-String
    $value = 0.0
    [void][double]::TryParse($out.Trim(), [ref]$value)
    return $value
}

function Test-SampledDecode {
    param([string]$Path)
    if ($FullDecode) {
        & $ffmpeg -v error -i $Path -f null - 2>&1 | Out-Null
        return ($LASTEXITCODE -eq 0)
    }
    # 默认抽样：首尾各 10s 真实解码（全解 30min×12 段会把验收时间翻倍）。
    & $ffmpeg -v error -i $Path -t 10 -f null - 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) { return $false }
    & $ffmpeg -v error -sseof -10 -i $Path -f null - 2>&1 | Out-Null
    return ($LASTEXITCODE -eq 0)
}

$rows = @()
$failures = 0
for ($round = 1; $round -le $Rounds; $round++) {
    Write-Host ("=== LT-01 第 {0}/{1} 轮（{2} 段 × {3}s）===" -f $round, $Rounds, $Segments, $Seconds)
    $roundRows = @()
    for ($segment = 1; $segment -le $Segments; $segment++) {
        $tag = "r{0}-s{1}" -f $round, $segment
        $outFile = Join-Path $work "lt01-$tag.mkv"
        $logFile = Join-Path $work "lt01-$tag.log"
        $stdout = Join-Path $work "lt01-$tag.out"
        $began = Get-Date
        $proc = Start-Process -FilePath $rec -PassThru -WindowStyle Hidden -RedirectStandardOutput $stdout `
            -ArgumentList @('--record', '--seconds', "$Seconds", '--fps', '30', '--out', "`"$outFile`"",
                            '--log-level', 'info', '--log-file', "`"$logFile`"")
        Register-SrProcess $proc
        $timeoutMs = ($Seconds + 180) * 1000
        $exited = $proc.WaitForExit($timeoutMs)
        $wall = [Math]::Round(((Get-Date) - $began).TotalSeconds, 1)
        $text = Read-TextSafe $stdout
        $exitCode = if ($exited) { $proc.ExitCode } else { -1 }
        if (-not $exited) { Stop-SrProcess $proc }
        Unregister-SrProcess $proc

        $mediaSeconds = if (Test-Path $outFile) { Get-MediaSeconds $outFile } else { 0.0 }
        $playable = if ($mediaSeconds -gt 0) { Test-SampledDecode $outFile } else { $false }
        $row = [pscustomobject]@{
            round        = $round
            segment      = $segment
            exit         = $exitCode
            wall_s       = $wall
            media_s      = [Math]::Round($mediaSeconds, 3)
            mem_peak_mb  = [double](Field $text 'mem_peak_mb')
            handles      = [long](Field $text 'handles')
            dropped      = [long](Field $text 'frames_dropped')
            captured     = [long](Field $text 'frames_captured')
            playable     = $playable
            file         = $outFile
        }
        $roundRows += $row
        $rows += $row
        # 逐段追加：6 小时的运行如果被中断，已完成的段不能只躺在 stdout 里。
        ($row | ConvertTo-Json -Compress) | Add-Content -Path (Join-Path $work 'segments.jsonl') -Encoding UTF8
        $durationOk = [Math]::Abs($row.media_s - $Seconds) -le 1.0
        $ok = ($exitCode -eq 0 -and $playable -and $durationOk)
        if (-not $ok) { $failures++ }
        Write-Host ("  段 {0}: exit={1} media={2}s mem_peak={3}MB handles={4} dropped={5} playable={6} {7}" -f `
            $segment, $exitCode, $row.media_s, $row.mem_peak_mb, $row.handles, $row.dropped, $playable,
            ($(if ($ok) { 'PASS' } else { 'FAIL' })))
        if (-not $ok) {
            [void](Save-FailureContext -Tag "lt01-$tag" -WorkDirs @($work) `
                -Note ("exit=$exitCode media=$($row.media_s) playable=$playable"))
        }
    }

    $first = $roundRows[0]
    $maxMem = ($roundRows | Measure-Object mem_peak_mb -Maximum).Maximum
    $maxHandles = ($roundRows | Measure-Object handles -Maximum).Maximum
    $memGrowth = if ($first.mem_peak_mb -gt 0) { ($maxMem - $first.mem_peak_mb) / $first.mem_peak_mb } else { 1.0 }
    $handleGrowth = if ($first.handles -gt 0) { ($maxHandles - $first.handles) / $first.handles } else { 1.0 }
    $memOk = $memGrowth -le 0.05
    $handleOk = $handleGrowth -le 0.10
    if (-not $memOk) { $failures++ }
    if (-not $handleOk) { $failures++ }
    Write-Host ("  轮 {0} 汇总：mem {1:N1}→{2:N1}MB (+{3:P1})，handles {4}→{5} (+{6:P1}) {7}/{8}" -f `
        $round, $first.mem_peak_mb, $maxMem, $memGrowth, $first.handles, $maxHandles, $handleGrowth,
        $(if ($memOk) { 'PASS' } else { 'FAIL' }), $(if ($handleOk) { 'PASS' } else { 'FAIL' }))
}

$summary = [pscustomobject]@{
    proxy        = $true
    note         = '代理：4×30min 连续段，不是单文件 8 小时'
    rounds       = $Rounds
    segments     = $Segments
    seconds      = $Seconds
    full_decode  = [bool]$FullDecode
    failures     = $failures
    rows         = $rows
}
$summaryPath = Join-Path $work 'summary.json'
$summary | ConvertTo-Json -Depth 5 | Set-Content -Path $summaryPath -Encoding UTF8
Write-Host ''
Write-Host ("LT01: {0} 段完成，失败 {1}；产物 {2}" -f $rows.Count, $failures, $work)
if ($failures -gt 0) { exit 1 }
exit 0
