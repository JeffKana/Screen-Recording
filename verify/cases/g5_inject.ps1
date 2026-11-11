# G5 acceptance case: fault injection matrix (T-16a)
#
# 锚点（轮 22 裁定）: ER-04 磁盘耗尽、ER-06 路径不可写、ER-02 强杀（代理 ER-01 断电）、M8-08 编码器失败
# ER-08 TDR 属 V1.5（M8-09），本用例**不覆盖**，不得以「跑一次没崩」冒充通过。
#
# 硬边界：同一注入跑 3 次，任一次结果不同即判「不稳定」，不挑最好那次。
#
# Usage: pwsh -File verify/cases/g5_inject.ps1 [-BuildDir build-msvc] [-Config Release]

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
$exe = Join-Path $root (Join-Path $BuildDir "bin\$Config\sr-recorder.exe")
if (-not (Test-Path $exe)) { $exe = Join-Path $root (Join-Path $BuildDir 'bin\sr-recorder.exe') }
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
if (-not $ffbin) { Write-Host 'BLOCKED: ffprobe not found'; exit 4 }

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
function Get-Duration {
    param([string]$Path)
    if (-not (Test-Path $Path)) { return 0.0 }
    $out = & (Join-Path $ffbin 'ffprobe.exe') -v error -show_entries format=duration -of default=nw=1 $Path 2>&1 | Out-String
    if ($out -match 'duration=([\d\.]+)') { return [double]$Matches[1] }
    return 0.0
}
function Test-Decodable {
    param([string]$Path)
    if (-not (Test-Path $Path)) { return $false }
    $decode = & (Join-Path $ffbin 'ffmpeg.exe') -hide_banner -v error -i $Path -f null - 2>&1 | Out-String
    return [string]::IsNullOrWhiteSpace($decode)
}
function Test-HasAudio {
    param([string]$Path)
    if (-not (Test-Path $Path)) { return $false }
    $out = & (Join-Path $ffbin 'ffprobe.exe') -v error -show_entries stream=codec_type -of default=nw=1 $Path 2>&1 | Out-String
    return ($out -match 'codec_type=audio')
}
function Test-Consistent {
    param([string]$Name, [string[]]$Outcomes)
    $unique = @($Outcomes | Select-Object -Unique)
    Assert "$Name-stable-3x" ($unique.Count -eq 1) ("3 次结果: " + ($Outcomes -join ' | '))
}

$work = Join-Path $root (Join-Path $BuildDir 'inject')
New-Item -ItemType Directory -Force -Path $work | Out-Null
Get-ChildItem $work -Recurse | Remove-Item -Force -Recurse -ErrorAction SilentlyContinue

# ---------------------------------------------------------------- ER-04 磁盘耗尽
Write-Host '--- ER-04 磁盘耗尽 ×3 ---'
$outcomes = @()
for ($i = 1; $i -le 3; $i++) {
    $file = Join-Path $work "disk_$i.mkv"
    # 每轮重算：上一轮写出的数据会占掉空间，沿用旧阈值会让后续轮次直接撞上「开录前拒绝」，
    # 测到的是另一条分支（exit 12）而不是写入路径的安全停止。
    $free = (Get-PSDrive (Get-Item $work).PSDrive.Name).Free
    $stopMb = [math]::Floor(($free - 2MB) / 1MB)
    $warnMb = $stopMb + 50
    $out = & $exe --record --seconds 30 --fps 30 --out $file --disk-stop-mb $stopMb --disk-warn-mb $warnMb 2>&1 | Out-String
    $stop = [int](Field $out 'disk_stop')
    $code = [int](Field $out 'record_exit')
    $size = if (Test-Path $file) { (Get-Item $file).Length } else { 0 }
    $part = Test-Path ($file + '.part')
    $ok = ($stop -eq 1) -and ($code -eq 0) -and ($size -gt 100KB) -and (-not $part) -and (Test-Decodable $file)
    # 一致性只比对「判定元组」：停止时刻本就允许抖动，文件大小不同不算不稳定。
    $outcomes += "stop=$stop,code=$code,part=$part,playable=$((Test-Decodable $file))"
    Assert "disk-run$i" $ok "stop=$stop code=$code size=$size part=$part"
}
Test-Consistent 'disk' $outcomes

# ---------------------------------------------------------------- ER-06 路径不可写
Write-Host '--- ER-06 路径不可写 ×3（开录前拒绝）---'
$outcomes = @()
for ($i = 1; $i -le 3; $i++) {
    $blocker = Join-Path $work "blocker_$i"
    New-Item -ItemType File -Path $blocker -Force | Out-Null
    $bad = Join-Path $blocker 'x.mkv'
    $out = & $exe --record --seconds 2 --out $bad 2>&1 | Out-String
    $code = [int](Field $out 'record_exit')
    $created = (Test-Path $bad) -or (Test-Path ($bad + '.part'))
    $outcomes += "code=$code,created=$created"
    Assert "path-run$i" (($code -eq 13) -and (-not $created)) "code=$code created=$created"
}
Test-Consistent 'path' $outcomes

# 录制中撤权：Windows 下 DACL 变更对已打开的句柄不生效，改判 BLOCKED（见 HANDOFF）
Write-Host '  BLOCKED mid-record-revoke — Windows DACL 对已打开句柄无效，留待轮 23 的 --fail-write-after'

# ---------------------------------------------------------------- ER-02 强杀（代理 ER-01）
Write-Host '--- ER-02 强杀 ×3（录制中 / 暂停中 / 收尾前）---'
$scenarios = @(
    # 强杀时刻必须晚于至少两个 Cluster 落盘：1.5s/簇，3s 时首簇仍在写，被杀即整簇作废。
    @{ name = 'recording';  args = @('--seconds', '60', '--fps', '30'); killAtMs = 8000 },
    @{ name = 'paused';     args = @('--seconds', '60', '--fps', '30', '--pause-at', '6', '--pause-duration', '20'); killAtMs = 9000 },
    @{ name = 'finalizing'; args = @('--seconds', '5', '--fps', '30'); killAtMs = 4700 }
)
foreach ($scenario in $scenarios) {
    $outcomes = @()
    for ($i = 1; $i -le 3; $i++) {
        # 每轮独立目录：--scan 是全目录扫描，混在一起会把上一轮的可修复文件算进来。
        $dir = Join-Path $work "kill_$($scenario.name)_$i"
        New-Item -ItemType Directory -Force -Path $dir | Out-Null
        $file = Join-Path $dir 'rec.mkv'
        $argList = @('--record') + $scenario.args + @('--out', "`"$file`"")
        $proc = Start-Process -FilePath $exe -PassThru -WindowStyle Hidden -ArgumentList $argList
        Register-SrProcess $proc
        Start-Sleep -Milliseconds $scenario.killAtMs
        if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force }
        Start-Sleep -Milliseconds 400

        $part = $file + '.part'
        $scan = & $exe --scan $dir 2>&1 | Out-String
        $repairable = $scan -match 'repairable=1'
        $repair = & $exe --repair $part 2>&1 | Out-String
        $recovered = [regex]::Match($repair, 'output=(.+)').Groups[1].Value.Trim()
        $dur = Get-Duration $recovered
        $hasAudio = Test-HasAudio $recovered
        $playable = Test-Decodable $recovered
        # 判据按需求原文：崩溃后恢复到「崩溃前 ≤10s 的内容」（M8-01）。
        # 实测损失另记 INFO：当前损失=未落盘 Cluster（≤1.5s）+ 编码器内部缓冲（≈1s）。
        $minDur = [Math]::Max(0.5, $scenario.killAtMs / 1000.0 - 10.0)
        # 工程护栏（非需求）：需求只要求 ≤10s，但短录制下 10s 下界形同虚设。
        # 这里额外要求丢失 ≤5s，用来在回归中守住「两个 Cluster + 编码器缓冲」的实际水平。
        $guardrailDur = $scenario.killAtMs / 1000.0 - 5.0
        $ok = $repairable -and $playable -and $hasAudio -and ($dur -ge $minDur) -and ($dur -ge $guardrailDur)
        $loss = [Math]::Round($scenario.killAtMs / 1000.0 - $dur, 2)
        Write-Host ("  INFO loss={0}s (kill={1}s, recovered={2}s)" -f $loss, ($scenario.killAtMs / 1000.0), [Math]::Round($dur, 2))
        $outcomes += "repairable=$repairable,playable=$playable,audio=$hasAudio,ok=$ok"
        Assert "kill-$($scenario.name)-run$i" $ok "repairable=$repairable playable=$playable audio=$hasAudio dur=$([Math]::Round($dur,2))s (需求下界=$([Math]::Round($minDur,1))s 护栏=$([Math]::Round($guardrailDur,1))s)"
    }
    Test-Consistent "kill-$($scenario.name)" $outcomes
}

# ---------------------------------------------------------------- M8-08 编码器失败
Write-Host '--- M8-08 编码器 ×3（软编回退 + 全失败分支）---'
$outcomes = @()
for ($i = 1; $i -le 3; $i++) {
    $file = Join-Path $work "enc_$i.mkv"
    $out = & $exe --record --seconds 2 --fps 30 --out $file 2>&1 | Out-String
    $code = [int](Field $out 'record_exit')
    $encoder = Field $out 'encoder'
    $ok = ($code -eq 0) -and ($encoder -eq 'mf-h264-sw') -and (Test-Decodable $file)
    $outcomes += "code=$code,encoder=$encoder,playable=$((Test-Decodable $file))"
    Assert "encoder-fallback-run$i" $ok "code=$code encoder=$encoder"
}
Test-Consistent 'encoder-fallback' $outcomes

$outcomes = @()
for ($i = 1; $i -le 3; $i++) {
    $file = Join-Path $work "encfail_$i.mkv"
    $out = & $exe --record --seconds 2 --out $file --fail-encoder 2>&1 | Out-String
    $code = [int](Field $out 'record_exit')
    $created = (Test-Path $file) -or (Test-Path ($file + '.part'))
    # 断言真实双分支：硬编与软编两次创建都失败，而不是调用方自造的字符串。
    $hasError = ($out -match 'hardware') -and ($out -match 'software')
    $ok = ($code -eq 6) -and (-not $created) -and $hasError
    $outcomes += "code=$code,created=$created,error=$hasError"
    Assert "encoder-allfail-run$i" $ok "code=$code created=$created error=$hasError"
}
Test-Consistent 'encoder-allfail' $outcomes

Write-Host ''
Write-Host ("RESULT: {0} PASS / {1} FAIL" -f $script:pass, $script:fail)
if ($script:fail -gt 0) { exit 1 }
exit 0
