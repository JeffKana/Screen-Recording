# G1 acceptance case: full state-machine walk + cross-process handshake
#   + illegal-command rejection + single-instance guard
# Covers tasks/G1/S1-001.md (M2-10) and S1-002.md (M2-01 / M2-12).
# Usage: pwsh -File verify/cases/g1_walk.ps1 [-BuildDir build-mingw]
#
# NOTE: srctl prints TWO snapshots per invocation: the one sent right after
# handshake (state BEFORE the command) and the one after the command. Assertions
# must read the LAST snapshot, otherwise every step looks like "no transition".

param(
    [string]$BuildDir = 'build-mingw',
    [string]$Config = 'Release'
)

$ErrorActionPreference = 'Stop'

# 轮 30（F2）：统一异常清理——登记 spawn 进程 + trap 兜底，中途抛异常也不留孤儿。
. (Join-Path $PSScriptRoot '..\lib\Cleanup.ps1')
# trap 必须留在脚本自身作用域：dot-source 进来的 trap 不接管调用方的终止错误。
trap { Clear-SrProcesses -IncludeNameFallback; Write-Host "FAIL: $_"; exit 1 }

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$exe = Join-Path $root (Join-Path $BuildDir "bin\$Config\sr-recorder.exe")
$ctl = Join-Path $root (Join-Path $BuildDir "bin\$Config\srctl.exe")
if (-not (Test-Path $exe)) { $exe = Join-Path $root (Join-Path $BuildDir 'bin\sr-recorder.exe') }
if (-not (Test-Path $ctl)) { $ctl = Join-Path $root (Join-Path $BuildDir 'bin\srctl.exe') }
$log = Join-Path $env:TEMP 'sr-g1-walk.log'
$secondLog = Join-Path $env:TEMP 'sr-g1-second.log'

if (-not (Test-Path $exe)) { Write-Host "FAIL: missing $exe"; exit 2 }
if (-not (Test-Path $ctl)) { Write-Host "FAIL: missing $ctl"; exit 2 }

Remove-Item $log -ErrorAction SilentlyContinue
Remove-Item $secondLog -ErrorAction SilentlyContinue

$proc = Start-Process -FilePath $exe -ArgumentList '--token', 'dev' `
    -RedirectStandardOutput $log -PassThru -WindowStyle Hidden
Register-SrProcess $proc

$pipe = $null
for ($i = 0; $i -lt 50; $i++) {
    Start-Sleep -Milliseconds 100
    if (Test-Path $log) {
        $match = Select-String -Path $log -Pattern '^pipe=(.+)$' -ErrorAction SilentlyContinue
        if ($match) { $pipe = $match.Matches[0].Groups[1].Value.Trim(); break }
    }
}
if (-not $pipe) {
    Write-Host 'FAIL: recorder did not publish a pipe name'
    if (-not $proc.HasExited) { $proc.Kill() }
    exit 3
}
Write-Host "recorder pid=$($proc.Id) pipe=$pipe"

$script:pass = 0
$script:fail = 0

function Step {
    param([string]$Command, [string]$Expect, [switch]$ExpectRejected)

    $out = & $ctl --pipe $pipe --token dev $Command 2>&1 | Out-String
    $matches2 = [regex]::Matches($out, 'state=(\w+)')
    if ($matches2.Count -eq 0) {
        $state = 'NONE'
    } else {
        $state = $matches2[$matches2.Count - 1].Groups[1].Value
    }
    $rejected = $out -match 'rejected:'

    $ok = ($state -eq $Expect) -and ($rejected -eq [bool]$ExpectRejected)
    if ($ok) {
        $script:pass++
        Write-Host ("  PASS {0,-16} -> {1}" -f $Command, $state)
    } else {
        $script:fail++
        Write-Host ("  FAIL {0,-16} -> {1} (expect {2}, rejected={3})" -f $Command, $state, $Expect, $rejected)
        Write-Host "       raw: $($out.Trim())"
    }
}

Write-Host '--- T01/T03: Idle -> Preflight -> Idle ---'
Step 'start' 'Preflight'
Step 'preflight-fail' 'Idle'

Write-Host '--- T01/T02/T05: countdown then cancel ---'
Step 'start' 'Preflight'
Step 'preflight-ok' 'Countdown'
Step 'cancel' 'Idle'

Write-Host '--- illegal command: resume in Idle must be rejected, state unchanged ---'
Step 'resume' 'Idle' -ExpectRejected

Write-Host '--- T04/T06/T07: record, pause, resume ---'
Step 'start' 'Preflight'
Step 'preflight-ok' 'Countdown'
Step 'countdown' 'Recording'
Step 'pause' 'Paused'
Step 'resume' 'Recording'

Write-Host '--- T11/T12: recover ok ---'
Step 'recover' 'Recovering'
Step 'recover-ok' 'Recording'

Write-Host '--- T11/T13/T10: recover fail -> stopping ---'
Step 'recover' 'Recovering'
Step 'recover-fail' 'Stopping'
Step 'finalize-done' 'Idle'

Write-Host '--- T08/T10: stop while recording ---'
Step 'start' 'Preflight'
Step 'preflight-ok' 'Countdown'
Step 'countdown' 'Recording'
Step 'stop' 'Stopping'
Step 'finalize-done' 'Idle'

Write-Host '--- T09: stop while paused ---'
Step 'start' 'Preflight'
Step 'preflight-ok' 'Countdown'
Step 'countdown' 'Recording'
Step 'pause' 'Paused'
Step 'stop' 'Stopping'
Step 'finalize-done' 'Idle'

Write-Host '--- T14/T15: crash and reset ---'
Step 'start' 'Preflight'
Step 'preflight-ok' 'Countdown'
Step 'countdown' 'Recording'
Step 'crash' 'Crashed'
Step 'reset' 'Idle'

Write-Host '--- M2-10: duplicate start while recording must be idempotent ---'
Step 'start' 'Preflight'
Step 'preflight-ok' 'Countdown'
Step 'countdown' 'Recording'
Step 'start' 'Recording'
Step 'stop' 'Stopping'
Step 'finalize-done' 'Idle'

Write-Host '--- M2-12: second instance must exit and report existing pid ---'
$second = Start-Process -FilePath $exe -ArgumentList '--token', 'dev' `
    -RedirectStandardOutput $secondLog -PassThru -Wait -WindowStyle Hidden
$second.Refresh()
$secondOut = if (Test-Path $secondLog) { Get-Content $secondLog -Raw } else { '' }
if ($second.ExitCode -eq 4 -and $secondOut -match 'already running, pid=\d+') {
    $script:pass++
    Write-Host ("  PASS second instance exit={0} msg={1}" -f $second.ExitCode, $secondOut.Trim())
} else {
    $script:fail++
    Write-Host ("  FAIL second instance exit={0} out={1}" -f $second.ExitCode, $secondOut.Trim())
}

Write-Host '--- graceful shutdown ---'
& $ctl --pipe $pipe --token dev shutdown | Out-Null
$exited = $proc.WaitForExit(5000)
if ($exited) {
    $proc.WaitForExit()
    $proc.Refresh()
    if ($proc.ExitCode -eq 0) {
        $script:pass++
        Write-Host '  PASS shutdown exit=0'
    } else {
        $script:fail++
        Write-Host "  FAIL shutdown exit=$($proc.ExitCode)"
    }
} else {
    $script:fail++
    Write-Host '  FAIL shutdown timeout'
    $proc.Kill()
}

Write-Host ''
Write-Host ("RESULT: {0} PASS / {1} FAIL" -f $script:pass, $script:fail)
if ($script:fail -gt 0) { exit 1 }
exit 0
