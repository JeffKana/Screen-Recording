# LT-02 代理：30 分钟静音段 + 恢复段无爆音。
#
# ⚠ 同 LT-01：这是**代理**，不是「8 小时长录制音频链路」证明。
#
# 两段：
#   1) 静音段：recorder 直接录 MuteSeconds（默认 1800s），期间不播放任何声音；
#      ffmpeg volumedetect 断言 mean_volume ≤ -50dB（loopback 无输入应是数字静音）。
#   2) 恢复段：录 RecoverySeconds（默认 30s），第 5s 起播放 1kHz 正弦 10s，
#      断言：有声（mean > -50dB）、不削波（max ≤ -1dB）、起播 0.5s 无满幅尖峰（无爆音）。
#
# Usage: pwsh -File verify/cases/g5_lt02.ps1 [-MuteSeconds 1800] [-RecoverySeconds 30]

param(
    [int]$MuteSeconds = 1800,
    [int]$RecoverySeconds = 30,
    [string]$BuildDir = 'build-msvc',
    [string]$Config = 'Release'
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot '..\lib\Cleanup.ps1')
. (Join-Path $PSScriptRoot '..\lib\Forensics.ps1')
trap { Clear-SrProcesses -IncludeNameFallback; Write-Host "FAIL: $_"; exit 1 }

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$rec = Join-Path $root (Join-Path $BuildDir "bin\$Config\sr-recorder.exe")
if (-not (Test-Path $rec)) { $rec = Join-Path $root (Join-Path $BuildDir 'bin\sr-recorder.exe') }
if (-not (Test-Path $rec)) { Write-Host "FAIL: missing $rec"; exit 2 }
$ffbin = Split-Path -Parent (Get-Command ffmpeg).Source
$ffmpeg = Join-Path $ffbin 'ffmpeg.exe'
$ffplay = Join-Path $ffbin 'ffplay.exe'
$ffprobe = Join-Path $ffbin 'ffprobe.exe'

$work = Join-Path $root ('verify/artifacts/lt02-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
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

function Get-VolumeMetrics {
    param([string]$Path, [double]$Start = -1, [double]$Duration = -1)
    $args = @('-hide_banner', '-nostats')
    if ($Start -ge 0) { $args += @('-ss', "$Start") }
    if ($Duration -gt 0) { $args += @('-t', "$Duration") }
    $args += @('-i', $Path, '-af', 'volumedetect', '-f', 'null', '-')
    $out = & $ffmpeg @args 2>&1 | Out-String
    $mean = if ($out -match 'mean_volume:\s*(-?[\d\.]+) dB') { [double]$Matches[1] } else { 0.0 }
    $max = if ($out -match 'max_volume:\s*(-?[\d\.]+) dB') { [double]$Matches[1] } else { 0.0 }
    return [pscustomobject]@{ Mean = $mean; Max = $max }
}

function Record-Segment {
    param([string]$Path, [int]$Seconds, [string]$Tag)
    $stdout = Join-Path $work "$Tag.out"
    $log = Join-Path $work "$Tag.log"
    $proc = Start-Process -FilePath $rec -PassThru -WindowStyle Hidden -RedirectStandardOutput $stdout `
        -ArgumentList @('--record', '--seconds', "$Seconds", '--fps', '30', '--out', "`"$Path`"",
                        '--log-level', 'info', '--log-file', "`"$log`"")
    Register-SrProcess $proc
    [void]$proc.WaitForExit(($Seconds + 180) * 1000)
    Unregister-SrProcess $proc
    return (Read-TextSafe $stdout)
}

Get-Process sr-ui, sr-recorder -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue

# ---------- 1) 静音段 ----------
Write-Host ("=== LT-02 静音段：{0}s（期间不播放任何声音）===" -f $MuteSeconds)
$muteFile = Join-Path $work 'lt02-mute.mkv'
$muteOut = Record-Segment -Path $muteFile -Seconds $MuteSeconds -Tag 'mute'
$muteExit = [int]($(if ($muteOut -match '(?m)^record_exit=(\d+)') { $Matches[1] } else { -1 }))
$muteMetrics = Get-VolumeMetrics -Path $muteFile
Assert 'mute-exit' ($muteExit -eq 0) "exit=$muteExit"
Assert 'mute-mean-50db' ($muteMetrics.Mean -le -50.0) ("mean={0}dB max={1}dB" -f $muteMetrics.Mean, $muteMetrics.Max)

# ---------- 2) 恢复段（静音中起播 1kHz） ----------
Write-Host ("=== LT-02 恢复段：{0}s，第 5s 起播放 1kHz/10s ===" -f $RecoverySeconds)
$tone = Join-Path $work 'tone.wav'
& $ffmpeg -hide_banner -loglevel error -f lavfi -i 'sine=frequency=1000:sample_rate=48000:duration=10' -y $tone 2>&1 | Out-Null
$recoveryFile = Join-Path $work 'lt02-recovery.mkv'
$stdout = Join-Path $work 'recovery.out'
$log = Join-Path $work 'recovery.log'
$proc = Start-Process -FilePath $rec -PassThru -WindowStyle Hidden -RedirectStandardOutput $stdout `
    -ArgumentList @('--record', '--seconds', "$RecoverySeconds", '--fps', '30', '--out', "`"$recoveryFile`"",
                    '--log-level', 'info', '--log-file', "`"$log`"")
Register-SrProcess $proc
Start-Sleep -Seconds 5
$player = Start-Process -FilePath $ffplay -PassThru -WindowStyle Hidden `
    -ArgumentList @('-nodisp', '-autoexit', '-loglevel', 'quiet', '-volume', '40', "`"$tone`"")
Register-SrProcess $player
[void]$proc.WaitForExit(($RecoverySeconds + 120) * 1000)
Unregister-SrProcess $proc
if (-not $player.HasExited) { Stop-SrProcess $player }
$recoveryOut = Read-TextSafe $stdout
$recoveryExit = [int]($(if ($recoveryOut -match '(?m)^record_exit=(\d+)') { $Matches[1] } else { -1 }))
$overall = Get-VolumeMetrics -Path $recoveryFile
# 起播点的真实位置由 silencedetect 找（固定 5.0s 可能落在静音里，测了个寂寞）。
$detect = & $ffmpeg -hide_banner -nostats -i $recoveryFile -af 'silencedetect=n=-50dB:d=0.3' -f null - 2>&1 | Out-String
$onset = 0.0
$matches = [regex]::Matches($detect, 'silence_end:\s*([\d\.]+)')
if ($matches.Count -gt 0) { [void][double]::TryParse($matches[0].Groups[1].Value, [ref]$onset) }
$windowStart = [Math]::Max(0.0, $onset - 0.1)
$boundary = Get-VolumeMetrics -Path $recoveryFile -Start $windowStart -Duration 0.5
Assert 'recovery-exit' ($recoveryExit -eq 0) "exit=$recoveryExit"
Assert 'recovery-audio-present' ($overall.Mean -gt -50.0) ("mean={0}dB max={1}dB" -f $overall.Mean, $overall.Max)
Assert 'recovery-no-clip' ($overall.Max -le -1.0) ("max={0}dB" -f $overall.Max)
Assert 'recovery-onset-found' ($onset -gt 0.0 -and $onset -lt $RecoverySeconds) ("onset={0}s" -f $onset)
Assert 'recovery-no-pop' ($boundary.Max -le -1.0) ("起播 {0}~{1}s max={2}dB" -f $windowStart, ($windowStart + 0.5), $boundary.Max)

$probe = & $ffprobe -v error -show_entries format=duration -of default=nw=1:nk=1 $recoveryFile 2>&1 | Out-String
$duration = 0.0
[void][double]::TryParse($probe.Trim(), [ref]$duration)
Assert 'recovery-duration' ([Math]::Abs($duration - $RecoverySeconds) -le 1.0) "duration=$duration"

$summary = [pscustomobject]@{
    proxy          = $true
    mute_seconds   = $MuteSeconds
    recovery_seconds = $RecoverySeconds
    mute_mean_db   = $muteMetrics.Mean
    mute_max_db    = $muteMetrics.Max
    recovery_mean_db = $overall.Mean
    recovery_max_db  = $overall.Max
    boundary_max_db  = $boundary.Max
    pass           = $script:pass
    fail           = $script:fail
}
$summary | ConvertTo-Json -Depth 4 | Set-Content -Path (Join-Path $work 'summary.json') -Encoding UTF8
Write-Host ''
Write-Host ("RESULT: {0} PASS / {1} FAIL ; 产物 {2}" -f $script:pass, $script:fail, $work)
if ($script:fail -gt 0) { exit 1 }
exit 0
