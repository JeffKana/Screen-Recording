# EVIDENCE · S4-007（轮 49 · M3-09#1/#2/#3 收口）

> 验收凭据。每条需求一条证据，全部是**可重跑命令 + 实际输出行**；`artifacts/` 未入库（`.gitignore`
> 已忽略 `tasks/**/artifacts/`），所以原始输出在下面按行贴出，验收方可用同一命令复现。
> 运行环境：Windows 11 build 26200 / i5-13500 / UHD 770；构建 `build-msvc`（VS17 x64, Release）。

---

## 1. P0-1 `docs/01` 默认值改口（需求：M3-09 文档口径）

```powershell
Select-String -Path docs/01-功能需求清单.md -Pattern 'M3-09 \||49（验收方裁定）'
git diff --stat ed8767a -- docs/05-非功能需求与验收.md
```

实际输出：

```
    111  | M3-09 | 容器格式：MKV（V1.0 默认）与 MP4（`--format mp4`） | MP4 优先 faststart 布局；MKV 支持多音轨与更长容错；切换格式不影响录制流程 | V1.0 |
    300  | 49（验收方裁定） | 2026-11-14 | M3-09 需求描述（默认容器） | 「容器格式：MP4（默认）与 MKV」→「容器格式：**MKV（V1.0 默认）**与 MP4（`--format mp4`）」… | 验收报告-轮48 §2.1 / `docs/22 §2` | 验收要点三句一字未改… |
--- docs/05 diff ---
（空——`docs/05` 一个字未改）
```

验收要点三句（faststart / 多音轨 / 切换格式）**未改动**，故 `docs/09` 子句数与统计不受影响。

---

## 2. P0-2 M3-09#1 faststart（moov 前置）

判据 ①~④（⑤ 见 §2.2 的 `.part` 专条与 MKV 回归）：

```powershell
pwsh -File verify/cases/g5_mp4.ps1
```

实际输出（节选，完整见 `SELFTEST.md §2`）：

```
  PASS r48-mp4-box-sample-table — mp4_box_check.py RESULT: 43 PASS / 0 FAIL；原始输出 box-check.txt
--- 轮 49 P0-2 faststart（moov 前置）---
  PASS r49-faststart-moov-before-mdat — 顶层 box：ftyp@0+32 moov@32+5200 mdat@5232+1379061；moov_off=32 < mdat_off=5232
  PASS r49-faststart-moov-in-first-64k — moov 结束偏移=5232（≤65536：前 64KB 内即可解析出容器头）
  PASS r48-judge1-format-duration — format_name=mov,mp4,m4a,3gp,3g2,mj2 duration=5.03873s vs 录制 5s（±0.2s）
  PASS r48-judge2-ffmpeg-decode — ffmpeg -v error -f null - : exit=0 error 行=0
```

字节级结构自检（不依赖 ffprobe）：

```powershell
python verify/tools/mp4_box_check.py "$env:TEMP\sr-mp4\r48.mp4"
```

```
  PASS top-ftyp — top boxes=ftyp,moov,mdat
  PASS mdat-payload-consistent — mdat 数据区 [5248,1384517) / 文件长度=1384517
  PASS moov-before-mdat — moov_off=32 < mdat_off=5232（faststart 布局）
  PASS moov-in-first-64k — moov 结束偏移=5232 <= 65536
  PASS mdat-no-padding — 样本字节合计=1379269 vs mdat 数据区=1379269
RESULT: 43 PASS / 0 FAIL ; file=r48.mp4
```

### 2.1 `.part` 语义（二选一：**判不可修复且保留**）

```powershell
# 用例内：后台录 30s → 等 .part > 64KB → 强杀 → 用顶层 box 扫描 + --repair 探测
pwsh -File verify/cases/g5_mp4.ps1    # 见 r49-part-* 四条
```

```
  PASS r49-part-written-without-moov — 缓冲期强杀后 .part 存在=True 大小=106496B 顶层 box=[ftyp@0 mdat@32]
  PASS r49-part-unrepairable-with-reason — --repair → exit=12（期望非 0）probe_repairable=0
       reason=[iso-bmff (mp4) part: moov missing; mp4 crash recovery is V1.1, file kept as-is]
  PASS r49-part-kept-intact — 判不可修复后 .part 仍保留：大小 106496 → 106496
  PASS r49-part-no-final-output — 崩溃后没有成品 .mp4：False
```

结论：**faststart 的 moov 只在 `Close()` 生成**，录制期崩溃的 `.part` 里没有 moov；恢复流程判
「不可修复」并**原样保留**（不删除、不改名、不产出「可播但时长错」的成品）。MP4 的恢复适配属 V1.1。

### 2.2 回归：MKV 非 faststart 路径不受影响

```powershell
pwsh -File verify/cases/g3_playback.ps1
pwsh -File verify/cases/g3_recovery.ps1
```

```
RESULT: 11 PASS / 0 FAIL        (g3_playback)
RESULT: 9 PASS / 0 FAIL         (g3_recovery)
```

---

## 3. P0-3 M3-09#2 MKV 多音轨

```powershell
pwsh -File verify/cases/g5_multiaudio.ps1
```

```
  PASS r49-dual-record-exit0 — exit=0 record_exit=0 format=mkv audio_tracks=2 write_errors=0 成片=True
       state_sequence=Idle,Preflight,Countdown,Recording,Stopping,Idle
  PASS r49-dual-two-audio-streams — ffprobe 音频流数=2（期望 2）：index=1 codec=aac ch=2 ; index=2 codec=aac ch=2
  PASS r49-each-track-decodable — a:0 exit=0 error行=0；a:1 exit=0 error行=0（各自独立 -map 解码，零错误）
  PASS r49-track-duration-within-100ms — a:0 末PTS=5015ms a:1 末PTS=5015ms 视频末PTS=4999ms；|Δ0|=16ms |Δ1|=16ms（≤100ms）
  PASS r49-single-track-unchanged — audio_tracks=1 ffprobe 音频流=1（期望 1）；state_sequence 与双轨逐字一致=[True]
  PASS r49-mp4-audio-track-limit — mp4 audio_tracks=1 ffprobe 音频流=1 全片解码 error 行=0；回落原因已记日志=True
RESULT: 10 PASS / 0 FAIL
```

口语化的对照（可直接手工重跑）：

```powershell
.\build-msvc\bin\Release\sr-recorder.exe --record --seconds 5 --fps 30 --bitrate 2000 --format mkv --audio-tracks 2 --out "$env:TEMP\dual.mkv"
ffprobe -v error -show_entries stream=index,codec_type,codec_name -of csv=p=0 "$env:TEMP\dual.mkv"
```

```
0,video,h264
1,audio,aac
2,audio,aac
```

---

## 4. P0-4 M3-09#3 UI 扩展名跟随格式

```powershell
pwsh -File verify/cases/g5_ui_format.ps1
```

```
  PASS r49-ui-container-saved — --set-container mp4 → exit=0，日志 container_saved=True
  PASS r49-ui-container-ini — INI 已写入 [output] container=mp4
  PASS r49-ui-container-invalid-rejected — --set-container avi → exit=13（期望非 0），原因已记录=True，INI 未被改写=True
  PASS r49-ui-mp4-two-sessions — spawn_count=2（期望 2）exited=True exit=0
  PASS r49-ui-mp4-file-001 — 期望产物 ...\mp4_dir\ac10_001.mp4
  PASS r49-ui-mp4-file-002-avoid — AC-10 避让在 .mp4 上成立：...\ac10_002.mp4（True）
  PASS r49-ui-mp4-no-mkv — 同目录 .mkv 残留=0（期望 0）
  PASS r49-ui-mp4-format-matches — ffprobe format_name：001=mov,mp4,m4a,3gp,3g2,mj2 / 002=mov,mp4,m4a,3gp,3g2,mj2
  PASS r49-ui-mp4-playable — 两段 .mp4 解码零错误
  PASS r49-ui-format-dispatched — --format 真的下发了=True；start_request 记录了容器=True
  PASS r49-ui-default-mkv-name — 默认容器 → ...\mkv_dir\def_001.mkv
  PASS r49-ui-default-no-mp4 — 同目录 .mp4 残留=0（期望 0）
  PASS r49-ui-default-mkv-playable — ffprobe format_name=matroska,webm
RESULT: 13 PASS / 0 FAIL
```

---

## 5. 红-绿对照（先红后绿，硬要求）

旧实现 = 轮 48 合并点 `ed8767a` 的独立构建（`git worktree add %TEMP%\sr-old-tree ed8767a` +
`cmake -G "Visual Studio 17 2022" -DSR_TEST_HOOKS=ON`）。命令统一为：

```powershell
$old = "$env:TEMP\sr-old-tree\build\bin\Release"
pwsh -File verify/cases/g5_mp4.ps1 -RecorderPath "$old\sr-recorder.exe"
pwsh -File verify/cases/g5_multiaudio.ps1 -RecorderPath "$old\sr-recorder.exe"
```

| 新断言 | 旧实现（`ed8767a`） | 新实现 |
| --- | --- | --- |
| `r49-faststart-moov-before-mdat` | **FAIL**：`顶层 box：ftyp@0+32 mdat@32+1379009；moov_off=-1 < mdat_off=32` | **PASS**：`moov@32 < mdat@5232` |
| `r49-faststart-moov-in-first-64k` | **FAIL**：`moov 结束偏移=-1` | **PASS**：`5232 ≤ 65536` |
| `r48-mp4-box-sample-table` | **FAIL** `40 PASS / 3 FAIL`（前置口径三条） | **PASS** `43 / 0` |
| `r49-part-unrepairable-with-reason` | **FAIL**：`reason=[no complete video cluster found]`（无法区分 MP4 与损坏 MKV） | **PASS**：`reason=[iso-bmff (mp4) part: moov missing; …]` |
| `g5_multiaudio` 10 条 | **0 PASS / 10 FAIL**（`--audio-tracks` 不存在 → `exit=1`、`audio_tracks=-1`） | **10 PASS / 0 FAIL** |
| `g5_ui_format` 13 条 | **红端不可自动执行**：旧 UI 不认识 `--set-container`，忽略后进入常驻模式 → 脚本会挂住（不是「通过」）。改用源码事实作红证据：`ed8767a` 的 `src/app/main.cpp:376` = `directory / (name + L".mkv")`（扩展名写死） | **13 PASS / 0 FAIL** |

红端两条 `RESULT` 原样：`g5_mp4` **15 PASS / 4 FAIL**、`g5_multiaudio` **0 PASS / 10 FAIL**。

> 诚实说明：`r49-part-written-without-moov`、`r49-part-kept-intact`、`r49-part-no-final-output` 在旧实现上
> 也是 PASS——它们描述的是「旧实现本来就有的行为」（录制期无 moov、断言不改文件），属于**回归护栏**，
> 不是本轮修复的证明。真正红→绿的是 `r49-part-unrepairable-with-reason`（原因可区分）与两条 faststart 断言。

---

## 6. 用例与工具改动清单（只收紧、不放宽）

| 文件 | 改动 | 方向 |
| --- | --- | --- |
| `verify/tools/mp4_box_check.py` | `moov-after-mdat` → `moov-before-mdat`；`mdat-payload-consistent` 改为「数据区顶到文件末尾」；新增 `moov-in-first-64k` | **收紧**（轮 48 的「moov 可后置」是起步期临时口径） |
| `verify/cases/g5_mp4.ps1` | 原有 13 条一条未删；新增 `Get-TopBoxes` 与 `r49-faststart-*`、`r49-part-*` 共 6 条 | **收紧 + 新增** |
| `verify/cases/g5_multiaudio.ps1` | 新增（10 条） | 新增 |
| `verify/cases/g5_ui_format.ps1` | 新增（13 条） | 新增 |

未改：`verify/lib/**`、`g5_closeout.ps1`、`g5_format.ps1` 及任何阈值。
