# TASK: S4-007 轮 49 · M3-09#1/#2/#3 收口（MP4 faststart + MKV 多音轨 + UI 扩展名）

阶段：G6｜角色：S4（产品实现卡，随主卡 `V1-022` 交付）｜轮次：49
来源：`.codebuddy/plans/轮49指令.md` §P0-1~§P0-4 / §通过判据 / §注意事项、`验收报告-轮48.md` §2 三件裁定

> 规划侧只下发了轮 49 指令（`.codebuddy/plans/轮49指令.md`），本卡是该指令在本文件域内的
> 派生执行卡；开工四项自查见 `SELFTEST.md §0`。本轮无并行实现卡，`S4-007` 与 `V1-022`
> 文件域已划开（本卡只动 `src/` 与三个验收用例；主卡只动文档与交付包），无交集。
> 上游：`main` @ `ed8767a`（PR #9 `round/48` + PR #10 验收记录已合并，轮 48 验收「达标，S=97.6」）。

## 需求范围

| # | 需求 | 验收要点原文（`docs/01` M3-09） | 判据（照抄轮 49 指令） | 证据形式 |
| --- | --- | --- | --- | --- |
| P0-1 | M3-09（文档口径） | 容器格式：MKV（V1.0 默认）与 MP4（`--format mp4`） | 改 `docs/01` M3-09 需求描述 + 变更记录；`docs/05 §7.2` 一字不动 | `docs/01:111` 与变更记录行；`git diff --stat docs/05` 为空 |
| P0-2 | M3-09#1 | MP4 优先 faststart 布局 | ① `moov` 偏移 < `mdat` 偏移 ② ffprobe 时长 ±0.2s ③ 全片解码零 error ④ 前 64KB 可解析 `moov` ⑤ 回归：非 faststart 路径仍可播；`.part` 语义有专门断言 | `g5_mp4.ps1` 的 `r49-faststart-*` / `r49-part-*` + `mp4_box_check.py` 43/0 |
| P0-3 | M3-09#2 | MKV 支持多音轨与更长容错 | 双 audio stream 各自可解、与视频差 ≤100ms；单音轨零回归 | `g5_multiaudio.ps1` 10 条（含单音轨回归与 MP4 单轨上限） |
| P0-4 | M3-09#3 | 切换格式不影响录制流程 | UI 输出名跟随格式；`AC-10` 的 `_001` 避让在 `.mp4` 上成立 | `g5_ui_format.ps1` 13 条 |

**实现要点（与判据一一对应）**

- **faststart**：`Mp4Writer::Close()` 先生成 moov（体积依赖 stco 绝对偏移 → 迭代到「体积 == 平移量」），
  再把 `[mdat_offset_, EOF)` 整体后移该体积（1MB 分块**从尾部往前**搬、单文件原地完成），最后把 moov
  写进腾出的位置。不额外占一块与成片等大的磁盘，也不留 `<name>.faststart` 半成品。
- **`.part` 语义（二选一已选定「判不可修复且保留」）**：faststart 的 moov 只在 `Close()` 生成，录制期
  强杀的 `.part` 只有 `ftyp + mdat`；`MkvRecovery::Probe` 对 ISO BMFF 判定 `repairable=0` 并给出
  `iso-bmff (mp4) part: moov missing` 的原因，**不删除、不改名、不产出「可播但时长错」的成品**。
  MP4 的恢复适配登记为 V1.1 前置（`docs/22`），不在 V1.0 冒充。
- **多音轨**：`MkvWriter::audio_info_` 改 `std::vector<AudioTrackInfo>`，TrackNumber 按位置分配
  （视频 1、音轨 2..N），新增 `WriteAudio(track_index, ...)`；`--audio-tracks <1~8>` 只影响容器轨数，
  不进 IPC、不进状态机。MP4 在 V1.0 只承载 1 条音轨 → 显式回落 + 日志，不静默丢请求。
- **UI 扩展名**：`AppConfig::container`（`[output] container=mkv|mp4`，默认 mkv）→ `BuildOutputPath`
  按它取扩展名、`RecorderController` 以 `--format` 透传给子进程；`--set-container` 提供无 GUI 的
  持久化入口（非法值拒绝，与 `--set-output` 同口径 exit 13）。

## 必读章节（开工四项自查已定位）

- `docs/01-功能需求清单.md` §M3-09（:111，验收要点三句原文）、`## 变更记录`（:293 起）
- `docs/05-非功能需求与验收.md` §7.2（发布判据）/ §7.3（判据不因实现困难放宽）——**只读，不改**
- `docs/02-系统架构.md` §7 模块表 / ADR-02（录制中写 MKV、停止时 remux）
- `CONTRACT.md` §2 文件域、§3 分层依赖、§6 错误码、§18.2 进程退出码（码 2 = 参数错误）
- `docs/22-轮47-V1.0差距盘点.md` §1/§2（唯一路线图；MP4 恢复 = V1.1 前置）
- `.codebuddy/plans/轮49指令.md`、`.codebuddy/plans/验收报告-轮48.md` §2

## 文件域白名单（越界即停工）

| 类别 | 文件 |
| --- | --- |
| 容器实现 | `src/mux/Mp4Writer.h`、`src/mux/Mp4Writer.cpp`、`src/mux/MkvWriter.h`、`src/mux/MkvWriter.cpp` |
| 会话/入口 | `src/recorder/RecordingSession.h`、`src/recorder/RecordingSession.cpp`、`src/recorder/main.cpp` |
| 恢复探测 | `src/resilience/MkvRecovery.cpp` |
| UI | `src/app/AppConfig.h`、`src/app/AppConfig.cpp`、`src/app/RecorderController.h`、`src/app/RecorderController.cpp`、`src/app/main.cpp` |
| 用例/工具 | `verify/tools/mp4_box_check.py`、`verify/cases/g5_mp4.ps1`、`verify/cases/g5_multiaudio.ps1`（新增）、`verify/cases/g5_ui_format.ps1`（新增） |
| 交付包 | `tasks/G6/S4-007/**`、`tasks/G6/V1-022/**` |

**禁止**：`vcpkg.json`（未改）、`docs/05`（未改）、`verify/lib/**`、他人任务卡、`*.exe`/`build-*/`
入库。`docs/01` / `docs/07` / `docs/09` / `docs/22` 由主卡 `V1-022` 负责回写，本卡只提结论。

## 覆盖子句

- M3-09#*（即 #1 faststart / #2 多音轨 / #3 切换格式不影响流程；`docs/01` 未拆更多子句）

## 交付状态

- M3-09#1: 已实现（faststart 五条判据 + `.part` 语义专门断言）
- M3-09#2: 已实现（双音轨 MKV 各自可解、末 PTS 差 16ms；单音轨零回归）
- M3-09#3: 已实现（三格式 `state_sequence` 逐字一致；UI 扩展名跟随格式、`.mp4` 上 `_001` 避让成立）

> 变更：轮 49 / `S4-007` / 依据 `.codebuddy/plans/轮49指令.md` §通过判据 #1~#4 与验收方轮 48 的三件裁定；
> 逐条证据见 `EVIDENCE.md`。本卡**不自评「已验收」**——结论由验收方独立重跑后给。
