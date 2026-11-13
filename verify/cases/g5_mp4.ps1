# 轮 48 · M3-09#1/#3 起步判据（MP4 writer 主体 + --format 入口）
#
# 判据来自 `.codebuddy/plans/轮48指令.md` §P0-2 五条 + §P0-3 三条，验收方可独立重跑。
#   P0-2 #1 ffprobe format_name/duration  #2 ffmpeg 全片解码  #3 帧数=frames_encoded
#          #4 音频时长 vs 视频末 PTS ≤100ms  #5 默认不带 --format 仍产 MKV
#   P0-3  状态序列一致 / 两容器均通过 1~4 / 非法值 exit 2 + usage
# 附加护栏：MP4 的 box / sample table 自洽（verify/tools/mp4_box_check.py）、
#           stss 首个同步样本为 1、--format mp4 未给 --out 时默认名跟随格式。
#
# Usage:
#   pwsh -File verify/cases/g5_mp4.ps1 [-BuildDir build-msvc] [-Config Release]
#   pwsh -File verify/cases/g5_mp4.ps1 -RecorderPath <轮47旧二进制>   # 红端复跑

param(
    [string]$BuildDir = 'build-msvc',
    [string]$Config = 'Release',
    [string]$RecorderPath = ''
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\lib\Cleanup.ps1')
. (Join-Path $PSScriptRoot '..\lib\Forensics.ps1')
trap {
    [void](Save-FailureContext -Tag 'g5_mp4' -WorkDirs @($script:work) -Note $_)
    Clear-SrProcesses -IncludeNameFallback
    Write-Host "FAIL: $_"
    exit 1
}

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if ([string]::IsNullOrWhiteSpace($RecorderPath)) {
    $RecorderPath = Join-Path $root (Join-Path $BuildDir "bin\$Config\sr-recorder.exe")
    if (-not (Test-Path $RecorderPath)) {
        $RecorderPath = Join-Path $root (Join-Path $BuildDir 'bin\sr-recorder.exe')
    }
}
if (-not (Test-Path $RecorderPath)) { Write-Host 'FAIL: sr-recorder.exe missing'; exit 2 }
Write-Host ("exe={0}" -f $RecorderPath)

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
if (-not $ffbin) { Write-Host 'FAIL: ffprobe/ffmpeg missing'; exit 4 }
$ffprobe = Join-Path $ffbin 'ffprobe.exe'
$ffmpeg = Join-Path $ffbin 'ffmpeg.exe'

$script:work = Join-Path $env:TEMP 'sr-mp4'
if (Test-Path $script:work) {
    Get-ChildItem $script:work -Recurse -Force | Remove-Item -Force -Recurse -ErrorAction SilentlyContinue
}
New-Item -ItemType Directory -Force -Path $script:work | Out-Null
$script:SrForensicsWorkDirs = @($script:work)

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
        [void](Save-FailureContext -Tag $Name -WorkDirs @($script:work) -Note $Detail)
    }
}
function Read-Text {
    param([string]$Path)
    if (-not (Test-Path $Path)) { return '' }
    return [System.IO.File]::ReadAllText($Path)
}
function Field {
    param([string]$Text, [string]$Name)
    if ($Text -match ("(?m)^" + [regex]::Escape($Name) + "=([^`r`n]*)")) { return $Matches[1].Trim() }
    return ''
}
function NumField {
    param([string]$Text, [string]$Name)
    $value = Field $Text $Name
    if ($value -match '^-?\d+(\.\d+)?$') { return [double]$value }
    return -1
}

$script:seq = 0
function Invoke-Recorder {
    param([string[]]$Arguments, [string]$WorkingDirectory = $script:work, [int]$WaitMs = 90000)
    $script:seq++
    $tag = 'run{0}' -f $script:seq
    $out = Join-Path $script:work ($tag + '.out')
    $err = Join-Path $script:work ($tag + '.err')
    $proc = Start-Process -FilePath $RecorderPath -PassThru -WindowStyle Hidden `
        -WorkingDirectory $WorkingDirectory -RedirectStandardOutput $out -RedirectStandardError $err `
        -ArgumentList $Arguments
    $finished = $proc.WaitForExit($WaitMs)
    $code = if ($finished) { $proc.ExitCode } else { -1 }
    $text = (Read-Text $out) + (Read-Text $err)
    [System.IO.File]::WriteAllText((Join-Path $script:work ($tag + '.cmd.txt')),
        ($RecorderPath + ' ' + ($Arguments -join ' ') + "`r`nexit=$code`r`n" + $text),
        [System.Text.UTF8Encoding]::new($false))
    return @{ Exit = $code; Text = $text; Tag = $tag }
}

function Probe-Json {
    param([string]$File, [string]$Entries)
    $raw = & $ffprobe -v error -show_entries $Entries -of json $File 2>&1 | Out-String
    try { return ($raw | ConvertFrom-Json) } catch { return $null }
}
function Decode-Clean {
    param([string]$File)
    $raw = (& $ffmpeg -v error -i $File -f null - 2>&1 | Out-String)
    $lines = @($raw -split "`r?`n" | Where-Object { $_.Trim() -ne '' })
    return @{ Exit = $LASTEXITCODE; Lines = $lines.Count; Head = ($lines | Select-Object -First 2) -join ';' }
}
function Count-VideoFrames {
    param([string]$File)
    # MKV 的 stream 里没有 nb_frames，必须 -count_frames 真解码计数（两容器同口径）。
    $raw = & $ffprobe -v error -select_streams v:0 -count_frames `
        -show_entries stream=nb_read_frames -of default=noprint_wrappers=1:nokey=1 $File 2>&1 | Out-String
    $first = ($raw.Trim() -split "`r?`n" | Select-Object -First 1)
    if ($first -match '^\d+$') { return [int]$first }
    return -1
}

$seconds = 5
$mp4 = Join-Path $script:work 'r48.mp4'
$mkv = Join-Path $script:work 'r48.mkv'
$defaultMkv = Join-Path $script:work 'r48-default.mkv'

# ---------------- P0-2 #1~#4：MP4 成片 ----------------
Write-Host '--- P0-2 MP4 5s 样本 ---'
$mp4Run = Invoke-Recorder -Arguments @('--record', '--seconds', "$seconds", '--fps', '30',
                                       '--bitrate', '2000', '--format', 'mp4', '--out', "`"$mp4`"")
$mp4Exit = [int](NumField $mp4Run.Text 'record_exit')
$framesEncoded = [int](NumField $mp4Run.Text 'frames_encoded')
$mp4State = Field $mp4Run.Text 'state_sequence'
$vLastMs = NumField $mp4Run.Text 'video_last_pts_ms'
$aLastMs = NumField $mp4Run.Text 'audio_last_pts_ms'
Assert 'r48-mp4-record-exit0' ($mp4Run.Exit -eq 0 -and $mp4Exit -eq 0 -and
        (Test-Path $mp4) -and -not [string]::IsNullOrWhiteSpace($mp4State)) `
    ("exit={0} record_exit={1} 成片={2} state_sequence={3}；证据 {4}.cmd.txt" -f `
        $mp4Run.Exit, $mp4Exit, (Test-Path $mp4), $mp4State, $mp4Run.Tag)

$fmt = Probe-Json -File $mp4 -Entries 'format=format_name,duration'
$fmtName = if ($fmt) { $fmt.format.format_name } else { '' }
$fmtDuration = if ($fmt) { [double]$fmt.format.duration } else { -1 }
Assert 'r48-judge1-format-duration' ($fmtName -match 'mp4' -and
        [Math]::Abs($fmtDuration - $seconds) -le 0.2) `
    ("format_name={0} duration={1}s vs 录制 {2}s（±0.2s）；证据 {3}.cmd.txt" -f `
        $fmtName, $fmtDuration, $seconds, $mp4Run.Tag)

$dec = Decode-Clean -File $mp4
Assert 'r48-judge2-ffmpeg-decode' ($dec.Exit -eq 0 -and $dec.Lines -eq 0) `
    ("ffmpeg -v error -f null - : exit={0} error 行={1} head=[{2}]" -f $dec.Exit, $dec.Lines, $dec.Head)

$streams = Probe-Json -File $mp4 -Entries 'stream=index,codec_type,codec_name,nb_frames,duration,time_base,r_frame_rate'
$videoStream = @($streams.streams | Where-Object { $_.codec_type -eq 'video' } | Select-Object -First 1)
$audioStream = @($streams.streams | Where-Object { $_.codec_type -eq 'audio' } | Select-Object -First 1)
$vFrames = if ($videoStream.Count) { [int]$videoStream[0].nb_frames } else { -1 }
Assert 'r48-judge3-frame-count' ($vFrames -eq $framesEncoded -and $framesEncoded -gt 0) `
    ("视频 nb_frames={0} vs frames_encoded={1}；codec={2} time_base={3} r_frame_rate={4}" -f `
        $vFrames, $framesEncoded,
        $(if ($videoStream.Count) { $videoStream[0].codec_name } else { 'N/A' }),
        $(if ($videoStream.Count) { $videoStream[0].time_base } else { 'N/A' }),
        $(if ($videoStream.Count) { $videoStream[0].r_frame_rate } else { 'N/A' }))

$aDuration = if ($audioStream.Count) { [double]$audioStream[0].duration } else { -1 }
$vDuration = if ($videoStream.Count) { [double]$videoStream[0].duration } else { -1 }
$avDeltaMs = [Math]::Abs($aDuration * 1000 - $vLastMs)
$vvDeltaMs = [Math]::Abs($aDuration - $vDuration) * 1000
Assert 'r48-judge4-av-duration' ($audioStream.Count -eq 1 -and $videoStream.Count -eq 1 -and
        $avDeltaMs -le 100 -and $vvDeltaMs -le 100) `
    ("音频时长={0}s 视频时长={1}s 视频末PTS={2}ms 音频末PTS={3}ms；|音频时长-视频末PTS|={4}ms、|音-视时长|={5}ms（≤100ms）" -f `
        $aDuration, $vDuration, $vLastMs, $aLastMs,
        [Math]::Round($avDeltaMs, 1), [Math]::Round($vvDeltaMs, 1))

$keyRaw = & $ffprobe -v error -select_streams v:0 -show_frames `
    -show_entries frame=key_frame -of json $mp4 2>&1 | Out-String
try { $keyFrames = $keyRaw | ConvertFrom-Json } catch { $keyFrames = $null }
$keyList = if ($keyFrames) { @($keyFrames.frames) } else { @() }
$firstKey = if ($keyList.Count -gt 0) { [int]$keyList[0].key_frame } else { -1 }
$keyCount = if ($keyList.Count -gt 0) { @($keyList | Where-Object { $_.key_frame -eq 1 }).Count } else { -1 }
Assert 'r48-mp4-stss-first-frame-key' ($firstKey -eq 1 -and $keyCount -gt 0 -and $keyCount -lt $vFrames) `
    ("首帧 key_frame={0}，同步帧={1}/{2}（stss 只标 IDR，不是全部同步）" -f $firstKey, $keyCount, $vFrames)

$boxRaw = python (Join-Path $root 'verify/tools/mp4_box_check.py') $mp4 2>&1 | Out-String
[System.IO.File]::WriteAllText((Join-Path $script:work 'box-check.txt'), $boxRaw,
    [System.Text.UTF8Encoding]::new($false))
$boxResult = if ($boxRaw -match 'RESULT:\s*(\d+)\s*PASS\s*/\s*(\d+)\s*FAIL') {
    @{ Pass = [int]$Matches[1]; Fail = [int]$Matches[2] }
} else { @{ Pass = 0; Fail = -1 } }
Assert 'r48-mp4-box-sample-table' ($boxResult.Fail -eq 0 -and $boxResult.Pass -ge 35) `
    ("mp4_box_check.py RESULT: {0} PASS / {1} FAIL；原始输出 {2}" -f `
        $boxResult.Pass, $boxResult.Fail, 'box-check.txt')

# ---------------- P0-2 #5 / P0-3：MKV 行为与格式切换 ----------------
Write-Host '--- P0-2 #5 / P0-3 格式切换 ---'
$mkvRun = Invoke-Recorder -Arguments @('--record', '--seconds', "$seconds", '--fps', '30',
                                       '--bitrate', '2000', '--format', 'mkv', '--out', "`"$mkv`"")
$defaultRun = Invoke-Recorder -Arguments @('--record', '--seconds', "$seconds", '--fps', '30',
                                           '--bitrate', '2000', '--out', "`"$defaultMkv`"")
$mkvFmt = Probe-Json -File $mkv -Entries 'format=format_name,duration'
$defaultFmt = Probe-Json -File $defaultMkv -Entries 'format=format_name,duration'
$mkvName = if ($mkvFmt) { $mkvFmt.format.format_name } else { '' }
$defaultName = if ($defaultFmt) { $defaultFmt.format.format_name } else { '' }
Assert 'r48-judge5-default-mkv' ($mkvRun.Exit -eq 0 -and $defaultRun.Exit -eq 0 -and
        $mkvName -match 'matroska' -and $defaultName -match 'matroska' -and
        (Test-Path $defaultMkv)) `
    ("--format mkv → {0}；不带 --format → {1}（两者均为 MKV，既有行为不变）" -f `
        $mkvName, $defaultName)

$mkvState = Field $mkvRun.Text 'state_sequence'
$defaultState = Field $defaultRun.Text 'state_sequence'
Assert 'r48-format-state-sequence' ($mp4State -ne '' -and $mp4State -eq $mkvState -and
        $mp4State -eq $defaultState) `
    ("mp4=[{0}] mkv=[{1}] default=[{2}]（格式只换封装器，不改状态机）" -f `
        $mp4State, $mkvState, $defaultState)

$mkvDec = Decode-Clean -File $mkv
$mkvFrameCount = Count-VideoFrames -File $mkv
$mp4FrameCount = Count-VideoFrames -File $mp4
Assert 'r48-both-containers-playable' ($mkvDec.Exit -eq 0 -and $mkvDec.Lines -eq 0 -and
        $dec.Exit -eq 0 -and $mkvFrameCount -eq $framesEncoded -and
        $mp4FrameCount -eq $framesEncoded) `
    ("MKV 解码 exit={0} error 行={1} 计帧={2}；MP4 解码 exit={3} 计帧={4}；frames_encoded={5}" -f `
        $mkvDec.Exit, $mkvDec.Lines, $mkvFrameCount, $dec.Exit, $mp4FrameCount, $framesEncoded)

$badRun = Invoke-Recorder -Arguments @('--record', '--format', 'avi', '--out', "`"$mp4`"")
Assert 'r48-invalid-format-exit2' ($badRun.Exit -eq 2 -and
        $badRun.Text -match 'invalid --format value' -and $badRun.Text -match '--format') `
    ("--format avi → exit={0}（期望 2），usage 含 --format={1}，原因可见={2}；证据 {3}.cmd.txt" -f `
        $badRun.Exit, [bool]($badRun.Text -match '--format'),
        [bool]($badRun.Text -match 'invalid --format value'), $badRun.Tag)

$missRun = Invoke-Recorder -Arguments @('--record', '--format')
Assert 'r48-missing-format-exit2' ($missRun.Exit -eq 2 -and $missRun.Text -match 'missing --format value') `
    ("--format 缺值 → exit={0}（期望 2）；证据 {1}.cmd.txt" -f $missRun.Exit, $missRun.Tag)

$nameDir = Join-Path $script:work 'default-name'
New-Item -ItemType Directory -Force -Path $nameDir | Out-Null
$nameRun = Invoke-Recorder -Arguments @('--record', '--seconds', '1', '--format', 'mp4') `
    -WorkingDirectory $nameDir
Assert 'r48-mp4-default-output-name' ($nameRun.Exit -eq 0 -and
        (Test-Path (Join-Path $nameDir 'out.mp4')) -and
        -not (Test-Path (Join-Path $nameDir 'out.mkv'))) `
    ("--format mp4 未给 --out → out.mp4 存在={0}、out.mkv 不存在={1}；证据 {2}.cmd.txt" -f `
        (Test-Path (Join-Path $nameDir 'out.mp4')),
        (-not (Test-Path (Join-Path $nameDir 'out.mkv'))), $nameRun.Tag)

Write-Host ''
Write-Host ("RESULT: {0} PASS / {1} FAIL ; 产物 {2}" -f $script:pass, $script:fail, $script:work)
if ($script:fail -gt 0) { exit 1 }
exit 0
