# TASK: S4-003 录制临时文件与停止时封装（M5-04）

阶段：G5｜角色：S4｜轮次：36｜来源：验收方轮 36 指令 P1-2（首批 5 条无卡子句收口）

## 需求范围

| 需求 | 子句 | 验收要点原文（docs/01） | 证据形式 |
| --- | --- | --- | --- |
| M5-04 | #1 | 录制过程中目标目录存在临时文件且不可被误认为成品 | 录制中途目录快照：存在 `.part`，产品列表里没有它 |
| M5-04 | #2 | 停止后临时文件被清理，仅留一个成品 | 停止后目录快照：`.part` 消失、只剩 1 个成品且可播 |

## 必读章节

- `docs/01-功能需求清单.md` M5-04、M5-06、M8-01、M8-07
- `docs/02-系统架构.md` §6（落盘与封装）
- `docs/05-非功能需求与验收.md` §5 AC-11（M5-04 + M8-01）
- `CONTRACT.md` §3（错误码）、§7

## 现状（轮 36 代码普查结论，**主体已实现**）

| 能力 | 现状 | 证据 |
| --- | --- | --- |
| 录制中写临时文件 | **已实现**：录制写 `xxx.mkv.part`，正常收尾才改名 | `src/recorder/RecordingSession.cpp:249-254`（`muxer.Open(part_path)`）；`src/recorder/OutputPath.cpp`（`PrepareOutputPath` / `CommitOutput`） |
| 停止后清理、只留一个成品 | **已实现**：`CommitOutput` 把 `.part` 改名成成品 | 同上（`RecordingSession.cpp:1106-1112`） |
| 同名冲突避让 | **已实现**：目标已存在时自动换名 | `OutputPath.cpp`（`renamed_for_conflict`） |
| `.part` 崩溃恢复 | **已实现**：`MkvRecovery` / `RepairIntoFallback` | `src/resilience/MkvRecovery.cpp`；用例 `g3_recovery.ps1` |
| 「不可被误认为成品」 | **部分**：UI 文件列表会过滤 `.part`（`g5_fileops.ps1:154-160` 断言 `file_op op=delete ok=0` + 日志含「临时文件」） | `verify/cases/g5_fileops.ps1:154-160` |
| **「分片」语义（多段 + 合并）** | **未实现**：本产品是单文件录制，没有分片目录与合并流程 | `docs/01` M5-04 的「临时分片 / 合并」措辞与实现不一致 |
| M5-04 专属验收用例 | **无**（`.part` 行为散在 `g5_fileops` / `g3_recovery` 里，没有一条断言同时覆盖「录制中存在 + 停止后清理」） | — |

## 文件域

- `src/recorder/OutputPath.cpp`（临时文件命名/提交，不越出既有职责）
- `src/app/`（产品列表过滤 `.part` 的展示口径，如需调整）
- `verify/cases/g5_tempfile.ps1`（新增：M5-04 两条断言）
- `tasks/G5/S4-003/*`

## 约束

- 不得改 `src/mux/` 的封装语义（不动已有 MKV 写入路径）。
- 「停止后仅留一个成品」必须按**目标目录**判定，不按进程内状态判定。
- 用例自清理目标目录（禁止读共享目录里的旧产物，D-37）。

## 完成定义（DoD）

- [ ] 录制中途目录里存在 `.mkv.part`，且 UI 产品列表里不出现它
- [ ] 停止后目录里 `.part` 消失、成品数 = 1、成片可播
- [ ] `verify/cases/g5_tempfile.ps1` 两条断言全绿
- [ ] 「分片/合并」措辞与实现的差异写进 `docs/01` 的文档缺陷单（**不改需求原文**）

## 覆盖子句

- M5-04#1
- M5-04#2

## 交付状态

- M5-04: 已实现

> 变更：轮 37 / S4（M5-04 结案）/ 依据：专属验收用例 `verify/cases/g5_tempfile.ps1` 落地
> （10 PASS / 0 FAIL），两条子句的验收要点各有独立断言；`g5_fileops` 9/9、`g3_recovery` 9/9 复跑通过。

> 轮 37：专属验收用例 `verify/cases/g5_tempfile.ps1` 已落地（10 PASS / 0 FAIL），
> 两条子句的验收要点各有独立断言 → 转「已实现」。
> 证据见 `tasks/G5/S4-003/EVIDENCE.md`。
