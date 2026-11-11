# SELFTEST: V1-005

## 0. 开工四项自查

| 检查项 | 结果 | 依据 |
| --- | --- | --- |
| 需求编号可检索 | 通过 | `docs/01:255` M8-05 原文；子句 #1「依据当前码率与剩余空间估算可录时长…误差 ≤20%」 |
| 必读章节可定位 | 通过 | 验收报告-轮41 §2/§7、轮42指令 §1/§4.1、`verify/tools/coverage_model.py:81/210-249` |
| 上游交付物可获取 | 通过 | 轮 40 S5-004 卡（M8-05#1 归属）、`docs/09`、R2 五连绿 runner |
| 文件域无交集 | 通过 | 本卡只写 `tasks/G5/V1-005/*` 与 `docs/07` 两行备注、`docs/09` 理由列 |

## 1. 自测命令与输出

| 命令 | 结果 |
| --- | --- |
| `python verify/tools/coverage_report.py --write`（回退） | `written`；98/2/16 |
| `python verify/tools/coverage_report.py --check`（回退） | `coverage table is up to date`，退出码 0 |
| `python verify/tools/contract_check.py` | C8 8/8 PASS（挂账理由已填） |
| `pwsh -File tasks/G6/V1-003/artifacts/run-five-runs.ps1` | 5×14/14 |
| `python verify/tools/coverage_report.py --write`（翻绿） | `written`；99/1/16 |
| `python verify/tools/coverage_report.py --check`（翻绿） | `coverage table is up to date`，退出码 0 |

## 2. 红-绿对照

| 断言 | 旧值 | 新值 |
| --- | --- | --- |
| M8-05#1 状态 | 已实现（轮 40） | 有卡未实现 → 已实现（两态迁移各一次 `--check`） |
| 覆盖统计 | 99/1/16 | 98/2/16（回退）→ 99/1/16（翻绿） |
| 卡标题写法 | `## 交付状态（…）` 导致该节未被解析、翻绿无效 | 纯 `## 交付状态` → 解析生效、统计正确 |

## 3. 回归

- 两次 `--check` 均 PASS；`contract_check` C8 PASS；`card_consistency_check` 0 冲突。
- 全量回归由主卡 `tasks/G6/V1-003/artifacts/r42-regression.txt` 汇总。

## 4. DoD 勾选

- [x] 开工四项自查通过
- [x] 回退：`--write` → `--check` PASS → 台账备注 1
- [x] 5 连绿原始输出齐备
- [x] 翻绿：`--write` → `--check` PASS → 台账备注 2
- [x] 未改 S5-004 历史卡、未改合并规则
- [x] 卡标题写法问题已修正并留痕
- [x] 四件套齐全；未自评为已验收
