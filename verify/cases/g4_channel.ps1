# G4 acceptance case: control channel over srctl (T-17a, round 25)
#
# 硬边界：功能场景一律用产品自带的 srctl，不再自造协议客户端。
# 唯一例外是 F（帧切分）：srctl 无法发送半帧，该场景只做**字节级写入**，
# 不实现协议解析，也不替代任何功能断言。
#
# A 未连接超时        → control=absent，照常录完
# B pause/resume      → srctl 端到端生效，状态序列 Paused→Recording
# C stop              → 提前收尾、exit=0、可播、结尾 Stopping,Idle
# D 暂停中 stop       → 同样干净收尾
# E 错误 token        → 被拒且录制不受影响
# F 帧切分（12 字节头 / 1 字节 payload 之间断开）→ 半帧不消费，补齐后恰 1 条
# G D-36 暂停断线     → ≤2s 自动恢复、pause_auto_resumed=1、成品可播、自行退出
# H 暂停超时          → pause_timeout_hit=1，成片时长=已录内容时长
# I 静默客户端        → control=rejected，wall 与无客户端差 ≤0.2s
# J 重连补当前快照    → 暂停中重连收到 state=Paused（非旧快照）
# K 进程残留          → 用例结束后不得留下 sr-recorder
#
# Usage: pwsh -File verify/cases/g4_channel.ps1 [-BuildDir build-msvc] [-Config Release]

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
$ctl = Join-Path $root (Join-Path $BuildDir "bin\$Config\srctl.exe")
if (-not (Test-Path $exe)) { $exe = Join-Path $root (Join-Path $BuildDir 'bin\sr-recorder.exe') }
if (-not (Test-Path $ctl)) { $ctl = Join-Path $root (Join-Path $BuildDir 'bin\srctl.exe') }
if (-not (Test-Path $exe) -or -not (Test-Path $ctl)) { Write-Host 'FAIL: binaries not found'; exit 2 }

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
$script:started = [System.Collections.Generic.List[System.Diagnostics.Process]]::new()
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
function Start-Recorder {
    param([string]$OutFile, [int]$Seconds, [int]$WaitMs, [string]$LogFile, [string]$PipeName = '', [string]$Extra = '')
    $ctlArgs = @('--record', '--seconds', "$Seconds", '--fps', '30', '--out', "`"$OutFile`"", '--control-pipe')
    if ($PipeName) { $ctlArgs += $PipeName }
    $ctlArgs += @('--control-wait', "$WaitMs")
    if ($Extra) { $ctlArgs += $Extra.Split(' ', [StringSplitOptions]::RemoveEmptyEntries) }
    $proc = Start-Process -FilePath $exe -PassThru -WindowStyle Hidden -RedirectStandardOutput $LogFile `
        -ArgumentList $ctlArgs
    $script:started.Add($proc)
    Register-SrProcess $proc
    return $proc
}
function Invoke-Ctl {
    param([int]$ProcessId, [string]$Command, [string]$Token = 'dev', [int]$HoldMs = 0)
    $pipe = "\\.\pipe\sr.recorder.$ProcessId"
    $ctlArgs = @('--pipe', $pipe, '--token', $Token, $Command)
    if ($HoldMs -gt 0) { $ctlArgs += @('--hold-ms', "$HoldMs") }
    $out = & $ctl @ctlArgs 2>&1 | Out-String
    return @{ Output = $out; Exit = $LASTEXITCODE }
}

$work = Join-Path $root (Join-Path $BuildDir 'channel')
New-Item -ItemType Directory -Force -Path $work | Out-Null
Get-ChildItem $work -Recurse | Remove-Item -Force -Recurse -ErrorAction SilentlyContinue

Write-Host '--- A 未连接超时 ---'
$fileA = Join-Path $work 'a.mkv'
$logA = Join-Path $work 'a.log'
$pA = Start-Recorder -OutFile $fileA -Seconds 4 -WaitMs 300 -LogFile $logA
$pA.WaitForExit(30000) | Out-Null
$outA = Get-Content $logA -Raw
Assert 'absent-control' ((Field $outA 'control') -eq 'absent') "control=$(Field $outA 'control')"
Assert 'absent-recorded' ([Math]::Abs((Get-Duration $fileA) - 4.0) -le 0.1) "duration=$(Get-Duration $fileA)"

Write-Host '--- B pause / resume (srctl) ---'
$fileB = Join-Path $work 'b.mkv'
$logB = Join-Path $work 'b.log'
$pB = Start-Recorder -OutFile $fileB -Seconds 8 -WaitMs 300 -LogFile $logB
Start-Sleep -Milliseconds 1800
$r1 = Invoke-Ctl -ProcessId $pB.Id -Command 'pause'
Assert 'pause-handshake' ($r1.Output -match 'handshake ok') "srctl: $($r1.Output.Trim().Split("`n")[0])"
Assert 'pause-exit' ($r1.Exit -eq 0) "exit=$($r1.Exit)"
Assert 'pause-state' ($r1.Output -match 'state=Paused') 'ack 快照为 Paused'
Start-Sleep -Milliseconds 600
$r2 = Invoke-Ctl -ProcessId $pB.Id -Command 'resume'
Assert 'resume-exit' ($r2.Exit -eq 0) "exit=$($r2.Exit)"
Assert 'resume-state' ($r2.Output -match 'state=Recording') 'ack 快照为 Recording'
$pB.WaitForExit(40000) | Out-Null
$outB = Get-Content $logB -Raw
$seqB = Field $outB 'state_sequence'
Assert 'pause-in-sequence' (([regex]::Matches($seqB, 'Paused')).Count -ge 1) "state_sequence=$seqB"
Assert 'applied-count' ([int](Field $outB 'commands_applied') -ge 2) "commands_applied=$(Field $outB 'commands_applied')"
Assert 'b-playable' (Test-Decodable $fileB) 'decode clean'

Write-Host '--- C stop (srctl) ---'
$fileC = Join-Path $work 'c.mkv'
$logC = Join-Path $work 'c.log'
$pC = Start-Recorder -OutFile $fileC -Seconds 20 -WaitMs 300 -LogFile $logC
Start-Sleep -Milliseconds 2000
$r3 = Invoke-Ctl -ProcessId $pC.Id -Command 'stop'
Assert 'stop-exit' ($r3.Exit -eq 0) "exit=$($r3.Exit)"
$pC.WaitForExit(30000) | Out-Null
$outC = Get-Content $logC -Raw
$durC = Get-Duration $fileC
Assert 'stop-early' ($durC -lt 10.0) "duration=$durC（应远小于 20s）"
Assert 'stop-exit-zero' ([int](Field $outC 'record_exit') -eq 0) "record_exit=$(Field $outC 'record_exit')"
Assert 'stop-sequence-tail' ((Field $outC 'state_sequence') -match 'Stopping,Idle$') "state_sequence=$(Field $outC 'state_sequence')"
Assert 'stop-playable' (Test-Decodable $fileC) 'decode clean'

Write-Host '--- D 暂停中 stop ---'
$fileD = Join-Path $work 'd.mkv'
$logD = Join-Path $work 'd.log'
$pD = Start-Recorder -OutFile $fileD -Seconds 20 -WaitMs 300 -LogFile $logD
Start-Sleep -Milliseconds 1800
$null = Invoke-Ctl -ProcessId $pD.Id -Command 'pause'
Start-Sleep -Milliseconds 500
$r5 = Invoke-Ctl -ProcessId $pD.Id -Command 'stop'
Assert 'paused-stop-exit' ($r5.Exit -eq 0) "exit=$($r5.Exit)"
$pD.WaitForExit(30000) | Out-Null
$outD = Get-Content $logD -Raw
Assert 'paused-stop-sequence' ((Field $outD 'state_sequence') -match 'Paused,Stopping,Idle$') "state_sequence=$(Field $outD 'state_sequence')"
Assert 'paused-stop-playable' (Test-Decodable $fileD) 'decode clean'

Write-Host '--- E 错误 token ---'
$fileE = Join-Path $work 'e.mkv'
$logE = Join-Path $work 'e.log'
$pE = Start-Recorder -OutFile $fileE -Seconds 5 -WaitMs 300 -LogFile $logE
Start-Sleep -Milliseconds 1800
$r6 = Invoke-Ctl -ProcessId $pE.Id -Command 'stop' -Token 'wrong-token'
Assert 'bad-token-rejected' ($r6.Exit -ne 0) "exit=$($r6.Exit)（不得成功）"
$pE.WaitForExit(30000) | Out-Null
$outE = Get-Content $logE -Raw
Assert 'bad-token-recording-ok' ([Math]::Abs((Get-Duration $fileE) - 5.0) -le 0.2) "duration=$(Get-Duration $fileE)（录制未被停止）"
Assert 'bad-token-status' ((Field $outE 'control') -in @('rejected', 'lost', 'absent')) "control=$(Field $outE 'control')"

Write-Host '--- F 帧切分（仅字节写入，不实现协议）---'
$fileF = Join-Path $work 'f.mkv'
$logF = Join-Path $work 'f.log'
$pF = Start-Recorder -OutFile $fileF -Seconds 5 -WaitMs 300 -LogFile $logF
Start-Sleep -Milliseconds 1800
$client = [System.IO.Pipes.NamedPipeClientStream]::new('.', "sr.recorder.$($pF.Id)", [System.IO.Pipes.PipeDirection]::InOut)
$client.Connect(4000)
$ms = [System.IO.MemoryStream]::new()
$bw = [System.IO.BinaryWriter]::new($ms)
$bw.Write([uint32]0x53455243); $bw.Write([uint16]1); $bw.Write([uint16]1)  # Hello
$bw.Write([uint32]5); $bw.Write([uint16]3); $bw.Write([byte[]][char[]]'dev')
$bw.Flush(); $hello = $ms.ToArray()
$client.Write($hello, 0, $hello.Length); $client.Flush()
Start-Sleep -Milliseconds 300
# Command 帧：12 字节头 + 1 字节 payload，在「头齐 / payload 不齐」处断开
$ms2 = [System.IO.MemoryStream]::new(); $bw2 = [System.IO.BinaryWriter]::new($ms2)
$bw2.Write([uint32]0x53455243); $bw2.Write([uint16]1); $bw2.Write([uint16]3); $bw2.Write([uint32]1); $bw2.Write([byte]200)
$bw2.Flush(); $frame = $ms2.ToArray()
$client.Write($frame, 0, 12); $client.Flush()      # 只发头部
Start-Sleep -Milliseconds 400
$client.Write($frame, 12, 1); $client.Flush()      # 再发 payload
Start-Sleep -Milliseconds 500
$client.Dispose()
$pF.WaitForExit(30000) | Out-Null
$outF = Get-Content $logF -Raw
Assert 'split-frame-parsed-once' ([int](Field $outF 'commands_rejected') -eq 1) "commands_rejected=$(Field $outF 'commands_rejected')（恰 1 条）"
Assert 'split-frame-recording-ok' (Test-Decodable $fileF) 'decode clean'

Write-Host '--- G D-36 暂停中断线（自动恢复）---'
$fileG = Join-Path $work 'g.mkv'
$logG = Join-Path $work 'g.log'
$pG = Start-Recorder -OutFile $fileG -Seconds 6 -WaitMs 300 -LogFile $logG
Start-Sleep -Milliseconds 2000
$rG = Invoke-Ctl -ProcessId $pG.Id -Command 'pause'
Assert 'auto-resume-pause-ack' ($rG.Output -match 'state=Paused') "srctl: $($rG.Output.Trim())"
$exitedG = $pG.WaitForExit(30000)
$outG = Get-Content $logG -Raw
$pausedG = [long](Field $outG 'paused_ms')
Assert 'auto-resume-flag' ([int](Field $outG 'pause_auto_resumed') -eq 1) "pause_auto_resumed=$(Field $outG 'pause_auto_resumed')"
Assert 'auto-resume-within-2s' ($pausedG -gt 0 -and $pausedG -le 2000) "paused_ms=$pausedG（≤2000）"
Assert 'auto-resume-control-lost' ((Field $outG 'control') -eq 'lost') "control=$(Field $outG 'control')"
Assert 'auto-resume-self-exit' ($exitedG -and [int](Field $outG 'record_exit') -eq 0) "exited=$exitedG record_exit=$(Field $outG 'record_exit')（不得挂死）"
Assert 'auto-resume-duration' ([Math]::Abs((Get-Duration $fileG) - 6.0) -le 0.1) "duration=$(Get-Duration $fileG)（暂停不计入）"
Assert 'auto-resume-playable' (Test-Decodable $fileG) 'decode clean'

Write-Host '--- H 手动暂停超时收尾（--pause-timeout）---'
# 计时暂停（--pause-duration）按计划终点执行、不受 timeout 截断（F3）；
# 这里用「srctl 保持连接的手动暂停」验证超时兜底：客户端不退出 → 不会走自动恢复。
$fileH = Join-Path $work 'h.mkv'
$logH = Join-Path $work 'h.log'
$pH = Start-Recorder -OutFile $fileH -Seconds 10 -WaitMs 300 -LogFile $logH -Extra '--pause-timeout 3'
Start-Sleep -Milliseconds 2000
$rH = Invoke-Ctl -ProcessId $pH.Id -Command 'pause' -HoldMs 6000
$exitedH = $pH.WaitForExit(20000)
$outH = Get-Content $logH -Raw
$durH = Get-Duration $fileH
$framesH = [int](Field $outH 'frames_encoded')
Assert 'pause-timeout-ack' ($rH.Output -match 'state=Paused') "srctl: $($rH.Output.Trim())"
Assert 'pause-timeout-exit' ($exitedH -and [int](Field $outH 'record_exit') -eq 0) "exited=$exitedH record_exit=$(Field $outH 'record_exit')"
Assert 'pause-timeout-flag' ([int](Field $outH 'pause_timeout_hit') -eq 1) "pause_timeout_hit=$(Field $outH 'pause_timeout_hit')"
Assert 'pause-timeout-no-auto-resume' ([int](Field $outH 'pause_auto_resumed') -eq 0) "pause_auto_resumed=$(Field $outH 'pause_auto_resumed')（客户端在线，不触发自动恢复）"
Assert 'pause-timeout-content-frames' ($framesH -ge 40 -and $framesH -le 70) "frames_encoded=$framesH"
Assert 'pause-timeout-duration' ([Math]::Abs($durH - ($framesH / 30.0)) -le 0.1) "duration=$durH content=$([Math]::Round($framesH/30.0,2))s（暂停不计入）"
Assert 'pause-timeout-sequence-tail' ((Field $outH 'state_sequence') -match 'Paused,Stopping,Idle$') "state_sequence=$(Field $outH 'state_sequence')"
Assert 'pause-timeout-playable' (Test-Decodable $fileH) 'decode clean'

Write-Host '--- I 静默客户端不阻塞（P0）---'
$fileBase = Join-Path $work 'base.mkv'
$logBase = Join-Path $work 'base.log'
$pBase = Start-Process -FilePath $exe -PassThru -WindowStyle Hidden -RedirectStandardOutput $logBase `
    -ArgumentList @('--record', '--seconds', '5', '--fps', '30', '--out', "`"$fileBase`"")
$script:started.Add($pBase)
Register-SrProcess $pBase
$pBase.WaitForExit(30000) | Out-Null
$outBase = Get-Content $logBase -Raw
$wallBase = [double](Field $outBase 'wall_seconds')

$silentPipe = 'sr.g4.silent.' + [guid]::NewGuid().ToString('N')
$readyFile = Join-Path $work 'silent.ready'
Remove-Item $readyFile -Force -ErrorAction SilentlyContinue
$silentJob = Start-Job -ScriptBlock {
    param($PipeName, $ReadyFile)
    Set-Content -Path $ReadyFile -Value 'ready'
    $stream = [System.IO.Pipes.NamedPipeClientStream]::new('.', $PipeName, [System.IO.Pipes.PipeDirection]::InOut)
    $stream.Connect(20000)
    Start-Sleep -Seconds 12
    $stream.Dispose()
} -ArgumentList $silentPipe, $readyFile
for ($i = 0; $i -lt 100 -and -not (Test-Path $readyFile); $i++) { Start-Sleep -Milliseconds 100 }

$fileI = Join-Path $work 'silent.mkv'
$logI = Join-Path $work 'silent.log'
$pI = Start-Recorder -OutFile $fileI -Seconds 5 -WaitMs 2000 -LogFile $logI -PipeName "\\.\pipe\$silentPipe"
$pI.WaitForExit(30000) | Out-Null
Stop-Job $silentJob -ErrorAction SilentlyContinue
Remove-Job $silentJob -Force -ErrorAction SilentlyContinue
$outI = Get-Content $logI -Raw
$wallI = [double](Field $outI 'wall_seconds')
Assert 'silent-control-rejected' ((Field $outI 'control') -eq 'rejected') "control=$(Field $outI 'control')"
Assert 'silent-handshake-timeout' ($outI -match 'handshake timeout') '握手超时被记录'
Assert 'silent-wall-not-delayed' ([Math]::Abs($wallI - $wallBase) -le 0.2) "silent=$wallI base=$wallBase diff=$([Math]::Round([Math]::Abs($wallI - $wallBase), 2))s"
Assert 'silent-duration' ([Math]::Abs((Get-Duration $fileI) - 5.0) -le 0.1) "duration=$(Get-Duration $fileI)"
Assert 'silent-exit' ([int](Field $outI 'record_exit') -eq 0) "record_exit=$(Field $outI 'record_exit')"

Write-Host '--- I2 静默客户端不阻塞主循环（短录制，放大停顿）---'
# 1s 录制 + 已就绪的静默客户端：旧实现的 1s 同步握手会让 wall 明显超出 1s；
# 新实现握手在主循环外推进，wall 应停在 ~1s。这条是 N-2 的红-绿依据。
$fileBase2 = Join-Path $work 'base_short.mkv'
$logBase2 = Join-Path $work 'base_short.log'
$pBase2 = Start-Process -FilePath $exe -PassThru -WindowStyle Hidden -RedirectStandardOutput $logBase2 `
    -ArgumentList @('--record', '--seconds', '1', '--fps', '30', '--out', "`"$fileBase2`"")
$script:started.Add($pBase2)
Register-SrProcess $pBase2
$pBase2.WaitForExit(30000) | Out-Null
$wallBase2 = [double](Field (Get-Content $logBase2 -Raw) 'wall_seconds')

$silentPipe2 = 'sr.g4.silent2.' + [guid]::NewGuid().ToString('N')
$readyFile2 = Join-Path $work 'silent2.ready'
Remove-Item $readyFile2 -Force -ErrorAction SilentlyContinue
$silentJob2 = Start-Job -ScriptBlock {
    param($PipeName, $ReadyFile)
    Set-Content -Path $ReadyFile -Value 'ready'
    $stream = [System.IO.Pipes.NamedPipeClientStream]::new('.', $PipeName, [System.IO.Pipes.PipeDirection]::InOut)
    $stream.Connect(20000)
    Start-Sleep -Seconds 8
    $stream.Dispose()
} -ArgumentList $silentPipe2, $readyFile2
for ($i = 0; $i -lt 100 -and -not (Test-Path $readyFile2); $i++) { Start-Sleep -Milliseconds 100 }

$fileI2 = Join-Path $work 'silent_short.mkv'
$logI2 = Join-Path $work 'silent_short.log'
$pI2 = Start-Recorder -OutFile $fileI2 -Seconds 1 -WaitMs 1000 -LogFile $logI2 -PipeName "\\.\pipe\$silentPipe2"
$pI2.WaitForExit(30000) | Out-Null
Stop-Job $silentJob2 -ErrorAction SilentlyContinue
Remove-Job $silentJob2 -Force -ErrorAction SilentlyContinue
$outI2 = Get-Content $logI2 -Raw
$wallI2 = [double](Field $outI2 'wall_seconds')
Assert 'silent-short-wall-not-delayed' ([Math]::Abs($wallI2 - $wallBase2) -le 0.2) "silent=$wallI2 base=$wallBase2 diff=$([Math]::Round([Math]::Abs($wallI2 - $wallBase2), 2))s"
Assert 'silent-short-duration' ([Math]::Abs((Get-Duration $fileI2) - 1.0) -le 0.1) "duration=$(Get-Duration $fileI2)"

Write-Host '--- J 重连补当前快照（P1）---'
$fileJ = Join-Path $work 'j.mkv'
$logJ = Join-Path $work 'j.log'
$pJ = Start-Recorder -OutFile $fileJ -Seconds 8 -WaitMs 300 -LogFile $logJ
Start-Sleep -Milliseconds 1800
$rJ1 = Invoke-Ctl -ProcessId $pJ.Id -Command 'pause'
Assert 'reconnect-pause-applied' ($rJ1.Output -match 'state=Paused') "srctl: $($rJ1.Output.Trim())"
Start-Sleep -Milliseconds 500
$rJ2 = Invoke-Ctl -ProcessId $pJ.Id -Command 'state'
Assert 'reconnect-snapshot-state' ($rJ2.Output -match 'state=Paused') "重连快照: $($rJ2.Output.Trim())（应等于 state_sequence 末态 Paused）"
Assert 'reconnect-snapshot-elapsed' ($rJ2.Output -match 'elapsed_ms=([1-9]\d*)') "重连快照必须是当前值"
$null = Invoke-Ctl -ProcessId $pJ.Id -Command 'resume'
$pJ.WaitForExit(30000) | Out-Null
$outJ = Get-Content $logJ -Raw
$seqJ = Field $outJ 'state_sequence'
Assert 'reconnect-sequence-exact' ($seqJ -eq 'Idle,Preflight,Countdown,Recording,Paused,Recording,Stopping,Idle') "state_sequence=$seqJ"
Assert 'reconnect-count' ([int](Field $outJ 'control_reconnects') -ge 2) "control_reconnects=$(Field $outJ 'control_reconnects')"
Assert 'reconnect-playable' (Test-Decodable $fileJ) 'decode clean'

Write-Host '--- K 进程残留（Blocker 判据附加项）---'
$leftover = @()
foreach ($proc in $script:started) {
    $proc.Refresh()
    if (-not $proc.HasExited) { $leftover += $proc.Id }
}
Assert 'no-leftover-recorder' ($leftover.Count -eq 0) "leftover=$($leftover -join ',')（不得留下 sr-recorder）"
foreach ($processId in $leftover) { Stop-Process -Id $processId -Force -ErrorAction SilentlyContinue }

Write-Host ''
Write-Host ("RESULT: {0} PASS / {1} FAIL" -f $script:pass, $script:fail)
if ($script:fail -gt 0) { exit 1 }
exit 0
