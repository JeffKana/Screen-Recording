# U1 定量：ui_smoke 连跑 N 次（默认 10），失败即停并保留现场。
#
# 轮 30 出现过一次 88/13（recorder 不退出 + 半截 .part），事后 3 次全绿且没留现场。
# 这里把「多次运行 + 失败取证」做成固定流程：
#   - 每轮独立进程跑 ui_smoke，输出落盘；
#   - 每轮结束后统计残留进程并清掉，避免污染下一轮；
#   - 任一失败立即停止，打印 summary 并退出 1（现场由 ui_smoke 的 Forensics 落盘）。
#
# Usage: pwsh -File verify/cases/soak_ui_smoke.ps1 [-Runs 10] [-BuildDir build-msvc] [-Config Release]

param(
    [int]$Runs = 10,
    [string]$BuildDir = 'build-msvc',
    [string]$Config = 'Release'
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot '..\lib\Cleanup.ps1')
trap {
    Clear-SrProcesses -IncludeNameFallback
    Write-Host "FAIL: $_"
    Write-Host $_.ScriptStackTrace
    exit 1
}

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$case = Join-Path $PSScriptRoot 'ui_smoke.ps1'
$outDir = Join-Path $root ('verify/artifacts/soak-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$results = @()
$failed = 0
for ($i = 1; $i -le $Runs; $i++) {
    Get-Process sr-ui, sr-recorder -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 700
    $log = Join-Path $outDir ("run-{0:D2}.log" -f $i)
    $began = Get-Date
    $proc = Start-Process -FilePath 'pwsh' -PassThru -Wait -WindowStyle Hidden `
        -ArgumentList @('-NoProfile', '-File', "`"$case`"", '-BuildDir', $BuildDir, '-Config', $Config) `
        -RedirectStandardOutput $log -RedirectStandardError ($log + '.err')
    $seconds = [Math]::Round(((Get-Date) - $began).TotalSeconds, 1)
    $text = Read-TextSafe $log
    $match = [regex]::Match($text, 'RESULT:\s*(\d+)\s*PASS\s*/\s*(\d+)\s*FAIL\s*/\s*(\d+)\s*SKIP')
    $pass = if ($match.Success) { [int]$match.Groups[1].Value } else { 0 }
    $fail = if ($match.Success) { [int]$match.Groups[2].Value } else { -1 }
    $skip = if ($match.Success) { [int]$match.Groups[3].Value } else { 0 }
    $leftover = @(Get-Process sr-ui, sr-recorder -ErrorAction SilentlyContinue)
    $forensics = [regex]::Match($text, '\[forensics\] 现场已保存: (.+)')
    $ok = ($proc.ExitCode -eq 0 -and $match.Success -and $fail -eq 0 -and $leftover.Count -eq 0)
    $results += [pscustomobject]@{
        run      = $i
        exit     = $proc.ExitCode
        pass     = $pass
        fail     = $fail
        skip     = $skip
        seconds  = $seconds
        leftover = $leftover.Count
        log      = $log
        forensics = if ($forensics.Success) { $forensics.Groups[1].Value.Trim() } else { '' }
    }
    Write-Host ("run {0}/{1}: exit={2} pass={3} fail={4} leftover={5} {6}s" -f `
        $i, $Runs, $proc.ExitCode, $pass, $fail, $leftover.Count, $seconds)
    if ($leftover.Count -gt 0) {
        foreach ($p in $leftover) { Write-Host ("  leftover pid={0} name={1}" -f $p.Id, $p.ProcessName) }
        Stop-SrProcess $null
        $leftover | Stop-Process -Force -ErrorAction SilentlyContinue
    }
    if (-not $ok) {
        $failed++
        Write-Host "FAIL: run $i 不稳定，按 U1 约定立即停止并保留现场"
        break
    }
}

$summary = Join-Path $outDir 'summary.json'
@{
    runs     = $results
    started  = $outDir
    total    = $Runs
    executed = $results.Count
    failed   = $failed
} | ConvertTo-Json -Depth 4 | Set-Content -Path $summary -Encoding UTF8

Write-Host ''
Write-Host ("SOAK: {0}/{1} 次通过，失败 {2} 次；产物 {3}" -f ($results.Count - $failed), $results.Count, $failed, $outDir)
if ($failed -gt 0) { exit 1 }
exit 0
