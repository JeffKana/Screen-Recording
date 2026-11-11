# EVIDENCE: S6-002（轮 33 → 轮 35 · 8h 单文件）

> 口径声明：8h 第 3 轮于 **02:47:03 正常收尾**，六项中 **5 项干净 PASS，1 项待裁定**
> （全片解码出现 6,667 条 null-muxer DTS 警告，但 864,000 帧、DTS 严格递增、解码零错误）。
> 按「全 PASS 才转已实现」，M6-02#1 / M6-09#1 保持「有卡未实现」，G5 不关闭；
> 裁定材料见 `DEFECT-02.md`。

## 运行史（三次）

| # | 启动 | 结束 | 时长 | 结果 | 原因 |
| --- | --- | --- | --- | --- | --- |
| 1 | 10:28:06 | 10:49 | 20min | 主动作废 | D-58 漂移算法假报 −2s，必假红 |
| 2 | 10:50:18 | 18:12:30 | **7h22m** | 被外部杀死 | D-60 并发 `g5_fileops` 的 `Get-Process sr-recorder \| Stop-Process` |
| 3 | 18:47:02 | 02:47:03 | **28,800.8s** | 5/6 PASS + 1 待裁定 | 独立镜像 `sr-recorder-lt01` + 锁文件；收尾正常 |

## 收尾结果（第 3 轮）

| # | 项 | 实测 | 判定 |
| --- | --- | --- | --- |
| 1 | 时长 | 28,799.971s；`record_exit=0`；864,000 帧 | PASS |
| 2 | 内存 | PeakWS 基线 549.3 → 峰值 550.6MB，**Δ1.3MB**；小时均值非单调 | PASS |
| 3 | 句柄 | 488 → 498，**+2.0%** | PASS |
| 4 | 丢帧 | 0 / 176,910 = **0.000%** | PASS |
| 5 | 漂移 | 16 点最差 **99.0ms @ 28,800s**，其余 0~26ms | PASS |
| 6 | 全片可播 | `-f null` exit 0，6,667 条 null-muxer DTS 警告、无解码错误；framecrc 864,000 帧 DTS 单调 | **待裁定** |

冻结现场：`verify/artifacts/FROZEN-20261028-0247/`
（22,374,134,176B，SHA256 `4811AA48…7BC9`；manifest 含 exe SHA256、起止时间、采样数 481）。

## 启动现场（第 3 轮）

- 命令：
  ```
  pwsh -NoProfile -File verify/cases/g5_lt01_real.ps1 -Seconds 28800 -SampleSeconds 60 -MinFreeGB 35
  ```
- 驱动进程：`pwsh` pid **56012**；录制进程：**`sr-recorder-lt01`** pid **53576**
- 产物目录：`verify/artifacts/lt01-real-20261027-184702/`
  - `lt01-8h.mkv`（单文件；录制中为 `.part`，正常收尾才改名）
  - `samples.jsonl`（60s 一行：t_ms / ws_mb / peak_ws_mb / private_mb / handles / cpu_s）
  - `lt01-8h.log`、`lt01-8h.out`、`summary.json`（收尾后生成，含 `recorder_sha256`）
- 构建：`sr-recorder.exe` SHA256 `56295C2A5D2C10A518815F54FDE1D7146763FAE8E6F6DADE3E784E2A7C2C10F7`
  （R5：与轮 31/32 的构建不可横比）
- 锁文件：`verify/artifacts/.lt01-running.json`（pid 53576）；**看到它就不要开跑任何用例**
- 前置：D: 可用 **126.6GB**；无其它 `sr-ui` / `sr-recorder`；
  `SetThreadExecutionState(ES_CONTINUOUS|ES_SYSTEM_REQUIRED|ES_DISPLAY_REQUIRED)`
  守护进程 pid 44344（10h）。
- 现场文件：`artifacts/driver.out`/`driver2.out`/`driver3.out`、`samples-*.jsonl`。

## 第 1 轮作废（D-58）

`ffprobe -read_intervals <At>%+2` 的「2s」从**实际读到的第一个包**算起：视频 seek 落在
关键帧（早 1~2s），音频没有关键帧，两条流读到不同末端，末包差假报约 −1990ms。

在 1 小时样片 `verify/artifacts/cbr60/cbr60.mkv` 上复测：

| 采样点 | 旧算法 | 修正算法 |
| --- | --- | --- |
| 60s / 600s / 1800s / 3000s / 3540s | −1975 ~ −1996ms | +12 / +8 / 0 / +14 / +11ms |

修正：统一读 `[At-10, At+10]`，取 `pts ≤ At+0.5` 的最后一个包。

## 第 2 轮作废（D-60）

18:12:30，`sr-recorder` pid 34308 静默消失，无 WER/Application Error；最后一条日志是
`metrics captured=439684`，无 `error`/`record_exit`。同时段验收方在复跑 `g5_fileops`，
其开场 `Get-Process sr-ui, sr-recorder | Stop-Process -Force` 按镜像名杀掉了 8h 进程。
完整时间线、复现与修复见 **`DEFECT-01.md`**；冻结现场在 `verify/artifacts/FROZEN-20261027-1812/`
（含 20.8GB `.part` 与 SHA256，`manifest.json`）。

## 判定方法（写进用例，禁止目测）

| 项 | 方法 | 判据 |
| --- | --- | --- |
| 可播 | 成片 `format.duration` + `ffmpeg -v error -i <file> -f null -` 全解 | ≥28,790s、零解码错误 |
| 内存 | 60s 外部采样 `PeakWorkingSet64`；基线 = 开录后 9~11min 均值；峰值 = 全序列最大 | 峰值 − 基线 ≤50MB |
| 无爬升 | 8 个整点小时均值 | 非逐段单调上升，且末段 − 首段 ≤50MB |
| 句柄 | 同上采样 `HandleCount`；峰值相对 10min 基线 | ≤ +10% |
| 丢帧 | recorder 自报 `frames_dropped / frames_captured` | < 0.1% |
| 漂移 | 每 30min 读 `[At-10, At+10]`，取 `pts ≤ At+0.5` 的末视频包 / 末音频包 PTS，差取绝对值 | 16 点最差 ≤100ms |

## 已完成的脚本自检（非 8h 结论）

用 45s 短跑验证脚本流程（`-Seconds 45 -SampleSeconds 5 -SkipFullDecode`）：

```
PASS lt01-exit-zero / lt01-duration (44.999s) / lt01-media-elapsed (44981ms)
PASS lt01-memory-delta (Δ1.1MB) / lt01-handles (+0.0%) / lt01-dropped (0/1795)
FAIL lt01-memory-no-trend（45s 只有 1 个小时均值，样本不足）
FAIL lt01-drift（45s 没有任何 30min 采样点）
```

两个 FAIL 是短跑样本不足的预期结果，不构成 8h 结论；8h 运行会产出 8 个小时均值与
16 个漂移点。

## 验收方待办（轮 35）

1. 对 `DEFECT-02` 的「全片可播」口径做裁定（`-f null` 字面 vs framecrc 实质）；
2. 复跑 `g5_format`（14 条）与 `ui_smoke`（连续 3 次）；执行方已自测，不作为验收数；
3. 六项全 PASS 后，才把 `tasks/G5/S6-002/TASK.md` 的 M6-02/M6-09 改「已实现」并重生成
   `docs/09`；在此之前保持「有卡未实现」。
