# HANDOFF: M6-03 三项指标 / M8-14 诊断包（轮 30 交付，轮 31 补正证据包）

## 状态

- 代码：`src/diag/`（Logger + DiagPack）、`src/recorder/RecordingSession.cpp` 指标计数、
  `src/ipc/Protocol.cpp` 快照扩展块、`src/app/main.cpp` 状态栏/悬浮条与导出入口。
- 验收：`verify/cases/g5_diag.ps1` 36 条；`ui_smoke` 101 条；契约 8/8。
- 覆盖：`M6-03#1/#2`、`M8-14#1/#2` 均「已实现」。

## 关键约束（改代码前必读）

1. **指标只能来自 recorder**：UI 侧不得用「帧数 ÷ 时间」倒推，否则是自证。
   新增字段走快照扩展块（`u16 ext_bytes + u16 ext_version`），不要改 base 布局。
2. **诊断包只能是文本**：`version.txt / gpu.txt / log.txt / config.ini`，不得出现画面/音频
   （05 ER-14）。导出前所有路径过 `SanitizeText`。
3. **日志只留最近 10 分钟**：环形缓冲 + `TruncateByAge` + 400KB 截尾；不做整文件拷贝。
4. **测试开关由 `SR_TEST_HOOKS` 隔离**：Release 包 `--help` 不得列出 `--set-*` / `--preflight-fail`。

## 目录迁移说明（轮 31 / U2）

原 `tasks/T-17/S1-001/` 已迁到 `tasks/G5/S1-002/`：工具能扫到但人靠目录会漏，
统一按「阶段/轮次」归档。引用旧路径的文档已同步。

## 下一轮接口

- `docs/10-轮31-长录制报告.md`：LT-01/LT-02 代理结论，注意**代理 ≠ 单文件 8 小时**。
- `verify/cases/g5_lt01.ps1` 依赖 recorder 汇总里的 `mem_peak_mb` / `handles`，
  这两个字段是在轮 31 加的（`GetProcessMemoryInfo` + `GetProcessHandleCount`）。
