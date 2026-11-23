# G3 acceptance case: output path handling (M5-15 / M8-06 / M8-07)
#
# A 同名不覆盖：连续两次录到同一路径，第二次自动追加 _001，两个文件都可播
# B 目标被独占占用：持有文件锁时仍能录完并落地到新名字，不判失败
# C 不可写 / 无法建目录：开录前即报错（record_exit=13），不产生碎片文件
#
# Usage: pwsh -File verify/cases/g3_output.ps1 [-BuildDir build-msvc] [-Config Release] [-RecorderPath <exe>]

param(
    [string]$BuildDir = 'build-msvc',
    [string]$Config = 'Release',
    # 轮 54（R1-52）：指定被测 recorder（用于「先红后绿」——拿 0.1.0 的旧 exe 跑同一份用例）。
    [string]$RecorderPath = ''
)

$ErrorActionPreference = 'Stop'

# 轮 30（F2）：统一异常清理——登记 spawn 进程 + trap 兜底，中途抛异常也不留孤儿。
. (Join-Path $PSScriptRoot '..\lib\Cleanup.ps1')
# trap 必须留在脚本自身作用域：dot-source 进来的 trap 不接管调用方的终止错误。
trap { Clear-SrProcesses -IncludeNameFallback; Write-Host "FAIL: $_"; exit 1 }

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$exe = Join-Path $root (Join-Path $BuildDir "bin\$Config\sr-recorder.exe")
if (-not (Test-Path $exe)) { $exe = Join-Path $root (Join-Path $BuildDir 'bin\sr-recorder.exe') }
if ($RecorderPath -ne '') { $exe = (Resolve-Path $RecorderPath).Path }
if (-not (Test-Path $exe)) { Write-Host "FAIL: sr-recorder.exe not found"; exit 2 }

function Resolve-FfmpegBin {
    $onPath = Get-Command ffprobe -ErrorAction SilentlyContinue
    if ($onPath) { return Split-Path -Parent $onPath.Source }
    $packages = Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Packages'
    if (Test-Path $packages) {
        $found = Get-ChildItem $packages -Recurse -Filter 'ffprobe.exe' -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($found) { return $found.DirectoryName }
    }
    return $null
}
$ffbin = Resolve-FfmpegBin

$script:pass = 0
$script:fail = 0
function Assert {
    param([string]$Name, [bool]$Ok, [string]$Detail)
    if ($Ok) { $script:pass++; Write-Host ("  PASS {0} — {1}" -f $Name, $Detail) }
    else { $script:fail++; Write-Host ("  FAIL {0} — {1}" -f $Name, $Detail) }
}
function Field {
    param([string]$Text, [string]$Key)
    if ($Text -match "(?m)^$Key=(.*)$") { return $Matches[1].Trim() }
    return ''
}

$work = Join-Path $root (Join-Path $BuildDir 'output')
New-Item -ItemType Directory -Force -Path $work | Out-Null
Get-ChildItem $work -Recurse | Remove-Item -Force -Recurse -ErrorAction SilentlyContinue

function Test-Playable {
    param([string]$Path)
    if (-not (Test-Path $Path)) { return $false }
    $probe = & python (Join-Path $root 'verify\tools\mkv_probe.py') $Path 2>&1 | Out-String
    if ($probe -notmatch 'RESULT: 0 failure') { return $false }
    if ($ffbin) {
        $decode = & (Join-Path $ffbin 'ffmpeg.exe') -hide_banner -v error -i $Path -f null - 2>&1 | Out-String
        if (-not [string]::IsNullOrWhiteSpace($decode)) { return $false }
    }
    return $true
}

Write-Host '--- A 同名不覆盖 (M5-15) ---'
$target = Join-Path $work 'same.mkv'
$first = & $exe --record --seconds 2 --fps 30 --out $target 2>&1 | Out-String
$second = & $exe --record --seconds 2 --fps 30 --out $target 2>&1 | Out-String
$out1 = Field $first 'output'
$out2 = Field $second 'output'
Assert 'first-keeps-name' ($out1 -eq $target) "output=$out1"
Assert 'second-renamed' ($out2 -ne $out1 -and $out2 -match '_001') "output=$out2"
Assert 'both-exist' ((Test-Path $out1) -and (Test-Path $out2)) "$((Get-Item $out1).Length) / $((Get-Item $out2).Length) bytes"
Assert 'first-playable' (Test-Playable $out1) 'container valid + decode clean'
Assert 'second-playable' (Test-Playable $out2) 'container valid + decode clean'

Write-Host '--- B 目标被独占占用 (M8-07) ---'
$stream = [System.IO.File]::Open($out1, 'Open', 'ReadWrite', 'None')
try {
    $third = & $exe --record --seconds 2 --fps 30 --out $target 2>&1 | Out-String
} finally {
    $stream.Close()
}
$out3 = Field $third 'output'
$code3 = [int](Field $third 'record_exit')
Assert 'recorded-despite-lock' ($code3 -eq 0) "record_exit=$code3 output=$out3"
Assert 'landed-on-new-name' ($out3 -ne $out1 -and $out3 -ne $out2) "output=$out3"
Assert 'third-playable' (Test-Playable $out3) 'container valid + decode clean'

Write-Host '--- C 无法建目录 / 不可写 (M8-06) ---'
$blocker = Join-Path $work 'blocker'
New-Item -ItemType File -Path $blocker -Force | Out-Null
$bad = Join-Path $blocker 'x.mkv'
$fourth = & $exe --record --seconds 2 --out $bad 2>&1 | Out-String
$code4 = [int](Field $fourth 'record_exit')
Assert 'preflight-rejected' ($code4 -eq 13) "record_exit=$code4"
Assert 'no-fragment' (-not (Test-Path $bad)) "exists=$(Test-Path $bad)"
Assert 'no-part-anywhere' (-not (Test-Path ($bad + '.part'))) 'no .part left behind'

Write-Host '--- D1 收尾前目标被创建：不得覆盖 (D-24) ---'
# Windows 上 rename 会覆盖已存在的目标，所以「准备路径之后别人创建了同名文件」
# 不会以 rename 失败的形式暴露。这里在录制中途写入哨兵，收尾后必须换名而非覆盖。
$race = Join-Path $work 'race.mkv'
$raceLog = Join-Path $work 'race.log'
$racing = Start-Process -FilePath $exe -PassThru -WindowStyle Hidden -RedirectStandardOutput $raceLog `
    -ArgumentList @('--record', '--seconds', '3', '--fps', '30', '--out', "`"$race`"")
Register-SrProcess $racing
Start-Sleep -Milliseconds 1500
$sentinel = [byte[]](1..32)
[System.IO.File]::WriteAllBytes($race, $sentinel)
$racing.WaitForExit()
$raceText = Get-Content $raceLog -Raw
$raceOut = Field $raceText 'output'
$sentinelIntact = $false
if (Test-Path $race) {
    $now = [System.IO.File]::ReadAllBytes($race)
    $sentinelIntact = ($now.Length -eq 32) -and
        ([System.BitConverter]::ToString($now) -eq [System.BitConverter]::ToString($sentinel))
}
Assert 'target-created-late-kept' ($raceOut -ne $race -and $sentinelIntact) "output=$raceOut sentinel_intact=$sentinelIntact"
Assert 'race-recording-playable' (Test-Playable $raceOut) 'container valid + decode clean'

Write-Host '--- D2 .part 残留避让 (M5-04) ---'
$res = Join-Path $work 'res.mkv'
$stalePart = $res + '.part'
[System.IO.File]::WriteAllBytes($stalePart, [byte[]](9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9))
$fifth = & $exe --record --seconds 2 --fps 30 --out $res 2>&1 | Out-String
$resOut = Field $fifth 'output'
$staleIntact = (Test-Path $stalePart) -and ((Get-Item $stalePart).Length -eq 16)
Assert 'stale-part-untouched' $staleIntact "part_bytes=$((Get-Item $stalePart).Length)（残留的 .part 不得被新录制覆盖）"
Assert 'residue-avoided-name' ($resOut -eq $res) "output=$resOut"

Write-Host '--- E 并发抢同一 --out：.part 原子占位 (R1-52) ---'
# 两个 recorder 抢同一个 --out。测试构建有 --wait-for-part：两个进程等到同一个屏障文件
# 出现才去选名，把「同一毫秒」变成确定性事件（没有屏障时撞车率只有 ~10%，修好了也测不出来）。
# 旧构建 / 交付包没有这个开关，自动退化成普通并发起跑 —— 同一份用例既能跑红也能跑绿。
$iterations = 20
$helpText = & $exe --help 2>&1 | Out-String
$hasBarrier = $helpText -match '--wait-for-part'
$produced = @()
$corrupt = 0
$badExit = 0
$collided = 0
for ($i = 1; $i -le $iterations; $i++) {
    $raceTarget = Join-Path $work ('conc{0:D2}.mkv' -f $i)
    $barrier = Join-Path $work ('conc{0:D2}.barrier' -f $i)
    $procs = @()
    $logs = @()
    foreach ($tag in @('a', 'b')) {
        $log = Join-Path $work ('conc{0:D2}.{1}.log' -f $i, $tag)
        $recArgs = @('--record', '--seconds', '1', '--fps', '15', '--bitrate', '1500',
            '--out', "`"$raceTarget`"")
        if ($hasBarrier) { $recArgs += @('--wait-for-part', "`"$barrier`"") }
        $procs += Start-Process -FilePath $exe -PassThru -WindowStyle Hidden `
            -RedirectStandardOutput $log -ArgumentList $recArgs
        $logs += $log
    }
    if ($hasBarrier) {
        Start-Sleep -Milliseconds 800
        New-Item -ItemType File -Force $barrier | Out-Null
    }
    foreach ($p in $procs) {
        $p.WaitForExit(120000) | Out-Null
        if ($p.ExitCode -ne 0) { $badExit++ }
    }
    $reservedThisRound = @()
    foreach ($log in $logs) {
        $text = Get-Content $log -Raw -ErrorAction SilentlyContinue
        $o = Field $text 'output'
        if ($o -ne '') {
            $produced += $o
            if (-not (Test-Playable $o)) { $corrupt++ }
        }
        # 轮 54：这一行是「谁占到了哪个临时文件」。两个进程同时选名时必然不同 —— 撞车的直接证据。
        if ($text -match '(?m)output reserved: (.+?\.part)\s*$') { $reservedThisRound += $Matches[1] }
    }
    if ($reservedThisRound.Count -eq 2 -and $reservedThisRound[0] -ne $reservedThisRound[1]) { $collided++ }
}
$unique = @($produced | Sort-Object -Unique)
$expected = 2 * $iterations
Assert 'r54-concurrent-no-corruption' ($corrupt -eq 0 -and $produced.Count -eq $expected) `
    ("并发 {0} 轮 × 2 进程（屏障={1}）：产物 {2} 个 / 损坏 {3} 个 / 撞车轮数 {4}" -f $iterations, $hasBarrier, $produced.Count, $corrupt, $collided)
$residue = @(Get-ChildItem $work -Filter 'conc*.part' -ErrorAction SilentlyContinue).Count
Assert 'r54-concurrent-stable-outcome' ($badExit -eq 0 -and $unique.Count -eq $expected -and $residue -eq 0) `
    ("exit≠0 次数={0}；互不覆盖的不同文件名 {1} 个（期望 {2}）；.part 残留 {3} 个" -f $badExit, $unique.Count, $expected, $residue)

Write-Host ''
Write-Host ("RESULT: {0} PASS / {1} FAIL" -f $script:pass, $script:fail)
if ($script:fail -gt 0) { exit 1 }
exit 0
