# HANDOFF: S5-006（红点图标 / 录制边框）

## 一、我改了什么接口？
无签名变更。`src/app/main.cpp` 新增 `MakeRedDotIcon()`、`BorderWndProc`、
`CreateBorderWindow()`、`UpdateRecordIndicator()`；`AppState` 新增
`border_hwnd` / `show_record_border` / `indicator_state` / `simulate_indicator_state`；
`UpdateUi` 调用新函数；新增 `--record-border`、`--simulate-record-state` 与 help 行。
`CONTRACT.md` / `vcpkg.json` 未改。

## 二、下游需要接什么？
1. 验收方独立重跑 `g5_r43_closeout.ps1` 的 M7-13 四条断言。
2. 若要终审「真实录制会话下的托盘/边框」，需补一条启动真实录制 → 暂停 → 停止的 UI 用例
   （本轮用注入状态，属代理验证）。
3. `--simulate-record-state` 只在 `SR_TEST_HOOKS` 构建暴露；发布包不含。

## 三、已知限制是什么？
- 代理验证：注入状态走同一条 UI 渲染分支，非真实录制会话。
- 暂停态屏幕采样 (240,240,240) 是细虚线间隙/桌面背景，断言判「与录制态不同」而非
  「必须采到黄色」；代码上暂停用黄色细虚线。
- 边框窗口为单显示器尺寸（`SM_CXSCREEN/SM_CYSCREEN`）；多屏扩展桌面未专项验证。

## 四、哪些需求我没做完？
| 项 | 状态 | 原因 / 下一步 |
| --- | --- | --- |
| M7-13#1/#2 产品呈现 | 已交付 | 12/12 用例中 M7-13 四条全绿 |
| 真实录制会话下的终审 | 没做 | 需新 UI 用例，轮 44 |
| 多屏边框 | 没做 | 需多屏尺寸处理 |
| V1.0 发布 | 仍不可发布 | 见主卡 HANDOFF |

## 五、交付包清单
`tasks/G5/S5-006/`：TASK.md、EVIDENCE.md、SELFTEST.md、HANDOFF.md。

**执行方不自评为已验收。**