# HANDOFF: S1-011（--cursor 透传）

## 一、我改了什么接口？
无签名变更。`main.cpp` 新增 `--cursor on|off` 解析、help 行、汇总 stdout 的
`include_cursor=0|1`；`RecordingSession` 的 `Options` 新增 `bool include_cursor = true`
并透传到 `CaptureRequest`。`CONTRACT.md` / `vcpkg.json` 未改。

## 二、下游需要接什么？
1. 若 UI 要暴露「光标开关」设置项，走 `src/app/RecorderController` 下发 `--cursor`，
   需另开 S5 卡并同步 `AppConfig`。
2. `--cursor` 只对 WGC 桌面源有效；若将来 DXGI/窗口源要支持，属 S2 域改动。

## 三、已知限制是什么？
- 全画面像素判定是抽样（步长 2），非逐像素。
- 行为断言用受控纯色背景；真实复杂桌面下的「任何位置无指针」未专项实测。
- 默认 on，保持轮 42 及以前的既有行为不变。

## 四、哪些需求我没做完？
| 项 | 状态 | 原因 / 下一步 |
| --- | --- | --- |
| M1-24#1/#2 CLI 透传 | 已交付 | 12/12 用例中 M1-24 六条全绿 |
| UI 设置项 | 没做 | 需 S5 卡，本轮文件域不含 `src/app/` |
| 其它采集源的光标开关 | 没做 | S2 域，且 `supports_cursor_toggle=false` |
| V1.0 发布 | 仍不可发布 | 见主卡 HANDOFF |

## 五、交付包清单
`tasks/G5/S1-011/`：TASK.md、EVIDENCE.md、SELFTEST.md、HANDOFF.md。

**执行方不自评为已验收。**