# 轮 48 起步 + 轮 49 收口 + 轮 52 拆分：M3-09#1（MP4 faststart）/ M3-09#3（格式切换）判据
#
# 判据来自 `.codebuddy/plans/轮48指令.md` §P0-2 五条 + §P0-3 三条 +
# `.codebuddy/plans/轮49指令.md` §P0-2（faststart 五条 + .part 语义）+
# `.codebuddy/plans/轮52指令.md` §P0-1/§P1（预留回填 + 判据拆分），验收方可独立重跑。
#   P0-2 #1 ffprobe format_name/duration  #2 ffmpeg 全片解码  #3 帧数=frames_encoded
#          #4 音频时长 vs 视频末 PTS ≤100ms  #5 默认不带 --format 仍产 MKV
#   轮 49  faststart：moov 偏移 < mdat 偏移 / moov 落在前 64KB / box 自检按前置口径
#          .part 语义：录制期强杀 → .part 只剩 ftyp+free+mdat（无 moov）→ 判不可修复且保留
#   轮 52  faststart 判据拆两条（F1-51 裁定，验收报告-轮51 §R3）：
#          ① 通用门槛：moov 偏移 < mdat 偏移（与时长/文件大小无关）；
#          ② 短素材启发式：**≤60s** 的样本要求 moov 结束于前 64KB（起播快的实测代理）；
#             长素材（120s）只判 ①，并显式断言「moov 结束已在 64KB 之外」——
#             证明 ② 不能当通用门槛（moov 体积随时长线性增长）。
#          另加：预留命中（faststart_mode=reserve + 顶层 free 填充）、
#                预留不足回退（--mp4-reserve 4096 → faststart_fallback=move，产物仍可播）。
#   P0-3  状态序列一致 / 两容器均通过 1~4 / 非法值 exit 2 + usage
# 附加护栏：MP4 的 box / sample table 自洽（verify/tools/mp4_box_check.py）、
#           stss 首个同步样本为 1、--format mp4 未给 --out 时默认名跟随格式。
#
# 注：`--mp4-reserve` 是 SR_TEST_HOOKS=ON 的调试开关（交付包不含），
#     本用例按 build-msvc（hooks ON）跑；用交付包 exe 跑会缺这条判据。
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
# 轮 52：faststart_* 是打在一条 `[record] faststart_mode=... ...` 里的空格分隔键值，
# 不是独立的 `key=value` 行，所以按「任意位置的 key=value」取。
function FieldAny {
    param([string]$Text, [string]$Name)
    if ($Text -match ([regex]::Escape($Name) + "=([^\s`r`n]+)")) { return $Matches[1].Trim() }
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
# 轮 49：顶层 box 扫描（只读文件头，不依赖 ffprobe）——faststart 判据要的是字节级偏移。
# 轮 52：默认读 2MB —— 预留区（free box）可能就有几十 KB~几 MB，64KB 视野会把 mdat 挡在外面。
function Get-TopBoxes {
    param([string]$File, [int]$HeadBytes = 2097152)
    $result = @()
    if (-not (Test-Path $File)) { return $result }
    $length = [Math]::Min($HeadBytes, (Get-Item $File).Length)
    $buffer = New-Object byte[] $length
    $stream = [System.IO.File]::OpenRead($File)
    try { [void]$stream.Read($buffer, 0, $length) } finally { $stream.Dispose() }
    $offset = 0
    while ($offset + 8 -le $length) {
        $size = ([int]$buffer[$offset] -shl 24) -bor ([int]$buffer[$offset + 1] -shl 16) -bor `
                ([int]$buffer[$offset + 2] -shl 8) -bor [int]$buffer[$offset + 3]
        $type = [System.Text.Encoding]::ASCII.GetString($buffer, $offset + 4, 4)
        if ($size -eq 1) {
            if ($offset + 16 -gt $length) { break }
            $size = 0
            for ($i = 8; $i -lt 16; $i++) { $size = ($size -shl 8) -bor [int]$buffer[$offset + $i] }
        }
        # 录制中的 .part：mdat 用 64 位 largesize 且尚未回填（0）——记一条再停，
        # 这样「只有 ftyp+mdat、没有 moov」在证据里是看得见的。
        $result += [pscustomobject]@{ Type = $type; Offset = $offset; Size = $size }
        if ($size -le 0) { break }
        $offset += $size
    }
    return $result
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

# ---------------- 轮 49/52 · P0-2 faststart（moov 前置） ----------------
Write-Host '--- 轮 49/52 P0-2 faststart（moov 前置 + 判据拆两条）---'
$boxes = @(Get-TopBoxes -File $mp4)
$boxSummary = ($boxes | ForEach-Object { "{0}@{1}+{2}" -f $_.Type, $_.Offset, $_.Size }) -join ' '
$moovBox = $boxes | Where-Object { $_.Type -eq 'moov' } | Select-Object -First 1
$mdatBox = $boxes | Where-Object { $_.Type -eq 'mdat' } | Select-Object -First 1
$moovOffset = if ($moovBox) { $moovBox.Offset } else { -1 }
$mdatOffset = if ($mdatBox) { $mdatBox.Offset } else { -1 }
# ① 通用门槛（轮 52 拆分后）：moov 必须在 mdat 之前，与时长/文件大小无关。
Assert 'r52-faststart-moov-before-mdat' ($moovOffset -ge 0 -and $mdatOffset -gt $moovOffset) `
    ("顶层 box：{0}；moov_off={1} < mdat_off={2}（通用门槛）" -f $boxSummary, $moovOffset, $mdatOffset)

# ② 短素材启发式（轮 52 拆分后）：≤60s 的样本要求 moov 结束于前 64KB。
$moovEnd = if ($moovBox) { $moovBox.Offset + $moovBox.Size } else { -1 }
Assert 'r52-faststart-short-moov-in-first-64k' ($seconds -le 60 -and $moovEnd -gt 0 -and $moovEnd -le 65536) `
    ("{0}s 样本（≤60s 短素材）：moov 结束偏移={1}（≤65536）；box 序列 {2}" -f $seconds, $moovEnd, $boxSummary)

# ③ 预留命中：日志声明 reserve + 预留区 ≥ moov + 顶层有 free 填充（不再搬 mdat）。
$fsMode = FieldAny $mp4Run.Text 'faststart_mode'
$fsReserve = [long](FieldAny $mp4Run.Text 'faststart_reserve')
$fsMoov = [long](FieldAny $mp4Run.Text 'faststart_moov_bytes')
$fsCloseUs = [long](FieldAny $mp4Run.Text 'faststart_close_us')
$freeBox = $boxes | Where-Object { $_.Type -eq 'free' } | Select-Object -First 1
Assert 'r52-faststart-reserve-hit' ($fsMode -eq 'reserve' -and $fsReserve -ge $fsMoov -and
        $fsMoov -gt 0 -and $null -ne $freeBox -and $fsCloseUs -ge 0) `
    ("faststart_mode={0} reserve={1}B moov={2}B 收尾={3}us；顶层 free={4}@+{5}（预留余量，播放器跳过）" -f `
        $fsMode, $fsReserve, $fsMoov, $fsCloseUs,
        ($null -ne $freeBox), $(if ($freeBox) { $freeBox.Size } else { '-' }))

# ---------------- 轮 52 · P0-1 判据 #4：预留不足 → 回退搬移且产物可播 ----------------
Write-Host '--- 轮 52 P0-1 回退路径（人为把预留调到 4KB）---'
$fallbackMp4 = Join-Path $script:work 'r52-fallback.mp4'
$fbRun = Invoke-Recorder -Arguments @('--record', '--seconds', "$seconds", '--fps', '30',
    '--bitrate', '2000', '--format', 'mp4', '--mp4-reserve', '4096', '--out', "`"$fallbackMp4`"")
$fbMode = FieldAny $fbRun.Text 'faststart_mode'
$fbReserve = [long](FieldAny $fbRun.Text 'faststart_reserve')
$fbMoov = [long](FieldAny $fbRun.Text 'faststart_moov_bytes')
$fbBoxes = @(Get-TopBoxes -File $fallbackMp4)
$fbSummary = ($fbBoxes | ForEach-Object { "{0}@{1}+{2}" -f $_.Type, $_.Offset, $_.Size }) -join ' '
$fbMoovBox = $fbBoxes | Where-Object { $_.Type -eq 'moov' } | Select-Object -First 1
$fbMdatBox = $fbBoxes | Where-Object { $_.Type -eq 'mdat' } | Select-Object -First 1
$fbMoovOffset = if ($fbMoovBox) { $fbMoovBox.Offset } else { -1 }
$fbMdatOffset = if ($fbMdatBox) { $fbMdatBox.Offset } else { -1 }
Assert 'r52-faststart-fallback-log' ($fbRun.Exit -eq 0 -and $fbMode -eq 'move' -and
        $fbReserve -eq 4096 -and $fbMoov -gt $fbReserve -and
        $fbRun.Text -match 'faststart_fallback=move') `
    ("exit={0} faststart_mode={1} reserve={2}B moov={3}B（moov > 预留 → 回退）；日志行含 faststart_fallback=move={4}" -f `
        $fbRun.Exit, $fbMode, $fbReserve, $fbMoov, [bool]($fbRun.Text -match 'faststart_fallback=move'))
$fbFmt = Probe-Json -File $fallbackMp4 -Entries 'format=format_name,duration'
$fbDuration = if ($fbFmt) { [double]$fbFmt.format.duration } else { -1 }
$fbDec = Decode-Clean -File $fallbackMp4
$fbBoxRaw = python (Join-Path $root 'verify/tools/mp4_box_check.py') $fallbackMp4 2>&1 | Out-String
$fbBoxResult = if ($fbBoxRaw -match 'RESULT:\s*(\d+)\s*PASS\s*/\s*(\d+)\s*FAIL') {
    @{ Pass = [int]$Matches[1]; Fail = [int]$Matches[2] }
} else { @{ Pass = 0; Fail = -1 } }
Assert 'r52-faststart-fallback-playable' ($fbMoovOffset -ge 0 -and $fbMdatOffset -gt $fbMoovOffset -and
        [Math]::Abs($fbDuration - $seconds) -le 0.2 -and
        $fbDec.Exit -eq 0 -and $fbDec.Lines -eq 0 -and $fbBoxResult.Fail -eq 0) `
    ("回退产物：box=[{0}]；moov_off={1} < mdat_off={2}；duration={3}s；ffmpeg error 行={4}；mp4_box_check {5} PASS/{6} FAIL" -f `
        $fbSummary, $fbMoovOffset, $fbMdatOffset, $fbDuration, $fbDec.Lines,
        $fbBoxResult.Pass, $fbBoxResult.Fail)

# ---------------- 轮 52 · P1 判据拆分：长素材（120s）只判通用门槛 ----------------
Write-Host '--- 轮 52 P1 长素材（120s）只判 moov 前置，不判前 64KB ---'
$longSeconds = 120
$longMp4 = Join-Path $script:work 'r52-long.mp4'
$longRun = Invoke-Recorder -Arguments @('--record', '--seconds', "$longSeconds", '--fps', '30',
    '--bitrate', '1000', '--format', 'mp4', '--out', "`"$longMp4`"") -WaitMs 300000
$longBoxes = @(Get-TopBoxes -File $longMp4)
$longSummary = ($longBoxes | ForEach-Object { "{0}@{1}+{2}" -f $_.Type, $_.Offset, $_.Size }) -join ' '
$longMoov = $longBoxes | Where-Object { $_.Type -eq 'moov' } | Select-Object -First 1
$longMdat = $longBoxes | Where-Object { $_.Type -eq 'mdat' } | Select-Object -First 1
$longMoovOffset = if ($longMoov) { $longMoov.Offset } else { -1 }
$longMdatOffset = if ($longMdat) { $longMdat.Offset } else { -1 }
$longMoovEnd = if ($longMoov) { $longMoov.Offset + $longMoov.Size } else { -1 }
$longFmt = Probe-Json -File $longMp4 -Entries 'format=format_name,duration'
$longDuration = if ($longFmt) { [double]$longFmt.format.duration } else { -1 }
$longDec = Decode-Clean -File $longMp4
$longBoxRaw = python (Join-Path $root 'verify/tools/mp4_box_check.py') $longMp4 2>&1 | Out-String
$longBoxResult = if ($longBoxRaw -match 'RESULT:\s*(\d+)\s*PASS\s*/\s*(\d+)\s*FAIL') {
    @{ Pass = [int]$Matches[1]; Fail = [int]$Matches[2] }
} else { @{ Pass = 0; Fail = -1 } }
$longDetail = ("{0}s 素材：box=[{1}]；moov_off={2} < mdat_off={3}（通用门槛成立）；moov 结束={4} > 65536" +
    "（短素材 64KB 启发式对长素材必然不成立，故不作通用门槛）；duration={5}s；ffmpeg error 行={6}；" +
    "mp4_box_check {7} PASS/{8} FAIL（64KB 项按口径跳过）") -f `
    $longSeconds, $longSummary, $longMoovOffset, $longMdatOffset, $longMoovEnd,
    $longDuration, $longDec.Lines, $longBoxResult.Pass, $longBoxResult.Fail
Assert 'r52-faststart-long-sample-generic-only' ($longRun.Exit -eq 0 -and
        $longMoovOffset -ge 0 -and $longMdatOffset -gt $longMoovOffset -and
        $longMoovEnd -gt 65536 -and [Math]::Abs($longDuration - $longSeconds) -le 0.5 -and
        $longDec.Exit -eq 0 -and $longDec.Lines -eq 0 -and $longBoxResult.Fail -eq 0) `
    $longDetail

# ---------------- 轮 49 · P0-2 `.part` 语义（缓冲期崩溃 → 不可修复但保留） ----------------
Write-Host '--- 轮 49 P0-2 .part 语义（录制期强杀）---'
$crashDir = Join-Path $script:work 'crash'
New-Item -ItemType Directory -Force -Path $crashDir | Out-Null
$crashOut = Join-Path $crashDir 'crash.mp4'
$crashPart = $crashOut + '.part'
$crashProc = Start-Process -FilePath $RecorderPath -PassThru -WindowStyle Hidden `
    -WorkingDirectory $crashDir -ArgumentList @('--record', '--seconds', '30', '--fps', '30',
        '--bitrate', '2000', '--format', 'mp4', '--out', "`"$crashOut`"")
$deadline = (Get-Date).AddSeconds(12)
while ((Get-Date) -lt $deadline) {
    if ((Test-Path $crashPart) -and (Get-Item $crashPart).Length -gt 65536) { break }
    Start-Sleep -Milliseconds 200
}
$partGrew = (Test-Path $crashPart) -and (Get-Item $crashPart).Length -gt 65536
# 强杀（模拟崩溃）：不经过 Close，因此 .part 里不会有 moov。
$crashProc | Stop-Process -Force -ErrorAction SilentlyContinue
$crashProc.WaitForExit(5000) | Out-Null
$partBoxes = @(Get-TopBoxes -File $crashPart)
$partSummary = ($partBoxes | ForEach-Object { "{0}@{1}" -f $_.Type, $_.Offset }) -join ' '
$partHasMoov = ($partBoxes | Where-Object { $_.Type -eq 'moov' }).Count -gt 0
Assert 'r49-part-written-without-moov' ($partGrew -and -not $partHasMoov) `
    ("缓冲期强杀后 .part 存在={0} 大小={1}B 顶层 box=[{2}]（faststart 的 moov 只在收尾生成，崩溃时不在盘上）" -f `
        (Test-Path $crashPart),
        $(if (Test-Path $crashPart) { (Get-Item $crashPart).Length } else { 0 }), $partSummary)

$probeBefore = (Get-Item $crashPart -ErrorAction SilentlyContinue)
$repairOut = & $RecorderPath --repair $crashPart 2>&1 | Out-String
$repairExit = $LASTEXITCODE
$probeRepairable = if ($repairOut -match 'probe_repairable=(\d+)') { [int]$Matches[1] } else { -1 }
$probeReason = if ($repairOut -match 'probe_reason=([^\r\n]+)') { $Matches[1].Trim() } else { '' }
Assert 'r49-part-unrepairable-with-reason' ($repairExit -ne 0 -and $probeRepairable -eq 0 -and
        $probeReason -match 'iso-bmff|moov') `
    ("--repair → exit={0}（期望非 0）probe_repairable={1} reason=[{2}]；原始输出 {3}" -f `
        $repairExit, $probeRepairable, $probeReason, 'crash-repair.txt')
[System.IO.File]::WriteAllText((Join-Path $script:work 'crash-repair.txt'), $repairOut,
    [System.Text.UTF8Encoding]::new($false))

$probeAfter = Get-Item $crashPart -ErrorAction SilentlyContinue
Assert 'r49-part-kept-intact' ($null -ne $probeAfter -and $null -ne $probeBefore -and
        $probeAfter.Length -eq $probeBefore.Length) `
    ("判不可修复后 .part 仍保留：大小 {0} → {1}（不删除、不产出「可播但时长错」的成品）" -f `
        $(if ($probeBefore) { $probeBefore.Length } else { -1 }),
        $(if ($probeAfter) { $probeAfter.Length } else { -1 }))
Assert 'r49-part-no-final-output' (-not (Test-Path $crashOut)) `
    ("崩溃后没有成品 .mp4：{0}（未完成的内容只以 .part 形态保留）" -f (Test-Path $crashOut))

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
