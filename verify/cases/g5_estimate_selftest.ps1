# 轮 41 · R1 字段缺失分支用例（M8-05#1 取数三档语义）
#
# 背景（验收报告-轮40 §2）：g5_storage.ps1:186-194 在 $estOut 为空时把四个字段全取 0，
# $errorRatio 落到 1.0 —— 用「没取到数据」算出了 100% 的误差，产生假 FAIL。
# 本用例做两件事：
#   1) 复刻轮 40 旧语义，喂空输出，断言它确实产出 100% 误差（证明缺陷存在，可作红证据）；
#   2) 调用被测脚本的 -SelfTestEstimate -SimulateMissingFields，断言三档语义的第三档
#      输出「数据未取到」/ SKIP / 0 FAIL，而不是「误差=100%」。
#
# 三档语义（轮 41 指令 §2）：齐全→算误差；缺一次→重试一次；仍缺→SKIP「数据未取到」。
#
# Usage: pwsh -File verify/cases/g5_estimate_selftest.ps1 [-BuildDir build-msvc]

param([string]$BuildDir = 'build-msvc')

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$case = Join-Path $PSScriptRoot 'g5_storage.ps1'

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

Write-Host '--- 断言 1：轮 40 旧语义喂空输出会算出 100% 误差（缺陷复刻）---'
$estOut = ''
$estSeconds = [double](Field $estOut 'estimated_record_seconds')
$freeBytes = [double](Field $estOut 'disk_free_bytes')
$bytesWritten = [double](Field $estOut 'bytes_written')
$wallSeconds = [double](Field $estOut 'wall_seconds')
$measuredBytesPerSec = if ($wallSeconds -gt 0) { $bytesWritten / $wallSeconds } else { 0 }
$stopBytes = 128MB
$expectedSeconds = if ($measuredBytesPerSec -gt 0) { ($freeBytes - $stopBytes) / $measuredBytesPerSec } else { 0 }
$legacyRatio = if ($expectedSeconds -gt 0) { [Math]::Abs($estSeconds - $expectedSeconds) / $expectedSeconds } else { 1.0 }
Assert 'legacy-empty-estout-yields-100pct' ($legacyRatio -eq 1.0) `
    "旧语义 errorRatio=$legacyRatio（四个字段全 0）→ 输出「误差=100.0%」的假 FAIL"

Write-Host '--- 断言 2~5：新语义下人为造字段缺失，必须报「数据未取到」而非算误差 ---'
if (-not (Test-Path $case)) { Write-Host 'FAIL: g5_storage.ps1 missing'; exit 2 }
$out = & pwsh -NoProfile -File $case -BuildDir $BuildDir -SelfTestEstimate -SimulateMissingFields 2>&1 | Out-String
$rc = $LASTEXITCODE
Write-Host ($out.Trim())
Write-Host ("  [子进程退出码] {0}" -f $rc)
Assert 'missing-fields-reports-not-acquired' ($out -match '数据未取到') '输出含「数据未取到」'
Assert 'missing-fields-no-100pct-error' ($out -notmatch '误差\s*=\s*100') '输出不含「误差=100%」'
Assert 'missing-fields-marked-skip' ($out -match 'SKIP m8-05-01') 'M8-05 断言标 SKIP'
Assert 'missing-fields-zero-fail' ($out -match '0 FAIL' -and $rc -eq 0) '0 FAIL 且退出码 0'

Write-Host '--- 断言 6~9：字段齐全时判据仍硬（证明三档语义没把尺子改软）---'
# 合成输出：free=15,000,000,000 B / wall=10s / written=7,740,000 B → 774,000 B/s，
# 实测口径 ≈19,206s。est=19,200 → 误差 <1%（应 PASS）；est=25,000 → 误差 ≈30%（应 FAIL）。
$base = 'record_exit=0\nwall_seconds=10\nbytes_written=7740000\ndisk_free_bytes=15000000000\n'
$good = $base + 'estimated_record_seconds=19200\n'
$bad  = $base + 'estimated_record_seconds=25000\n'
$goodOut = & pwsh -NoProfile -File $case -BuildDir $BuildDir -SelfTestEstimate -EstimatePayload $good 2>&1 | Out-String
$goodRc = $LASTEXITCODE
Assert 'complete-fields-pass' ($goodOut -match 'PASS m8-05-01' -and $goodRc -eq 0) `
    ("齐全且达标 → PASS；{0}" -f (($goodOut -split "`n" | Where-Object { $_ -match 'm8-05-01' }) -join ' ').Trim())
$badOut = & pwsh -NoProfile -File $case -BuildDir $BuildDir -SelfTestEstimate -EstimatePayload $bad 2>&1 | Out-String
$badRc = $LASTEXITCODE
Assert 'complete-fields-real-error-still-fails' ($badOut -match 'FAIL m8-05-01' -and $badRc -eq 1) `
    ("齐全但真实超标 → 仍 FAIL（退出码 {0}）" -f $badRc)
Assert 'real-error-shows-ratio' ($badOut -match '误差\s*=\s*\d+') '真实超标时给出误差数值'
Assert 'real-error-not-masked-as-skip' ($badOut -notmatch '数据未取到' -and $badOut -notmatch 'SKIP m8-05-01') `
    '真实超标没有被伪装成「数据未取到」SKIP'

Write-Host ''
Write-Host ("RESULT: {0} PASS / {1} FAIL" -f $script:pass, $script:fail)
if ($script:fail -gt 0) { exit 1 }
exit 0
