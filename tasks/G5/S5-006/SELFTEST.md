# SELFTEST: S5-006

## 0. 开工四项自查
| 检查项 | 结果 | 依据 |
| --- | --- | --- |
| 需求编号可检索 | 通过 | `docs/01:237` |
| 必读章节可定位 | 通过 | `CONTRACT §2`（S5 域 `src/app/`）、轮43指令 §1 P1 |
| 上游交付物可获取 | 通过 | 现有 `UpdateTray` / `AppState` / 托盘图标初始化 |
| 文件域无交集 | 通过 | 本卡仅 `src/app/main.cpp` |

## 1. 自测命令与输出
| 命令 | 结果 |
| --- | --- |
| 三构建编译 | exit=0 warnings=0 errors=0 |
| `sr-ui.exe --hidden --simulate-record-state recording --exit-after-ms 6000` | 边框可见、屏幕边缘红色、日志 state=recording |
| 同上 `paused` | 边框颜色与录制态可区分、日志 state=paused |
| 同上 `--record-border off` | 边框隐藏、日志 border=0 |
| `pwsh -File verify/cases/g5_r43_closeout.ps1` | M7-13 四条断言全绿 |

## 2. 红-绿对照
| 断言 | 旧值 | 新值 |
| --- | --- | --- |
| 托盘录制态图标 | 系统 `IDI_INFORMATION`，无红点 | 自绘 `red_dot_pixel=0xFF0000FF` |
| 屏幕边框 | 无 | 录制红框可见、暂停区分、可关 |

## 3. 回归
全量回归见主卡 `artifacts/r43-regression.txt`；重点 `ui_smoke` 3×103。

## 4. DoD 勾选
- [x] 开工四项自查
- [x] 一条需求一条证据，含像素级自检日志
- [x] 先红后绿、三构建零告警
- [x] 未引三方依赖（GDI 自绘）、未改接口签名、未越域
- [x] 代理验证已标注；四件套齐全；未自评为已验收