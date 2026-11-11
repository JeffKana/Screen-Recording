# HANDOFF: V1-006（M6-10#2 HVCI）

## 一、我改了什么接口？
无接口变更。新增 `verify/cases/g5_r42_hvci.ps1`；`docs/14` 的矩阵首格实测值由本卡与主卡共同维护。
`src/`、`CONTRACT.md`、`vcpkg.json` 均未改。

## 二、下游需要接什么？
1. 验收方独立重跑 `pwsh -File verify/cases/g5_r42_hvci.ps1`；非 HVCI 机器会 SKIP（这是设计，不是缺陷）。
2. 轮 43 继续补其余 11 格矩阵与 M6-06（150% DPI）。

## 三、已知限制是什么？
- 本机是 HVCI 已启用的 **Win11 家庭版 build 26200**；结论只覆盖这一个环境，不外推到 Win10 或服务器版。
- 验证的是「可正常安装运行」，不含 DPI、混合显卡、杀软等其它组合。
- 首轮 GUI stdout 误判已修正，见 `EVIDENCE.md`。

## 四、哪些需求我没做完？
| 项 | 状态 | 原因 / 下一步 |
| --- | --- | --- |
| M6-10#2 HVCI 实测 | 已交付 | 7/7，真实环境 |
| 矩阵其余 11 格 | 没做 | 需真机/150% DPI，轮 43 |
| M6-06 | 没做 | 环境 BLOCKED |
| V1.0 发布 | 仍不可发布 | 11 条无卡等，见主卡 HANDOFF |

## 五、交付包清单
`tasks/G6/V1-006/`：TASK.md、EVIDENCE.md、SELFTEST.md、HANDOFF.md；
原始输出在 `tasks/G6/V1-003/artifacts/g5_r42_hvci.ps1.txt`。

**执行方不自评为已验收。**