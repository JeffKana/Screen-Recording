# G5 acceptance case: fallback commit / backend degradation / recovery naming (T-16b)
#
# A 写失败 → 兜底提交（P0-1/P0-2）：注入落在 MkvWriter::WriteRaw 内，走真实 write_errors → fatal
#    判据：exit 16、fallback_path 可播、时长 ≥ 已落盘 −1.5s、原目录无成品无 0 字节
# B 后端真实降级（P0-3）：恢复时排除原后端，走 CaptureChain 真实降级
#    判据：backend_changed=1 + WARNING + 录制不中断 + 成片可播
# C 恢复产物命名（P0-4 / D-32）：修复后源 .part 改名，二次扫描不再提示，二次修复不覆盖
#
# Usage: pwsh -File verify/cases/g5_fallback.ps1 [-BuildDir build-msvc] [-Config Release]

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

$work = Join-Path $root (Join-Path $BuildDir 'fallback_case')
New-Item -ItemType Directory -Force -Path $work | Out-Null
Get-ChildItem $work -Recurse | Remove-Item -Force -Recurse -ErrorAction SilentlyContinue

Write-Host '--- A 写失败 → 兜底提交 (P0-1/P0-2) ---'
$origDir = Join-Path $work 'orig'
$fbDir = Join-Path $work 'fallback'
$outFile = Join-Path $origDir 'rec.mkv'
$outA = & $exe --record --seconds 10 --fps 30 --out $outFile --fail-write-after 8 --fallback-dir $fbDir 2>&1 | Out-String
$codeA = [int](Field $outA 'record_exit')
$fbPath = Field $outA 'fallback_path'
$srcPtsMs = [long](Field $outA 'fallback_source_pts_ms')
$fbDur = Get-Duration $fbPath
Assert 'fallback-exit-code' ($codeA -eq 16) "record_exit=$codeA"
Assert 'fallback-path-exists' ((Field $outA 'fallback_path') -ne '' -and (Test-Path $fbPath)) "path=$fbPath"
Assert 'fallback-playable' (Test-Decodable $fbPath) 'decode clean'
Assert 'fallback-has-audio' (Test-HasAudio $fbPath) 'audio track present'
# 关键判据：修复后时长 ≥ 已落盘末尾 PTS − 1.5s（一个 Cluster 的余量）
$minDur = [Math]::Max(0.5, ($srcPtsMs / 1000.0) - 1.5)
Assert 'fallback-duration' ($fbDur -ge $minDur) "duration=$([Math]::Round($fbDur,2))s min=$([Math]::Round($minDur,2))s src_pts=${srcPtsMs}ms"
$origEntries = @(Get-ChildItem $origDir -ErrorAction SilentlyContinue)
$zeroByte = @($origEntries | Where-Object { $_.Length -eq 0 })
Assert 'orig-dir-clean' ($origEntries.Count -eq 0) "entries=$($origEntries.Count)"
Assert 'orig-no-zero-byte' ($zeroByte.Count -eq 0) "zero_byte=$($zeroByte.Count)"
Assert 'orig-no-final-file' (-not (Test-Path $outFile)) "final_exists=$(Test-Path $outFile)"

Write-Host '--- B 后端真实降级 (P0-3) ---'
$degFile = Join-Path $work 'degrade.mkv'
$outB = & $exe --record --seconds 6 --fps 30 --out $degFile --pause-at 3 --pause-duration 2 --fail-resume-backend 2>&1 | Out-String
$codeB = [int](Field $outB 'record_exit')
$changedB = [int](Field $outB 'backend_changed')
$backendB = Field $outB 'backend'
$durB = Get-Duration $degFile
$framesB = [int](Field $outB 'frames_encoded')
Assert 'degrade-exit-code' ($codeB -eq 0) "record_exit=$codeB（录制不得中断）"
Assert 'degrade-backend-changed' ($changedB -eq 1) "backend_changed=$changedB backend=$backendB"
Assert 'degrade-warning-logged' ($outB -match 'backend changed after resume') 'WARNING 已输出'
Assert 'degrade-still-playable' ((Test-Decodable $degFile) -and ([Math]::Abs($durB - 6.0) -le 0.1)) "duration=$durB"
Assert 'degrade-frame-count' ($framesB -eq 180) "frames_encoded=$framesB"

Write-Host '--- C 恢复产物命名 (P0-4 / D-32) ---'
$recDir = Join-Path $work 'recovery'
New-Item -ItemType Directory -Force -Path $recDir | Out-Null
$killFile = Join-Path $recDir 'rec.mkv'
$proc = Start-Process -FilePath $exe -PassThru -WindowStyle Hidden `
    -ArgumentList @('--record', '--seconds', '60', '--fps', '30', '--out', "`"$killFile`"")
Register-SrProcess $proc
Start-Sleep -Milliseconds 6000
if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force }
Start-Sleep -Milliseconds 400

$part = $killFile + '.part'
$scan1 = & $exe --scan $recDir 2>&1 | Out-String
Assert 'scan-before-repair' ($scan1 -match 'repairable=1') '首次扫描判定可修复'

$repair1 = & $exe --repair $part 2>&1 | Out-String
$out1 = [regex]::Match($repair1, 'output=(.+)').Groups[1].Value.Trim()
Assert 'repair-first-ok' (($repair1 -match 'repair=ok') -and (Test-Path $out1)) "output=$out1"
Assert 'source-renamed' ((Test-Path ($part + '.repaired')) -and (-not (Test-Path $part))) '源 .part 已改名'

$scan2 = & $exe --scan $recDir 2>&1 | Out-String
Assert 'scan-after-repair-clean' ($scan2 -match 'scanned=0') "二次扫描: $($scan2.Trim().Split("`n")[0])"

# 再放一个同源 .part：二次修复必须另起名，不得覆盖上一次产物
Copy-Item $out1 $part -Force
$repair2 = & $exe --repair $part 2>&1 | Out-String
$out2 = [regex]::Match($repair2, 'output=(.+)').Groups[1].Value.Trim()
Assert 'repair-second-renamed' (($out2 -ne $out1) -and (Test-Path $out2)) "second=$out2"
Assert 'first-output-intact' (Test-Path $out1) "first=$out1 仍存在"

Write-Host '--- D 无兜底目录 → exit 15 且保留 .part ---'
$origD = Join-Path $work 'orig_no_fb'
$fileD = Join-Path $origD 'rec.mkv'
$outD = & $exe --record --seconds 10 --out $fileD --fail-write-after 8 2>&1 | Out-String
$codeD = [int](Field $outD 'record_exit')
$partD = $fileD + '.part'
Assert 'no-fallback-exit-code' ($codeD -eq 15) "record_exit=$codeD"
Assert 'no-fallback-keeps-part' (Test-Path $partD) "part_exists=$(Test-Path $partD)"
Assert 'no-fallback-no-final' (-not (Test-Path $fileD)) "final_exists=$(Test-Path $fileD)"

Write-Host '--- E 不可修复 .part → --fallback-commit (P0-4) ---'
$garbageDir = Join-Path $work 'garbage'
New-Item -ItemType Directory -Force -Path $garbageDir | Out-Null
$garbage = Join-Path $garbageDir 'g.mkv.part'
[System.IO.File]::WriteAllBytes($garbage, [byte[]](1..16))
$fbE = Join-Path $work 'fallback_garbage'
$outE = & $exe --fallback-commit $garbage --fallback-dir $fbE 2>&1 | Out-String
Assert 'unrepairable-exit-15' ([int](Field $outE 'record_exit') -eq 15) "record_exit=$(Field $outE 'record_exit')"
$fbCount = @(Get-ChildItem $fbE -File -ErrorAction SilentlyContinue).Count
Assert 'unrepairable-no-garbage' ($fbCount -eq 0) "fallback_files=$fbCount"
Assert 'unrepairable-part-kept' ((Test-Path $garbage) -and ((Get-Item $garbage).Length -eq 16)) '原 .part 保留且未被改写'
Write-Host ''
Write-Host ("RESULT: {0} PASS / {1} FAIL" -f $script:pass, $script:fail)
if ($script:fail -gt 0) { exit 1 }
exit 0
