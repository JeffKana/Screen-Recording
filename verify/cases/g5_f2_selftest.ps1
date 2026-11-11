# 轮 42 · F2 校验自检：待裁定申请 + 已实现 -> 报警（先红后绿）
#
# 依据：验收报告-轮41 §5 F2「待裁定项先翻绿」；轮42指令 §1 P1。
# 造 3 张假卡到临时目录（不在 tasks/ 下，不污染真实卡扫描）：
#   ① 待裁定申请 + 已实现     -> 必须 FAIL（红 / 报警生效）
#   ② 同一引用 + 有卡未实现   -> 必须 PASS（绿 / 正确挂账）
#   ③ 已裁定归档措辞 + 已实现 -> 必须 PASS（绿 / 不误伤历史事实）
# 再跑全仓真实卡 -> 必须 0 冲突。
#
# Usage: pwsh -File verify/cases/g5_f2_selftest.ps1

param([string]$Tool = 'verify/tools/card_consistency_check.py')

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Push-Location $root
$script:pass = 0
$script:fail = 0
function Assert {
    param([string]$Name, [bool]$Ok, [string]$Detail)
    if ($Ok) { $script:pass++; Write-Host ("  PASS {0} — {1}" -f $Name, $Detail) }
    else { $script:fail++; Write-Host ("  FAIL {0} — {1}" -f $Name, $Detail) }
}

$tmp = Join-Path $env:TEMP 'sr-r42-f2-selftest'
if (Test-Path $tmp) { Remove-Item $tmp -Recurse -Force }
New-Item -ItemType Directory -Force -Path $tmp | Out-Null

try {
    $pending = @'
# 假卡 A：待裁定项先翻绿

见 `docs/17-轮42-假需求裁定申请.md`（待裁定）。

## 覆盖子句

- M9-98#1

## 交付状态

- M9-98#1: 已实现
'@
    $held = @'
# 假卡 B：待裁定项正确挂账

见 `docs/17-轮42-假需求裁定申请.md`（待裁定）。

## 覆盖子句

- M9-98#1

## 交付状态

- M9-98#1: 有卡未实现（待裁定）
'@
    $resolved = @'
# 假卡 C：已裁定归档（历史事实，不报警）

见 `docs/17-轮42-假需求裁定申请.md`，轮 42 已裁定，结论维持已实现，见裁定结果。

## 覆盖子句

- M9-98#1

## 交付状态

- M9-98#1: 已实现
'@

    foreach ($pair in @(@('pending', $pending), @('held', $held), @('resolved', $resolved))) {
        $dir = Join-Path $tmp $pair[0]
        New-Item -ItemType Directory -Force -Path $dir | Out-Null
        [System.IO.File]::WriteAllText((Join-Path $dir 'TASK.md'), $pair[1],
            [System.Text.UTF8Encoding]::new($false))
        $out = & python $Tool --tasks-dir $dir 2>&1 | Out-String
        $rc = $LASTEXITCODE
        [System.IO.File]::WriteAllText((Join-Path $tmp ($pair[0] + '.txt')),
            ($out + "`nEXIT=$rc`n"), [System.Text.UTF8Encoding]::new($false))
        Set-Variable -Name ("result_" + $pair[0]) -Value @{ out = $out; rc = $rc }
    }

    # 断言只依赖退出码与 ASCII 标记：不同 runner 捕获 Python UTF-8 输出时中文可能被解码成乱码，
    # 用中文文本做匹配会假 FAIL（全量回归首轮实测到 2/4）。退出码与 ASCII 标记与编码无关。
    Assert 'f2-pending-ref-alarms' ($result_pending.rc -eq 1 -and
                                   $result_pending.out -match 'F2_PENDING_REF') `
        ("待裁定申请 + 已实现 → 退出码 {0}，ASCII 标记 F2_PENDING_REF 命中={1}" -f `
         $result_pending.rc, ($result_pending.out -match 'F2_PENDING_REF'))
    Assert 'f2-held-state-passes' ($result_held.rc -eq 0) `
        ("同一引用但挂「有卡未实现」→ 退出码 {0}（不报警）" -f $result_held.rc)
    Assert 'f2-adjudicated-exempt' ($result_resolved.rc -eq 0) `
        ("已裁定归档措辞 + 已实现 → 退出码 {0}（不误伤历史事实）" -f $result_resolved.rc)

    $real = & python $Tool 2>&1 | Out-String
    $realRc = $LASTEXITCODE
    Assert 'f2-real-cards-zero-conflict' ($realRc -eq 0 -and $real -match 'card_consistency: OK') `
        ("全仓真实卡：退出码 {0}，OK 标记命中={1}" -f $realRc, ($real -match 'card_consistency: OK'))
} finally {
    Pop-Location
}

Write-Host ''
Write-Host ("RESULT: {0} PASS / {1} FAIL" -f $script:pass, $script:fail)
if ($script:fail -gt 0) { exit 1 }
exit 0
