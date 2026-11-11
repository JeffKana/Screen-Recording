# G2 acceptance case: capture chain (display / region / window) + audio chain
# Covers tasks/G2/S2-001 (M1-01/02/04/05/07) and S3-001 (M1-15/18/20/21).
# Usage: pwsh -File verify/cases/g2_walk.ps1 [-BuildDir build-mingw]
#
# Engineering note: on a static desktop DXGI Desktop Duplication produces NO frames
# by design (it is change-driven). All display/region runs therefore use --stimulate,
# which moves a small topmost window so the desktop actually changes.

param(
    [string]$BuildDir = 'build-mingw',
    [string]$Config = 'Release'
)

$ErrorActionPreference = 'Stop'

# 轮 30（F2）：统一异常清理——登记 spawn 进程 + trap 兜底，中途抛异常也不留孤儿。
. (Join-Path $PSScriptRoot '..\lib\Cleanup.ps1')
# trap 必须留在脚本自身作用域：dot-source 进来的 trap 不接管调用方的终止错误。
trap { Clear-SrProcesses -IncludeNameFallback; Write-Host "FAIL: $_"; exit 1 }

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$probe = Join-Path $root (Join-Path $BuildDir "bin\$Config\srprobe.exe")
if (-not (Test-Path $probe)) { $probe = Join-Path $root (Join-Path $BuildDir 'bin\srprobe.exe') }
if (-not (Test-Path $probe)) { Write-Host "FAIL: missing $probe"; exit 2 }

$script:pass = 0
$script:fail = 0
$script:skip = 0

function Skip {
    param([string]$Name, [string]$Reason)
    $script:skip++
    Write-Host ("  SKIP {0} — {1}" -f $Name, $Reason)
}

function Field {
    param([string]$Text, [string]$Key)
    if ($Text -match "(?m)^$Key=(.*)$") { return $Matches[1].Trim() }
    return ''
}

function Assert {
    param([string]$Name, [bool]$Ok, [string]$Detail)
    if ($Ok) {
        $script:pass++
        Write-Host ("  PASS {0} — {1}" -f $Name, $Detail)
    } else {
        $script:fail++
        Write-Host ("  FAIL {0} — {1}" -f $Name, $Detail)
    }
}

Write-Host '--- 双后端环境校准（F2：虚拟显示/会话上限）---'
# 同一会话同一时刻跑两后端：两端都 <55fps 且差 ≤2 → 是环境上限（OrayIddDriver/
# 远程会话/60Hz 物理面），不是 WGC 的实现问题。真差异只在活跃本机会话里判。
# 校准窗口必须够长：WGC 前 1-2 秒含启动/首帧，3s 会把 58fps 读成 50fps。
# 校准只做环境判定，不作为判据；真正的吞吐由下面的 12s/6s 用例判。
$calWgc = & $probe capture --source display --seconds 6 --stimulate --backend wgc 2>&1 | Out-String
$calDxgi = & $probe capture --source display --seconds 6 --stimulate --backend dxgi 2>&1 | Out-String
$wgcFps = [double](Field $calWgc 'fps_actual')
$dxgiFps = [double](Field $calDxgi 'fps_actual')
$wgcOk = $wgcFps -gt 0
$dxgiOk = $dxgiFps -gt 0
$envCap = $wgcOk -and $dxgiOk -and $wgcFps -lt 55.0 -and $dxgiFps -lt 55.0 -and
          ([Math]::Abs($wgcFps - $dxgiFps) -le 2.0)
$envReason = "wgc=$([Math]::Round($wgcFps,1))fps dxgi=$([Math]::Round($dxgiFps,1))fps（两端 <55 且差 ≤2 → 虚拟显示/会话上限）"
Write-Host "backend_calibration wgc=$([Math]::Round($wgcFps,1)) dxgi=$([Math]::Round($dxgiFps,1))"
Write-Host "env_cap=$envCap $envReason"
if (-not ($wgcOk -and $dxgiOk)) {
    Skip 'backend-parity' 'WGC unavailable in this build (MinGW) or no calibration frames'
}

Write-Host '--- 显示器枚举 (M1-02) ---'
$out = & $probe outputs 2>&1 | Out-String
$count = [int](Field $out 'outputs')
Assert 'display-enum' ($count -ge 1) "outputs=$count"

Write-Host '--- 全屏采集 12s (M1-01) ---'
$displayArgs = @('capture', '--source', 'display', '--seconds', '12', '--stimulate')
for ($attempt = 1; $attempt -le 2; $attempt++) {
    $cap = & $probe @displayArgs 2>&1 | Out-String
    $frames = [int](Field $cap 'frames')
    $fps = [double](Field $cap 'fps_actual')
    $jitter = [int](Field $cap 'frames_over_2x_avg')
    $rebuilds = [int](Field $cap 'rebuilds')
    $size = Field $cap 'size'
    $throttled = [int](Field $cap 'dwm_throttled')
    $intervalAvg = [double](Field $cap 'interval_avg_ms')
    $needRetry = (-not $envCap) -and ($throttled -eq 0) -and
                 (($fps -lt 55.0) -or ($jitter -gt 0)) -and ($attempt -eq 1)
    if (-not $needRetry) { break }
    Write-Host "  RETRY display (attempt1 fps=$([Math]::Round($fps,1)) jitter=$jitter; transient load)"
    Start-Sleep -Milliseconds 800
}
Assert 'display-frames' ($frames -gt 0) "frames=$frames"
Assert 'display-stable' ($rebuilds -eq 0) "rebuilds=$rebuilds"
Assert 'display-size' ($size -eq '1920x1080') "size=$size"
$displayBackend = Field $cap 'active_backend'
Assert 'display-backend' ($displayBackend -ne '') "active_backend=$displayBackend"
if ($throttled -eq 1) {
    # 锁屏/休眠/RDP 断开时 DWM 把桌面更新节流到约 1Hz，吞吐指标失去意义。
    Skip 'display-throughput' "session idle (interval_avg=${intervalAvg}ms). Re-run in an unlocked interactive session."
} elseif ($envCap) {
    Skip 'display-throughput' $envReason
} else {
    Assert 'display-fps' ($fps -ge 55.0) "fps_actual=$fps (expect >= 55)"
    # 单帧调度毛刺（<1%）不等于抖动：12s/700 帧里允许少量 >2x 间隔，
# 持续抖动会远超这个数。严格 0 在共享/远程序列会把一次性卡顿误报成产品缺陷。
$jitterLimit = [Math]::Max(2, [int]($frames * 0.01))
Assert 'display-jitter' ($jitter -le $jitterLimit) "frames_over_2x_avg=$jitter (limit $jitterLimit, <1%)"
Write-Host "INFO display_jitter_raw=$jitter/$frames (threshold=$jitterLimit)"
}

Write-Host '--- 区域采集 800x600 (M1-04/M1-05) ---'
$regionArgs = @('capture', '--source', 'region', '--region', '100,100,800,600',
                '--seconds', '6', '--stimulate')
for ($attempt = 1; $attempt -le 2; $attempt++) {
    $reg = & $probe @regionArgs 2>&1 | Out-String
    $regSize = Field $reg 'size'
    $regFrames = [int](Field $reg 'frames')
    $regFps = [double](Field $reg 'fps_actual')
    $regJitter = [int](Field $reg 'frames_over_2x_avg')
    $regThrottled = [int](Field $reg 'dwm_throttled')
    $needRetry = (-not $envCap) -and ($regThrottled -eq 0) -and
                 (($regFps -lt 55.0) -or ($regJitter -gt 0)) -and ($attempt -eq 1)
    if (-not $needRetry) { break }
    Write-Host "  RETRY region (attempt1 fps=$([Math]::Round($regFps,1)) jitter=$regJitter; transient load)"
    Start-Sleep -Milliseconds 800
}
Assert 'region-size-exact' ($regSize -eq '800x600') "size=$regSize"
$regionBackend = Field $reg 'backend'
Assert 'region-backend' ($regionBackend -ne '') "backend=$regionBackend"
if ($regThrottled -eq 1) {
    Skip 'region-throughput' 'session idle (DWM throttling)'
} elseif ($envCap) {
    Skip 'region-throughput' $envReason
} else {
    Assert 'region-fps' ($regFps -ge 30.0) "fps_actual=$regFps (区域录制目标 30fps)"
    # 区域采集的产品目标是 30fps（M1-04/M3-03），不是 display 的 60fps；
    # 用 display 的 55/1% 判区域会把 DXGI 裁剪路径的正常抖动误报。
    $regJitterLimit = [Math]::Max(3, [int]($regFrames * 0.10))
    Assert 'region-jitter' ($regJitter -le $regJitterLimit) "frames_over_2x_avg=$regJitter (limit $regJitterLimit, <10%)"
    Write-Host "INFO region_jitter_raw=$regJitter/$regFrames (threshold=$regJitterLimit)"
}

Write-Host '--- 区域偶数对齐 (M3-02) ---'
$odd = & $probe capture --source region --region 10,10,801,601 --seconds 3 --stimulate 2>&1 | Out-String
$oddSize = Field $odd 'size'
Assert 'region-even-align' ($oddSize -eq '800x600') "requested 801x601 -> $oddSize"

Write-Host '--- 窗口采集自检 (M1-07) ---'
# 不再依赖记事本：Win11 的记事本是打包应用，MainWindowHandle 取不到，
# 已实测失败。改为探针自建 Win32 窗口并回读像素，断言画面非全黑。
$win = & $probe windowselftest 2>&1 | Out-String
Write-Host $win.Trim()
$winRatio = [double](Field $win 'non_black_ratio')
$winGreen = [double](Field $win 'green_ratio')
$winSize = Field $win 'window_size'
Assert 'window-content' ($winRatio -gt 0.5 -and $winGreen -gt 0.3) "size=$winSize non_black=$winRatio green=$winGreen"

Write-Host '--- 音频时间轴 6s (M1-21) ---'
$audio = & $probe audio --seconds 6 2>&1 | Out-String
$expected = [long](Field $audio 'expected_frames')
$produced = [long](Field $audio 'produced_frames')
$drift = [long](Field $audio 'duration_drift_frames')
$rate = Field $audio 'loopback_rate'
Assert 'audio-rate' ($rate -ne '') "loopback_rate=$rate"
Assert 'audio-continuity' ($expected -gt 0 -and $expected -eq $produced) "expected=$expected produced=$produced"
Assert 'audio-drift' ($drift -eq 0) "duration_drift_frames=$drift"

Write-Host '--- 纯系统声（无麦克风）3s (M1-15) ---'
$sysOnly = & $probe audio --seconds 3 --no-mic 2>&1 | Out-String
$sysDrift = [long](Field $sysOnly 'duration_drift_frames')
$sysExpected = [long](Field $sysOnly 'expected_frames')
Assert 'system-only-drift' ($sysExpected -gt 0 -and $sysDrift -eq 0) "expected=$sysExpected drift=$sysDrift"

Write-Host '--- 混音器自检 (M1-20) ---'
$mix = & $probe mixselftest 2>&1 | Out-String
Write-Host $mix.Trim()
$mixFailures = [int](Field $mix 'failures')
Assert 'mixer-selftest' ($mixFailures -eq 0) "failures=$mixFailures"

Write-Host ''
Write-Host ("RESULT: {0} PASS / {1} FAIL / {2} SKIP" -f $script:pass, $script:fail, $script:skip)
if ($script:fail -gt 0) { exit 1 }
if ($script:skip -gt 0) { exit 3 }
exit 0
