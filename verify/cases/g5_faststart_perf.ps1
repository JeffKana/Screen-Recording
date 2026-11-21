# 轮 52 P0-1 判据 ①③：MP4 faststart「预留 + 回填」的收尾开销与文件布局（大素材实测）
#
# 背景（轮 51 实测 + 验收报告-轮51 §R2）：
#   旧实现把 mdat 数据区整体后移给 moov 让位，收尾开销 O(文件大小)：
#   1.15GB/240s 实测 0.60s（≈0.52 s/GB），HDD 上 21GB(8h) 外推 ≈280s（用户点停止后卡 4 分钟）。
#   轮 52 改为「开录预留 free box + 收尾回填 moov」，收尾只写 moov 体积的字节。
#
# 判据（`.codebuddy/plans/轮52指令.md` §P0-1）：
#   ① 1.15GB/240s 素材 stop→可播 增量 ≤1s（对比同参数 MKV）；
#   ② 8h 外推 ≤10s（用本用例实测的每 GB 系数，附计算式）；
#   ③ moov 偏移 < mdat 偏移；ffprobe / 全解零 error；sample table 自洽。
#
# 做法：同参数录两遍（mp4 / mkv，240s / 40Mbps），
#   * 主判据用产品自报的 `faststart_close_us`（收尾窗口实测：moov 生成 + 回填/搬移 + 落盘）；
#   * 交叉校验用两次子进程总墙钟差（含进程启动与编码器收尾，容噪）。
#
# Usage:
#   pwsh -File verify/cases/g5_faststart_perf.ps1 [-Seconds 240] [-BitrateKbps 40000] [-SkipFullDecode]

param(
    [int]$Seconds = 240,
    [int]$BitrateKbps = 40000,
    [int]$Fps = 30,
    [string]$BuildDir = 'build-msvc',
    [string]$Config = 'Release',
    [switch]$SkipFullDecode
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\lib\Cleanup.ps1')
. (Join-Path $PSScriptRoot '..\lib\Forensics.ps1')
trap {
    [void](Save-FailureContext -Tag 'g5_faststart_perf' -WorkDirs @($script:work) -Note $_)
    Clear-SrProcesses -IncludeNameFallback
    Write-Host "FAIL: $_"
    exit 1
}

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$rec = Join-Path $root (Join-Path $BuildDir "bin\$Config\sr-recorder.exe")
if (-not (Test-Path $rec)) { $rec = Join-Path $root (Join-Path $BuildDir 'bin\sr-recorder.exe') }
if (-not (Test-Path $rec)) { Write-Host 'FAIL: sr-recorder.exe missing'; exit 2 }
Write-Host ("exe={0} size={1}" -f $rec, (Get-Item $rec).Length)

$ffprobe = (Get-Command ffprobe -ErrorAction SilentlyContinue).Source
$ffmpeg = (Get-Command ffmpeg -ErrorAction SilentlyContinue).Source
if (-not $ffprobe) { Write-Host 'FAIL: ffprobe missing'; exit 4 }
if (-not $ffmpeg) { Write-Host 'FAIL: ffmpeg missing'; exit 4 }

$script:work = Join-Path $root ('verify/artifacts/faststart-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Force -Path $script:work | Out-Null
$script:SrForensicsWorkDirs = @($script:work)
Write-Host ("工作目录：{0}" -f $script:work)

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
function Read-TextSafe {
    param([string]$Path)
    if (-not (Test-Path $Path)) { return '' }
    return [System.IO.File]::ReadAllText($Path)
}
function FieldAny {
    param([string]$Text, [string]$Name)
    if ($Text -match ([regex]::Escape($Name) + "=([^\s`r`n]+)")) { return $Matches[1].Trim() }
    return ''
}
function NumAny {
    param([string]$Text, [string]$Name)
    $value = FieldAny $Text $Name
    if ($value -match '^-?\d+(\.\d+)?$') { return [double]$value }
    return -1
}

# 同参数录一遍，返回墙钟与 stdout。
function Invoke-Record {
    param([string]$Name, [string]$Format)
    $out = Join-Path $script:work ($Name + '.' + $Format)
    $stdout = Join-Path $script:work ($Name + '.out')
    $logFile = Join-Path $script:work ($Name + '.log')
    $recArgs = @('--record', '--seconds', "$Seconds", '--fps', "$Fps", '--bitrate', "$BitrateKbps",
        '--rate-mode', 'cbr', '--format', $Format, '--out', "`"$out`"",
        '--log-level', 'info', '--log-file', "`"$logFile`"")
    Write-Host ("录制 {0}：{1}s / {2}kbps / {3} …" -f $Name, $Seconds, $BitrateKbps, $Format)
    $watch = [System.Diagnostics.Stopwatch]::StartNew()
    $proc = Start-Process -FilePath $rec -PassThru -WindowStyle Hidden `
        -WorkingDirectory $script:work -RedirectStandardOutput $stdout `
        -RedirectStandardError ($stdout + '.err') -ArgumentList $recArgs
    $finished = $proc.WaitForExit(($Seconds + 600) * 1000)
    $watch.Stop()
    $code = if ($finished) { $proc.ExitCode } else { -1 }
    if (-not $finished) { $proc | Stop-Process -Force -ErrorAction SilentlyContinue }
    $text = (Read-TextSafe $stdout) + (Read-TextSafe ($stdout + '.err'))
    [System.IO.File]::WriteAllText((Join-Path $script:work ($Name + '.cmd.txt')),
        ($rec + ' ' + ($recArgs -join ' ') + "`r`nexit=$code`r`n" + $text),
        [System.Text.UTF8Encoding]::new($false))
    Write-Host ("  完成：exit={0} 墙钟={1:N3}s 文件={2}B" -f $code, $watch.Elapsed.TotalSeconds,
        $(if (Test-Path $out) { (Get-Item $out).Length } else { 0 }))
    return @{
        Exit = $code; Text = $text; Path = $out; WallMs = [long]$watch.Elapsed.TotalMilliseconds
        Bytes = $(if (Test-Path $out) { (Get-Item $out).Length } else { 0 })
    }
}

$mp4 = Invoke-Record -Name 'perf-mp4' -Format 'mp4'
$mkv = Invoke-Record -Name 'perf-mkv' -Format 'mkv'

$closeUs = [long](NumAny $mp4.Text 'faststart_close_us')
$reserve = [long](NumAny $mp4.Text 'faststart_reserve')
$moovBytes = [long](NumAny $mp4.Text 'faststart_moov_bytes')
$mode = FieldAny $mp4.Text 'faststart_mode'
$wallDeltaMs = $mp4.WallMs - $mkv.WallMs
$gb = if ($mp4.Bytes -gt 0) { $mp4.Bytes / 1GB } else { 0 }
$perGbMs = if ($gb -gt 0) { $closeUs / 1000.0 / $gb } else { 0 }
$extrapolated8hS = ($perGbMs / 1000.0) * 21.0

# 判据 ①：1.15GB/240s stop→可播 增量 ≤1s（对比同参数 MKV）
Assert 'fs-perf-mp4-close-under-1s' ($mp4.Exit -eq 0 -and $mkv.Exit -eq 0 -and
        $closeUs -ge 0 -and $closeUs -le 1000000) `
    ("MP4 faststart 收尾={0}us（{1:N3}ms，判据 ≤1,000,000us）；对比同参数 MKV 墙钟差={2}ms（{3}s 素材 / {4:N0}kbps）" -f `
        $closeUs, ($closeUs / 1000.0), $wallDeltaMs, $Seconds, $BitrateKbps)
Assert 'fs-perf-mp4-vs-mkv-wall-delta' ([Math]::Abs($wallDeltaMs) -le 2000) `
    ("MP4 墙钟={0}ms vs MKV 墙钟={1}ms，差={2}ms（容噪 ±2000ms：两次录制总时长相同，差异只在收尾）" -f `
        $mp4.WallMs, $mkv.WallMs, $wallDeltaMs)

# 判据 ②：8h 外推 ≤10s（按实测每 GB 系数线性外推，附计算式）
Assert 'fs-perf-8h-extrapolation' ($perGbMs -ge 0 -and $extrapolated8hS -le 10) `
    ("每 GB 收尾 {0:N3}ms/GB × 21GB（8h@6Mbps 外推）= {1:N2}s ≤ 10s；计算式：{2}ms ÷ {3:N3}GB × 21GB" -f `
        $perGbMs, $extrapolated8hS, ($closeUs / 1000.0), $gb)

# 判据 ③：布局（moov < mdat）+ ffprobe + 全解零 error + box 自洽
# 顶层 box 扫描：只读文件头 2MB（预留区可能几十 KB~几 MB，64KB 视野会把 mdat 挡在外面）。
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
        $result += [pscustomobject]@{ Type = $type; Offset = $offset; Size = $size }
        if ($size -le 0) { break }
        $offset += $size
    }
    return $result
}
$boxes = @(Get-TopBoxes -File $mp4.Path)
$boxSummary = ($boxes | ForEach-Object { "{0}@{1}+{2}" -f $_.Type, $_.Offset, $_.Size }) -join ' '
$moovBox = $boxes | Where-Object { $_.Type -eq 'moov' } | Select-Object -First 1
$mdatBox = $boxes | Where-Object { $_.Type -eq 'mdat' } | Select-Object -First 1
$moovOffset = if ($moovBox) { $moovBox.Offset } else { -1 }
$mdatOffset = if ($mdatBox) { $mdatBox.Offset } else { -1 }
$fmtRaw = & $ffprobe -v error -show_entries format=format_name,duration,bit_rate -of default=nw=1 $mp4.Path 2>&1 | Out-String
$duration = -1.0
if ($fmtRaw -match 'duration=([\d\.]+)') { $duration = [double]$Matches[1] }
$probeErrorLines = @(($fmtRaw -split "`r?`n") | Where-Object { $_.Trim() -match 'error' }).Count
$boxCheckRaw = python (Join-Path $root 'verify/tools/mp4_box_check.py') $mp4.Path 2>&1 | Out-String
[System.IO.File]::WriteAllText((Join-Path $script:work 'box-check.txt'), $boxCheckRaw,
    [System.Text.UTF8Encoding]::new($false))
$boxResult = if ($boxCheckRaw -match 'RESULT:\s*(\d+)\s*PASS\s*/\s*(\d+)\s*FAIL') {
    @{ Pass = [int]$Matches[1]; Fail = [int]$Matches[2] }
} else { @{ Pass = 0; Fail = -1 } }

$decodeDetail = 'SKIP'
$decodeOk = $true
if (-not $SkipFullDecode) {
    Write-Host '全片解码中（ffmpeg -v error -f null，判零 error）…'
    $decodeOut = & $ffmpeg -hide_banner -v error -i $mp4.Path -f null - 2>&1 | Out-String
    $decodeErrors = @(($decodeOut -split "`r?`n") | Where-Object { $_.Trim() })
    $decodeOk = ($LASTEXITCODE -eq 0) -and ($decodeErrors.Count -eq 0)
    $decodeDetail = ("ffmpeg exit={0} error 行={1} head=[{2}]" -f $LASTEXITCODE, $decodeErrors.Count,
        (($decodeErrors | Select-Object -First 2) -join ';'))
}
Assert 'fs-perf-layout-and-playable' ($moovOffset -ge 0 -and $mdatOffset -gt $moovOffset -and
        $boxResult.Fail -eq 0 -and [Math]::Abs($duration - $Seconds) -le 1.0 -and
        $probeErrorLines -eq 0 -and $decodeOk) `
    ("box=[{0}]；moov_off={1} < mdat_off={2}；duration={3}s（±1s）；ffprobe error 行={4}；" +
     "mp4_box_check {5} PASS/{6} FAIL；{7}") -f `
        $boxSummary, $moovOffset, $mdatOffset, $duration, $probeErrorLines,
        $boxResult.Pass, $boxResult.Fail, $decodeDetail

$summary = [pscustomobject]@{
    exe_sha256 = (Get-FileHash $rec -Algorithm SHA256).Hash
    seconds = $Seconds; fps = $Fps; bitrate_kbps = $BitrateKbps
    mp4_bytes = $mp4.Bytes; mkv_bytes = $mkv.Bytes
    mp4_wall_ms = $mp4.WallMs; mkv_wall_ms = $mkv.WallMs; wall_delta_ms = $wallDeltaMs
    faststart_mode = $mode; faststart_reserve_bytes = $reserve; faststart_moov_bytes = $moovBytes
    faststart_close_us = $closeUs
    per_gb_ms = [Math]::Round($perGbMs, 3); extrapolated_8h_s = [Math]::Round($extrapolated8hS, 2)
    layout = $boxSummary
    box_check_pass = $boxResult.Pass; box_check_fail = $boxResult.Fail
    duration_s = $duration; decode = $decodeDetail
    pass = $script:pass; fail = $script:fail
    work = $script:work
}
$summary | ConvertTo-Json -Depth 4 | Set-Content -Path (Join-Path $script:work 'summary.json') -Encoding UTF8

Write-Host ''
Write-Host ("FASTSTART-PERF: {0} PASS / {1} FAIL ; 产物 {2}" -f $script:pass, $script:fail, $script:work)
if ($script:fail -gt 0) { exit 1 }
exit 0
