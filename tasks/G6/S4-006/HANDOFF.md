# HANDOFF: S4-006（轮 48 · MP4 writer 起步）

## 1. 我改了什么接口？

**既有公开接口签名一字未改**，未新增 `src/core/interfaces/` 下的接口，因此
`contract_check.py` C4 仍 8/8、**无需契约变更单**；`CONTRACT.md` 未动（根目录属 O0）。

| 文件 | 变化 | 下游影响 |
| --- | --- | --- |
| `src/mux/MuxFormat.h`（新） | `enum class ContainerFormat { Mkv, Mp4 }` + 共享 `VideoTrackInfo` / `AudioTrackInfo`（avcC / ASC 两种容器都用同一 blob） | `MkvWriter`、`Mp4Writer`、`RecordingSession`、`main.cpp` 共用；默认 `Mkv` |
| `src/mux/MkvWriter.h` | 两个嵌套 TrackInfo 改成共享结构的**类内别名** `using VideoTrackInfo = sr::mux::VideoTrackInfo;` | 既有 `MkvWriter::VideoTrackInfo` 调用方不用改；MKV 写盘字节行为不变（`g3_playback` 11/0、`g3_recovery` 9/0） |
| `src/mux/Mp4Writer.h/.cpp`（新） | 与 `MkvWriter` 并列：`Open` / `SetVideoTrack` / `SetAudioTrack` / `WriteVideo` / `WriteAudio` / `FlushPending` / `Close` / `SetFailWriteAfter` / 只读统计 | 由会话的 `MuxerRouter` 选择；`ClusterCount()` 对 MP4 恒为 0（无 Cluster 概念） |
| `src/mux/CMakeLists.txt` | `sr_mux` 源文件清单加 `Mp4Writer.cpp` | 无新库、无三方依赖 |
| `src/recorder/RecordingSession.h` | `Options::format`（默认 Mkv） | 只影响封装器选择，不进状态机 |
| `src/recorder/RecordingSession.cpp` | 新增匿名命名空间 `MuxerRouter`（两个成员 + 转发）；`muxer` 声明改为 `MuxerRouter muxer(options.format)`；三处 `MkvWriter::TrackInfo` 改用共享结构 | 会话主体（采集/编码/暂停/收尾/状态迁移）一行未改；`state_sequence` 三容器实测逐字一致 |
| `src/recorder/main.cpp` | `--format mkv\|mp4`（产品 help，非调试钩子）、非法值/缺值 `exit 2` + usage、`Options::parse_exit_code`（其它解析失败仍为历史码 1）、`--format mp4` 未给 `--out` 时默认 `out.mp4` | 发布包（`SR_TEST_HOOKS=OFF`）也认 `--format`；`build_release.ps1` 17/0 无调试开关泄漏 |

**MP4 布局**：`ftyp` + `mdat`（size=1 + 64 位 largesize，Close 时用 `_fseeki64` 回填）+ `moov`（后置）。
视频 timescale 90000、音频 = sample_rate、movie = 1000；`stbl` 含
`stsd(avc1+avcC / mp4a+esds)`、`stts`、`ctts`、`stss`（视频）、`stsc`、`stsz`、`stco|co64`。
A/V 在 mdat 里交错，故一个样本一个 chunk（`stsc` 固定 `(1,1,1)`）。

**关键修复（B 帧）**：MF 软编输出 PTS 在解码顺序里是 1,3,2,5,4,… 非单调；
按相邻 PTS 差写 `stts` 会得 0/9000 交替、时长 7.37s（录 5s）并让 ffmpeg 报 DTS 非单调。
现在把 PTS 集合排序当真实 DTS 序列 → `stts` 取相邻 DTS 差，PTS−DTS 放 `ctts`（有负偏移时 version 1
带符号），`stss` 扫 AVCC 样本里的 NAL type 5 只标真实 IDR（不信任会话按 GOP 假设传的 `keyframe`）。

## 2. 下游需要接什么？

1. **验收方**：按轮 48 卡 §通过判据独立重跑；关键命令见 `SELFTEST.md §1`。
   `g5_mp4.ps1 -RecorderPath <轮47二进制>` 可复现红端（0/13）。
2. **轮 49 `S4-006` 续卡 / `S4-007`**：
   - M3-09#1 **faststart**：moov 前置重排 + ffprobe 实测偏移；本卡 moov 后置，不算完成。
   - M3-09#2：MKV 多音轨（`audio_info_` 单成员、TrackNumber 硬编码 1/2）。
   - MP4 崩溃恢复：moov 在收尾才写，`.part` 不可播；`MkvRecovery` 是 MKV 专用，
     需给 MP4 单独设计（不要在 MKV 的 `FlushPending` 上改）。
   - 覆盖表：三条 M3-09 子句目前仍「无卡」，收口后由收口卡统一翻绿。
3. **O0 回写 `CONTRACT.md §18.2`**：轮 48 卡 §P0-3 授权 `--format` 非法值 `exit 2`，
   但官方退出码表只列 0/6/10/11/12/13/14/15/16。根目录文件执行侧只读，未改；请 O0 补一行。
4. **O0 裁定文档缺陷单**：`docs/01:111` 原文「容器格式：MP4（默认）与 MKV」与
   轮 48 卡判据 5「默认 mkv」冲突。本卡按判据实现（默认 mkv），**不自行解释需求、不改原文**，
   已在 `tasks/G6/V1-021/HANDOFF.md §2` 上报。
5. **UI（S5 域）**：`src/app/main.cpp:376` 仍固定生成 `.mkv` 输出名，本轮未接线；
   MP4 目前只有 CLI 入口，UI 要支持需另立卡。
6. **发布范围**：用户 2026-11-13 拍板 `docs/05 §7.2` ⑤⑥ 依赖真机，v0.1 暂不做兼容性、
   仅以当前机器为准。这是范围收窄的书面例外，路线图回写在 `docs/22`；执行侧不宣布可发布。

## 3. 已知限制是什么？

- faststart 未做、moov 后置；MP4 的崩溃恢复与写失败兜底（`RepairIntoFallback`）未适配，留轮 49。
- `stts` 在 Close 时由排序后的 PTS 生成；若注入导致重复/倒退 PTS，会做防御性 +1 tick 单调修正，
  该分支未在极端注入下验证。
- 逐样本元数据（sizes/offsets/PTS/ctts）常驻内存 ≈16B/样本，8h@30fps 约 20MB；未做 8h MP4 长稳。
- `co64`（偏移 >4GB）与 64 位 mdat 回填路径代码在，但未在真机 >4GB 文件上实测。
- 本机是 i5-13500 / UHD 770 / Win11 build 26200，**不是** `docs/05 §1` 的 B 机；
  矩阵单屏格用 `--display 0` 代表单屏（降级验证），150% DPI / 三厂商硬编 / Win10 均缺真机。
- `keyframe` 形参保留仅为与 `MkvWriter` 接口对齐，`stss` 不读它（已 `(void)keyframe` 并注释原因）。

## 4. 哪些需求我**没**做完？

| 需求 | 状态 | 没做完的部分 |
| --- | --- | --- |
| M3-09#1 | **起步，未翻绿** | faststart（moov 前置）重排 + 偏移实测；MP4 崩溃恢复 |
| M3-09#2 | **未开工** | MKV 多音轨与更长容错（轮 49 `S4-007`） |
| M3-09#3 | **起步，未翻绿** | 格式入口与状态机一致性已交付，但依赖 #1 收口终审；UI 侧格式选择未接线 |
| M3-04#1 | 维持既有「已实现」 | 本卡未改判据；5 连复核由 `V1-021` 完成（5×15/0） |
| M1-27#1 / M6-06 / 4 条真机子句 | 未碰 | 终审与真机项见 `docs/22` |
| `docs/05 §7.2` ⑤⑥ | 范围收窄（O0 例外） | 仍缺真机，判据本身未达成；不冒充 PASS |
| 验收结论 | 不写 | 由验收方独立重跑后给 |

## 2.1 交付通道状态（如实记录，非验收结论）

- 分支：`round/48`，从 `main` @ `91bbbe2` 拉出；`git fetch origin` 当时因 TLS 错误未成功，
  本地 `origin/main` 引用与用户书面确认的合并 SHA 一致，PR 创建前会再核一次。
- PR 编号与 CI 三项结果在 PR 创建后由主卡 `V1-021/HANDOFF.md` 补记。
