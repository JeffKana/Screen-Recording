# TASK: V1-024 轮 52 · faststart 预留回填 + 长稳第 3 次 + F3-42 定向复现

阶段：G6｜角色：V1（主卡：`src/mux` faststart / 用例 / 证据 / 交付包）｜轮次：52
来源：`.codebuddy/plans/轮52指令.md`（判据以此为准）+ `.codebuddy/plans/验收报告-轮51.md`（§R1/§R2/§R3/§R4）
+ `docs/26`（缺陷分级）+ `docs/22`（六条判据差距）

> 本轮**只做三件事**：① MP4 faststart 改「预留 moov + 回填」；② 长稳第 3 次；③ `F3-42` 定向复现。
> **不改默认容器**（仍 MKV）、**不做 MP4 崩溃恢复**（V1.1）、**不改 `docs/01` 需求原文**。
> 上游：`main` @ `91b96c1`。全轮在同一个 exe 上跑并记 SHA256（`EVIDENCE.md §0`）。

## 需求范围

| # | 项 | 要求（照抄轮 52 卡） | 证据形式 |
| --- | --- | --- | --- |
| P0-1 | MP4 faststart「预留 moov + 回填」（R2） | 开录按样本数上限预留 `free` box；Close 把 moov 写进预留区，**不再全量搬移 mdat**；预留不足 → 回退搬移并打 `faststart_fallback=move` | 判据五条：① 1.15GB/240s stop→可播 ≤1s（对比同参数 MKV）② 8h 外推 ≤10s（附计算式）③ `moov` 偏移 < `mdat` 偏移、ffprobe/全解零 error ④ 人为调小预留 → 回退且产物可播 ⑤ `g5_mp4` 19/0、`g3_playback` 11/0（`EVIDENCE.md §1`） |
| P0-2a | LT-01 第 3 次 | 8h 单文件，六项判据同前两次；记录 exe SHA256 与介质类型 | `verify/artifacts/lt01-real-*/summary.json`（`EVIDENCE.md §4`） |
| P0-2b | LT-02 第 3 次 | 30min 静音段 + 恢复段 8/8 | `verify/artifacts/lt02-*/`（`EVIDENCE.md §4`） |
| P0-3 | `F3-42` 定向复现（R1） | 场景各 10 次：① 两个用例并发 ② 抢单实例锁 ③ 抢占同一输出路径；任一次复现 → 升 Blocker + 缺陷单 + Forensics | `verify/cases/g5_f342_repro.ps1` + `runs.csv`/`summary.json`（`EVIDENCE.md §3`） |
| P1-a | `g5_mp4` 判据拆分（R3） | ① 通用：`moov` 偏移 < `mdat`；② 短素材（≤60s）：`moov` 在前 64KB；两条都要先红后绿 | `EVIDENCE.md §2` 红-绿表 |
| P1-b | issue 模板演练（R4） | `gh issue create` 提一条 `[drill]` 样例，记录 issue 号后关闭 | `EVIDENCE.md §5` |
| P1-c | `docs/22` | 判据 ③ 更新为 3/3；判据 ② 引用 `docs/26` 最新数字 | `docs/22` |
| P1-d | 台账 | 轮 52 一行留白 | `docs/07` |

## 必读章节（开工四项自查见 `SELFTEST.md §0`）

- `.codebuddy/plans/轮52指令.md`（本卡全文）、`.codebuddy/plans/验收报告-轮51.md`（四个裁定）
- `docs/26 §7`（`F1-51`：moov 线性增长）、`docs/22`（六条判据差距）、`docs/05 §7.1/§7.2`
- `docs/01` M3-09 验收要点原文（「MP4 优先 faststart 布局」）
- `docs/20`（分支 / PR / CI / 台账一行制）、`docs/06`（四件套 / 先红后绿）
- `CONTRACT.md §7`（IPC 协议，未改动）、`§18.2`（退出码表）

## 文件域白名单

| 类别 | 文件 |
| --- | --- |
| 实现 | `src/mux/Mp4Writer.h`、`src/mux/Mp4Writer.cpp` |
| 接线 | `src/recorder/RecordingSession.h`、`src/recorder/RecordingSession.cpp`、`src/recorder/main.cpp`（`--mp4-reserve` 调试开关） |
| 用例/工具 | `verify/cases/g5_mp4.ps1`（判据拆分 + 回退断言）、`verify/cases/g5_faststart_perf.ps1`（新）、`verify/cases/g5_f342_repro.ps1`（新）、`verify/tools/mp4_box_check.py`（64KB 判据按口径拆分） |
| 文档 | `docs/07`、`docs/22`、`docs/26` |
| 交付包 | `tasks/G6/V1-024/**`（`artifacts/` 不入库） |

**禁止**：`verify/` 下既有断言的放宽（只做「拆分」且有裁定依据）、`docs/01`（需求原文）、`docs/05`（判据）、
`vcpkg.json`、其他容器的写出路径（`src/mux/MkvWriter.*`）、MP4 崩溃恢复（V1.1）。

## 覆盖子句

无（`M3-09#1` 已在本轮前记「已实现」，本轮是**性能与判据口径**收口，不新增/回退任何子句；
覆盖表应保持 **111 / 1 / 4**，`coverage_report --check` PASS 即为证据）。

## 交付状态

不声明子句完成。P0/P1 逐条结果见 `EVIDENCE.md`；**未做的项在 `HANDOFF.md §3` 逐条写明「未做/未测」**。
本轮不自评「已验收」：faststart 五条与 LT 原始输出由验收方独立重跑核对。
