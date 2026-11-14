# EVIDENCE: S4-006（轮 48 · MP4 writer 主体 + `--format` 入口）

> 口径声明：本卡是 **M3-09 起步切片**，只证明「可播 MP4 + 格式入口 + 状态机不变」，
> **不证明 faststart（moov 前置）已实现**、不证明多音轨、不写验收结论。
> 数据来源：2026-11-13/14 · `build-msvc/bin/Release/sr-recorder.exe`（另含 Debug、MinGW、
> `SR_TEST_HOOKS=OFF` 发布包）· Win11 build 26200 / i5-13500 / UHD 770 /
> ffprobe & ffmpeg 9.0.2（WinGet，PATH 可见）。
> 主用例 `verify/cases/g5_mp4.ps1` 与结构核验 `verify/tools/mp4_box_check.py` 由 `V1-021` 落地；
> 原始输出在 `tasks/G6/V1-021/artifacts/`（`artifacts/` 被 `.gitignore` 排除，命令可重跑再生）。

## 0. 先红后绿（本卡所有新断言）

- 红端基线：轮 47 二进制（`build-msvc/bin/Debug` 改动前另存 `%TEMP%\sr-r48-baseline\sr-recorder.exe`），
  SHA256 `DF97B02E39DFC5B4F76CE46D51CC4337ED220F0F2FFB0ECB487025C6058A2DF6`
  （`tasks/G6/V1-021/artifacts/baseline-sha256.txt`）。
- 命令（红）：
  `pwsh -NoProfile -File verify/cases/g5_mp4.ps1 -RecorderPath "$env:TEMP\sr-r48-baseline\sr-recorder.exe"`
- 输出：`artifacts/r48-mp4-red.txt` —— **`RESULT: 0 PASS / 13 FAIL`**；
  首条 `FAIL r48-mp4-record-exit0 — exit=1 record_exit=-1 成片=False`（旧 CLI 不认 `--format`）。
- 命令（绿）：`pwsh -NoProfile -File verify/cases/g5_mp4.ps1`
- 输出：`artifacts/r48-mp4-green.txt` —— **`RESULT: 13 PASS / 0 FAIL`**。
- 结论：**部分通过→通过**；验收判定由验收方独立重跑后给。

## P0-2 #1 · ffprobe `format_name` / `duration`

- 命令：`.\build-msvc\bin\Release\sr-recorder.exe --record --seconds 5 --fps 30 --bitrate 2000 --format mp4 --out $env:TEMP\sr-mp4\r48.mp4`
  再 `ffprobe -v error -show_entries format=format_name,duration -of json <file>`
- 输出（原始行）：`PASS r48-judge1-format-duration — format_name=mov,mp4,m4a,3gp,3g2,mj2 duration=5.03873s vs 录制 5s（±0.2s）`；
  `record_exit=0`、`state_sequence=Idle,Preflight,Countdown,Recording,Stopping,Idle`。
- 结论：通过（5s 录制 → 5.039s，判据 ±0.2s）

## P0-2 #2 · ffmpeg 全片解码

- 命令：`ffmpeg -v error -i <file.mp4> -f null -`
- 输出：`PASS r48-judge2-ffmpeg-decode — ffmpeg -v error -f null - : exit=0 error 行=0 head=[]`
- 结论：通过（零 error 级输出）

## P0-2 #3 · 视频帧数 = `frames_encoded`

- 命令：`ffprobe -v error -show_entries stream=index,codec_type,codec_name,nb_frames,duration,time_base,r_frame_rate -of json <file>`
- 输出：`PASS r48-judge3-frame-count — 视频 nb_frames=150 vs frames_encoded=150；codec=h264 time_base=1/90000 r_frame_rate=30/1`；
  另 `ffprobe -count_frames` 两容器各 150 帧（`r48-both-containers-playable`）。
- 结论：通过

## P0-2 #4 · 音频时长 vs 视频末 PTS ≤100ms（M6-09 口径）

- 命令：同上 `-show_entries stream=codec_type,duration` + recorder stdout 的 `video_last_pts_ms` / `audio_last_pts_ms`
- 输出：`PASS r48-judge4-av-duration — 音频时长=5.03873s 视频时长=5s 视频末PTS=4999ms 音频末PTS=5015ms；|音频时长-视频末PTS|=39.7ms、|音-视时长|=38.7ms（≤100ms）`
- 结论：通过

## P0-2 #5 · 默认不带 `--format` 仍产 MKV

- 命令：分别跑 `--format mkv`、不带 `--format`，再 `ffprobe -show_entries format=format_name`
- 输出：`PASS r48-judge5-default-mkv — --format mkv → matroska,webm；不带 --format → matroska,webm（两者均为 MKV，既有行为不变）`；
  `PASS r48-format-state-sequence — mp4/mkv/default 三次 state_sequence 逐字一致`。
- MKV 零回归（独立跑）：`g3_playback` **11 PASS / 0 FAIL**、`g3_recovery` **9 PASS / 0 FAIL**
  （`artifacts/reg-g3_playback.txt`、`reg-g3_recovery.txt`）。
- 结论：通过（未改 `MkvWriter` 的写盘/FlushPending/恢复语义；只把两个嵌套 TrackInfo 提成共享别名）

## P0-2 附加 · sample table 正确（轮 48 卡 §注意事项 #1/#2）

- 命令：`python verify/tools/mp4_box_check.py <file.mp4>`
- 输出（原始行）：`RESULT: 42 PASS / 0 FAIL`，关键条目：
  - `PASS mdat-largesize — header=16 largesize=1478112 box_size=1478112`
  - `PASS mdat-payload-consistent — mdat 数据区 [48,1478144) / moov 起点=1478144`
  - `PASS video-stts-duration — stts 合计=450000 vs mdhd.duration=450000`
  - `PASS video-stts-deltas-positive — entries=[(150, 3000)]`（delta 来自真实 DTS 差，90000/30=3000，非 `1/fps` 累加）
  - `PASS video-stsz-count / video-offset-count / video-offsets-in-mdat / video-samples-fit-mdat`
  - `PASS video-stss-valid — 同步样本=[1, 31, 61, 91, 121] / 总样本=150`
  - `PASS video-ctts-count — version=1 ctts.samples=150 vs 150`
  - `PASS audio-stts-duration — stts 合计=222208 vs mdhd.duration=222208`
  - `PASS mdat-no-padding — 样本字节合计=1478096 vs mdat 数据区=1478096`
- 命令（stss 只标 IDR 的独立旁证）：
  `ffprobe -v error -select_streams v:0 -show_frames -show_entries frame=key_frame -of json <file>`
- 输出：`PASS r48-mp4-stss-first-frame-key — 首帧 key_frame=1，同步帧=5/150`。
- **首版错误（红）与纠正（绿）**：首版按相邻 PTS 差写 `stts`，实测 B 帧重排
  （前 16 个输出 PTS 为 333333/999999/666666/1666665/… 非单调）→ `stts` 出现 `0/9000` 交替、
  `format duration=7.366667`（录制 5s）、`avg_frame_rate=4500/221`、
  `ffmpeg -f null -` 报 `non monotonically increasing dts`。纠正为「PTS 集合排序 = 真实 DTS 序列，
  `stts` 取相邻 DTS 差；PTS−DTS 放 `ctts`（version 1 带符号）；`stss` 扫 AVCC NAL type 5 只标真实 IDR」后
  `duration=5.038730`、`avg_frame_rate=30/1`、解码零 error。两次实测见 `artifacts/r48-mp4-green.txt` 的历史记录
  与本卡 `SELFTEST.md §2`。
- 结论：通过

## P0-3 · 格式切换不改状态机 / 两容器均可播 / 非法值 exit 2

- 状态序列：命令见 `g5_mp4.ps1` 三次录制 →
  `PASS r48-format-state-sequence — mp4=[Idle,Preflight,Countdown,Recording,Stopping,Idle] mkv=[同] default=[同]`。
- 两容器可播：`PASS r48-both-containers-playable — MKV 解码 exit=0 error 行=0 计帧=150；MP4 解码 exit=0 计帧=150；frames_encoded=150`。
- 非法值：`--format avi` → `PASS r48-invalid-format-exit2 — exit=2（期望 2），usage 含 --format=True，原因可见=True`；
  `--format` 缺值 → `PASS r48-missing-format-exit2 — exit=2`。
- 默认输出名：`--format mp4` 未给 `--out` → `PASS r48-mp4-default-output-name — out.mp4 存在=True、out.mkv 不存在=True`。
- 结论：通过（`--format` 只影响 `MuxerRouter` 的封装器选择；`RecordingSession` 状态机迁移点一行未改）

## 发布包与三构建

- 命令：`pwsh -NoProfile -File verify/tools/build_release.ps1` →
  `RESULT: 17 PASS / 0 FAIL`；`PASS release-help-no-debug-switch — --help 里残留的调试开关：无`，
  且发布包 `--help` 含 `--format mkv|mp4`（产品参数，非 `SR_TEST_HOOKS` 调试开关）。
  发布包二进制录 2s MP4：`record_exit=0`、`frames_encoded=60`、
  `ffprobe format_name=mov,mp4,... duration=2.043356`、
  `python verify/tools/mp4_box_check.py` → `RESULT: 42 PASS / 0 FAIL`。
- 命令：`cmake --build build-msvc --config Debug` / `--config Release` / `cmake --build build-mingw`
  → 三个 `exit=0 warnings=0`（`artifacts/build-*.log`；MinGW 侧 `_fseeki64` 亦编译通过）。
- 静态：`coverage_report.py --check` PASS（**108/1/7 不变**）、`contract_check.py` 8/8、
  `card_consistency_check.py` 62 卡 0 冲突、`domain_table_check.py` `MISMATCH_COUNT=0`。

## 已知限制（不得计为通过）

1. **faststart 未做**：`moov` 在 `mdat` 之后；轮 49 才做 moov 前置重排并实测偏移。
2. **MP4 崩溃恢复弱于 MKV**：moov 在收尾才写，进程被杀的 `.part` 无法播放；
   `MkvRecovery` 是 MKV 专用，MP4 写失败/恢复路径留轮 49。本卡未改 MKV 的 `FlushPending` / 恢复语义。
3. **B 帧支持依赖 `ctts`**：`--inject-drop-every` 等注入会改变输出包节奏，未逐项验证；
   sample table 在 Close 时把重复/倒退 PTS 做防御性单调修正（+1 tick），极端注入下需轮 49 复核。
4. **长录制内存**：`stsz`/`stco`/`stts` 需要逐样本元数据（约 16 字节/样本），
   8h@30fps 约 20MB 常驻；本轮只验到 5s / 60s 样本，未做 8h MP4。
5. `mdat` 用 64 位 largesize + `_fseeki64` 回填，>4GB 走 `co64`；该分支未在真机上实测。
6. M3-09#1/#2/#3 **未翻绿**，`docs/09` 保持 108/1/7；执行方不写验收结论。
