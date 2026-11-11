# SELFTEST: V1-004（轮 41 · 无卡收口 5 条）

## 0. 开工四项自查

| 检查项 | 结果 | 依据 |
| --- | --- | --- |
| 需求编号可检索 | 通过 | 5 条子句验收要点逐条抄自 `docs/01`（:251 / :205 / :206 / :117 / :199），与 `docs/09` 子句表一致 |
| 必读章节可定位 | 通过 | `docs/03 §编码选型`、`docs/05 §1/§2.1/§7.2`、`docs/06 §4.2/§4.3`、`docs/12`、`verify/lib/Cleanup.ps1` 均在位 |
| 上游交付物可获取 | 通过 | 五处既有实现 + `g3_recovery` 9/9、`g5_inject` 28/28、`g5_encoder_list` 17/17 |
| 文件域无交集 | 通过 | 本卡 `verify/cases/g5_closeout.ps1` + `docs/15` + `tasks/G5/V1-004/*`；与 V1-002 / S1-009 目录互斥 |

## 1. 自测命令与输出

| 命令 | 结果 | 证据 |
| --- | --- | --- |
| `pwsh -File verify/cases/g5_closeout.ps1`（首轮） | `15 PASS / 1 FAIL / 1 SKIP` | 两处取数缺陷，见 §2 |
| `pwsh -File verify/cases/g5_closeout.ps1`（修正后） | `18 PASS / 0 FAIL / 0 SKIP`，`CLOSEOUT_EXIT=0` | `tasks/G6/V1-002/artifacts/g5_closeout.txt` |
| `python verify/tools/coverage_report.py --write` | `written`；99/1/16 | `docs/09:25` |
| `python verify/tools/coverage_report.py --check` | `coverage table is up to date` | — |
| `python verify/tools/contract_check.py` | `8 PASS / 0 FAIL`（C8 缺理由 0） | — |
| `python verify/tools/card_consistency_check.py` | 36 卡 `0 处冲突` | — |
| `python verify/tools/domain_table_check.py` | `MISMATCH_COUNT=0` | — |

## 2. 红-绿对照

| 断言 | 旧实现实测 | 新实现实测 |
| --- | --- | --- |
| `m8-01-01-loss-within-10s` | 无专属断言（覆盖表「无卡」，理由写「缺 TASK.md 追溯」） | `PASS` killed_at=12s recovered=9.450s loss=2.550s |
| `m6-10-01-*` | 无专属断言 | 3 条 `PASS`：包内驱动条目 0、install 脚本命中 0、源码命中 0 |
| `m6-11-01-ui-status-refresh` | 首轮 `SKIP`（`app_ready=False`） | `PASS` hasMetrics=True idleChanged=True；源码未改，是用例共享读修复 |
| `m3-15-01-*` | 无专属断言 | 4 条 `PASS`：丢帧 89 / duration 5.983s / 60/1 / 双轨解码零错误 |
| `m6-04-02-hw-unavailable-with-reason` | 首轮 `FAIL`（汇总串误当逐条行，原因行=2） | `PASS`：三条 `available=0 + 非空 reason` 规范行 |

> 首轮的两处 FAIL/SKIP 都是**用例取数缺陷**，修复后断言收紧（`-ge 3` → `-eq 3`）而非放宽；
> 没有改产品代码、没有改判据。

## 3. 回归

- 本卡新增 `g5_closeout.ps1` 不修改任何既有用例；跑完自带 `Clear-SrProcesses -IncludeNameFallback`。
- 契约 / 卡面 / 域表 / 覆盖表四项检查均通过（见 §1）。
- 判据 §3 #4 的 `g3_recovery` / `g5_inject` / `g5_encoder_list` 等全量回归由
  `tasks/G6/V1-002/artifacts/run-regression.ps1` 承担，结果见该包 `regression.txt`。

## 4. DoD 勾选

- [x] 开工四项自查通过并已记录
- [x] 每条需求 → 一条证据，含命令与输出（`EVIDENCE.md §1~§5`）
- [x] 红-绿对照表完整（旧值 + 新值）
- [x] 覆盖 99/1/16，`--check` 通过，C8 8/8、卡面 0 冲突、域表 0 不匹配
- [x] 文件域无越界，`vcpkg.json` / `CONTRACT.md` / `coverage_model.py` 未改
- [x] `## 覆盖子句` 只列 5 条实际子句；`## 交付状态` 逐条与实际一致
- [x] 已知限制与降级验证标注齐全（`EVIDENCE.md §8`）
- [x] 未自评为已验收；`docs/15` 冲突不静默
- [x] `ui_smoke` 3×103 连跑由主卡承担：run1/run2/run3 均 `103 PASS / 0 FAIL`
  （`tasks/G6/V1-002/artifacts/ui-smoke-x3.txt`），本卡不重复跑
