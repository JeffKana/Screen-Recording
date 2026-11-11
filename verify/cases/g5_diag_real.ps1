# 诊断包「只留最近 10 分钟」——用**真实长会话日志**验证（轮 32 P1）
#
# 轮 31 只用合成的 2 小时日志验过 TruncateByAge（--diag-window-test），
# 没有证明「真实 recorder 日志（30min 以上）导出后确实被裁到 10 分钟」。
# 本用例拿一段真实会话日志当输入，跑 `sr-recorder --export-diag`，检查产物：
#   1. log.txt 含 recorder 段（此前 recorder 的 --export-diag 根本不带自己的日志）；
#   2. 产物时间跨度 ≤ 10min（+60s 余量），而源日志跨度 ≥ 30min；
#   3. 产物 ≤ 400KB，且最后一条与源日志最后一条一致（裁尾不裁头）。
#
# Usage: pwsh -File verify/cases/g5_diag_real.ps1 -LogFile <30min 会话日志> [-BuildDir build-msvc]

param(
    [string]$LogFile = '',
    [string]$BuildDir = 'build-msvc',
    [string]$Config = 'Release'
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot '..\lib\Cleanup.ps1')
. (Join-Path $PSScriptRoot '..\lib\Forensics.ps1')
trap {
    [void](Save-FailureContext -Tag 'g5_diag_real' -WorkDirs $script:SrForensicsWorkDirs -Note $_)
    Clear-SrProcesses -IncludeNameFallback
    Write-Host "FAIL: $_"
    exit 1
}

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$rec = Join-Path $root (Join-Path $BuildDir "bin\$Config\sr-recorder.exe")
if (-not (Test-Path $rec)) { $rec = Join-Path $root (Join-Path $BuildDir 'bin\sr-recorder.exe') }
if (-not (Test-Path $rec)) { Write-Host "FAIL: missing $rec"; exit 2 }

if ([string]::IsNullOrWhiteSpace($LogFile)) {
    $candidate = Get-ChildItem (Join-Path $root 'verify\artifacts') -Recurse -Filter '*.log' -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Length -gt 64KB } |
        Sort-Object Length -Descending | Select-Object -First 1
    if ($candidate) { $LogFile = $candidate.FullName }
}
if ([string]::IsNullOrWhiteSpace($LogFile) -or -not (Test-Path $LogFile)) {
    Write-Host 'BLOCKED: 没有可用的长会话日志（先跑 cbr60 / LT-01）'
    exit 4
}

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
        [void](Save-FailureContext -Tag $Name -WorkDirs $script:SrForensicsWorkDirs -Note $Detail)
    }
}
function Get-Span {
    param([string]$Path)
    $first = $null
    $last = $null
    $count = 0
    $reader = New-Object System.IO.StreamReader((New-Object System.IO.FileStream($Path, [System.IO.FileMode]::Open,
        [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)), [System.Text.Encoding]::UTF8)
    while ($null -ne ($line = $reader.ReadLine())) {
        $match = [regex]::Match($line, '^t=(\d+) ')
        if (-not $match.Success) { continue }
        $value = [long]$match.Groups[1].Value
        if ($null -eq $first) { $first = $value }
        $last = $value
        $count++
    }
    $reader.Dispose()
    return [pscustomobject]@{
        First = $first
        Last  = $last
        Span  = if ($null -ne $first -and $null -ne $last) { $last - $first } else { 0 }
        Lines = $count
    }
}

$work = Join-Path $env:TEMP ('sr-diag-real-' + (Get-Date -Format 'HHmmss'))
New-Item -ItemType Directory -Force -Path $work | Out-Null
$script:SrForensicsWorkDirs = @($work)

# 用副本：既有产物是验收凭据，不能被一次导出一边读一边写（轮 32 踩过，
# 老版本 `--export-diag --log-file X` 会以 "wb" 打开 X，把源日志截成 0 字节）。
$sourceCopy = Join-Path $work 'source.log'
Copy-Item $LogFile $sourceCopy -Force

$src = Get-Span $sourceCopy
Assert 'source-is-long-session' ($src.Span -ge 28 * 60 * 1000) `
    ("源日志 {0}：{1} 行，跨度 {2:N0}s（30min 会话）" -f (Split-Path -Leaf $LogFile), $src.Lines, ($src.Span / 1000))

# R2（轮 33）：导出前后源日志必须 intact。源日志在导出后仍会被追加（正常日志写入），
# 因此判据是「大小不减少 + 前缀哈希不变」，不是「完全不变」。
function Get-PrefixHash {
    param([string]$Path, [int]$Length = 4096)
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    $take = [Math]::Min($Length, $bytes.Length)
    $sha = [System.Security.Cryptography.SHA256]::Create()
    $hash = $sha.ComputeHash($bytes, 0, $take)
    $sha.Dispose()
    return [pscustomobject]@{ Length = $bytes.Length; Hash = [Convert]::ToHexString($hash) }
}
$sourceBefore = Get-PrefixHash $sourceCopy

$before = (Get-ChildItem $work -Directory -ErrorAction SilentlyContinue | Measure-Object).Count
$stdout = & $rec --export-diag $work --log-file $sourceCopy --log-level info 2>&1 | Out-String
$sourceAfter = Get-PrefixHash $sourceCopy
Assert 'diag-source-size-intact' ($sourceAfter.Length -ge $sourceBefore.Length) `
    ("源日志 {0} → {1} 字节（只允许追加）" -f $sourceBefore.Length, $sourceAfter.Length)
Assert 'diag-source-prefix-intact' ($sourceAfter.Hash -eq $sourceBefore.Hash) `
    '源日志前 4KB 未被改写（D-54 回归护栏）'
Write-Host $stdout.Trim()
$diagDir = $null
if ($stdout -match 'diag_path=(.+)$') { $diagDir = $Matches[1].Trim() }
if (-not $diagDir -or -not (Test-Path $diagDir)) {
    $diagDir = (Get-ChildItem $work -Directory | Sort-Object LastWriteTime -Descending | Select-Object -First 1).FullName
}
Assert 'diag-pack-exported' ($stdout -match 'diag_pack=ok' -and (Test-Path (Join-Path $diagDir 'log.txt'))) `
    ("path={0}" -f $diagDir)

$logTxt = Join-Path $diagDir 'log.txt'
$tail = Get-Span $logTxt
Assert 'diag-has-recorder-section' ((Get-Content $logTxt -TotalCount 200 | Out-String) -match 'recorder log') `
    'log.txt 含 recorder 段（轮 32 修复：此前不带自身日志）'
Assert 'diag-span-within-10min' ($tail.Span -le (10 * 60 * 1000 + 60 * 1000)) `
    ("导出跨度 {0:N0}s（判据 ≤ 660s）" -f ($tail.Span / 1000))
Assert 'diag-keeps-newest' ([Math]::Abs($tail.Last - $src.Last) -le 1000) `
    ("导出末条 t={0} / 源末条 t={1}" -f $tail.Last, $src.Last)
$bytes = (Get-Item $logTxt).Length
Assert 'diag-under-400kb' ($bytes -le 400 * 1024) ("log.txt {0:N0} 字节" -f $bytes)
Assert 'diag-dropped-old-lines' ($tail.Lines -lt $src.Lines) ("源 {0} 行 → 导出 {1} 行" -f $src.Lines, $tail.Lines)

Write-Host ''
Write-Host ("RESULT: {0} PASS / {1} FAIL ; 产物 {2}" -f $script:pass, $script:fail, $diagDir)
if ($script:fail -gt 0) { exit 1 }
exit 0
