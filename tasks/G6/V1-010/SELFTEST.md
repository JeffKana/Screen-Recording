# SELFTEST: V1-010

## 0. 开工四项自查
| 检查项 | 结果 | 依据 |
| --- | --- | --- |
| 需求编号可检索 | 通过 | `docs/01:56` M1-24、`:237` M7-13 原文 |
| 必读章节可定位 | 通过 | 轮43指令 §1 P1/§3 #4、`CONTRACT §2`（S1/S5 域） |
| 上游交付物可获取 | 通过 | 轮 42 交付包、WGC cursor toggle 现状、UI 托盘现状 |
| 文件域无交集 | 通过 | 本卡仅收口用例 + `ColorWindow.ps1`；产品改动由 S1-011/S5-006 独占 |

## 1. 自测命令与输出
| 命令 | 结果 |
| --- | --- |
| `pwsh -File verify/cases/g5_r43_closeout.ps1` | 12 PASS / 0 FAIL / 0 SKIP |
| `python verify/tools/coverage_report.py --write` + `--check` | 107/2/7，`up to date` |
| `python verify/tools/contract_check.py` | 8/8 PASS |
| `python verify/tools/card_consistency_check.py` | 49 卡 0 冲突 |
| 三构建 | `exit=0 warnings=0 errors=0` |

## 2. 红-绿对照
| 断言 | 旧实现 | 新实现 |
| --- | --- | --- |
| `--cursor off` | 无 CLI 入口，`include_cursor` 恒 true | `include_cursor=0` + 成片 0 个指针像素 |
| `--cursor on` | 同恒 true | `include_cursor=1` + 指针随位置移动、无残影 |
| 托盘红点 | `icon_active` 是系统 `IDI_INFORMATION` | 自绘图标像素 `0xFF0000FF` |
| 屏幕边框 | 无边框窗口 | 录制中红框可见、暂停黄框可区分、可关闭 |

## 3. 回归
全量回归见主卡 `artifacts/r43-regression.txt`；重点 `ui_smoke` 3×103（正常态状态栏不变）。

## 4. DoD 勾选
- [x] 开工四项自查
- [x] 每条子句一条证据，含命令与实测值
- [x] 新增断言都先红后绿（旧实现无入口/无窗口，红值已记录）
- [x] 三构建零告警、覆盖 107/2/7
- [x] 降级/代理验证逐条标注
- [x] 四件套齐全；未自评为已验收