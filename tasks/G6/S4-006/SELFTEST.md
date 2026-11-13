# SELFTEST: S4-006（轮 48 · MP4 writer 起步）

> 执行方自测，非验收结论。共用主卡 `tasks/G6/V1-021/artifacts/` 的原始输出；
> 机器：Win11 build 26200 / i5-13500 / UHD 770 / ffprobe & ffmpeg 9.0.2 /
> MSVC Release+Debug / MinGW Release / `SR_TEST_HOOKS=OFF` 发布包。

## 0. 开工四项自查

| 检查项 | 结果 | 依据 |
| --- | --- | --- |
| 需求编号可检索 | **通过** | `docs/01`：`M3-09`:111、`M3-04`:106、`M3-05`:107、`M6-04`:199、`M1-27`:59、`M6-06`:201；原始输出 `tasks/G6/V1-021/artifacts/selftest-0-inputs.txt` |
| 必读章节可定位 | **通过** | `docs/05 §7.2`:214、`docs/02 §7`:222、`CONTRACT §2`:24 / `§18.2`:279、`docs/22 §1`:10、`docs/INDEX.md`:33（`docs/22` 已登记）；同上 |
| 上游交付物可获取 | **通过** | `main` @ `91bbbe2`（PR #7 / #8 已合并）；`origin/main` 与该 SHA 一致（`git fetch` 当时因 TLS `unexpected eof` 失败，改用本地远端引用 + 用户书面确认核对）；轮 47 二进制另存 `%TEMP%\sr-r48-baseline\`（SHA256 `DF97B02E…2DF6`）；`MkvWriter` 接口与 ffprobe/ffmpeg 可取 |
| 文件域无交集 | **通过** | 本轮只有 `S4-006`（`src/mux/`、`src/recorder/`、`tasks/G6/S4-006/*`）与 `V1-021`（`verify/`、`docs/`、`tasks/G6/V1-021/*`）；白名单逐项比对无重叠，无其它并行任务 |

> 规划侧只下发 `.codebuddy/plans/轮48指令.md`（无现成 `TASK.md`），按轮 47 `S4-008` / `V1-020` 的派生方式立卡。

## 1. 自测命令与输出

| 命令 | 结果 | 产物 |
| --- | --- | --- |
| `cmake --build build-msvc --config Release` / `--config Debug` / `cmake --build build-mingw` | 三个 `exit=0 warnings=0` | `artifacts/build-msvc-Release.log`、`build-msvc-Debug.log`、`build-mingw-Release.log` |
| `pwsh -NoProfile -File verify/cases/g5_mp4.ps1 -RecorderPath <轮47二进制>` | `RESULT: 0 PASS / 13 FAIL`（红） | `artifacts/r48-mp4-red.txt` |
| `pwsh -NoProfile -File verify/cases/g5_mp4.ps1` | `RESULT: 13 PASS / 0 FAIL`（绿） | `artifacts/r48-mp4-green.txt` |
| `python verify/tools/mp4_box_check.py <r48.mp4>` | `RESULT: 42 PASS / 0 FAIL` | 用例产物 `box-check.txt` |
| `ffprobe -v error -show_entries format=format_name,duration -of json <r48.mp4>` | `mov,mp4,m4a,3gp,3g2,mj2` / `5.038730` | 同上 |
| `ffmpeg -v error -i <r48.mp4> -f null -` | `exit=0`，error 行 0 | 同上 |
| `.\build-msvc\bin\Release\sr-recorder.exe --record --seconds 5 --fps 30 --bitrate 2000 --format mp4 --out <tmp>` | `record_exit=0`、`frames_encoded=150`、`video_last_pts_ms=4999`、`audio_last_pts_ms=5015` | `artifacts/r48-mp4-green.txt` run1 |
| `... --format avi` / `... --format`（缺值） | `exit=2` + usage（两次） | `r48-mp4-green.txt` run4/run5 |
| `pwsh -NoProfile -File verify/cases/g3_playback.ps1` / `g3_recovery.ps1` | `11 PASS / 0 FAIL` / `9 PASS / 0 FAIL` | `artifacts/reg-g3_playback.txt`、`reg-g3_recovery.txt` |
| `pwsh -NoProfile -File verify/tools/build_release.ps1` | `RESULT: 17 PASS / 0 FAIL`；发布包 `--help` 无调试开关、`--format` 可见 | `artifacts/r48-release-build.txt` |
| 发布包二进制 `--record --seconds 2 --format mp4` + `mp4_box_check.py` | `record_exit=0` / `duration=2.043356` / `42 PASS / 0 FAIL` | `artifacts/r48-release-build.txt` 附记 |
| `python verify/tools/coverage_report.py --check` / `contract_check.py` / `card_consistency_check.py` / `domain_table_check.py` | `coverage table is up to date`（108/1/7）/ `8 PASS, 0 FAIL` / `62 卡 0 冲突` / `MISMATCH_COUNT=0` | `artifacts/r48-static-checks.txt`、`r48-final-verification.txt` |

## 2. 红-绿对照

| 新断言 | 旧实现实测（轮 47 二进制） | 新实现实测 |
| --- | --- | --- |
| `r48-mp4-record-exit0` | FAIL：`--format` 未知参数 → `exit=1`、无成片 | PASS：`exit=0`、`record_exit=0`、成片存在 |
| `r48-judge1-format-duration` | FAIL：无成片、`format_name=` 空 | PASS：`mov,mp4,...` / `5.03873s`（±0.2s） |
| `r48-judge2-ffmpeg-decode` | FAIL：`Error opening input` | PASS：`exit=0`、error 行 0 |
| `r48-judge3-frame-count` | FAIL：`nb_frames=-1` | PASS：`150 = frames_encoded` |
| `r48-judge4-av-duration` | FAIL：时长均为 -1 | PASS：差 39.7ms / 38.7ms ≤100ms |
| `r48-mp4-stss-first-frame-key` | FAIL：无帧 | PASS：首帧 key_frame=1，同步帧 5/150 |
| `r48-mp4-box-sample-table` | FAIL：`RESULT: 0 PASS / -1 FAIL`（文件不存在） | PASS：`42 PASS / 0 FAIL` |
| `r48-judge5-default-mkv` | FAIL：`--format mkv` 也不认 | PASS：两种调用都产 MKV |
| `r48-format-state-sequence` | FAIL：mp4/mkv 序列为空 | PASS：三次逐字一致 |
| `r48-both-containers-playable` | FAIL：无法解码 | PASS：两容器各 150 帧、零 error |
| `r48-invalid-format-exit2` / `r48-missing-format-exit2` | FAIL：均 `exit=1` | PASS：均 `exit=2` |
| `r48-mp4-default-output-name` | FAIL：无 out.mp4 | PASS：`out.mp4` 存在、`out.mkv` 不存在 |
| **合计** | **`0 PASS / 13 FAIL`** | **`13 PASS / 0 FAIL`** |

覆盖状态：**没有**状态迁移。两张卡都不声明 `## 覆盖子句`，`coverage_report.py --check` 保持
`108 / 1 / 7`。理由：M3-09#1 的 faststart、#2 的多音轨、#3 的收口判据都留轮 49，
起步切片翻绿任何子句都会造成「结论宽于证据」（验收报告-轮47 R1/R2 的教训）。

## 3. 回归

| 用例 | 结果 |
| --- | --- |
| `g3_playback`（MKV 三件套） | `11 PASS / 0 FAIL` |
| `g3_recovery`（MKV 恢复） | `9 PASS / 0 FAIL` |
| `g4_gate` | `12 PASS / 0 FAIL` |
| `g5_encoder_list` | 首跑 `15/1`（UI 控件 hwnd=0 时序抖动），复跑 `17 PASS / 0 FAIL` |
| `ui_smoke` | 首跑 `102/1`（foreground_hwnd=0），后续连续 3 次 `103 PASS / 0 FAIL` |
| `g5_mp4`（新增）/ `g5_closeout`（`V1-021` 修正） | `13 PASS / 0 FAIL` / `20 PASS / 0 FAIL / 0 SKIP` |
| `g5_format` 5 连（含 2 次 active） | 五次各 `15 PASS / 0 FAIL`，见 `artifacts/r48-format-5runs-summary.txt` |
| 静态四项 | 覆盖 `--check` PASS、契约 8/8、卡面 62 卡 0 冲突、域表 0 mismatch |
| 三构建 + 发布包 | MSVC Debug/Release、MinGW Release 零告警；`build_release.ps1` 17/0 |

> 依据：产品改动集中在 `src/mux/` 与 `src/recorder/` 的封装器接线，按轮 48 卡 §通过判据 #5
> 跑规定回归集，并补跑 MKV 专属三件套与发布包自检。两处 UI 时序抖动如实保留，未改判据。

## 4. DoD 勾选

- [x] 开工四项自查全部通过并已记录（§0）
- [x] 任务卡每条需求 → 一条证据，含命令与输出（`EVIDENCE.md`）
- [x] 新增断言先红后绿，旧二进制 SHA 留档（§2）
- [x] MP4 五条判据 + 格式切换三条全有可重跑命令
- [x] MSVC Release / Debug / MinGW Release 零告警；规定回归零失败（抖动已说明）
- [x] 默认 MKV 零回归；未改 `MkvWriter` 的 FlushPending / 恢复语义
- [x] 未改 `verify/` 下既有判据与阈值；新增文件：`g5_mp4.ps1`、`mp4_box_check.py`
- [x] 未引三方依赖（`vcpkg.json`、根 `CMakeLists.txt` 未改）；`src/core/interfaces/` 未动，无需契约变更单
- [x] 无死代码、无调试残留（临时 PTS 打印已删；`git grep mp4pts` 无结果）
- [x] 改动未越白名单；文档已同步（`docs/07` 自述、`docs/22`、`docs/INDEX.md`）
- [x] `HANDOFF.md` 四问齐答，含「哪些需求没做完」
- [x] `## 交付状态` 与实际一致；`## 覆盖子句` 只列实际覆盖的（无）
- [x] 已知限制已写明（faststart/恢复/B 帧/长录制内存/发布包）
- [x] 未自评「已验收」
