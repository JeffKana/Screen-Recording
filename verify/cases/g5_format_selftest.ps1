# 轮 43 · F2 自检：取数失败与断言失败分离
#
# 判据（轮 43 指令 §3 #2）：人为造一次 recorder 不启动 → 输出应是 1 条 SKIP 而非 10 条 FAIL。
# 红证据（自然复现，非构造）：verify/artifacts/format-20261110-183428/summary.json
#   pass=4 / fail=10，context.txt 记 fps60.mkv.part=108B、fps60.out=0B；
#   验收报告-轮42「三种失败形态」第 1 次 4/10。
#
# 本自检的所有分支都不启动 recorder（模拟模式不清理进程，不干扰同机录制）。
# Usage: pwsh -File verify/cases/g5_format_selftest.ps1

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$case = Join-Path $PSScriptRoot 'g5_format.ps1'
# 轮 44：本轮护栏的原始输出落 V1-013 的 artifacts（沿用 V1-008 的文件域会造成跨域写）。
$art = Join-Path $root 'tasks/G6/V1-013/artifacts'
if (-not (Test-Path $art)) { New-Item -ItemType Directory -Force -Path $art | Out-Null }

$script:pass = 0; $script:fail = 0
function Assert {
    param([string]$Name, [bool]$Ok, [string]$Detail)
    if ($Ok) { $script:pass++; Write-Host ("  PASS {0} — {1}" -f $Name, $Detail) }
    else { $script:fail++; Write-Host ("  FAIL {0} — {1}" -f $Name, $Detail) }
}
function Invoke-Case {
    # 参数名不能用 $Args：那是 PowerShell 自动变量，splatting 会展开成空（首轮自检实测到）。
    param([string]$Tag, [string[]]$CaseArgs)
    $out = & pwsh -NoProfile -File $case @CaseArgs 2>&1 | Out-String
    $rc = $LASTEXITCODE
    [System.IO.File]::WriteAllText((Join-Path $art "$Tag.txt"),
        ($out + "`nEXIT=$rc`n"), [System.Text.UTF8Encoding]::new($false))
    $skipLines = @(($out -split "`n") | Where-Object { $_ -match '^\s*SKIP ' })
    $failLines = @(($out -split "`n") | Where-Object { $_ -match '^\s*FAIL ' })
    $passLines = @(($out -split "`n") | Where-Object { $_ -match '^\s*PASS ' })
    $result = (($out -split "`n") | Where-Object { $_ -match '^RESULT:' } | Select-Object -Last 1)
    return [pscustomobject]@{
        Out = $out; Rc = $rc; Skip = $skipLines.Count; Fail = $failLines.Count
        Pass = $passLines.Count; Result = $(if ($result) { $result.Trim() } else { '' })
        SkipText = $(if ($skipLines.Count) { $skipLines[0].Trim() } else { '' })
    }
}

Write-Host '--- F2 ① 造假 recorder 不启动：应为 1 SKIP / 0 FAIL / 退出码 0 ---'
$r1 = Invoke-Case -Tag 'f2-no-start' -CaseArgs @('-SimulateNoStart')
Assert 'f2-no-start-one-skip' ($r1.Skip -eq 1 -and $r1.Fail -eq 0 -and $r1.Pass -eq 0) `
    ("SKIP={0} FAIL={1} PASS={2}；{3}" -f $r1.Skip, $r1.Fail, $r1.Pass, $r1.SkipText)
Assert 'f2-no-start-exit-zero' ($r1.Rc -eq 0) ("退出码={0}（SKIP 不计 PASS/FAIL，用例判不可判而非失败）" -f $r1.Rc)
Assert 'f2-no-start-retried-once' ($r1.Out -match 'attempts=2') `
    ('文案含 attempts=2，证明「缺失→重试一次→仍缺→SKIP」链路真实走过')
Assert 'f2-no-start-stdout-tail' ($r1.Out -match 'stdout 尾 20 行' -and $r1.Out -match 'stdout 为空') `
    '输出附 stdout 尾 20 行与「stdout 为空」说明'
Assert 'f2-no-start-result-line' ($r1.Result -match '0 PASS / 0 FAIL / 1 SKIP') `
    ("RESULT 行：{0}" -f $r1.Result)

Write-Host '--- F2 ② 产品明确失败（record_exit=6）：必须判 FAIL，不得伪装 SKIP ---'
$r2 = Invoke-Case -Tag 'f2-product-failure' -CaseArgs @('-SimulateProductFailure')
Assert 'f2-product-failure-is-fail' ($r2.Fail -ge 1 -and $r2.Skip -eq 0 -and $r2.Rc -eq 1) `
    ("FAIL={0} SKIP={1} 退出码={2}；{3}" -f $r2.Fail, $r2.Skip, $r2.Rc, $r2.Result)
Assert 'f2-product-failure-not-skip' ($r2.Out -match '产品明确失败' -and $r2.Out -notmatch '数据未取到') `
    '文案明确「产品明确失败」，不出现「数据未取到」'

Write-Host '--- F2 ③ 关键字段缺失：重试一次仍缺 → SKIP（不拿 0 算结论）---'
$r3 = Invoke-Case -Tag 'f2-missing-fields' -CaseArgs @('-SimulateMissingFields')
Assert 'f2-missing-fields-skip' ($r3.Skip -eq 1 -and $r3.Fail -eq 0 -and $r3.Rc -eq 0) `
    ("SKIP={0} FAIL={1} 退出码={2}" -f $r3.Skip, $r3.Fail, $r3.Rc)
Assert 'f2-missing-fields-reason' ($r3.Out -match 'record_exit 字段缺失|stdout 为空') `
    ('文案指出具体缺失字段，而不是把空串当 0 去算：' + $r3.SkipText)

Write-Host '--- F2 ④ 判据未被改软（改尺子检测）---'
$text = [System.IO.File]::ReadAllText($case, [System.Text.Encoding]::UTF8)
$guards = @(
    # 轮 44：逐秒 spread 断言被裁定替换成 5 秒滑窗 spread，但判据数字仍是 -le 0.10。
    @{ Name = 'f2-threshold-spread-10pct'; Pattern = '\$w\.spread -le 0\.10' },
    @{ Name = 'f2-threshold-nominal-20pct'; Pattern = 'cbrStats\.deviation -le 0\.20' },
    @{ Name = 'f2-threshold-vbr-peak-102pct'; Pattern = 'CapKbps \* 1\.02' },
    @{ Name = 'f2-threshold-vbr-nominal-20pct'; Pattern = 'vbrStats\.deviation -le 0\.20' },
    @{ Name = 'f2-threshold-60fps-rate'; Pattern = '\$enc60 -ge 58\.0' },
    @{ Name = 'f2-spread-formula-unchanged'; Pattern = '\$result\.spread = \(\$result\.max - \$result\.min\) / \$result\.avg' },
    @{ Name = 'f2-bucket-formula-unchanged'; Pattern = 'buckets\[\$second\] \+= \$size' }
)
foreach ($g in $guards) {
    Assert $g.Name ($text -match $g.Pattern) ("用例文本仍含判据/口径：{0}" -f $g.Pattern)
}

Write-Host '--- F2 ⑤ 历史红证据在位（自然复现，不是构造）---'
$red = Join-Path $root 'verify/artifacts/format-20261110-183428/summary.json'
if (Test-Path $red) {
    $j = Get-Content $red -Raw -Encoding UTF8 | ConvertFrom-Json
    Assert 'f2-red-evidence-10-fail' ($j.fail -eq 10 -and $j.pass -eq 4) `
        ("轮 42 现场 summary.json：pass={0} fail={1}（recorder 未启动时刷出 10 条假 FAIL）" -f $j.pass, $j.fail)
} else {
    Assert 'f2-red-evidence-10-fail' $false "历史现场缺失：$red"
}

Write-Host '--- 轮 44 反向护栏：滑窗过 10% / 单秒过 2× 各必须判 FAIL（19 条 = 17 + 2）---'
# 序列 1：10 个 6000 桶 + 10 个 5000 桶 → 5 秒滑窗从 6000 滑到 5000，spread 18.2% > 10%；
#         单秒峰值 6000 ≤ 12000（2× 标称），所以**只有**滑窗断言该 FAIL。
$sWindow = (@(6000) * 10 + @(5000) * 10) -join ','
$r4 = Invoke-Case -Tag 'r44-window-spread-fail' -CaseArgs @('-SimulateCbrRates', $sWindow)
Assert 'r44-window-spread-fail' ($r4.Fail -eq 1 -and $r4.Rc -eq 1 -and
        $r4.Out -match 'FAIL cbr-window5-spread' -and
        $r4.Out -match 'PASS cbr-single-second-cap' -and $r4.Skip -eq 0) `
    ("滑窗过 10% → FAIL 且退出码 1，不藏成 SKIP：FAIL={0} SKIP={1} rc={2}；{3}" -f `
     $r4.Fail, $r4.Skip, $r4.Rc, $r4.Result)
# 序列 2：9 桶，第 5 桶 16699kbps（轮 43 真实失控值 2.8×）。9 桶共 5 个滑窗，
#         每个窗口都含该尖峰 → 窗口值全等 8140kbps、spread 0.0% PASS；
#         但单秒 16699 > 12000 → **只有**硬挡板该 FAIL。这条专门证明「滑窗不许稀释 IDR 失控」。
$sCap = (@(6000) * 4 + @(16699) + @(6000) * 4) -join ','
$r5 = Invoke-Case -Tag 'r44-single-second-cap-fail' -CaseArgs @('-SimulateCbrRates', $sCap)
Assert 'r44-single-second-cap-fail' ($r5.Fail -eq 1 -and $r5.Rc -eq 1 -and
        $r5.Out -match 'FAIL cbr-single-second-cap' -and
        $r5.Out -match 'PASS cbr-window5-spread' -and $r5.Skip -eq 0) `
    ("单秒 16,699kbps 超 2× 但滑窗 0.0% → 硬挡板仍 FAIL：FAIL={0} SKIP={1} rc={2}；{3}" -f `
     $r5.Fail, $r5.Skip, $r5.Rc, $r5.Result)

Write-Host ''
Write-Host ("RESULT: {0} PASS / {1} FAIL" -f $script:pass, $script:fail)
if ($script:fail -gt 0) { exit 1 }
exit 0
