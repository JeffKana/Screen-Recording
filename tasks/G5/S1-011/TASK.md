# TASK: S1-011 轮 43 · M1-24 光标采集开关（CLI 透传）

阶段：G5｜角色：S1｜轮次：43｜来源：`.codebuddy/plans/轮43指令.md` §1 P1

## 需求范围
M1-24（`docs/01:56`）：「光标采集开关：可选择是否将鼠标指针写入成片」；
验收要点「关闭后成片任何位置都不出现鼠标指针；开启后指针位置与操作一致、无残影」。
引擎已有 `CaptureRequest::include_cursor` 与 `WgcCaptureSource::IsCursorCaptureEnabled`，
只缺 CLI 入口与透传。

## 文件域（白名单）
- `src/recorder/main.cpp`（`--cursor on|off` 解析、help、stdout `include_cursor=`、透传）
- `src/recorder/RecordingSession.h`、`src/recorder/RecordingSession.cpp`（Options 与 CaptureRequest 透传）
- `tasks/G5/S1-011/*`（本卡四件套）

> 不改 `src/capture/`（S2 域）、不改 `verify/`、`CONTRACT.md`、`vcpkg.json`。
> 行为断言在 `tasks/G6/V1-010`；本卡不声明需求子句（状态由 V1-010 统一声明）。

## 约束
1. 非法值不静默回落（`--cursor` 只接受 on/off）；默认 on，保持既有行为。
2. `stdout` 新增 `include_cursor=0|1` 供断言核验，不改任何既有字段。
3. 三构建零告警；发布包 help 应含 `--cursor`。
4. 无接口签名变更，不开契约变更单。

## 完成定义（DoD）
- [ ] `--cursor off/on` 各自 `record_exit=0` 且 `include_cursor` 对应 0/1
- [ ] `g5_r43_closeout.ps1` 的 M1-24 四条像素级断言全绿
- [ ] 三构建零告警、四件套齐全

## 覆盖子句

无（实现卡，状态由 `tasks/G6/V1-010` 统一声明）。

## 交付状态

无（本卡不声明需求子句）。