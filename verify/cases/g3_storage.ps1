# G3 acceptance case: storage guard (M8-03 / M8-04 / M8-05)
#
# A 正常路径：可用空间与可录时长可读出，产物可播
# B 低磁盘：把 stop 线卡到「当前可用 - 2MB」，录制应当提前收尾、产物完整可播、不产生 0 字节文件
# C 开录前拒绝：stop 线高于可用空间时直接失败，且不留下碎片文件
#
# 说明：B 用「抬高停止线」触发，而不是真把磁盘写满——后者会破坏机器且不可重复。
# 触发的是与真实耗尽完全相同的代码分支（must_stop → break → 正常收尾）。
#
# Usage: pwsh -File verify/cases/g3_storage.ps1 [-BuildDir build-msvc] [-Config Release]

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

$work = Join-Path $root (Join-Path $BuildDir 'storage')
New-Item -ItemType Directory -Force -Path $work | Out-Null
Get-ChildItem $work -File | Remove-Item -Force -ErrorAction SilentlyContinue

Write-Host '--- A 正常路径 (M8-05 可录时长) ---'
$okFile = Join-Path $work 'ok.mkv'
$outA = & $exe --record --seconds 2 --fps 30 --out $okFile 2>&1 | Out-String
$freeA = [long](Field $outA 'disk_free_bytes')
$estA = [double](Field $outA 'estimated_record_seconds')
Assert 'free-space-readable' ($freeA -gt 0) "free_bytes=$freeA"
Assert 'estimate-positive' ($estA -gt 0) "estimated_record_seconds=$estA"
Assert 'normal-output' ((Test-Path $okFile) -and ((Get-Item $okFile).Length -gt 0)) "file=$okFile"

Write-Host '--- B 低磁盘：提前收尾 (M8-04) ---'
$drive = (Get-Item $work).PSDrive.Name
$free = (Get-PSDrive $drive).Free
$stopMb = [math]::Floor(($free - 2MB) / 1MB)
$warnMb = $stopMb + 50
$lowFile = Join-Path $work 'low.mkv'
$outB = & $exe --record --seconds 30 --fps 30 --out $lowFile --disk-stop-mb $stopMb --disk-warn-mb $warnMb 2>&1 | Out-String
$wallB = [double](Field $outB 'wall_seconds')
$diskStop = [int](Field $outB 'disk_stop')
$diskWarn = [int](Field $outB 'disk_warn')
Assert 'warn-fired' ($diskWarn -eq 1) "disk_warn=$diskWarn"
Assert 'stopped-early' ($diskStop -eq 1 -and $wallB -lt 25) "disk_stop=$diskStop wall=${wallB}s (requested 30s)"
Assert 'no-zero-byte-file' ((Test-Path $lowFile) -and ((Get-Item $lowFile).Length -gt 100KB)) `
    "size=$((Get-Item $lowFile -ErrorAction SilentlyContinue).Length)"
Assert 'no-part-leftover' (-not (Test-Path ($lowFile + '.part'))) 'part cleaned up'

Write-Host '--- B 续：产物结构合法且可解码 ---'
$probeOut = & python (Join-Path $root 'verify\tools\mkv_probe.py') $lowFile 2>&1 | Out-String
Assert 'container-valid' ($probeOut -match 'RESULT: 0 failure') `
    (($probeOut -split "`n" | Where-Object { $_ -match 'FAIL' }) -join '; ')

if ($ffbin) {
    $decode = & (Join-Path $ffbin 'ffmpeg.exe') -hide_banner -v error -i $lowFile -f null - 2>&1 | Out-String
    Assert 'decodable' ([string]::IsNullOrWhiteSpace($decode)) `
        $(if ($decode.Trim()) { $decode.Trim().Substring(0, [Math]::Min(160, $decode.Trim().Length)) } else { 'decode clean' })
} else {
    Write-Host '  SKIP decodable — ffmpeg 不可用'
}

Write-Host '--- C 开录前拒绝，且不留碎片 ---'
$rejectFile = Join-Path $work 'reject.mkv'
$outC = & $exe --record --seconds 5 --out $rejectFile --disk-stop-mb 999999999 2>&1 | Out-String
$codeC = [int](Field $outC 'record_exit')
Assert 'preflight-rejected' ($codeC -eq 12) "record_exit=$codeC"
Assert 'no-fragment-created' (-not (Test-Path $rejectFile)) "exists=$(Test-Path $rejectFile)"

Write-Host ''
Write-Host ("RESULT: {0} PASS / {1} FAIL" -f $script:pass, $script:fail)
if ($script:fail -gt 0) { exit 1 }
exit 0
