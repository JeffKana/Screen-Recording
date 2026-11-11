# G5 可观测性用例（轮 30 / T-17）：
#   A M6-03 三项指标：recorder 侧 1Hz 采样 + 丢帧注入 + UI 显示（数值来自 recorder）
#   B M8-14 诊断包：四类产物 / 无画面音频 / <5MB / ≤2s / 用户名脱敏
#   C 日志分级：debug > info > warn；--log-level 与 INI 都可切换
#   D 覆盖表三态：--self-test 证明「有卡未实现」不再被算作已实现；C8 仍 PASS
#   E 统一清理：人为制造异常后 sr-ui / sr-recorder 残留 = 0
#
# Usage: pwsh -File verify/cases/g5_diag.ps1 [-BuildDir build-msvc] [-Config Release]

param(
    [string]$BuildDir = 'build-msvc',
    [string]$Config = 'Release'
)

$ErrorActionPreference = 'Stop'

# 轮 30（F2）：统一异常清理——登记 spawn 进程 + trap 兜底，中途抛异常也不留孤儿。
. (Join-Path $PSScriptRoot '..\lib\Cleanup.ps1')
# trap 必须留在脚本自身作用域：dot-source 进来的 trap 不接管调用方的终止错误。
trap { Clear-SrProcesses -IncludeNameFallback; Write-Host "FAIL: $_"; exit 1 }

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
function Resolve-Exe([string]$name) {
    $a = Join-Path $root (Join-Path $BuildDir "bin\$Config\$name")
    if (Test-Path $a) { return $a }
    return Join-Path $root (Join-Path $BuildDir "bin\$name")
}
$ui = Resolve-Exe 'sr-ui.exe'
$rec = Resolve-Exe 'sr-recorder.exe'
if (-not (Test-Path $ui)) { Write-Host "FAIL: missing $ui"; exit 2 }
if (-not (Test-Path $rec)) { Write-Host "FAIL: missing $rec"; exit 2 }

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
    }
}
function Skip {
    param([string]$Name, [string]$Reason)
    $script:skip++
    Write-Host ("  SKIP {0} — {1}" -f $Name, $Reason)
}
function Read-Log {
    param([string]$Path)
    return (Read-TextSafe $Path)
}
function Wait-Log {
    param([string]$Path, [string]$Pattern, [int]$TimeoutMs = 8000)
    $deadline = (Get-Date).AddMilliseconds($TimeoutMs)
    while ((Get-Date) -lt $deadline) {
        $text = Read-Log $Path
        if ($text -match $Pattern) { return $text }
        Start-Sleep -Milliseconds 100
    }
    return (Read-Log $Path)
}
function Parse-LogLines {
    param([string]$Text)
    $out = @()
    foreach ($line in ($Text -split "`r?`n")) {
        $m = [regex]::Match($line, '^t=(\d+) (.*)$')
        if ($m.Success) {
            $out += [pscustomobject]@{ T = [long]$m.Groups[1].Value; Text = $m.Groups[2].Value }
        }
    }
    return $out
}
function Field {
    param([string]$Text, [string]$Key)
    if ($Text -match "(?m)^$Key=(.*)$") { return $Matches[1].Trim() }
    return ''
}
function Press-Combo {
    param([byte[]]$Keys)
    foreach ($key in $Keys) { [UiDiagNative]::keybd_event($key, 0, 0, [UIntPtr]::Zero) }
    Start-Sleep -Milliseconds 80
    for ($i = $Keys.Count - 1; $i -ge 0; $i--) {
        [UiDiagNative]::keybd_event($Keys[$i], 0, 2, [UIntPtr]::Zero)
    }
    Start-Sleep -Milliseconds 80
}
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class UiDiagNative {
    [DllImport("user32.dll")] public static extern void keybd_event(byte bVk, byte bScan, uint dwFlags, UIntPtr dwExtraInfo);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindowW(string lpClassName, IntPtr lpWindowName);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr hDlg, int nIDDlgItem);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr hWnd, System.Text.StringBuilder lpString, int nMaxCount);
}
'@
function Get-StatusText {
    $main = [UiDiagNative]::FindWindowW('SrUiMainWindow', [IntPtr]::Zero)
    if ($main -eq [IntPtr]::Zero) { return '' }
    $sb = New-Object System.Text.StringBuilder 2048
    [void][UiDiagNative]::GetWindowTextW([UiDiagNative]::GetDlgItem($main, 200), $sb, 2048)
    return $sb.ToString()
}

$work = Join-Path $env:TEMP ('sr-g5-diag-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $work | Out-Null
Get-Process sr-ui, sr-recorder -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue

$versionText = (& $rec --version | Out-String)
$testHooks = $versionText -match 'test_hooks=1'

# ---------------- A M6-03 三项指标 ----------------
Write-Host '--- A 三项指标（采集 / 编码 / 丢帧）---'
$recLog = Join-Path $work 'rec.log'
$recOut = Join-Path $work 'rec.out'
$fileA = Join-Path $work 'metrics.mkv'
$recArgs = @('--record', '--seconds', '6', '--fps', '30', '--out', "`"$fileA`"",
             '--log-file', "`"$recLog`"", '--log-level', 'info')
if ($testHooks) { $recArgs += @('--inject-drop-every', '10') }
$pRec = Start-Process -FilePath $rec -PassThru -WindowStyle Hidden -RedirectStandardOutput $recOut `
    -ArgumentList $recArgs
Register-SrProcess $pRec
$recExited = $pRec.WaitForExit(90000)
if (-not $recExited) { Stop-SrProcess $pRec }
$recText = Read-Log $recLog
$recStdout = Read-Log $recOut
$lines = @(Parse-LogLines $recText)
$metrics = @($lines | Where-Object { $_.Text -match '^metrics ' })
Assert 'metrics-samples' ($metrics.Count -ge 3) "samples=$($metrics.Count)"
$deltas = @()
for ($i = 1; $i -lt $metrics.Count; $i++) { $deltas += ($metrics[$i].T - $metrics[$i - 1].T) }
$badDelta = @($deltas | Where-Object { $_ -lt 700 -or $_ -gt 1400 })
Assert 'metrics-1hz-refresh' ($deltas.Count -ge 2 -and $badDelta.Count -eq 0) `
    "deltas=$(($deltas -join ','))ms"

$encodeValues = @()
$captureValues = @()
$droppedValues = @()
foreach ($item in $metrics) {
    if ($item.Text -match 'capture_fps=([\d\.]+)') { $captureValues += [double]$Matches[1] }
    if ($item.Text -match 'encode_fps=([\d\.]+)') { $encodeValues += [double]$Matches[1] }
    if ($item.Text -match 'dropped=(\d+)') { $droppedValues += [long]$Matches[1] }
}
$lastEncode = if ($encodeValues.Count -gt 0) { $encodeValues[-1] } else { -1 }
Assert 'metrics-encode-fps' ($lastEncode -ge 15 -and $lastEncode -le 40) "encode_fps=$lastEncode"
Assert 'metrics-values-from-recorder' ($recText -match 'metrics capture_fps=[\d\.]+ encode_fps=[\d\.]+ dropped=\d+ captured=\d+') '日志行含三项指标与累计采集数'

if ($testHooks) {
    $finalDropped = if ($droppedValues.Count -gt 0) { $droppedValues[-1] } else { 0 }
    Assert 'metrics-drop-injected' ($finalDropped -gt 0) "dropped=$finalDropped"
    Assert 'metrics-drop-logged' ($recText -match 'dropped=[1-9]\d*') '日志出现 dropped>0'
    $summaryDropped = [long](Field $recStdout 'frames_dropped')
    Assert 'metrics-drop-summary' ($summaryDropped -gt 0) "frames_dropped=$summaryDropped"
} else {
    Skip 'metrics-drop-injected' 'SR_TEST_HOOKS=OFF（无 --inject-drop-every）'
    Skip 'metrics-drop-logged' 'SR_TEST_HOOKS=OFF'
    Skip 'metrics-drop-summary' 'SR_TEST_HOOKS=OFF'
}
$capturedSummary = [long](Field $recStdout 'frames_captured')
Assert 'metrics-captured-summary' ($capturedSummary -gt 0) "frames_captured=$capturedSummary"
Assert 'metrics-record-exit' ([int](Field $recStdout 'record_exit') -eq 0) "record_exit=$(Field $recStdout 'record_exit')"

# UI 侧：状态栏必须显示同一组指标（值由 recorder 快照下发，UI 不推算）
Write-Host '--- A2 UI 显示（状态栏 + 快照日志）---'
$uiLog = Join-Path $work 'ui.log'
$uiOutFile = Join-Path $work 'ui.mkv'
$ini = Join-Path $work 'config.ini'
$uiProc = Start-Process -FilePath $ui -PassThru -WindowStyle Hidden `
    -ArgumentList @('--hidden', '--config', $ini, '--out', $uiOutFile, '--log', $uiLog, '--exit-on-stop')
Register-SrProcess $uiProc
$null = Wait-Log $uiLog 'app_ready' 10000
Press-Combo @(0x11, 0x12, 0x78)   # Ctrl+Alt+F9 start
$null = Wait-Log $uiLog 'recording_indicator' 8000
Start-Sleep -Milliseconds 2200
$status = Get-StatusText
Assert 'ui-metrics-status' ($status -match '采集 [\d\.]+ / 编码 [\d\.]+ fps') "status=$status"
Assert 'ui-metrics-dropped' ($status -match '丢帧 \d+') "status=$status"
$uiText = Read-Log $uiLog
Assert 'ui-metrics-from-recorder' ($uiText -match 'snapshot state=Recording .*capture_fps=[\d\.]+ encode_fps=[\d\.]+ dropped=\d+') `
    'snapshot 行带三项指标'
Press-Combo @(0x11, 0x12, 0x7A)   # Ctrl+Alt+F11 stop
$uiExited = $uiProc.WaitForExit(20000)
if (-not $uiExited) { Stop-SrProcess $uiProc }
Assert 'ui-clean-exit' ($uiExited -and $uiProc.ExitCode -eq 0) "exit=$($uiProc.ExitCode)"

# ---------------- B M8-14 诊断包 ----------------
Write-Host '--- B 诊断包（四类产物 / 无画面 / <5MB / ≤2s）---'
$diagRoot = Join-Path $work 'diag'
New-Item -ItemType Directory -Force -Path $diagRoot | Out-Null
$diagLog = Join-Path $work 'diag.log'
$diagProc = Start-Process -FilePath $ui -PassThru -Wait -WindowStyle Hidden `
    -ArgumentList @('--export-diag', $diagRoot, '--log', $diagLog, '--config', $ini)
Register-SrProcess $diagProc
Assert 'diag-exit' ($diagProc.ExitCode -eq 0) "exit=$($diagProc.ExitCode)"
$diagText = Read-Log $diagLog
$pack = Get-ChildItem $diagRoot -Directory -ErrorAction SilentlyContinue | Select-Object -First 1
Assert 'diag-directory' ($null -ne $pack) "dir=$diagRoot"
if ($null -ne $pack) {
    $files = @(Get-ChildItem $pack.FullName -File)
    $names = @($files | ForEach-Object { $_.Name })
    foreach ($required in 'version.txt', 'gpu.txt', 'log.txt', 'config.ini') {
        Assert "diag-file-$required" ($names -contains $required) ($names -join ',')
    }
    $totalBytes = ($files | Measure-Object Length -Sum).Sum
    Assert 'diag-size-5mb' ($totalBytes -lt 5 * 1024 * 1024) "bytes=$totalBytes"
    $match = [regex]::Match($diagText, 'diag_pack ok=1 files=(\d+) bytes=(\d+) ms=(\d+)')
    Assert 'diag-log-elapsed-2s' ($match.Success -and [int]$match.Groups[3].Value -le 2000) `
        "log=$($match.Value)"
    $binaryHits = @()
    foreach ($file in $files) {
        $bytes = [System.IO.File]::ReadAllBytes($file.FullName)
        $text = [System.Text.Encoding]::ASCII.GetString($bytes)
        foreach ($marker in 'PNG', 'JFIF', 'ftyp', 'ID3', 'RIFF', 'ELF') {
            if ($text.Contains($marker)) { $binaryHits += "$($file.Name):$marker" }
        }
    }
    Assert 'diag-no-media' ($binaryHits.Count -eq 0) "hits=$($binaryHits -join ',')"
    $allText = ''
    foreach ($file in $files) { $allText += (Read-Log $file.FullName) }
    $userName = $env:USERNAME
    Assert 'diag-user-redacted' (-not ($userName -and $allText -match [regex]::Escape($userName))) '路径用户名已脱敏'
    $version = Read-Log (Join-Path $pack.FullName 'version.txt')
    Assert 'diag-version-content' (($version -match '(?m)^version=') -and ($version -match '(?m)^test_hooks=\d')) `
        'version.txt 含版本与构建开关'
    $config = Read-Log (Join-Path $pack.FullName 'config.ini')
    Assert 'diag-config-session' ($config -match '\[session\]') 'config.ini 含会话快照'

    # 第二个入口：recorder 也能独立导出（故障后 UI 不在时用）
    $diagRoot2 = Join-Path $work 'diag-rec'
    New-Item -ItemType Directory -Force -Path $diagRoot2 | Out-Null
    $recDiagOut = Join-Path $work 'rec-diag.out'
    $recDiag = Start-Process -FilePath $rec -PassThru -Wait -WindowStyle Hidden -RedirectStandardOutput $recDiagOut `
        -ArgumentList @('--export-diag', $diagRoot2, '--config', $ini)
    Register-SrProcess $recDiag
    $recDiagText = Read-Log $recDiagOut
    $pack2 = Get-ChildItem $diagRoot2 -Directory -ErrorAction SilentlyContinue | Select-Object -First 1
    $files2 = if ($pack2) { @(Get-ChildItem $pack2.FullName -File) } else { @() }
    Assert 'diag-recorder-entry' ($recDiag.ExitCode -eq 0 -and $files2.Count -eq 4 -and
                                  (Read-Log $recDiagOut) -match 'diag_pack=ok') `
        "exit=$($recDiag.ExitCode) files=$($files2.Count)"
}

# ---------------- C 日志分级 ----------------
Write-Host '--- C 日志分级（debug > info > warn）---'
$levelCounts = @{}
foreach ($level in 'debug', 'info', 'warn') {
    $logPath = Join-Path $work "level-$level.log"
    $outPath = Join-Path $work "level-$level.out"
    $filePath = Join-Path $work "level-$level.mkv"
    $proc = Start-Process -FilePath $rec -PassThru -WindowStyle Hidden -RedirectStandardOutput $outPath `
        -ArgumentList @('--record', '--seconds', '3', '--fps', '30', '--out', "`"$filePath`"",
                        '--log-level', $level, '--log-file', "`"$logPath`"")
    Register-SrProcess $proc
    $null = $proc.WaitForExit(60000)
    $levelCounts[$level] = @(Parse-LogLines (Read-Log $logPath)).Count
}
Assert 'log-level-gradient' ($levelCounts['debug'] -gt $levelCounts['info'] -and
                             $levelCounts['info'] -gt $levelCounts['warn']) `
    "debug=$($levelCounts['debug']) info=$($levelCounts['info']) warn=$($levelCounts['warn'])"
Assert 'log-debug-lines' ((Read-Log (Join-Path $work 'level-debug.log')) -match ' submit frame=') 'debug 才有帧级行'
Assert 'log-warn-quiet' ((Read-Log (Join-Path $work 'level-warn.log')) -notmatch 'metrics ') 'warn 下无 info 指标行'

# INI [log] level=debug 生效（不传 --log-level）
$iniDebug = Join-Path $work 'debug.ini'
$iniText = "[hotkeys]`r`nstart=Ctrl+Alt+F9`r`npause=Ctrl+Alt+F10`r`nstop=Ctrl+Alt+F11`r`n`r`n[log]`r`nlevel=debug`r`n"
[System.IO.File]::WriteAllText($iniDebug, $iniText, [System.Text.UTF8Encoding]::new($false))
$iniLog = Join-Path $work 'ini-debug.log'
$iniProc = Start-Process -FilePath $ui -PassThru -Wait -WindowStyle Hidden `
    -ArgumentList @('--hidden', '--exit-after-ms', '1500', '--config', $iniDebug, '--log', $iniLog)
Register-SrProcess $iniProc
Assert 'log-ini-level' ((Read-Log $iniLog) -match '\[debug\]') 'INI [log] level=debug 生效'
$warnLog = Join-Path $work 'ui-warn.log'
$warnProc = Start-Process -FilePath $ui -PassThru -Wait -WindowStyle Hidden `
    -ArgumentList @('--hidden', '--exit-after-ms', '1200', '--config', $ini, '--log', $warnLog, '--log-level', 'warn')
Register-SrProcess $warnProc
Assert 'log-cli-level-warn' ((Read-Log $warnLog) -notmatch 'app_ready') '--log-level warn 压掉 info 行'

# ---------------- D 覆盖表三态 + C8 ----------------
Write-Host '--- D 覆盖表三态（已实现 / 有卡未实现 / 无卡）---'
$py = 'python'
$selfTest = & $py (Join-Path $root 'verify/tools/coverage_report.py') --self-test 2>&1 | Out-String
Assert 'coverage-self-test' (($LASTEXITCODE -eq 0) -and ($selfTest -match 'self_test ok')) $selfTest.Trim()
$check = & $py (Join-Path $root 'verify/tools/coverage_report.py') --check 2>&1 | Out-String
Assert 'coverage-up-to-date' (($LASTEXITCODE -eq 0) -and ($check -match 'up to date')) $check.Trim()
$contract = & $py (Join-Path $root 'verify/tools/contract_check.py') 2>&1 | Out-String
Assert 'coverage-c8' ($contract -match '\[PASS\] C8 ') 'C8 要求「有卡未实现 + 无卡」逐条挂账'

# ---------------- E 统一清理（F2） ----------------
Write-Host '--- E 人为异常后无残留 ---'
$probe = Join-Path $work 'cleanup-probe.ps1'
$cleanupLib = Join-Path $root 'verify/lib/Cleanup.ps1'
$probeText = @"
`$ErrorActionPreference = 'Stop'
. '$cleanupLib'
trap { Clear-SrProcesses -IncludeNameFallback; Write-Host "FAIL: `$_"; exit 1 }
`$p = Start-Process -FilePath '$ui' -PassThru -WindowStyle Hidden -ArgumentList @('--hidden','--exit-after-ms','30000','--log',(Join-Path '$work' 'probe.log'))
Register-SrProcess `$p
Start-Sleep -Milliseconds 700
throw 'induced failure'
"@
[System.IO.File]::WriteAllText($probe, $probeText, [System.Text.UTF8Encoding]::new($false))
$probeOut = & pwsh -NoProfile -File $probe 2>&1 | Out-String
Start-Sleep -Milliseconds 600
$leftover = @(Get-Process sr-ui, sr-recorder -ErrorAction SilentlyContinue)
Assert 'cleanup-trap-fired' ($probeOut -match 'FAIL: induced failure') $probeOut.Trim()
Assert 'cleanup-no-leftover' ($leftover.Count -eq 0) "leftover=$($leftover.Count)"

Clear-SrProcesses -IncludeNameFallback
Write-Host ''
Write-Host ("RESULT: {0} PASS / {1} FAIL / {2} SKIP" -f $script:pass, $script:fail, $script:skip)
if ($script:fail -gt 0) { exit 1 }
exit 0
