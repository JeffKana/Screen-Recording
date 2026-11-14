# HANDOFF · S4-007（轮 49 · M3-09#1/#2/#3 收口）

## 1. 我做了什么

| 层 | 改动 | 为什么 |
| --- | --- | --- |
| 容器（MP4） | `Mp4Writer::Close()` 改成 faststart：先 `FinalizeTrack` → 迭代求 moov 体积（stco 绝对偏移依赖 moov 自身体积，迭代到「体积 == 平移量」）→ `MoveMdatForward()` 把 `[mdat_offset_, EOF)` 整体后移 → `SeekAbs(mdat_offset_)` 后写 moov。新增 `MoveMdatForward`（1MB 分块、从尾部往前、单文件原地）。`PatchMdatSize` 的归位改 64 位 seek。 | 需求原文「MP4 优先 faststart 布局」。分块原地搬移不额外占一块与成片等大的磁盘（长录制 1~20GB），也不留临时半成品文件。 |
| 容器（MKV） | `audio_info_` 由单成员改 `std::vector<AudioTrackInfo>`；TrackNumber 按位置分配（视频 1、音轨 2..N，TrackUid 同步）；新增 `WriteAudio(track_index, …)`，原三参重载转调 `track 0`。 | M3-09#2「MKV 支持多音轨」。单音轨路径写出的 EBML 与轮 48 逐字节一致（同一段代码、同一组元素顺序）。 |
| 会话 | `Options::audio_tracks`（1~8，默认 1）；`submit_audio_block` 与收尾 flush 都按轨循环写；MP4 请求多轨时**显式回落为 1 并 `LogWarn`**；`Stats::audio_tracks` 回传实际轨数。 | 让「容器支持多轨」在真实录制路径上可达，同时不静默丢用户请求。 |
| 入口 | `sr-recorder --audio-tracks <1~8>`（缺值/非数字/越界 → `exit 2` + usage）；stdout 增 `format=` / `audio_tracks=`；`print_usage` 增一行。 | 参数错误口径沿用 `CONTRACT §18.2`「2 = 参数错误」，不新造退出码。 |
| 恢复探测 | `MkvRecovery::Probe` 对 ISO BMFF 头（`ftyp`）给专门原因：`iso-bmff (mp4) part: moov missing; mp4 crash recovery is V1.1, file kept as-is`，`repairable` 仍为 false。 | 之前所有失败原因都是 `no complete video cluster found`，用户/验收无法区分「MP4 未适配」与「MKV 真损坏」。 |
| UI | `AppConfig::container`（`[output] container=mkv\|mp4`，默认 mkv，非法值报错不回落）；`BuildOutputPath` 按它取扩展名；`RecorderController` 透传 `--format`；新增 `--set-container`（回读校验，非法值 exit 13 且 INI 不变）。 | M3-09#3 + 验收报告轮 48 F3：UI 固定 `.mkv` 会让 `--format mp4` 拿到 `.mkv` 文件名。 |

## 2. `.part` 语义的裁定（**必读**）

轮 49 卡要求「要么把临时 moov 落到 `.part` 尾部再回填，要么明确该 `.part` 判为不可修复且保留，二选一」。

**本轮选后者**，理由与代价：

- faststart 后 moov 只在 `Close()` 生成；录制期（含 mdat 搬移窗口内）进程被杀，`.part` 里没有可用 moov。
- 因此 MP4 的 `.part` **判不可修复、原样保留**：不删除、不改名、不产出「可播但时长错」的成品；
  `--list` 里该条目 `state=broken`，`--repair` 返回非 0 且原因写明 `iso-bmff (mp4) part: moov missing`。
- **代价**：MP4 录到一半崩溃 = 这段内容不可恢复（MKV 可以截断修复）。这正是 `docs/01` M3-09 默认值
  定为 MKV 的产品理由，也是 `docs/22` 把「MP4 崩溃恢复」登记为 **V1.1 前置**的原因。
- **没有做的另一条路**：把临时 moov 落 `.part` 尾部再回填，相当于在 MP4 上重造一遍 MKV 的恢复语义，
  属 V1.1 的恢复适配范围，本轮不开口子。

## 3. 我是怎么验证的

- 主证据：`EVIDENCE.md`（每条需求一条，含可重跑命令与原始输出行）。
- 汇总：`SELFTEST.md`（开工四项自查、三构建、四个用例、回归、红端、静态检查、DoD 勾选）。
- 关键结果：`g5_mp4` 19/0（box 自检 43/0）、`g5_multiaudio` 10/0、`g5_ui_format` 13/0；
  回归 `g3_playback` 11/0、`g3_recovery` 9/0、`g4_gate` 12/0、`ui_smoke` 3×103/0；
  红端（`ed8767a` 构建）`g5_mp4` 15/4、`g5_multiaudio` 0/10。
- 想自己复核 faststart，最快的一条：
  `python verify/tools/mp4_box_check.py <任意 mp4>` → 应看到 `moov-before-mdat`、`moov-in-first-64k` 两条 PASS。

## 4. 哪些需求我**没**做完 / 明确不做

1. **MP4 崩溃恢复**：本卡只做到「判不可修复 + 保留原文件」，恢复能力属 **V1.1 前置**。不冒充。
2. **真实双源录音**（系统声 + 麦克风各一条轨）：属 `M1-19` / V1.5。本轮两条轨是**同一路混音输出的
   同源镜像**，只证明「容器多轨能力 + 每轨独立 sample table / 独立可解 / 时长对齐」，不证明双设备混录。
3. **MP4 多音轨**：V1.0 只 1 条（`--audio-tracks >1` 对 mp4 显式回落 + 日志）。没做 mp4 多音轨容器写入。
4. **faststart 的收尾开销未做长录制实测**：8h / 20GB 级文件的搬移耗时与崩溃窗口没有实测数据
   （本轮最长样本 5s）。已登记为已知限制，见 §5。
5. **`--no-faststart` 开关**：轮 49 卡判据 ⑤ 写的是「非 faststart 路径（如 `--no-faststart` **若实现**）」，
   本轮没有实现该开关，回避方式是用 MKV 路径 + 常规 MP4 播放做回归（`g3_playback` / `g5_mp4` 的
   `r48-judge2-ffmpeg-decode`）。**这是一个可争议的取舍：如果验收方要求 MP4 内部有「非 faststart」对照，
   本卡不满足，需要补一张小卡。**

## 5. 风险与回退

| 风险 | 影响 | 回退 |
| --- | --- | --- |
| mdat 搬移窗口内崩溃 → 该 `.part` 头部是半搬状态 | 内容不可恢复（本来也没有 moov 可恢复） | 无代码回退；文件保留在磁盘，V1.1 恢复适配时再处理 |
| 长录制收尾多一次全量 I/O（读+写各 ≈ mdat 体积） | 8h/20GB 收尾可能多几秒到几十秒；磁盘剩余空间不足时写失败 → `Close` 失败 → exit 10 并保留 `.part` | 若验收认为不可接受，可改为「两次落盘」方案（写 `ftyp+moov+mdat` 到新文件再改名），代价是峰值占用 2× 空间 |
| MP4 增量写入与 `stco` 偏移强耦合，未来若加分片 moov 需重算 | 维护复杂度 | 保持单 moov，不做 fMP4 |
| UI 新增 `[output] container` 键 | 老 INI 无该键 → 默认 mkv（已验：`ui_smoke` 3×103 全绿） | 删除该键即回落默认 |
| `--audio-tracks` 是调试/本地参数，不进 IPC | 轨道选择未来若需要 UI 入口，需另开卡 | — |

## 6. 交接给验收方的两个具体请求

1. 请独立确认 `.part` 语义的选择（§2）：本卡选「判不可修复且保留」是否符合轮 49 卡「二选一」的意图。
2. 请裁定 §4 第 5 条（`--no-faststart` 未实现）是否算判据 ⑤ 的缺口——本卡按「若实现」的字面意思处理，
   但如果验收要求 MP4 内部存在非 faststart 对照，需补卡。
