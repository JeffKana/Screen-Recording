# 轮 43 · F2 自检：取数失败与断言失败分离
# 轮 46 · M3-04#1 判据改为「全片平均 ≤10% + 单秒 ≤2×」（docs/21 §2），
#        反向护栏随之换成新判据的两条，并新增「平均口径不许用逐秒再平均」护栏。
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
$lib = Join-Path $PSScriptRoot '..\lib\CbrCriteria.ps1'
# 轮 46：本轮护栏的原始输出落 V1-019 自己的 artifacts（不写其它卡的目录）。
$art = Join-Path $root 'tasks/G6/V1-019/artifacts'
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

Write-Host '--- F2 ④ 判据未被改软（改尺子检测）：新判据 + 共用实现 + VBR 原判据 ---'
$text = [System.IO.File]::ReadAllText($case, [System.Text.Encoding]::UTF8)
$libText = if (Test-Path $lib) { [System.IO.File]::ReadAllText($lib, [System.Text.Encoding]::UTF8) } else { '' }
$guards = @(
    # docs/21 §2：全片平均偏差 ≤10%（共用实现里只有一个 0.10 阈值）。
    @{ Name = 'f2-threshold-average-10pct'; Pattern = 'deviation -le 0\.10'; Source = 'lib' },
    # docs/21 §4：单秒 2× 硬挡板不可放宽。
    @{ Name = 'f2-threshold-cap-2x'; Pattern = '2 \* \$NominalKbps'; Source = 'lib' },
    @{ Name = 'f2-threshold-vbr-peak-102pct'; Pattern = 'CapKbps \* 1\.02'; Source = 'case' },
    @{ Name = 'f2-threshold-vbr-nominal-20pct'; Pattern = 'vbrStats\.deviation -le 0\.20'; Source = 'case' },
    @{ Name = 'f2-threshold-60fps-rate'; Pattern = '\$enc60 -ge 58\.0'; Source = 'case' },
    # docs/21 §4 #2：平均口径写死全片 bytes_written/wall_seconds，不许逐秒再平均。
    @{ Name = 'f2-average-source-wholefile'; Pattern = 'Field \$outCbr ''bytes_written'''; Source = 'case' },
    @{ Name = 'f2-bucket-formula-unchanged'; Pattern = 'buckets\[\$second\] \+= \$size'; Source = 'lib' }
)
foreach ($g in $guards) {
    $src = if ($g.Source -eq 'lib') { $libText } else { $text }
    Assert $g.Name ($src -match $g.Pattern) ("判据/口径仍在：{0}（{1}）" -f $g.Pattern, $g.Source)
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

Write-Host '--- 轮 46 反向护栏 ①：全片平均超 10% 必须 FAIL（单秒仍 PASS）---'
# 20 个 6000 桶：逐秒峰值 6000 ≤ 12000；但全片 35,000,000 B / 40 s = 7,000kbps，
# 偏差 16.7% > 10% → 只有 cbr-average-deviation 该 FAIL。
$sAvg = (@(6000) * 20) -join ','
$r4 = Invoke-Case -Tag 'r46-average-deviation-fail' -CaseArgs @(
    '-SimulateCbrRates', $sAvg, '-SimulateCbrBytes', '35000000', '-SimulateCbrSecs', '40')
Assert 'r46-average-deviation-fail' ($r4.Fail -eq 1 -and $r4.Rc -eq 1 -and
        $r4.Out -match 'FAIL cbr-average-deviation' -and
        $r4.Out -match 'PASS cbr-single-second-cap' -and $r4.Skip -eq 0) `
    ("平均 16.7% 超 10% → FAIL 且退出码 1：FAIL={0} SKIP={1} rc={2}；{3}" -f `
     $r4.Fail, $r4.Skip, $r4.Rc, $r4.Result)

Write-Host '--- 轮 46 反向护栏 ②：单秒超 2× 必须 FAIL（平均仍 PASS）---'
# 第 5 桶 16,699kbps（轮 43 真实失控值 2.8×）；全片 30,000,000 B / 40 s = 6,000kbps
# 偏差 0.0% → 只有硬挡板该 FAIL。证明「平均通过也不稀释单秒失控」。
$sCap = (@(6000) * 4 + @(16699) + @(6000) * 4) -join ','
$r5 = Invoke-Case -Tag 'r46-single-second-cap-fail' -CaseArgs @(
    '-SimulateCbrRates', $sCap, '-SimulateCbrBytes', '30000000', '-SimulateCbrSecs', '40')
Assert 'r46-single-second-cap-fail' ($r5.Fail -eq 1 -and $r5.Rc -eq 1 -and
        $r5.Out -match 'FAIL cbr-single-second-cap' -and
        $r5.Out -match 'PASS cbr-average-deviation' -and $r5.Skip -eq 0) `
    ("单秒 16,699kbps 超 2× 但全片平均 0.0% → 硬挡板仍 FAIL：FAIL={0} SKIP={1} rc={2}；{3}" -f `
     $r5.Fail, $r5.Skip, $r5.Rc, $r5.Result)

Write-Host '--- 轮 46 反向护栏 ③：平均口径不许用「逐秒再平均」代替 ---'
# 20 个 9000 桶：逐秒均值 9000kbps（+50%），若实现拿它当平均判据就会 FAIL；
# 全片 30,000,000 B / 40 s = 6,000kbps 偏差 0.0% → 正确实现必须 PASS。
$sSrc = (@(9000) * 20) -join ','
$r6 = Invoke-Case -Tag 'r46-average-source-wholefile' -CaseArgs @(
    '-SimulateCbrRates', $sSrc, '-SimulateCbrBytes', '30000000', '-SimulateCbrSecs', '40')
Assert 'r46-average-source-wholefile' ($r6.Fail -eq 0 -and $r6.Rc -eq 0 -and
        $r6.Out -match 'PASS cbr-average-deviation' -and
        $r6.Out -match 'bytes_written=30000000') `
    ("逐秒均值 9000 但全片 6000 → 平均判据仍 PASS（口径来自 bytes_written/wall_seconds）：FAIL={0} rc={1}；{2}" -f `
     $r6.Fail, $r6.Rc, $r6.Result)

Write-Host '--- 轮 46 反向护栏 ④：两条新断言在位、旧的 5 秒滑窗断言已删 ---'
Assert 'r46-two-new-assertions-present' ($text -match "Assert 'cbr-average-deviation'" -and
        $text -match "Assert 'cbr-single-second-cap'") `
    '用例文本含 cbr-average-deviation / cbr-single-second-cap 两条独立断言'
Assert 'r46-window5-criterion-removed' ($text -notmatch "Assert 'cbr-window5-spread'" -and
        $text -notmatch 'Get-Window5Stats') `
    '旧的滑窗判据函数/断言已删除（逐秒与滑窗方差不作判据，docs/21 §2）'

Write-Host ''
Write-Host ("RESULT: {0} PASS / {1} FAIL" -f $script:pass, $script:fail)
if ($script:fail -gt 0) { exit 1 }
exit 0
