# HANDOFF: S5-005（画质档位 / 背压标识）

## 一、我改了什么接口？
无签名变更。新增：`AppConfig` 的 `quality_preset` / `quality` 字段与
`ApplyQualityPreset` / `SaveQualityPreset` / `QualityPresetNames` / `QualityPresetCount`；
`main.cpp` 的 `kSettingsPresetCombo=108`、`kSettingsPresetValues=109`、`RefreshPresetValues`、
`CBN_SELCHANGE` 联动分支、`--set-preset` / `--simulate-backpressure` 调试开关、
`AppState::backpressure_inject`、`UpdateUi` 的背压前缀；
`RecorderController.cpp` 的 `preset_dispatched` 日志。`CONTRACT.md` / `vcpkg.json` 未改。

## 二、下游需要接什么？
1. 新增控件 ID 108/109，后续设置页改动不要占用；窗口高度已 500 → 570。
2. INI 中文必须由产品 `WritePrivateProfileStringW` 写入（系统 ANSI）；手写 UTF8 会乱码，
   用例已改走 `--set-preset`。
3. `--set-preset` / `--simulate-backpressure` 是验收注入入口，位于 `SR_TEST_HOOKS` 段；
   发布构建不暴露。

## 三、已知限制是什么？
- M3-10#1 只联动 fps/码率/画质，**分辨率不随档位缩放**；验收要点原文只要求「各参数联动变化」。
- M6-11#2 用注入丢帧走同一 UI 渲染分支（代理验证），产品无队列深度字段。
- 正常录制状态栏两行文本保持不变，避免破坏 `g5_closeout` M6-11#1 与 `g5_storage` UI 断言。

## 四、哪些需求我没做完？
| 项 | 状态 | 原因 / 下一步 |
| --- | --- | --- |
| M3-10#1 / #2 / M6-11#2 | 已交付 | 12/12 用例中本卡 8 条断言全绿 |
| 分辨率联动 | 没做 | 需缩放输出能力，属新战线 |
| M7-13#1/#2（托盘红点、边框） | 没做，仍无卡 | 需自绘图标 + 分层窗口 + 像素级验证，轮 43 |
| V1.0 发布 | 仍不可发布 | 见主卡 HANDOFF |

## 五、交付包清单
`tasks/G5/S5-005/`：TASK.md、EVIDENCE.md、SELFTEST.md、HANDOFF.md。
原始输出在 `tasks/G6/V1-003/artifacts/g5_r42_closeout.ps1.txt`。

**执行方不自评为已验收。**