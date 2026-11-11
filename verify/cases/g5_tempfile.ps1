# 轮 37 · M5-04 录制临时文件与停止时封装：
#   #1 录制过程中目标目录存在临时文件，且**不可被误认为成品**（此时目录里没有 .mkv 成品，
#      列表里也没有 kind=video 的条目，只有 kind=part）
#   #2 停止后临时文件被清理，**仅留一个成品**（0 个 .part + 1 个 .mkv，且成片可播、时长正确）
#
# 落点（产品代码，不在用例里实现）：
#   src/recorder/RecordingSession.cpp：录制中写 xxx.mkv.part；
#   src/recorder/OutputPath.cpp：正常收尾 CommitOutput 把它改名成成品；
#   崩溃残留由 src/resilience/MkvRecovery.cpp 离线修复（g3_recovery 覆盖）——**不在本用例范围**。
#
# Usage: pwsh -File verify/cases/g5_tempfile.ps1 [-BuildDir build-msvc] [-Config Release]

param(
    [string]$BuildDir = 'build-msvc',
    [string]$Config = 'Release'
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot '..\lib\Cleanup.ps1')
. (Join-Path $PSScriptRoot '..\lib\Forensics.ps1')
trap {
    [void](Save-FailureContext -Tag 'g5_tempfile' -WorkDirs $script:SrForensicsWorkDirs -Note $_)
    Clear-SrProcesses -IncludeNameFallback
    Write-Host "FAIL: $_"
    exit 1
}

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$rec = Join-Path $root (Join-Path $BuildDir "bin\$Config\sr-recorder.exe")
if (-not (Test-Path $rec)) { $rec = Join-Path $root (Join-Path $BuildDir 'bin\sr-recorder.exe') }
if (-not (Test-Path $rec)) { Write-Host 'FAIL: binaries missing'; exit 2 }
$ffprobe = (Get-Command ffprobe -ErrorAction SilentlyContinue).Source
if (-not $ffprobe) { Write-Host 'BLOCKED: ffprobe not found'; exit 4 }

$work = Join-Path $env:TEMP 'sr-tempfile'
if (Test-Path $work) { Get-ChildItem $work -Recurse -Force | Remove-Item -Force -Recurse -ErrorAction SilentlyContinue }
New-Item -ItemType Directory -Force -Path $work | Out-Null
$script:SrForensicsWorkDirs = @($work)

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

$product = Join-Path $work 'clip.mkv'
$stdout = Join-Path $work 'rec.out'
$logFile = Join-Path $work 'rec.log'

Write-Host '--- 录制中：临时文件存在、成品不存在 ---'
$proc = Start-Process -FilePath $rec -PassThru -WindowStyle Hidden `
    -RedirectStandardOutput $stdout -RedirectStandardError (Join-Path $work 'rec.err') `
    -ArgumentList @('--record', '--seconds', '6', '--fps', '30', '--bitrate', '2000',
                    '--out', "`"$product`"", '--log-level', 'info',
                    '--log-file', "`"$logFile`"")
Register-SrProcess $proc
$script:SrForensicsWorkDirs += $work

Start-Sleep -Seconds 3
$parts = @(Get-ChildItem $work -Filter '*.mkv.part' -File -ErrorAction SilentlyContinue)
$videos = @(Get-ChildItem $work -Filter '*.mkv' -File -ErrorAction SilentlyContinue)
Assert 'm5-04-part-exists' ($parts.Count -ge 1) `
    ("录制中 .part 数量={0}（期望 ≥1）：{1}" -f $parts.Count, (($parts | ForEach-Object { $_.Name }) -join ','))
Assert 'm5-04-no-product-during-recording' ($videos.Count -eq 0) `
    ("录制中 .mkv 成品数量={0}（期望 0）" -f $videos.Count)
Assert 'm5-04-part-named-from-product' ($parts.Count -ge 1 -and $parts[0].Name -eq 'clip.mkv.part') `
    ("临时文件名={0}（期望 clip.mkv.part：与成品同名加 .part，停止时改名）" -f ($(if ($parts.Count -ge 1) { $parts[0].Name } else { '<none>' })))

# 列表里不能把 .part 当成品：sr-recorder --list 只应看到 kind=part，没有 kind=video。
$listOut = Join-Path $work 'list.out'
$listProc = Start-Process -FilePath $rec -PassThru -WindowStyle Hidden -RedirectStandardOutput $listOut `
    -RedirectStandardError (Join-Path $work 'list.err') `
    -ArgumentList @('--list', "`"$work`"")
Wait-Process -Id $listProc.Id -Timeout 20000 -ErrorAction SilentlyContinue
$listText = if (Test-Path $listOut) { [System.IO.File]::ReadAllText($listOut) } else { '' }
$kindPart = ([regex]::Matches($listText, 'entry=kind=part')).Count
$kindVideo = ([regex]::Matches($listText, 'entry=kind=video')).Count
Assert 'm5-04-list-not-product' ($kindPart -ge 1 -and $kindVideo -eq 0) `
    ("--list 输出 kind=part={0} / kind=video={1}（期望 ≥1 / 0：临时文件不得出现在成品条目里）" -f $kindPart, $kindVideo)

Write-Host '--- 停止后：临时文件清理、只剩一个成品 ---'
$finished = $proc.WaitForExit(30000)
if (-not $finished) { Stop-SrProcess $proc }
Unregister-SrProcess $proc
$exitCode = if ($finished) { $proc.ExitCode } else { -1 }
Assert 'm5-04-record-exit' ($exitCode -eq 0) ("record_exit={0}" -f $exitCode)

$partsAfter = @(Get-ChildItem $work -Filter '*.mkv.part' -File -ErrorAction SilentlyContinue)
$videosAfter = @(Get-ChildItem $work -Filter '*.mkv' -File -ErrorAction SilentlyContinue)
Assert 'm5-04-part-cleaned' ($partsAfter.Count -eq 0) `
    ("停止后 .part 数量={0}（期望 0）：{1}" -f $partsAfter.Count, (($partsAfter | ForEach-Object { $_.Name }) -join ','))
Assert 'm5-04-single-product' ($videosAfter.Count -eq 1) `
    ("停止后 .mkv 数量={0}（期望 1）：{1}" -f $videosAfter.Count, (($videosAfter | ForEach-Object { $_.Name }) -join ','))

if ($videosAfter.Count -eq 1) {
    $duration = 0.0
    $dur = & $ffprobe -v error -show_entries format=duration -of default=nw=1:nk=1 $videosAfter[0].FullName 2>&1 | Out-String
    [void][double]::TryParse($dur.Trim(), [ref]$duration)
    Assert 'm5-04-product-duration' ($duration -ge 5.0 -and $duration -le 7.0) `
        ("成片时长 {0:N3}s（期望 5~7s）" -f $duration)

    $streams = & $ffprobe -v error -show_entries stream=codec_type -of csv=p=0 $videosAfter[0].FullName 2>&1 | Out-String
    Assert 'm5-04-product-decodable' (($streams -match 'video') -and ($streams -match 'audio')) `
        ("成片轨道：{0}" -f (($streams -split "`r?`n" | Where-Object { $_.Trim() }) -join ','))

    $decode = & ffmpeg -hide_banner -v error -i $videosAfter[0].FullName -f null - 2>&1 | Out-String
    Assert 'm5-04-product-decode-clean' ([string]::IsNullOrWhiteSpace($decode)) `
        ("解码 stderr 行数={0}" -f @(($decode -split "`r?`n") | Where-Object { $_.Trim() }).Count)
}

Write-Host ''
Write-Host ("RESULT: {0} PASS / {1} FAIL ; 产物 {2}" -f $script:pass, $script:fail, $work)
if ($script:fail -gt 0) { exit 1 }
exit 0
