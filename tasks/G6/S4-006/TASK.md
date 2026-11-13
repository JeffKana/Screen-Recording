# TASK · G6/S4-006（轮 48 · MP4 writer 主体 + 格式入口）

> 派生自 `.codebuddy/plans/轮48指令.md`（下称「轮 48 卡」）§P0-2 / §P0-3。
> 上游：`main` @ `91bbbe2`（PR #7 `round/47` + PR #8 验收记录已合并，S=94.7）。
> 本卡是**起步切片**：faststart（moov 前置）与 M3-09#2 多音轨留轮 49，不在本卡判据内。
> 规划侧只下发了轮 48 指令（无现成 `TASK.md`），按轮 47 `S4-008` / `V1-020` 的派生方式立卡。

## 需求范围（原文口径）

`docs/01-功能需求清单.md:111`：

| 需求 | 原文 | 本卡承诺 |
| --- | --- | --- |
| M3-09#1 | MP4 优先 faststart 布局 | **起步**：`ftyp` + `mdat` + `moov`（moov 可后置）、1 路 H.264 + 1 路 AAC、sample table 正确。**faststart 未做** |
| M3-09#3 | 切换格式不影响录制流程 | `--format mkv\|mp4`（默认 mkv）、状态机迁移不变、非法值 `exit 2` + usage |
| M3-04#1 | CBR 全片平均 ≤10% + 单秒 ≤2× | 不改判据；5 连复核在主卡 `V1-021` |

轮 48 卡 §P0-2 五条判据（本卡逐条给证据）：

| # | 判据 |
| --- | --- |
| 1 | `ffprobe -show_entries format=format_name,duration` → `mov,mp4,...`，duration = 录制秒数 ±0.2s |
| 2 | `ffmpeg -v error -i out.mp4 -f null -` → exit 0、无 error 级输出 |
| 3 | 视频帧数 = `frames_encoded` |
| 4 | 音频时长与视频末 PTS 差 ≤100ms（沿用 M6-09 口径） |
| 5 | 默认不带 `--format` 仍产 MKV，逐字节行为不变（回归 `g3_playback`） |

轮 48 卡 §P0-3：`--format` 只影响封装器选择、不得改变状态机迁移；同参数各录一次
`state_sequence` 完全一致；非法值 `exit 2`（参数错误）+ usage，不静默回退。

## 必读章节（开工四项自查已定位）

- `docs/01` §M3-09（:111）、§M3-04（:106）、§M3-05（:107）
- `docs/05` §7.2（:214）六条发布判据、§7.3（判据不因实现困难放宽）
- `docs/02` §7（:235 模块表 / `:254` ADR-02：录制中写 MKV、停止时 remux 为 MP4）
- `CONTRACT.md` §2（文件域）、§3（分层依赖）、§4（8 个 core 接口）、§5（状态机 T01~T15）、§18.2（退出码表）
- `docs/19` §0.B（M3-09 轮 48/49 排期）、`docs/22` §1/§2（唯一路线图）
- `.codebuddy/plans/轮48指令.md`、`.codebuddy/plans/验收报告-轮47.md`

## 文件域白名单（越界即停工）

| 文件 | 动作 |
| --- | --- |
| `src/mux/MuxFormat.h` | 新增：`ContainerFormat` 枚举 + 共享 `VideoTrackInfo` / `AudioTrackInfo` |
| `src/mux/Mp4Writer.h` / `src/mux/Mp4Writer.cpp` | 新增：MP4 封装器（与 `MkvWriter` 并列） |
| `src/mux/MkvWriter.h` | 仅把嵌套的两个 TrackInfo 改成共享结构别名；**不改 MKV 行为** |
| `src/mux/CMakeLists.txt` | 源文件清单加 `Mp4Writer.cpp` |
| `src/recorder/RecordingSession.h` / `.cpp` | `Options.format` 字段 + 封装器路由（不改状态机、不改 FlushPending/恢复语义） |
| `src/recorder/main.cpp` | `--format mkv\|mp4` 解析、非法值 `exit 2`、`PrintUsage` 一行、默认输出名 |
| `tasks/G6/S4-006/*` | 四件套 |

> 与轮 47 `S4-008` 的先例一致：`src/recorder/` 的接线由本卡显式声明（CLI 入口与会话
> 的封装器选择层），不动 `src/core/interfaces/`，不改状态机；`domain_table_check.py`
> 校验的是两张角色域表本身，不因这些接线改动而失配。
> **新增验收文件归 `V1-021`**（V1 名义域）：`verify/cases/g5_mp4.ps1`、
> `verify/tools/mp4_box_check.py`，见主卡白名单。

**只读禁改**：`src/core/`、`src/ipc/`、`src/app/`、`src/resilience/`、`vcpkg.json`、
根 `CMakeLists.txt`、`CONTRACT.md`、`docs/`、`verify/cases/` 下既有其它用例（含 `g5_closeout.ps1`，归 `V1-021`）。

## 上游交付物

- `main` @ `91bbbe2`；`round/48` 从该提交拉出。
- `build-msvc/bin/Release/sr-recorder.exe`（轮 47 末态，作红端基线）、ffprobe / ffmpeg（PATH）。
- `MkvWriter` 的 `Open/SetVideoTrack/SetAudioTrack/WriteVideo/WriteAudio/FlushPending/Close` 签名，
  `RecordingSession` 的 `state_sequence` / `frames_encoded` / `record_exit` 输出格式。

## 约束

1. **不引三方依赖**：`vcpkg.json` 只读；MP4 writer 只用 C++ 标准库 + `cstdio`。
2. **不改接口签名**：既有 `MkvWriter` 公开方法签名一字不动；不新增 `src/core/interfaces/` 下的接口，
   故**无需契约变更单**（`contract_check.py` C4 仍 8/8）。
3. **新增 CLI 参数与 `exit 2` 由轮 48 卡 §P0-3 直接授权（O0 决策）**；`CONTRACT.md §18.2`
   退出码表由 O0 回写，根目录文件执行侧只读，见 `HANDOFF.md §2`。
4. `docs/01` M3-09 原文写「MP4（默认）与 MKV」，与轮 48 卡判据 5「默认 mkv」矛盾：
   **按判据实现（默认 mkv）并提文档缺陷单**，不自行解释、不改需求原文（见 `V1-021/HANDOFF.md`）。
5. sample table：`stts` 的 delta 来自相邻真实 PTS/DTS 差（不用 `1/fps` 累加）；`stss` 只标真实 IDR
   （扫 AVCC 样本的 NAL type 5，不信任调用侧按 GOP 假设传的 `keyframe`）。
6. 不动 MKV 的 `FlushPending` / `MkvRecovery` / 恢复语义；两份封装器共用 `.part` 约定。
7. 用户拍板（2026-11-13）：`docs/05 §7.2` ⑤⑥ 依赖真机，**v0.1 暂不做兼容性，仅以当前机器为准**；
   本卡不把 ⑤⑥ 写成 PASS、不冒充降级版本，路线图回写归 `V1-021`。

## 完成定义（DoD）

- [ ] `g5_mp4.ps1` 五条判据 + 格式切换状态序列 + 非法值 `exit 2` 全部有证据，新增断言先红后绿
- [ ] 默认 MKV 零回归（`g3_playback` 11、`g3_recovery` 9、`g4_gate` 12、`g5_encoder_list` 17、`ui_smoke` 3×103）
- [ ] MSVC Debug / Release / MinGW Release 三构建 `exit=0 warnings=0`
- [ ] 契约 8/8、覆盖 `--check` PASS、卡面 0 冲突、域表 0 mismatch
- [ ] 无死代码 / 调试残留；改动未越白名单
- [ ] 四件套齐备；`## 交付状态` 与实际一致；未自评「已验收」

## 覆盖子句

无（起步切片，不独占声明任何子句完成）。
M3-09#1 / #2 / #3 保持 `docs/09` 的「无卡 + 挂账理由」不变，轮 49 收口后由收口卡统一翻绿；
本卡不制造「结论宽于证据」的假绿（验收报告-轮47 R1/R2 的教训）。

## 交付状态

本卡不声明子句完成：M3-09#1 的 faststart、M3-09#2 的多音轨、M3-09#3 的收口判据均留轮 49。
起步产物（可播 MP4 + 格式入口）已按轮 48 卡 §P0-2/§P0-3 交付，证据见 `EVIDENCE.md`。
