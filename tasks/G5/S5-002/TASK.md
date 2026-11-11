# TASK: S5-002 M5-06 录制文件列表（轮 32）

阶段：G5｜角色：S5｜轮次：32｜来源：验收方轮 32 指令 P0-3

## 需求范围

| 需求 | 子句 | 验收要点原文（docs/01） | 证据形式 |
| --- | --- | --- | --- |
| M5-06 | #1 | 列表按时间倒序 | `g5_list.ps1` 顺序断言 + 文件 mtime 对照 |
| M5-06 | #2 | 录制中的条目实时更新时长 | 录制中 `list_live` 采样，时长增长 ≥1s |
| M5-06 | #3 | 异常中断的条目标注「可恢复」 | 列表 `state=recoverable` 与 `sr-recorder --scan` 的 `repairable=1` 集合逐一相等 |

同时要求（任务卡附加，来自验收方注意事项③）：**「可恢复」必须复用 `--scan` 的判定**，不允许列表自己算一套。

## 必读章节

- `docs/01-功能需求清单.md` M5-06、M5-04（临时文件与成品）
- `docs/02-系统架构.md` §4（UI ↔ Recorder 边界：UI 不持有采集/编码对象）
- `docs/06-子智能体执行规程.md` §5（文件域与契约变更）

## 文件域

- `src/recorder/main.cpp`（新增 `--list` / `--list-skip` 子命令）
- `src/resilience/MkvRecovery.{h,cpp}`（新增 `InspectHeader`：只读头取时长/分辨率）
- `src/core/types/StorageConfig.h`（`RecoveryItem` 增加只读展示字段）
- `src/app/main.cpp`、`src/app/CMakeLists.txt`（列表窗口 + comctl32）
- `verify/cases/g5_list.ps1`

## 上游交付

- M5-04 的 `.part` → 成品改名（`OutputPath::CommitOutput`）
- M8-02 的 `MkvRecovery::Scan/Probe`（`--scan` 的输出格式）
- 轮 31 `tasks/G5/S1-004`：M5-06 三条全部「未开工」

## 约束

- UI 只链接 `sr_ipc`/`sr_core`/`sr_diag`，不得直接链接 capture/encode/mux/resilience。
- 列表数据一律来自 `sr-recorder` 子进程，不在 UI 里复制 MKV 解析。
- 不得修改 `--scan`/`--repair` 的既有输出格式（既有用例依赖 `file=/repairable=/reason=`）。
- 新依赖只允许系统库（comctl32），不得引入三方库。

## 完成定义（DoD）

- [x] `sr-recorder --list <dir>` 输出「时间倒序」的条目，含 名称/大小/时间/时长/分辨率/状态
- [x] `可恢复` 与 `--scan` 同源（同一次 `Probe`），用例断言两者集合相等
- [x] 录制中的条目每秒刷新时长（数据取自 recorder 的 `session_elapsed_ms`）
- [x] 主窗口按钮 + 托盘菜单 + `--show-list` 三处入口
- [x] `g5_list` 24/24，`ui_smoke` 101/101 无回归

## 覆盖子句

- M5-06#1
- M5-06#2
- M5-06#3

## 交付状态

- M5-06: 已实现

> 三条判据均有自动化断言；「重命名/删除」属 M5-07，不在本卡范围。
