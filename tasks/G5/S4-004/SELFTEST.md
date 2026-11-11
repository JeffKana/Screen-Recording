# SELFTEST: S4-004

## 0. 开工四项自查
| 检查项 | 结果 | 依据 |
| --- | --- | --- |
| 需求编号可检索 | 通过 | `docs/01:106` M3-04 验收要点 |
| 必读章节可定位 | 通过 | 轮43指令 §1/§2、`CONTRACT §2`（S4 域 `src/encode/`） |
| 上游交付物可获取 | 通过 | V1-007 的 5+5 次定性报告与 NAL 分解 |
| 文件域无交集 | 通过 | 本卡仅 `src/encode/MfVideoEncoder.cpp/.h` |

## 1. 自测命令与输出
| 命令 | 结果 |
| --- | --- |
| `cmake --build build-msvc --config Release/Debug` / `build-mingw` | 三构建零告警 |
| `pwsh -File tasks/G6/V1-007/artifacts/run-f1-after.ps1` | 10 次 CBR 40s：静态 3/5 超、运动 5/5 超、运动 avg 8,039kbps |
| 回滚后重跑三构建 | 零告警 |

## 2. 红-绿对照
| 断言 | 旧实现 | 按秒结算后 |
| --- | --- | --- |
| 运动源 spread ≤10% | 5/5 超（12.1~26.4%） | 5/5 超（19.3~24.9%） |
| 运动源 avg 偏差 ≤20% | 4.6% PASS | **34% FAIL（新回归）** |

## 3. 回归
全量回归见 `tasks/G6/V1-007/artifacts/r43-regression.txt`。

## 4. DoD 勾选
- [x] 开工四项自查
- [x] 先红后绿尝试，红绿值都记录
- [x] 三构建零告警（含回滚后）
- [ ] **10 次全 ≤10%：未达成**；`g5_format` 3 次全绿：见主卡回归
- [x] 未改 `verify/`、未改 CBR 模式实现、未改判据、未越域
- [x] 如实回滚、不假绿；四件套齐全