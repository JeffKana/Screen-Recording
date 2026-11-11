# HANDOFF: V1-008（F2 取数失败分离）

## 一、我改了什么接口？
无接口变更。只改 `verify/cases/g5_format.ps1`（取数前置、重试一次、SKIP 语义、
3 个模拟参数）并新增 `verify/cases/g5_format_selftest.ps1`。
`RESULT:` 行增加 SKIP 计数；`summary.json` 增加 `skip` 字段。
**所有断言阈值与逐秒口径一字未改**。

## 二、下游需要接什么？
1. 验收方独立重跑 `pwsh -File verify/cases/g5_format_selftest.ps1`（17/17）与
   `pwsh -File verify/cases/g5_format.ps1 -SimulateNoStart`（1 SKIP）。
2. 其它用例若也有「取数失败算结论」，可复用 `Test-SegmentData` 的三档语义
   （与轮 41 R1 `g5_storage` 同族），但不要直接 dot-source 本脚本。
3. F1 产品过冲仍在，回归里 `g5_format` 的 `m3-04-cbr-spread` 可能仍 FAIL ——
   那是**真实产品失败**，不要把它改成 SKIP。

## 三、已知限制是什么？
- 模拟分支不启动 recorder、不清理进程；与真实录制不是同一条路径。
- 「数据齐但 `record_exit!=0`」判 FAIL，不判 SKIP；这是刻意保留的护栏。
- 首轮自检曾因 `$Args` 自动变量冲突误启动 recorder，已修并留痕。

## 四、哪些需求我没做完？
| 项 | 状态 | 原因 / 下一步 |
| --- | --- | --- |
| F2 取数分离 | 已交付 | 1 SKIP / 0 FAIL，17/17 自检 |
| F1 产品过冲 | 不在本卡 | 归 S4-004 / `docs/17`，裁定后处理 |
| g5_format 连跑 3 次全绿 | **未达成**（14/14、14/14、13/1） | 第 3 次 `m3-04-cbr-spread` spread 154.7% 是**真实产品失败**；F2 只把「数据未取到」判 SKIP，不藏产品失败 |
| V1.0 发布 | 仍不可发布 | 见主卡 HANDOFF |

## 五、交付包清单
`tasks/G6/V1-008/`：TASK.md、EVIDENCE.md、SELFTEST.md、HANDOFF.md + `artifacts/`。

**执行方不自评为已验收。**