# TASK: S6-001 诊断包「最近日志」真实截断（轮 32）

阶段：G5｜角色：S6｜轮次：32｜来源：验收方轮 32 指令 P1-4

## 需求范围

| 需求 | 子句 | 验收要点原文（docs/01） | 证据形式 |
| --- | --- | --- | --- |
| M8-14 | #1 | 一键导出含版本、硬件信息、最近日志、当前配置的诊断包 | 用**真实 30min+ 会话日志**导出，`log.txt` 跨度 ≤10min 且保留最新行 |

轮 31 的缺口：截断只用合成的 2 小时日志（`--diag-window-test`）验过，
没证明真实会话日志会被裁；且 `sr-recorder --export-diag` **根本没带自己的日志**
（只把路径写进 `session`），「最近日志」名不副实。

## 必读章节

- `docs/01-功能需求清单.md` M8-14
- `docs/05-非功能需求与验收.md` §3（诊断包体积/脱敏）
- `docs/06-子智能体执行规程.md` §6.1

## 文件域

- `src/recorder/main.cpp`（`--export-diag` 传入 `recorder_log_path`）
- `src/diag/DiagPack.cpp`（只读，不改截断逻辑）
- `verify/cases/g5_diag_real.ps1`

## 上游交付

- 轮 30 `tasks/G5/S1-002`（T-17）：`TruncateByAge` + 400KB 上限 + 脱敏
- 轮 32 的真实 60min 会话日志（`verify/artifacts/cbr60/cbr60.log`）

## 约束

- 不得放宽 10min / 400KB 上限来「通过」。
- 不得整文件拷贝（大日志会撑爆包体）。
- 不得改 `g5_diag.ps1` 既有 36 条断言。

## 完成定义（DoD）

- [x] `--export-diag` 带 `--log-file` 时，`log.txt` 含 recorder 段
- [x] 真实 ≥30min 日志导出后跨度 ≤ 660s（10min + 1min 余量）
- [x] 导出保留最新一条（末条时间戳与源日志一致）
- [x] `log.txt` ≤ 400KB
- [x] `g5_diag` 36/36 无回归

## 覆盖子句

- M8-14#1

## 交付状态

- M8-14: 已实现
