# EVIDENCE: V1-008（F2 取数失败与断言失败分离）

> 执行方证据，非验收结论。数据 2026-11-11。

## 一条需求一条证据

判据（轮 43 指令 §3 #2）：人为造一次 recorder 不启动 → 输出应是 **1 条 SKIP 而非 10 条 FAIL**。

- 命令：`pwsh -File verify/cases/g5_format_selftest.ps1`
- 输出：`artifacts/f2-selftest.txt` — **17 PASS / 0 FAIL**，逐组：
  - `f2-no-start-one-skip`：`SKIP=1 FAIL=0 PASS=0`
  - `f2-no-start-exit-zero`：退出码 0
  - `f2-no-start-retried-once`：文案含 `attempts=2`（重试一次真实走过）
  - `f2-no-start-stdout-tail`：附 stdout 尾 20 行 + 「stdout 为空」
  - `f2-no-start-result-line`：`RESULT: 0 PASS / 0 FAIL / 1 SKIP`
  - `f2-product-failure-is-fail` / `f2-product-failure-not-skip`：数据齐但
    `record_exit=6` → **1 FAIL / 0 SKIP / 退出码 1**（不把产品失败藏成 SKIP）
  - `f2-missing-fields-skip` / `f2-missing-fields-reason`：缺 `record_exit` → SKIP 而非拿 0 算
  - 7 条判据文本护栏：`spread -le 0.10`、`deviation -le 0.20`、`CapKbps * 1.02`、
    60fps `>= 58.0`、spread 公式、分桶公式 —— **判据一字未改**
  - `f2-red-evidence-10-fail`：历史现场 `verify/artifacts/format-20261110-183428/summary.json`
    `pass=4 fail=10`（`.part=108B`、`stdout=0B`）
- 红-绿对照：

| 新断言 | 旧实现实测 | 新实现实测 |
| --- | --- | --- |
| 造假不启动 | 14 条断言照跑，刷 **10 条假 FAIL**（轮 42 现场 4/10） | **1 SKIP / 0 FAIL / 退出码 0** |
| 产品明确失败 | 与取数失败混在一起 | 判 FAIL，不伪装 SKIP |
| 缺字段重试 | 无 | `attempts=2` → 仍缺判 SKIP |

- 实现：`g5_format.ps1` 新增 `Test-SegmentData` / `New-SegmentResult` /
  `Confirm-Segment` / `Invoke-DataSkip`；`Record-Segment` 返回结构化结果并重试一次；
  6 个录制段都接了取数前置。模拟入口 `-SimulateNoStart` /
  `-SimulateProductFailure` / `-SimulateMissingFields` 是验收方要求的可重跑分支。
- 首轮自检 8/9 FAIL 的根因：`$Args` 与 PowerShell 自动变量冲突，splatting 展开成空
  → 用例真的启动了 recorder；改名 `$CaseArgs` 后 17/17。属**自检取数缺陷**，判据未放宽。

## 已知限制
- 本卡不改产品、不声明需求子句；`g5_format` 的产品侧过冲归 `docs/17`。
- 模拟分支不清理进程（避免误杀同机录制），正常模式仍按原样清理。
- 执行方不自评为已验收。