# 轮 52 P0-3：`F3-42`（recorder 静默退出）定向复现 —— 三场景 × N 次
#
# 背景（`docs/26 §3.1`）：轮 42 出现一次「recorder 静默退出」——`.part` 只有 108B（仅容器头）、
# stdout 空、无残留进程；此后 8 轮未复现。验收报告-轮51 §R1 裁定：轮 52 做一次定向复现，
# 三个场景各 10 次，任一次复现即升 Blocker 并出缺陷单（附 Forensics 现场）；
# 30 次全不出现 → 在 `docs/26` 写「定向复现 30 次未出现」，级别保持「未定性观察」。
#
# 三个场景（`.codebuddy/plans/轮52指令.md` §P0-3）：
#   ① 两个用例并发：两个 recorder 同时开录，各自不同 --out；
#   ② 抢单实例锁：A 先持锁开录，B 在 0.2s 后启动（应当打印 already running 后退出，不影响 A）；
#   ③ 抢占同一输出路径：两个 recorder 同时用同一个 --out。
#
# 判定「静默退出」的口径（可复核）：
#   * 退出码非 0 **且** stdout 里没有 `record_exit=`（正常收尾必打印）**且** 输出文本几乎为空；或
#   * 留下 `.part` 但大小 < 4KB（只有容器头、没有数据）且没有成品。
#
# Usage:
#   pwsh -File verify/cases/g5_f342_repro.ps1 [-Iterations 10] [-BuildDir build-msvc] [-Seconds 4]

param(
    [int]$Iterations = 10,
    [string]$BuildDir = 'build-msvc',
    [string]$Config = 'Release',
    [int]$Seconds = 4,
    [int]$SecondDelayMs = 200
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\lib\Cleanup.ps1')
. (Join-Path $PSScriptRoot '..\lib\Forensics.ps1')
trap {
    [void](Save-FailureContext -Tag 'g5_f342' -WorkDirs @($script:work) -Note $_)
    Clear-SrProcesses -IncludeNameFallback
    Write-Host "FAIL: $_"
    exit 1
}

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$rec = Join-Path $root (Join-Path $BuildDir "bin\$Config\sr-recorder.exe")
if (-not (Test-Path $rec)) { $rec = Join-Path $root (Join-Path $BuildDir 'bin\sr-recorder.exe') }
if (-not (Test-Path $rec)) { Write-Host 'FAIL: sr-recorder.exe missing'; exit 2 }
$ffprobe = (Get-Command ffprobe -ErrorAction SilentlyContinue).Source
Write-Host ("exe={0} sha256={1}" -f $rec, (Get-FileHash $rec -Algorithm SHA256).Hash)

$script:work = Join-Path $root ('verify/artifacts/f342-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Force -Path $script:work | Out-Null
$script:SrForensicsWorkDirs = @($script:work)
Write-Host ("工作目录：{0}（每轮 3 场景 × {1} 次）" -f $script:work, $Iterations)

function Read-TextSafe {
    param([string]$Path)
    if (-not (Test-Path $Path)) { return '' }
    return [System.IO.File]::ReadAllText($Path)
}
function Start-Recorder {
    param([string]$Tag, [string]$Output)
    $stdout = Join-Path $script:work ($Tag + '.out')
    $proc = Start-Process -FilePath $rec -PassThru -WindowStyle Hidden `
        -WorkingDirectory $script:work -RedirectStandardOutput $stdout `
        -RedirectStandardError ($stdout + '.err') `
        -ArgumentList @('--record', '--seconds', "$Seconds", '--fps', '30', '--bitrate', '1500',
            '--out', "`"$Output`"")
    return @{ Proc = $proc; Stdout = $stdout; Output = $Output }
}
function Wait-Record {
    param($Handle, [int]$TimeoutMs = 60000)
    $finished = $Handle.Proc.WaitForExit($TimeoutMs)
    if (-not $finished) {
        $Handle.Proc | Stop-Process -Force -ErrorAction SilentlyContinue
        $Handle.Proc.WaitForExit(5000) | Out-Null
    }
    $text = (Read-TextSafe $Handle.Stdout) + (Read-TextSafe ($Handle.Stdout + '.err'))
    $part = $Handle.Output + '.part'
    $partBytes = if (Test-Path $part) { (Get-Item $part).Length } else { 0 }
    $outBytes = if (Test-Path $Handle.Output) { (Get-Item $Handle.Output).Length } else { 0 }
    $playable = $false
    if ($outBytes -gt 0 -and $ffprobe) {
        $probe = & $ffprobe -v error -show_entries format=duration -of default=nw=1:nk=1 $Handle.Output 2>&1 | Out-String
        $duration = 0.0
        $playable = [double]::TryParse($probe.Trim(), [ref]$duration) -and $duration -gt 0
    }
    $exit = if ($finished) { $Handle.Proc.ExitCode } else { -1 }
    $silent = (-not $finished) -or
              (($exit -ne 0) -and ($text -notmatch 'record_exit=') -and ($text.Trim().Length -lt 32)) -or
              (($partBytes -gt 0) -and ($partBytes -lt 4096) -and ($outBytes -eq 0))
    return [pscustomobject]@{
        exit = $exit
        exited = $finished
        stdout_bytes = $text.Length
        has_record_exit = [bool]($text -match 'record_exit=')
        part_bytes = $partBytes
        out_bytes = $outBytes
        playable = $playable
        silent = $silent
    }
}

$rows = @()
$reproductions = 0

for ($i = 1; $i -le $Iterations; $i++) {
    # ---- 场景 ① 两个用例并发（不同输出）----
    $a = Start-Recorder -Tag ("s1-{0}-a" -f $i) -Output (Join-Path $script:work ("s1-{0}-a.mkv" -f $i))
    $b = Start-Recorder -Tag ("s1-{0}-b" -f $i) -Output (Join-Path $script:work ("s1-{0}-b.mkv" -f $i))
    $ra = Wait-Record $a
    $rb = Wait-Record $b
    foreach ($pair in @(@('s1-a', $ra), @('s1-b', $rb))) {
        $rows += [pscustomobject]@{ iter = $i; scene = $pair[0]; exit = $pair[1].exit
            exited = $pair[1].exited; stdout_bytes = $pair[1].stdout_bytes
            has_record_exit = $pair[1].has_record_exit; part_bytes = $pair[1].part_bytes
            out_bytes = $pair[1].out_bytes; playable = $pair[1].playable; silent = $pair[1].silent
            note = '' }
        if ($pair[1].silent) { $reproductions++ }
    }

    # ---- 场景 ② 抢单实例锁（服务态：`--record` 路径不取锁，锁只在服务态取）----
    # A 起服务态实例（持锁），B 在 SecondDelayMs 后起第二个 → 必须打印 already running 并 exit 4。
    $aStdout = Join-Path $script:work ("s2-{0}-a.out" -f $i)
    $aProc = Start-Process -FilePath $rec -PassThru -WindowStyle Hidden -WorkingDirectory $script:work `
        -RedirectStandardOutput $aStdout -RedirectStandardError ($aStdout + '.err') -ArgumentList @()
    Start-Sleep -Milliseconds $SecondDelayMs
    $bStdout = Join-Path $script:work ("s2-{0}-b.out" -f $i)
    $bProc = Start-Process -FilePath $rec -PassThru -WindowStyle Hidden -WorkingDirectory $script:work `
        -RedirectStandardOutput $bStdout -RedirectStandardError ($bStdout + '.err') -ArgumentList @()
    $bExited = $bProc.WaitForExit(10000)
    if (-not $bExited) { $bProc | Stop-Process -Force -ErrorAction SilentlyContinue }
    $bText = (Read-TextSafe $bStdout) + (Read-TextSafe ($bStdout + '.err'))
    $bExit = if ($bExited) { $bProc.ExitCode } else { -1 }
    if (-not $aProc.HasExited) { $aProc | Stop-Process -Force -ErrorAction SilentlyContinue }
    $aProc.WaitForExit(5000) | Out-Null
    $lockRejected = [bool]($bText -match 'already running')
    $rows += [pscustomobject]@{ iter = $i; scene = 's2-b(lock)'; exit = $bExit
        exited = $bExited; stdout_bytes = $bText.Length; has_record_exit = $false
        part_bytes = 0; out_bytes = 0; playable = $false; silent = $false
        note = ("lock_rejected={0}" -f $lockRejected) }

    # ---- 场景 ③ 抢占同一输出路径 ----
    $same = Join-Path $script:work ("s3-{0}.mkv" -f $i)
    $a = Start-Recorder -Tag ("s3-{0}-a" -f $i) -Output $same
    $b = Start-Recorder -Tag ("s3-{0}-b" -f $i) -Output $same
    $ra = Wait-Record $a
    $rb = Wait-Record $b
    foreach ($pair in @(@('s3-same', $ra), @('s3-same', $rb))) {
        $rows += [pscustomobject]@{ iter = $i; scene = $pair[0]; exit = $pair[1].exit
            exited = $pair[1].exited; stdout_bytes = $pair[1].stdout_bytes
            has_record_exit = $pair[1].has_record_exit; part_bytes = $pair[1].part_bytes
            out_bytes = $pair[1].out_bytes; playable = $pair[1].playable; silent = $pair[1].silent
            note = '' }
        if ($pair[1].silent) { $reproductions++ }
    }
    Write-Host ("第 {0}/{1} 轮完成" -f $i, $Iterations)
}

# 残留进程检查（复现定义里的「无残留进程」）
$residual = @(Get-Process sr-recorder -ErrorAction SilentlyContinue)
$rows | Export-Csv -Path (Join-Path $script:work 'runs.csv') -NoTypeInformation -Encoding UTF8
$summary = [pscustomobject]@{
    exe_sha256 = (Get-FileHash $rec -Algorithm SHA256).Hash
    iterations = $Iterations
    seconds_per_run = $Seconds
    second_delay_ms = $SecondDelayMs
    scenario_runs = $rows.Count
    silent_exits = $reproductions
    residual_processes = $residual.Count
    lock_rejected = @($rows | Where-Object { $_.note -match 'lock_rejected=True' }).Count
    playable_failures = @($rows | Where-Object { [int]$_.out_bytes -gt 0 -and $_.playable -ne 'True' }).Count
    work = $script:work
}
$summary | ConvertTo-Json -Depth 3 | Set-Content -Path (Join-Path $script:work 'summary.json') -Encoding UTF8

Write-Host ''
Write-Host ("场景运行 {0} 次（{1} 轮 × 场景）；静默退出 {2} 次；残留进程 {3}；单实例锁拒绝 {4}/{1}；成品不可播 {5}" -f `
    $rows.Count, $Iterations, $reproductions, $residual.Count, $summary.lock_rejected,
    $summary.playable_failures)
if ($reproductions -gt 0) {
    Write-Host 'F342-REPRO: 复现（按验收报告-轮51 §R1 升 Blocker）'
    exit 1
}
Write-Host 'F342-REPRO: 未复现'
exit 0
