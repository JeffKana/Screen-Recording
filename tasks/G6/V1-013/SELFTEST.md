# SELFTEST: V1-013

## 0. 开工四项自查
| 检查项 | 结果 | 依据 |
| --- | --- | --- |
| 需求编号可检索 | 通过 | 轮44指令 §2 P0（反向护栏），关联 M3-04#1 |
| 必读章节可定位 | 通过 | 轮44指令 §1/§2、`tasks/G6/V1-012/TASK.md`（模拟参数） |
| 上游交付物可获取 | 通过 | 轮 43 `g5_format_selftest.ps1` 17 条 + 轮 44 `-SimulateCbrRates` |
| 文件域无交集 | 通过 | 本卡仅 `verify/cases/g5_format_selftest.ps1` |

## 1. 自测命令与输出
| 命令 | 结果 |
| --- | --- |
| `pwsh -File verify/cases/g5_format_selftest.ps1` | **19 PASS / 0 FAIL**，rc=0 |
| 原 17 条 | 全部保留并通过 |
| `artifacts/r44-window-spread-fail.txt` | 1 PASS / 1 FAIL / 0 SKIP，rc=1 |
| `artifacts/r44-single-second-cap-fail.txt` | 1 PASS / 1 FAIL / 0 SKIP，rc=1 |

## 2. 红-绿对照
| 断言 | 旧值 | 新值 |
| --- | --- | --- |
| self-test 条数 | 17 | 19 |
| 滑窗过 10% | 无护栏 | 精确 `FAIL cbr-window5-spread` |
| 单秒 2.8× 但滑窗 0.0% | 无护栏（会被稀释放过） | 精确 `FAIL cbr-single-second-cap` |

## 3. 回归
全量回归见主卡 `artifacts/r44-regression.txt`。

## 4. DoD 勾选
- [x] 开工四项自查
- [x] 总数 19、原 17 条不删
- [x] 两条护栏都先红后绿，注入输出可核验
- [x] 守卫仍强制 `-le 0.10`，判据未改软
- [x] 异常不藏成 SKIP
- [x] 未越文件域、四件套齐全、未自评为已验收