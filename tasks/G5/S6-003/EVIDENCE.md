# EVIDENCE: S6-003（轮 36 · R1 尾部补齐 + R2 解码判据 + G5 登记关闭）

> 环境：本机 / Windows 11 24H2 / MSVC 17.14 Release / 虚拟显示（OrayIddDriver）/
> ffmpeg 9.0.2（`C:\Users\98468\AppData\Local\Microsoft\WinGet\Packages\Gyan.FFmpeg…\bin`）
> 采样率：WASAPI 回环 44,100Hz / 2ch（**不是 44100 之外的其它值**，实测 `ffprobe` 与 recorder 一致）

---

## M6-09#1 —— 8 小时不间断录制后文件完整可播、音画同步无累积漂移（≤100ms）

### 证据 1：R1 —— 尾部音频补齐（红 → 绿）

**需求**：尾点偏差 ≤100ms **且** ≤ 内部采样点最大偏差 ×3（验收方轮 35 R1）。
**实现落点**：`src/recorder/RecordingSession.cpp` 收尾段（视频编码器冲刷之后、AAC 编码器冲刷之前）。

| # | 项 | 内容 |
| --- | --- | --- |
| 根因 | 主循环内的音频 pull 只在循环内发生，最后一次 pull 之后**没有补拉**；AAC 编码器还把末尾不足一帧的部分按帧边界输出 → 音轨比视频短 0~186ms | |
| 做法 | ① 先按时间轴再拉一次（补「最后一次 pull 之后流逝的时间」的真实音频）；② 仍短于视频则**补静音**，目标 = 视频轨末尾 PTS 向上取整到的 **AAC 帧边界 + 1 帧**，补写上限 200ms | |
| 目标推导 | 末帧 PTS = (ceil(S/1024) − 1) × 1024（S = 已提交样本数）→ 要让末帧 ≥ 视频末尾，需 S ≥ ceil(V/1024)×1024 + 1。该 +1 帧就是「必须多提交的那一帧」 | |
| 观测量 | `audio_tail_fill_frames` / `audio_tail_silence_frames`（新增只读 Stats 字段，stdout 可读） | |

**红绿对照表**（新断言 `lt01-tail-drift`）：

| 新断言 | 旧实现实测（旧二进制） | 新实现实测 |
| --- | --- | --- |
| `lt01-tail-drift`（尾点 ≤100ms 且 ≤3×内部最大） | **FAIL**：尾点偏差 **92.0ms**，内部 19 点最大 21.0ms → 92 > 63ms | **PASS**：尾点偏差 **1.0ms**（60s）/ **4.0ms**（10min），内部最大 21.0ms / 12.0ms → 1 ≤ 63、4 ≤ 36 |

**旧二进制不是伪造的**：用的是 8h 那次运行的**同一个** recorder 镜像
（`verify/artifacts/lt01-real-20261027-184702/sr-recorder-lt01.exe`，
SHA256 `56295C2A5D2C10A518815F54FDE1D7146763FAE8E6F6DADE3E784E2A7C2C10F7`，
与 `tasks/G5/S6-002/artifacts/FROZEN-20261028-0247-manifest.json` 的 `exe_sha256` **逐位一致**），
复制到 `verify/artifacts/r1-oldbuild/bin/sr-recorder.exe` 供用例取用。

- 红（命令）：

```powershell
pwsh -NoProfile -File verify/cases/g5_lt01_real.ps1 -Seconds 60 -SampleSeconds 5 `
     -MinFreeGB 1 -DriftIntervalSeconds 3 -BuildDir verify/artifacts/r1-oldbuild
```

```text
  PASS lt01-drift — 20 个采样点，最差 92.0ms（判据 ≤100ms）
  FAIL lt01-tail-drift — 尾点(at=60s) 偏差 92.0ms；内部 19 点最大 21.0ms（判据 ≤100ms 且 ≤3×内部最大=63.0ms）
LT01-REAL: 9 PASS / 1 FAIL ; 产物 …\verify\artifacts\lt01-real-20261028-173816
```

> 注意旧实现**同时**命中了旧判据（最差 92ms ≤ 100ms → PASS）——这正是验收方 R1 的论点：
> 只看「最差 ≤100ms」是靠余量碰运气（92 与 100 只差 8ms）。新断言把它变成结构性判据。

- 绿（命令，三次连跑 + 10min）：

```powershell
pwsh -NoProfile -File verify/cases/g5_lt01_real.ps1 -Seconds 60  -SampleSeconds 5  -MinFreeGB 1 -DriftIntervalSeconds 3
pwsh -NoProfile -File verify/cases/g5_lt01_real.ps1 -Seconds 600 -SampleSeconds 30 -MinFreeGB 1 -DriftIntervalSeconds 30
```

```text
===== 60s run #1 =====  PASS lt01-tail-drift — 尾点(at=60s) 偏差 1.0ms；内部 19 点最大 21.0ms（判据 ≤100ms 且 ≤3×内部最大=63.0ms）
===== 60s run #2 =====  PASS lt01-tail-drift — 尾点(at=60s) 偏差 1.0ms；内部 19 点最大 21.0ms（判据 ≤100ms 且 ≤3×内部最大=63.0ms）
===== 60s run #3 =====  PASS lt01-tail-drift — 尾点(at=60s) 偏差 1.0ms；内部 19 点最大 21.0ms（判据 ≤100ms 且 ≤3×内部最大=63.0ms）
===== 10min     =====  PASS lt01-tail-drift — 尾点(at=600s) 偏差 4.0ms；内部 19 点最大 12.0ms（判据 ≤100ms 且 ≤3×内部最大=36.0ms）
```

**连跑 3 次不贴阈值**：60s 三次都是 **1.0ms**（限值 63ms，差 62 倍）；10min **4.0ms**（限值 36ms，差 9 倍）。

**未重跑 8h 的理由**（轮 36 指令 §5.2 允许）：R1 只改**收尾最后 ~100ms** 的音轨，与「8 小时」这个时长尺度无关；
改动后尾点偏差在 60s / 10min 两级实测为 1.0ms / 4.0ms，且判据比 8h 旧产物（99ms）**更严**。
8h 产物已用 `-DecodeOnlyPath` 在新判据下复判（见证据 2），不重录。

### 证据 2：R2 —— 8h 产物用新判据复判（解码三件套）

**命令**（复判模式：**不重录 8 小时**，只对既有产物跑新判据）：

```powershell
pwsh -NoProfile -File verify/cases/g5_lt01_real.ps1 `
     -DecodeOnlyPath verify/artifacts/lt01-real-20261027-184702/lt01-8h.mkv -MinFreeGB 1
```

```text
复判既有产物：…\verify\artifacts\lt01-real-20261027-184702\lt01-8h.mkv
全片解码中（ffmpeg -v error，null muxer，-fps_mode passthrough）…
全片 framecrc 校验（帧数 + DTS 严格递增）…
  PASS lt01-full-decode — mode=-fps_mode passthrough exit=0 stderr_lines=6667 benign=6667 error_lines=0；
                         video_frames=864000 frames_encoded=864000 audio_frames=1240308 dts_violations=0
  PASS lt01-drift — 16 个采样点，最差 99.0ms（判据 ≤100ms）
  FAIL lt01-tail-drift — 尾点(at=28800s) 偏差 99.0ms；内部 15 点最大 26.0ms（判据 ≤100ms 且 ≤3×内部最大=78.0ms）
LT01-REAL: 2 PASS / 1 FAIL
```

三件套逐条对照（`docs/05` §4.2）：

| # | 判据 | 实测 | 结论 |
| --- | --- | --- | --- |
| ① | 解码零错误：exit=0，stderr 无解码错误 | exit=**0**；stderr **6,667 行全部判为 benign**（null muxer 的 `non monotonically increasing dts to muxer`），**error_lines=0** | ✅ |
| ② | 帧数 + DTS 严格递增 | `video_frames=864000` == `frames_encoded=864000`；`dts_violations=0`（音频流 1,240,308 帧同样单调） | ✅ |
| ③ | 默认 vsync 的 null-muxer DTS 警告不计 | 6,667 条被显式剔除并计数（`benign=6667`），不是靠忽略 stderr | ✅ |

> **判据口径差异（须报告）**：验收方轮 35 写的「`-vsync 0` → 0 条警告」在本机 ffmpeg 9.0.2 上**不复现**：
> `-vsync` 已被移除（报 `Unrecognized option 'vsync'`），等价选项 `-fps_mode passthrough` 下 stderr 仍是 6,668 行
> （含 1 行 h264 `co located POCs unavailable` + 6,667 行 null muxer 警告）。
> 用例因此按裁定③**显式剔除并计数**这类行，而不是依赖某个 ffmpeg 版本的行为——
> 这比「依赖 -vsync 0 出 0 条」更稳：换 ffmpeg 版本不会改变结论。

**`lt01-tail-drift` 在 8h 产物上 FAIL 是预期结果、且是更强的红证据**：
该产物由**旧构建**产出（`summary.json` 里 `audio_tail_fill_frames=0` 即为铁证），
尾点 99ms > 3×26ms=78ms → 旧机制在新判据下**结构性 FAIL**（验收方 R1 的担心得到实证）。

### 证据 3：R3 —— 结尾点进明细数组

8h 复判的 `summary.json`：`drift_points` 共 **16** 点（1800, 3600, …, 28800），
第 16 点 = `{"at_s": 28800, "video_pts": 28799.971, "audio_pts": 28799.872, "drift_ms": 99.0}`；
`worst_drift_ms=99.0`、`internal_max_drift_ms=26.0`、`end_drift_ms=99.0`、`end_drift_at_s=28800` 均已落盘。

### 证据 4：回归（R1 是产品行为改动）

| 用例 | 结果 | 说明 |
| --- | --- | --- |
| `g4_control` | **32 PASS / 0 FAIL** | 暂停/恢复中断点：`state_sequence=Idle,Preflight,Countdown,Recording,Paused,Recording,Paused,Recording,Stopping,Idle`、`multi-pause-count=2` |
| `g3_recovery` | **9 PASS / 0 FAIL** | `.part` 恢复后时长：`recovered-duration=3.459s`（轮 35 基线 3.436s，差 23ms 在采样抖动内）、`loss=2.54s`（基线 2.56s） |
| `g5_format` | **14 PASS / 0 FAIL** | CBR/VBR 码率统计：VBR 运动源 avg 6,022kbps 偏差 0.4%、峰值 7,512 < 9,000、filler=0、静态只记录、模式互不覆盖 |
| `g4_gate` / `g4_channel` / `g4_elapsed` | 12/0、52/0、13/0 | |
| `g3_output` / `g3_playback` / `g3_storage` | 15/0、11/0、11/0 | |
| `ui_smoke` ×3 | **103/0/0 ×3** | 与轮 35 验收方复跑一致（残留进程 0） |
| `g5_lt01_real`（60s ×3 + 10min） | **10 PASS / 0 FAIL** ×4 | |
| 契约 `contract_check.py` | **C1~C8 8/8 PASS** | |

---

## M6-02#1 —— 8 小时录制内存峰值与 10 分钟时相差 ≤ 50MB，无持续爬升趋势

### 证据 5：内存 / 句柄 / 丢帧（8h 产物复判 + 10min 新录）

| 项 | 8h 产物（轮 35 第 3 轮） | 10min 新录（轮 36） | 判据 |
| --- | --- | --- | --- |
| 内存峰值 − 10min 基线 | Δ**1.3MB**（549.3 → 550.6MB，PeakWS） | Δ**0.0MB**（544.0MB 全程平） | ≤50MB |
| 无持续爬升 | 8 段小时均值非逐段上升 | 分段均值 [360.8, 541.2, 539.9, 539.8, 540.2, 540.1, 540.1, 540.3]（暖机后 7 段非逐段上升，末−首 = −0.9MB） | 无持续爬升 |
| 句柄 | 488 → 498（+2.0%） | 483 → 494（+2.3%） | ≤+10% |
| 丢帧 | 0 / 176,910 | 0 / 34,735（0.000%） | <0.1% |
| 时长 | 28,799.971s，frames_encoded=864,000 | 599.999s，frames_encoded=18,000 | ≥ 秒数−10 |

### 证据 6：构建（零告警）

```powershell
cmake --build build-msvc --config Release   # 0 error / 0 warning
cmake --build build-msvc --config Debug     # 0 error / 0 warning
cmake --build build-mingw --config Release   # 0 error / 0 warning
```

---

## G5 登记关闭（记录动作，非证据动作）

### 证据 7：登记三步

```powershell
# ① 交付状态转「已实现」
#    tasks/G5/S6-002/TASK.md  → - M6-02#1: 已实现 / - M6-09#1: 已实现
#    tasks/G5/S1-006/TASK.md  → - M6-02#1: 已实现 / - M6-09#1: 已实现（证据一律指向 S6-002）
# ② 重生成覆盖表
python verify/tools/coverage_report.py --write
# ③ 核对
python verify/tools/coverage_report.py --check
python verify/tools/contract_check.py
```

```text
需求库 144 条, V1.0 66 条 / 116 子句, 已实现 87, 有卡未实现 0, 无卡 29, 已挂账 29, 缺理由 0
written: docs/09-V1.0覆盖表.md
coverage table is up to date
汇总: 8 项检查, 8 PASS, 0 FAIL
```

→ **G5 登记关闭达成：87 已实现 / 0 有卡未实现 / 29 无卡，`--check` 通过，契约 8/8。**

### 证据 8：P1 建卡后的终态（同一生成器）

```text
需求库 144 条, V1.0 66 条 / 116 子句, 已实现 87, 有卡未实现 4, 无卡 25, 已挂账 29, 缺理由 0
coverage table is up to date
汇总: 8 项检查, 8 PASS, 0 FAIL
```

变化只有 4 条子句：`M1-08#1`、`M1-08#2`、`M1-09#1`、`M1-27#1` 由「无卡」变成「有卡未实现」
（因为轮 36 按 P1 指令为它们建了卡）。**G5 的关闭依据是登记那一刻的 87/0/29 快照，不是终态。**

> **发现 R5（需 O0/验收方裁定，本轮不改他人卡）**：`M3-05` 与 `M5-04` 在轮 36 指令的
> 「首批 5 条无卡」清单里，但覆盖表显示二者**早已是「已实现」**（来自 `tasks/G3/S4-001`）。
> 而代码普查（`tasks/G5/S4-002/TASK.md` 的现状表）显示：
> **M3-05 实为未实现**（全仓无「编码器列表 + 不可用原因」结构，`MfVideoEncoder` 只取 `activates[0]`；
> 而 `tasks/G3/S4-001/EVIDENCE.md:62-65` 自己也写的是「PARTIAL：探测框架在，候选集不完整」）。
> → **覆盖表上的「已实现」宽于证据**，与项目反模式清单（「结论宽于证据会让台账失真」）冲突。
> 本轮**不擅自改他人卡**，请验收方/O0 裁定：要么按 `S4-002` 实现，要么修正 `S4-001` 的交付状态声明。

---

## 附：本轮改动清单（文件域）

| 文件 | 改动 |
| --- | --- |
| `src/recorder/RecordingSession.h` | `Stats` 新增 2 个只读字段（`audio_tail_fill_frames` / `audio_tail_silence_frames`） |
| `src/recorder/RecordingSession.cpp` | ① 音频提交/写盘收敛到唯一入口 `submit_audio_block`；② 收尾段新增「尾部音频补齐」（真实音频优先 + 补静音，200ms 上限）；③ 跟踪视频轨末尾 PTS（`max_video_pts_ms`） |
| `src/recorder/main.cpp` | stdout 新增两行（`audio_tail_fill_frames=` / `audio_tail_silence_frames=`） |
| `verify/cases/g5_lt01_real.ps1` | ① 解码判据改三件套（② framecrc 帧数 + DTS 严格递增；③ null-muxer 警告剔除并计数）；④ 新增 `lt01-tail-drift` 断言；⑤ 结尾点进明细数组（R3）；⑥ 采样间隔参数化 `-DriftIntervalSeconds`；⑦ 复判模式 `-DecodeOnlyPath`；⑧ 内存趋势判定改为「暖机后 7 段」（短录制可用） |
| `docs/05-非功能需求与验收.md` | 新增 §4.2「M6-09 判据注释」（三件套 + 尾点判据） |
| `docs/07-执行台账.md` | 轮 36 七列行 |
| `docs/09-V1.0覆盖表.md` | 生成器重写（87/0/29 → 建卡后 87/4/25） |
| `docs/11b-轮36-G6交付方案.md` | 新增（G6 方案） |
| `tasks/G5/S1-006/TASK.md`、`tasks/G5/S6-002/TASK.md` | 交付状态转已实现 + 交叉引用 |
| `tasks/G5/S2-002`、`S4-002`、`S4-003`、`S5-003` | P1 的 4 张新卡（M1-08/09、M3-05、M5-04、M1-27） |

**未改**：`vcpkg.json`、`verify/` 下除 `g5_lt01_real.ps1` 外的任何用例、`src/core/`、`src/ipc/`、`src/mux/`、`src/encode/`、`src/app/`、`src/capture/`。
