# 轮 42 · M8-05#1 新口径与长窗口护栏的红绿自检
#
# 验收方最容易做歪的地方（轮42指令 §2）：「把 A 实现成产品公式的复读机，测试就永远绿」。
# 本用例证明两条断言各自有判别力：
#   ① 主判据（配置码率）：旧 10s 实测口径会假 FAIL 的那组数据，新口径应 PASS；
#   ② 长窗口护栏：真超标 / 短窗口 / 数据缺失都必须 FAIL，且不是 SKIP（拒 C）。
#
# Usage: pwsh -File verify/cases/g5_longwindow_selftest.ps1 [-BuildDir build-msvc]

param([string]$BuildDir = 'build-msvc')

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$case = Join-Path $PSScriptRoot 'g5_storage.ps1'
. (Join-Path $PSScriptRoot '..\lib\LongWindow.ps1')

$script:pass = 0
$script:fail = 0
function Assert {
    param([string]$Name, [bool]$Ok, [string]$Detail)
    if ($Ok) { $script:pass++; Write-Host ("  PASS {0} — {1}" -f $Name, $Detail) }
    else { $script:fail++; Write-Host ("  FAIL {0} — {1}" -f $Name, $Detail) }
}
function Field {
    param([string]$Text, [string]$Name)
    if ($Text -match "(?m)^$Name=([^`r`n]*)") { return $Matches[1].Trim() }
    return ''
}

$configuredBps = Get-ConfiguredBytesPerSec -VideoBitrateKbps 6000   # 774000 B/s
$stopBytes = 128MB
$free = 10263678976.0      # 轮 42 探针 r42-longprobe.txt 的 disk_free_bytes
$est = [Math]::Floor(($free - $stopBytes) / $configuredBps)   # 产品按配置码率算出的估算秒数

Write-Host '--- 断言 1：轮 41 的 10s 实测口径在这组真值上会假 FAIL（红证据）---'
# run3 实测 954,577 B/s（首帧 IDR + 容器开销），est 用配置码率算出 => 旧口径误差 ≈ 23.3%
$shortRate = 954577.0
$oldExpected = ($free - $stopBytes) / $shortRate
$oldRatio = [Math]::Abs($est - $oldExpected) / $oldExpected
Assert 'legacy-10s-window-false-fail' ($oldRatio -gt 0.20) `
    ("旧口径：est={0}s 期望=(free−stop)/954577={1:N0}s 误差={2:P1} > 20% → FAIL（轮 41 run3 复现）" -f `
     $est, $oldExpected, $oldRatio)

Write-Host '--- 断言 2：配置码率主判据在同组真值上 PASS（绿证据）---'
$newExpected = ($free - $stopBytes) / $configuredBps
$newRatio = [Math]::Abs($est - $newExpected) / $newExpected
Assert 'configured-bps-main-criterion-pass' ($newRatio -le 0.20) `
    ("新口径：est={0}s 期望=(free−stop)/{1:N0}={2:N0}s 误差={3:P2} ≤ 20% → PASS" -f `
     $est, $configuredBps, $newExpected, $newRatio)
Assert 'configured-bps-formula-is-6192kbps' ($configuredBps -eq 774000) `
    "6000 kbps 视频 + 192 kbps 音频 = 6192 kbps → $configuredBps B/s（DiskEstimate.h:8）"

Write-Host '--- 断言 3~5：长窗口护栏必须有判别力（不是复读机 / 不藏 SKIP）---'
$short = Get-LongWindowDeviation -FileBytes 46440000 -WallSeconds 10 -ConfiguredBps $configuredBps
Assert 'guard-rejects-10s-window' (-not $short.Valid -and -not $short.Pass) `
    ("10s 短窗口被拒：Valid={0} Reason={1}" -f $short.Valid, $short.Reason)

$over = Get-LongWindowDeviation -FileBytes (1000000 * 60.1) -WallSeconds 60.1 -ConfiguredBps $configuredBps
Assert 'guard-catches-real-30pct-overshoot' ($over.Valid -and -not $over.Pass -and $over.Deviation -gt 0.20) `
    ("真实超标被抓：平均={0:N0} B/s 偏差={1:P1} → FAIL（证明护栏不是永远绿）" -f $over.AvgBps, $over.Deviation)

$missing = Get-LongWindowDeviation -FileBytes 0 -WallSeconds 60.1 -ConfiguredBps $configuredBps
Assert 'guard-data-missing-fails-not-skip' (-not $missing.Valid -and -not $missing.Pass) `
    ("数据缺失判 FAIL 而非 SKIP：Reason={0}（拒 C）" -f $missing.Reason)

Write-Host '--- 断言 6~9：g5_storage 自测注入下两条断言同时存在且各自独立输出 ---'
$goodEst = 'record_exit=0\nwall_seconds=10\nbytes_written=7740000\ndisk_free_bytes=10263678976\nestimated_record_seconds=' + $est + '\n'
$goodLw = 'long_file_bytes=46440000\nlong_wall_seconds=60.03\n'
$badLw = 'long_file_bytes=60010000\nlong_wall_seconds=60.1\n'   # ≈ 998,502 B/s，偏差 29%

$okOut = & pwsh -NoProfile -File $case -BuildDir $BuildDir -SelfTestEstimate `
    -EstimatePayload $goodEst -LongWindowPayload $goodLw 2>&1 | Out-String
$okRc = $LASTEXITCODE
Assert 'both-assertions-present' ($okOut -match 'm8-05-01-estimate-error-20pct' -and
                                 $okOut -match 'm8-05-longwindow-bitrate-20pct') `
    '主判据与长窗口护栏两条断言都出现在同一份输出里'
Assert 'both-assertions-independent-pass' ($okOut -match 'PASS m8-05-01-estimate' -and
                                          $okOut -match 'PASS m8-05-longwindow' -and $okRc -eq 0) `
    ("达标数据下两条各自 PASS，退出码 {0}" -f $okRc)

$badOut = & pwsh -NoProfile -File $case -BuildDir $BuildDir -SelfTestEstimate `
    -EstimatePayload $goodEst -LongWindowPayload $badLw 2>&1 | Out-String
$badRc = $LASTEXITCODE
Assert 'guard-fails-while-main-passes' ($badOut -match 'PASS m8-05-01-estimate' -and
                                       $badOut -match 'FAIL m8-05-longwindow' -and $badRc -eq 1) `
    ("主判据 PASS 但长窗口真超标 → 套件仍 FAIL（退出码 {0}）；只留主判据就会永远绿" -f $badRc)
Assert 'guard-fail-is-not-skip' ($badOut -notmatch 'SKIP m8-05-longwindow' -and
                                 $badOut -match 'FAIL m8-05-longwindow') `
    '真实超标标 FAIL，没有伪装成 SKIP'

Write-Host ''
Write-Host ("RESULT: {0} PASS / {1} FAIL" -f $script:pass, $script:fail)
if ($script:fail -gt 0) { exit 1 }
exit 0
