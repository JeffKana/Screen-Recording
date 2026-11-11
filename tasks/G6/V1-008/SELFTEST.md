# SELFTEST: V1-008

## 0. 开工四项自查
| 检查项 | 结果 | 依据 |
| --- | --- | --- |
| 需求编号可检索 | 通过 | 轮43指令 §1 P0 F2 / §3 #2；用例侧工作不声明子句 |
| 必读章节可定位 | 通过 | 验收报告-轮42 三种失败形态、`docs/06 §4.2` |
| 上游交付物可获取 | 通过 | `verify/cases/g5_format.ps1`、历史现场 summary.json |
| 文件域无交集 | 通过 | 本卡仅 `g5_format.ps1` + `g5_format_selftest.ps1`，与 S4-004/V1-007/V1-010 互斥 |

## 1. 自测命令与输出
| 命令 | 结果 |
| --- | --- |
| `pwsh -File verify/cases/g5_format_selftest.ps1` | 17 PASS / 0 FAIL |
| `pwsh -File verify/cases/g5_format.ps1 -SimulateNoStart` | 1 SKIP / 0 FAIL / exit 0 |
| `pwsh -File verify/cases/g5_format.ps1 -SimulateProductFailure` | 1 FAIL / 0 SKIP / exit 1 |
| `python verify/tools/card_consistency_check.py` | 0 冲突 |

## 2. 红-绿对照
见 `EVIDENCE.md` 表；红证据为轮 42 自然复现的 4/10 现场。

## 3. 回归
全量回归见 `tasks/G6/V1-007/artifacts/r43-regression.txt`（含 g5_format 连跑 3 次）。

## 4. DoD 勾选
- [x] 开工四项自查
- [x] 一条判据一条证据，含可重跑命令
- [x] 先红后绿，红为历史现场自然复现
- [x] 判据阈值一字未改（7 条文本护栏）
- [x] 产品失败不伪装 SKIP
- [x] 四件套齐全；未自评为已验收