# M5-06 录制文件列表（轮 32）
#
# 判据（docs/01）：
#   #1 列表按时间倒序；
#   #2 录制中的条目实时更新时长；
#   #3 异常中断的条目标注「可恢复」。
#
# 「可恢复」必须与 `sr-recorder --scan` 同一判定：本用例逐条比对两者的集合，
# 不允许列表自己算一套（两套判定迟早打架）。
#
# Usage: pwsh -File verify/cases/g5_list.ps1 [-BuildDir build-msvc] [-Config Release]

param(
    [string]$BuildDir = 'build-msvc',
    [string]$Config = 'Release'
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot '..\lib\Cleanup.ps1')
. (Join-Path $PSScriptRoot '..\lib\Forensics.ps1')
trap {
    [void](Save-FailureContext -Tag 'g5_list' -WorkDirs $script:SrForensicsWorkDirs -Note $_)
    Clear-SrProcesses -IncludeNameFallback
    Write-Host "FAIL: $_"
    exit 1
}

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$rec = Join-Path $root (Join-Path $BuildDir "bin\$Config\sr-recorder.exe")
if (-not (Test-Path $rec)) { $rec = Join-Path $root (Join-Path $BuildDir 'bin\sr-recorder.exe') }
if (-not (Test-Path $rec)) { Write-Host "FAIL: missing $rec"; exit 2 }
$ui = Join-Path $root (Join-Path $BuildDir "bin\$Config\sr-ui.exe")
if (-not (Test-Path $ui)) { $ui = Join-Path $root (Join-Path $BuildDir 'bin\sr-ui.exe') }
if (-not (Test-Path $ui)) { Write-Host "FAIL: missing $ui"; exit 2 }
$ffbin = Split-Path -Parent (Get-Command ffprobe).Source
$ffprobe = Join-Path $ffbin 'ffprobe.exe'

# 工作目录放 TEMP：Start-Process -ArgumentList 不加引号，工作区路径含空格（D-20）。
$work = Join-Path $env:TEMP 'sr-list-case'
if (Test-Path $work) { Remove-Item $work -Recurse -Force }
New-Item -ItemType Directory -Force -Path $work | Out-Null
foreach ($sub in @('files', 'live')) {
    New-Item -ItemType Directory -Force -Path (Join-Path $work $sub) | Out-Null
}
$script:SrForensicsWorkDirs = @($work)
Get-Process sr-ui, sr-recorder -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue

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
        [void](Save-FailureContext -Tag $Name -WorkDirs @($work) -Note $Detail)
    }
}
function Read-Shared {
    param([string]$Path)
    if (-not (Test-Path $Path)) { return '' }
    try {
        $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open,
                                         [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
        $reader = New-Object System.IO.StreamReader($stream, [System.Text.Encoding]::UTF8)
        $text = $reader.ReadToEnd()
        $reader.Dispose()
        $stream.Dispose()
        return $text
    } catch {
        return ''
    }
}
function Wait-Log {
    param([string]$Path, [string]$Pattern, [int]$TimeoutMs)
    $deadline = (Get-Date).AddMilliseconds($TimeoutMs)
    while ((Get-Date) -lt $deadline) {
        $text = Read-Shared $Path
        if ($text -match $Pattern) { return $text }
        Start-Sleep -Milliseconds 100
    }
    return (Read-Shared $Path)
}
function Get-Duration {
    param([string]$Path)
    $out = & $ffprobe -v error -show_entries format=duration -of default=nw=1:nk=1 $Path 2>&1 | Out-String
    $value = 0.0
    [void][double]::TryParse($out.Trim(), [ref]$value)
    return $value
}
function Record-Clip {
    param([string]$Path, [int]$Seconds)
    $null = Start-Process -FilePath $rec -PassThru -Wait -WindowStyle Hidden `
        -ArgumentList @('--record', '--seconds', "$Seconds", '--out', "`"$Path`"", '--log-level', 'warn')
}
function Get-ListEntries {
    param([string]$Text)
    $rows = @()
    foreach ($line in ($Text -split "`r?`n")) {
        $match = [regex]::Match($line, 'list_entry idx=(\d+) name=(.*?) state=(\w+) size=(\d+) duration_ms=(-?\d+) w=(\d+) h=(\d+)')
        if (-not $match.Success) { continue }
        $rows += [pscustomobject]@{
            Index    = [int]$match.Groups[1].Value
            Name     = $match.Groups[2].Value
            State    = $match.Groups[3].Value
            Size     = [long]$match.Groups[4].Value
            Duration = [long]$match.Groups[5].Value
            Width    = [int]$match.Groups[6].Value
            Height   = [int]$match.Groups[7].Value
        }
    }
    return $rows
}

# ---------------- A 准备素材：3 个成品 + 1 可恢复 .part + 1 不可恢复 .part ----------------
Write-Host '--- A 准备列表素材 ---'
$dir = Join-Path $work 'files'
$fileA = Join-Path $dir 'a.mkv'
$fileB = Join-Path $dir 'b.mkv'
$fileC = Join-Path $dir 'c.mkv'
Record-Clip -Path $fileA -Seconds 2
Start-Sleep -Milliseconds 1100
Record-Clip -Path $fileB -Seconds 3
Start-Sleep -Milliseconds 1100
Record-Clip -Path $fileC -Seconds 1

# 可恢复：拷一个合法文件再追加垃圾（截断点落在 Cluster 中段，等价于崩溃现场）。
$crashPart = Join-Path $dir 'crash.mkv.part'
Copy-Item $fileC $crashPart
$junk = New-Object byte[] 65536
for ($i = 0; $i -lt $junk.Length; $i++) { $junk[$i] = 0xAB }
$stream = [System.IO.File]::Open($crashPart, [System.IO.FileMode]::Append)
$stream.Write($junk, 0, $junk.Length)
$stream.Dispose()
# 不可恢复：完全不是 MKV。
$brokenPart = Join-Path $dir 'broken.mkv.part'
[System.IO.File]::WriteAllText($brokenPart, 'not a matroska file', [System.Text.UTF8Encoding]::new($false))

$scanText = (& $rec --scan $dir 2>&1 | Out-String)
$scanRecoverable = @()
$scanBroken = @()
$current = ''
foreach ($line in ($scanText -split "`r?`n")) {
    if ($line -match '^file=(.+)$') { $current = Split-Path -Leaf $Matches[1] }
    elseif ($line -match '^repairable=1$' -and $current) { $scanRecoverable += $current }
    elseif ($line -match '^repairable=0$' -and $current) { $scanBroken += $current }
}
Assert 'scan-baseline' (($scanRecoverable -contains 'crash.mkv.part') -and ($scanBroken -contains 'broken.mkv.part')) `
    ("scan repairable=[{0}] broken=[{1}]" -f ($scanRecoverable -join ','), ($scanBroken -join ','))

# ---------------- B 列表：倒序 / 时长 / 分辨率 / 状态 ----------------
Write-Host '--- B 列表倒序与元数据 ---'
$ini = Join-Path $work 'list.ini'
$setLog = Join-Path $work 'set.log'
$null = Start-Process -FilePath $ui -PassThru -Wait -WindowStyle Hidden `
    -ArgumentList @('--set-output', $dir, '--config', $ini, '--log', $setLog)
$listLog = Join-Path $work 'list.log'
$listApp = Start-Process -FilePath $ui -PassThru -WindowStyle Hidden `
    -ArgumentList @('--hidden', '--show-list', '--config', $ini, '--log', $listLog, '--exit-after-ms', '6000')
Register-SrProcess $listApp
$null = Wait-Log $listLog 'list_reload ok=1 count=5' 15000
$text = Read-Shared $listLog
$entries = Get-ListEntries $text
Assert 'list-count' ($entries.Count -eq 5) "entries=$($entries.Count)"
Assert 'list-has-window' ($text -match 'list_open dir=') '列表窗口已打开'

$expectedOrder = @($dir | Get-ChildItem -File | Sort-Object LastWriteTime -Descending |
    Select-Object -ExpandProperty Name)
$actualOrder = @($entries | Sort-Object Index | Select-Object -ExpandProperty Name)
Assert 'list-desc-order' (($actualOrder -join ',') -eq ($expectedOrder -join ',')) `
    ("actual=[{0}] expected=[{1}]" -f ($actualOrder -join ','), ($expectedOrder -join ','))
$mtimes = @($dir | Get-ChildItem -File | Sort-Object LastWriteTime -Descending |
    Select-Object -ExpandProperty LastWriteTime)
$descending = $true
for ($i = 1; $i -lt $mtimes.Count; $i++) {
    if ($mtimes[$i] -gt $mtimes[$i - 1]) { $descending = $false }
}
Assert 'list-mtime-descending' $descending "mtime: $(($mtimes | ForEach-Object { $_.ToString('HH:mm:ss.fff') }) -join ' > ')"

foreach ($pair in @(@('a.mkv', $fileA), @('b.mkv', $fileB), @('c.mkv', $fileC))) {
    $row = $entries | Where-Object { $_.Name -eq $pair[0] } | Select-Object -First 1
    $actual = if ($row) { $row.Duration } else { -1 }
    $expected = [int][Math]::Round((Get-Duration $pair[1]) * 1000)
    Assert ("list-duration-{0}" -f $pair[0]) ($row -and [Math]::Abs($actual - $expected) -le 500) `
        ("duration_ms={0} ffprobe={1}" -f $actual, $expected)
    Assert ("list-resolution-{0}" -f $pair[0]) ($row -and $row.Width -eq 1920 -and $row.Height -eq 1080) `
        ("{0}x{1}" -f $(if ($row) { $row.Width } else { 0 }), $(if ($row) { $row.Height } else { 0 }))
    # R5（轮 33）：大小列不能只是「有这一列」，必须与磁盘字节数一致。
    $expectedSize = (Get-Item $pair[1]).Length
    Assert ("list-size-{0}" -f $pair[0]) ($row -and $row.Size -eq $expectedSize) `
        ("size={0} disk={1}" -f $(if ($row) { $row.Size } else { -1 }), $expectedSize)
    Assert ("list-state-{0}" -f $pair[0]) ($row -and $row.State -eq 'complete') `
        ("state={0}" -f $(if ($row) { $row.State } else { 'missing' }))
}

# ---------------- C 可恢复标注 = --scan 的判定 ----------------
Write-Host '--- C 可恢复标注（复用 --scan）---'
$listRecoverable = @($entries | Where-Object { $_.State -eq 'recoverable' } | Select-Object -ExpandProperty Name)
$listBroken = @($entries | Where-Object { $_.State -eq 'broken' } | Select-Object -ExpandProperty Name)
Assert 'list-recoverable-matches-scan' (($listRecoverable -join ',') -eq ($scanRecoverable -join ',')) `
    ("list=[{0}] scan=[{1}]" -f ($listRecoverable -join ','), ($scanRecoverable -join ','))
Assert 'list-broken-matches-scan' (($listBroken -join ',') -eq ($scanBroken -join ',')) `
    ("list=[{0}] scan=[{1}]" -f ($listBroken -join ','), ($scanBroken -join ','))
$crashRow = $entries | Where-Object { $_.Name -eq 'crash.mkv.part' } | Select-Object -First 1
Assert 'list-recoverable-badge' ($crashRow -and $crashRow.State -eq 'recoverable' -and $crashRow.Duration -gt 0) `
    ("crash.mkv.part state={0} duration_ms={1}" -f $(if ($crashRow) { $crashRow.State } else { 'missing' }), $(if ($crashRow) { $crashRow.Duration } else { 0 }))
$listApp.WaitForExit(12000) | Out-Null

# ---------------- D 录制中条目实时更新时长 ----------------
Write-Host '--- D 录制中实时时长 ---'
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class ListNative {
    [DllImport("user32.dll")] public static extern void keybd_event(byte bVk, byte bScan, uint dwFlags, UIntPtr dwExtraInfo);
}
'@
$liveDir = Join-Path $work 'live'
$liveIni = Join-Path $work 'live.ini'
$liveSetLog = Join-Path $work 'live-set.log'
$null = Start-Process -FilePath $ui -PassThru -Wait -WindowStyle Hidden `
    -ArgumentList @('--set-output', $liveDir, '--config', $liveIni, '--log', $liveSetLog)
$liveLog = Join-Path $work 'live.log'
$liveOut = Join-Path $liveDir 'live.mkv'
# 不用 --exit-on-stop：停止后应用还要留在列表里把「已完成」条目刷出来。
$live = Start-Process -FilePath $ui -PassThru -WindowStyle Hidden `
    -ArgumentList @('--hidden', '--show-list', '--config', $liveIni, '--log', $liveLog,
                    '--out', "`"$liveOut`"")
Register-SrProcess $live
$null = Wait-Log $liveLog 'app_ready' 10000
foreach ($key in @(0x11, 0x12, 0x78)) { [ListNative]::keybd_event($key, 0, 0, [UIntPtr]::Zero) }
Start-Sleep -Milliseconds 80
foreach ($key in @(0x78, 0x12, 0x11)) { [ListNative]::keybd_event($key, 0, 2, [UIntPtr]::Zero) }
$null = Wait-Log $liveLog 'recording_indicator' 10000
Start-Sleep -Milliseconds 3500
$liveText = Read-Shared $liveLog
$liveEntries = @(Get-ListEntries $liveText | Where-Object { $_.State -eq 'recording' })
Assert 'list-recording-row' ($liveEntries.Count -ge 1) "recording rows=$($liveEntries.Count)"
$recName = if ($liveEntries.Count -ge 1) { $liveEntries[-1].Name } else { '' }
Assert 'list-recording-name' ($recName -eq 'live.mkv.part') "name=$recName"
$liveDurations = @([regex]::Matches($liveText, 'list_live name=live\.mkv\.part duration_ms=(\d+)') |
    ForEach-Object { [long]$_.Groups[1].Value })
$liveGrowth = if ($liveDurations.Count -ge 2) { $liveDurations[-1] - $liveDurations[0] } else { -1 }
Assert 'list-live-updates' ($liveDurations.Count -ge 2 -and $liveGrowth -ge 1000) `
    ("samples={0} first={1} last={2} delta={3}ms" -f $liveDurations.Count, $liveDurations[0], $liveDurations[-1], $liveGrowth)
$recorderPid = if ($liveText -match 'spawn pid=(\d+)') { [int]$Matches[1] } else { 0 }
$partOnDisk = Join-Path $liveDir 'live.mkv.part'
Assert 'list-live-part-exists' (Test-Path $partOnDisk) "part=$partOnDisk"
foreach ($key in @(0x11, 0x12, 0x7A)) { [ListNative]::keybd_event($key, 0, 0, [UIntPtr]::Zero) }
Start-Sleep -Milliseconds 80
foreach ($key in @(0x7A, 0x12, 0x11)) { [ListNative]::keybd_event($key, 0, 2, [UIntPtr]::Zero) }
$null = Wait-Log $liveLog 'list_reload ok=1 count=1 active=0' 15000
$afterText = Read-Shared $liveLog
$afterRows = @(Get-ListEntries $afterText | Where-Object { $_.State -eq 'complete' -and $_.Name -eq 'live.mkv' })
Assert 'list-after-stop' ($afterRows.Count -ge 1) "rows=$($afterRows.Count)"
if ($afterRows.Count -ge 1) {
    Assert 'list-after-stop-duration' ($afterRows[-1].Duration -ge 2000) "duration_ms=$($afterRows[-1].Duration)"
}
Assert 'list-no-leftover-recorder' ($recorderPid -gt 0 -and -not (Get-Process -Id $recorderPid -ErrorAction SilentlyContinue)) "pid=$recorderPid"
[void][System.Diagnostics.Process]::GetProcessById($live.Id).CloseMainWindow()
Start-Sleep -Milliseconds 800
if (-not $live.HasExited) { Stop-Process -Id $live.Id -Force -ErrorAction SilentlyContinue }

Write-Host ''
Write-Host ("RESULT: {0} PASS / {1} FAIL" -f $script:pass, $script:fail)
if ($script:fail -gt 0) { exit 1 }
exit 0
