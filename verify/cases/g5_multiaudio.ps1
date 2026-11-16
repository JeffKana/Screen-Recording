# 轮 49 · M3-09#2（MKV 多音轨）+ MP4 单轨限制判据
#
# 判据来自 `.codebuddy/plans/轮49指令.md` §P0-3：
#   #1 `--audio-tracks` 能产出至少 2 条音轨
#   #2 ffprobe -show_streams 两条 codec_type=audio、各自可独立解码
#   #3 每轨时长与视频差 ≤100ms
#   #4 回归：单音轨 MKV 行为不变（默认不带 --audio-tracks 仍是 1 条）
# 附加：MP4 在 V1.0 只有 1 条音轨（显式回落 + 日志，不静默丢请求）；
#       `--audio-tracks` 非法值 → exit 2 + usage（CONTRACT §18.2「参数错误」）。
#
# 用法:
#   pwsh -File verify/cases/g5_multiaudio.ps1 [-BuildDir build-msvc] [-Config Release]
#   pwsh -File verify/cases/g5_multiaudio.ps1 -RecorderPath <轮48旧二进制>   # 红端复跑

param(
    [string]$BuildDir = 'build-msvc',
    [string]$Config = 'Release',
    [string]$RecorderPath = ''
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\lib\Cleanup.ps1')
. (Join-Path $PSScriptRoot '..\lib\Forensics.ps1')
trap {
    [void](Save-FailureContext -Tag 'g5_multiaudio' -WorkDirs @($script:work) -Note $_)
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

$script:work = Join-Path $env:TEMP 'sr-multiaudio'
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
    param([string[]]$Arguments, [int]$WaitMs = 90000)
    $script:seq++
    $tag = 'run{0}' -f $script:seq
    $out = Join-Path $script:work ($tag + '.out')
    $err = Join-Path $script:work ($tag + '.err')
    $proc = Start-Process -FilePath $RecorderPath -PassThru -WindowStyle Hidden `
        -WorkingDirectory $script:work -RedirectStandardOutput $out -RedirectStandardError $err `
        -ArgumentList $Arguments
    $finished = $proc.WaitForExit($WaitMs)
    $code = if ($finished) { $proc.ExitCode } else { -1 }
    $text = (Read-Text $out) + (Read-Text $err)
    [System.IO.File]::WriteAllText((Join-Path $script:work ($tag + '.cmd.txt')),
        ($RecorderPath + ' ' + ($Arguments -join ' ') + "`r`nexit=$code`r`n" + $text),
        [System.Text.UTF8Encoding]::new($false))
    return @{ Exit = $code; Text = $text; Tag = $tag }
}

function Get-Streams {
    param([string]$File)
    $raw = & $ffprobe -v error -show_streams -of json $File 2>&1 | Out-String
    try { return @(($raw | ConvertFrom-Json).streams) } catch { return @() }
}
# MKV 的音频 stream 没有 duration 字段（本产品不写 TrackDuration），
# 所以「轨道时长」按真解码后的最后一帧 PTS 取——与视频末 PTS 同口径比较。
function Get-LastPtsMs {
    param([string]$File, [string]$Stream)
    $raw = & $ffprobe -v error -select_streams $Stream -show_entries frame=pts_time `
        -of csv=p=0 $File 2>&1
    # "$_"：ffprobe 失败时会往管道里塞 ErrorRecord，不能直接调 .Trim()。
    $values = @($raw | ForEach-Object { "$_".Trim() } | Where-Object { $_ -match '^-?\d+(\.\d+)?$' })
    if ($values.Count -eq 0) { return -1 }
    return [double]$values[-1] * 1000.0
}
function Decode-Track {
    param([string]$File, [string]$Map)
    $raw = (& $ffmpeg -v error -i $File -map $Map -f null - 2>&1 | Out-String)
    $lines = @($raw -split "`r?`n" | Where-Object { $_.Trim() -ne '' })
    return @{ Exit = $LASTEXITCODE; Lines = $lines.Count; Head = ($lines | Select-Object -First 2) -join ';' }
}

$seconds = 5
$dualMkv = Join-Path $script:work 'dual.mkv'
$singleMkv = Join-Path $script:work 'single.mkv'
$dualMp4 = Join-Path $script:work 'dual.mp4'

# ---------------- P0-3 #1~#3：双音轨 MKV ----------------
Write-Host '--- P0-3 双音轨 MKV（--audio-tracks 2）---'
$dualRun = Invoke-Recorder -Arguments @('--record', '--seconds', "$seconds", '--fps', '30',
                                        '--bitrate', '2000', '--format', 'mkv',
                                        '--audio-tracks', '2', '--out', "`"$dualMkv`"")
$dualExit = [int](NumField $dualRun.Text 'record_exit')
$dualTracks = [int](NumField $dualRun.Text 'audio_tracks')
$dualFormat = Field $dualRun.Text 'format'
$dualState = Field $dualRun.Text 'state_sequence'
$dualWriteErrors = [int](NumField $dualRun.Text 'write_errors')
$vLastMs = NumField $dualRun.Text 'video_last_pts_ms'
Assert 'r49-dual-record-exit0' ($dualRun.Exit -eq 0 -and $dualExit -eq 0 -and (Test-Path $dualMkv) -and
        $dualFormat -eq 'mkv' -and $dualTracks -eq 2 -and $dualWriteErrors -eq 0) `
    ("exit={0} record_exit={1} format={2} audio_tracks={3} write_errors={4} 成片={5} state_sequence={6}；证据 {7}.cmd.txt" -f `
        $dualRun.Exit, $dualExit, $dualFormat, $dualTracks, $dualWriteErrors, (Test-Path $dualMkv),
        $dualState, $dualRun.Tag)

$streams = Get-Streams -File $dualMkv
$audioStreams = @($streams | Where-Object { $_.codec_type -eq 'audio' })
Assert 'r49-dual-two-audio-streams' ($audioStreams.Count -eq 2) `
    ("ffprobe 音频流数={0}（期望 2）：{1}" -f $audioStreams.Count,
        (($audioStreams | ForEach-Object { "index={0} codec={1} ch={2}" -f $_.index, $_.codec_name, $_.channels }) -join ' ; '))

$a0 = Decode-Track -File $dualMkv -Map '0:a:0'
$a1 = Decode-Track -File $dualMkv -Map '0:a:1'
Assert 'r49-each-track-decodable' ($a0.Exit -eq 0 -and $a0.Lines -eq 0 -and $a1.Exit -eq 0 -and $a1.Lines -eq 0) `
    ("a:0 exit={0} error行={1}；a:1 exit={2} error行={3}（各自独立 -map 解码，零错误）" -f `
        $a0.Exit, $a0.Lines, $a1.Exit, $a1.Lines)

$a0Last = Get-LastPtsMs -File $dualMkv -Stream 'a:0'
$a1Last = Get-LastPtsMs -File $dualMkv -Stream 'a:1'
$vLast = Get-LastPtsMs -File $dualMkv -Stream 'v:0'
$d0 = [Math]::Abs($a0Last - $vLast)
$d1 = [Math]::Abs($a1Last - $vLast)
Assert 'r49-track-duration-within-100ms' ($a0Last -gt 0 -and $a1Last -gt 0 -and $vLast -gt 0 -and
        $d0 -le 100 -and $d1 -le 100) `
    ("a:0 末PTS={0}ms a:1 末PTS={1}ms 视频末PTS={2}ms；|Δ0|={3}ms |Δ1|={4}ms（≤100ms）" -f `
        [Math]::Round($a0Last, 1), [Math]::Round($a1Last, 1), [Math]::Round($vLast, 1),
        [Math]::Round($d0, 1), [Math]::Round($d1, 1))

# ---------------- P0-3 #4：单音轨零回归 ----------------
Write-Host '--- P0-3 单音轨回归（不带 --audio-tracks）---'
$singleRun = Invoke-Recorder -Arguments @('--record', '--seconds', "$seconds", '--fps', '30',
                                          '--bitrate', '2000', '--format', 'mkv', '--out', "`"$singleMkv`"")
$singleTracks = [int](NumField $singleRun.Text 'audio_tracks')
$singleStreams = @(Get-Streams -File $singleMkv | Where-Object { $_.codec_type -eq 'audio' })
$singleState = Field $singleRun.Text 'state_sequence'
Assert 'r49-single-track-unchanged' ($singleRun.Exit -eq 0 -and $singleTracks -eq 1 -and
        $singleStreams.Count -eq 1 -and $singleState -eq $dualState) `
    ("audio_tracks={0} ffprobe 音频流={1}（期望 1）；state_sequence 与双轨逐字一致=[{2}]" -f `
        $singleTracks, $singleStreams.Count, ($singleState -eq $dualState))

# ---------------- MP4 在 V1.0 只承载 1 条音轨（显式回落，不静默丢） ----------------
Write-Host '--- MP4 单轨上限（显式回落 + 日志）---'
$mp4Run = Invoke-Recorder -Arguments @('--record', '--seconds', "$seconds", '--fps', '30',
                                       '--bitrate', '2000', '--format', 'mp4',
                                       '--audio-tracks', '2', '--out', "`"$dualMp4`"")
$mp4Tracks = [int](NumField $mp4Run.Text 'audio_tracks')
$mp4Streams = @(Get-Streams -File $dualMp4 | Where-Object { $_.codec_type -eq 'audio' })
$mp4Decoded = Decode-Track -File $dualMp4 -Map '0'
Assert 'r49-mp4-audio-track-limit' ($mp4Run.Exit -eq 0 -and $mp4Tracks -eq 1 -and
        $mp4Streams.Count -eq 1 -and $mp4Decoded.Lines -eq 0 -and
        $mp4Run.Text -match 'mp4 supports 1 audio track') `
    ("mp4 audio_tracks={0} ffprobe 音频流={1} 全片解码 error 行={2}；回落原因已记日志={3}；证据 {4}.cmd.txt" -f `
        $mp4Tracks, $mp4Streams.Count, $mp4Decoded.Lines,
        [bool]($mp4Run.Text -match 'mp4 supports 1 audio track'), $mp4Run.Tag)

# ---------------- 参数校验：非法 --audio-tracks → exit 2 ----------------
Write-Host '--- --audio-tracks 参数校验 ---'
foreach ($bad in @('0', '9', 'abc')) {
    $badRun = Invoke-Recorder -Arguments @('--record', '--audio-tracks', $bad, '--out', "`"$dualMkv`"")
    Assert ("r49-audio-tracks-invalid-{0}" -f $bad) ($badRun.Exit -eq 2 -and
            $badRun.Text -match 'invalid --audio-tracks value') `
        ("--audio-tracks {0} → exit={1}（期望 2），原因可见={2}，usage 含该参数={3}；证据 {4}.cmd.txt" -f `
            $bad, $badRun.Exit, [bool]($badRun.Text -match 'invalid --audio-tracks value'),
            [bool]($badRun.Text -match '--audio-tracks'), $badRun.Tag)
}
$missRun = Invoke-Recorder -Arguments @('--record', '--audio-tracks')
Assert 'r49-audio-tracks-missing-exit2' ($missRun.Exit -eq 2 -and
        $missRun.Text -match 'missing --audio-tracks value') `
    ("--audio-tracks 缺值 → exit={0}（期望 2）；证据 {1}.cmd.txt" -f $missRun.Exit, $missRun.Tag)

Write-Host ''
Write-Host ("RESULT: {0} PASS / {1} FAIL ; 产物 {2}" -f $script:pass, $script:fail, $script:work)
if ($script:fail -gt 0) { exit 1 }
exit 0
