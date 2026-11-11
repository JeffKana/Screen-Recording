# SELFTEST: V1-014

## 0. 开工四项自查
| 检查项 | 结果 | 依据 |
| --- | --- | --- |
| 需求编号可检索 | 通过 | `docs/01:106` M3-04#1（轮 44 措辞） |
| 必读章节可定位 | 通过 | 轮44指令 §1/§3 #5、`docs/17 §7`、轮 42 V1-005 状态迁移范式 |
| 上游交付物可获取 | 通过 | 轮 43 V1-009 卡、10 连测 runner、19 条自检 |
| 文件域无交集 | 通过 | 本卡 + V1-009 的状态移交；`docs/09`/`docs/07` 由 V1-011 写 |

## 1. 自测命令与输出
| 命令 | 结果 |
| --- | --- |
| `python verify/tools/coverage_report.py --write` / `--check`（起点） | `up to date`，rc=0（第 1 次） |
| `pwsh -File tasks/G6/V1-011/artifacts/run-r44-cbr10.ps1` | `ALL_10_PASS=True`，10/10 |
| `python verify/tools/coverage_report.py --write` / `--check`（翻绿） | `up to date`，rc=0（第 2 次）；108/1/7 |
| `python verify/tools/contract_check.py` | 8/8 PASS |
| `python verify/tools/card_consistency_check.py` | 0 冲突 |

## 2. 红-绿对照
见 `EVIDENCE.md` 表；红证据为轮 43 的 5+5 次定性与回退记录。

## 3. 回归
全量回归见 `tasks/G6/V1-011/artifacts/r44-regression.txt`。

## 4. DoD 勾选
- [x] 开工四项自查
- [x] 翻绿条件 3 条逐条核过，10 次原始输出齐备
- [x] 两次 `--check` 都过 + 台账两行备注
- [x] 状态移交消除 R5 冲突，历史结论未改写
- [x] `## 交付状态` 与实际一致、未改合并规则、未改码控
- [x] 四件套齐全；未自评为已验收