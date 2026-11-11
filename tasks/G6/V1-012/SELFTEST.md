# SELFTEST: V1-012

## 0. 开工四项自查
| 检查项 | 结果 | 依据 |
| --- | --- | --- |
| 需求编号可检索 | 通过 | `docs/01:106` M3-04#1（轮 44 新判据） |
| 必读章节可定位 | 通过 | 轮44指令 §1/§3 #2、`docs/17 §7` 裁定结果 |
| 上游交付物可获取 | 通过 | 轮 43 `g5_format.ps1`（F2 版）、`g5_cbr_probe.ps1` 逐秒口径 |
| 文件域无交集 | 通过 | 本卡仅 `verify/cases/g5_format.ps1`；selftest 由 V1-013 独占 |

## 1. 自测命令与输出
| 命令 | 结果 |
| --- | --- |
| `pwsh -File verify/cases/g5_format.ps1 -SimulateCbrRates '6000×10,5000×10'` | `FAIL cbr-window5-spread`（18.2%）+ `PASS cbr-single-second-cap`；rc=1 |
| `pwsh -File verify/cases/g5_format.ps1 -SimulateCbrRates '6000×4,16699,6000×4'` | `PASS cbr-window5-spread`（0.0%）+ `FAIL cbr-single-second-cap`；rc=1 |
| `pwsh -File verify/cases/g5_format_selftest.ps1` | **19 PASS / 0 FAIL**（V1-013） |
| 真实 10 连测 | 见主卡 `artifacts/r44-cbr10.txt`（10/10） |
| `pwsh -File verify/cases/g5_format.ps1`（回归复跑，修 `m3-04-mode-isolation` 后） | **15 PASS / 0 FAIL / 0 SKIP**，`artifacts/rerun-g5-format.txt` |
| `pwsh -File verify/cases/g5_format_selftest.ps1`（复跑） | **19 PASS / 0 FAIL** |

## 2. 红-绿对照
见 `EVIDENCE.md` 表；红证据来自轮 43 的 5+5 次定性报告（真实失控值），非构造。

## 3. 回归
全量回归见主卡 `artifacts/r44-regression.txt`（`g5_format` 与 19 条自检均在清单内）。

## 4. DoD 勾选
- [x] 开工四项自查
- [x] 一条需求一条证据，含可重跑命令与输出
- [x] 新增两条断言都先红后绿，旧值已记录
- [x] 滑窗口径六条写进用例注释
- [x] 判据数字 10% / 2× 一字未动；F2 取数语义未动
- [x] 未越文件域、未改 `src/`、未改 `docs/`
- [x] 四件套齐全；未自评为已验收