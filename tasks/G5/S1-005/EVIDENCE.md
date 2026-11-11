# EVIDENCE: G5/S1-005（M3-04 CBR 码率控制）

> 口径声明: 本轮证明「CBR 逐秒波动 ≤10% 且均值贴合标称」，**不证明 VBR**（#2 仍缺）。
> 数据来源: 2026-10-26 12:18~13:2x / `build-msvc`（MSVC 19.44 Release）/ 本机
> 编码器: `mf-h264-sw`（本机无可用硬编 MFT，走软编路径；硬编分支的降级逻辑见 Handoff）

## M3-04#1 CBR 模式下成片码率波动 ≤ 10%

### 旧实现实测（轮 31，未修复）

| 指标 | 实测 |
| --- | --- |
| 逐秒码率 | 1,864 ~ 5,889 kbps |
| 均值 | 2,510 kbps（标称 6,000） |
| spread | **160.4%** |
| nominal 偏差 | **58%** |

命令（旧）: `sr-recorder --record --seconds 40 --fps 30 --bitrate 6000 --out cbr.mkv`
原因: 只设 `MF_MT_AVG_BITRATE`，MF 软编把它当目标而非约束，静态桌面远远低于标称。

### 新实现实测

- 验证方式: 同一命令，`g5_format.ps1` 逐秒统计
- 命令: `pwsh -NoProfile -File verify/cases/g5_format.ps1 -BuildDir build-msvc -Config Release`
- 输出: `verify/artifacts/format-20261026-114921/`
- 结论: **通过**

| 断言 | 实测（最终实现，连跑两次） | 判据 |
| --- | --- | --- |
| `m3-04-cbr-mode` | `rate_control=cbr`、`cbr_filler_bytes=20,642,877` / `26,810,175` | ICodecAPI 生效 |
| `m3-04-cbr-spread` | 逐秒 **6,000 ~ 6,000 kbps**，avg 6,000，spread **0.0%**（两次） | ≤10% |
| `m3-04-cbr-nominal` | avg 6,000 kbps vs 标称 6,000（**0.0%**） | ≤20% |
| 可解码 | `ffmpeg -v error -i cbr.mkv -f null -` 无输出；`g3_playback` 11/11 | 零错误 |
| 容器结构 | `python verify/tools/mkv_probe.py cbr.mkv` → `RESULT: 0 failure(s)`，NAL types 含 12 | 结构合法 |
| 产物 | `verify/artifacts/format-20261026-151751/`、`format-20261026-151912/` | — |

> 交付二进制（18:35 构建）再跑一次：`cbr_avg_kbps=6000.0`、`cbr_spread=0.0`、
> `cbr_filler_bytes=16,839,132`、`stream_changes=0`，产物 `verify/artifacts/format-20261026-184550/`。

### 实现要点（为什么不是「只加两行 SetValue」）

1. **ICodecAPI**：`CODECAPI_AVEncCommonRateControlMode=CBR` + `MeanBitRate`，并**回读**确认
   （有的 MFT 接受 `SetValue` 却不生效）。
2. **HRD 缓冲 0.1s**：只设 CBR 时 MF 软编按 0.5s 以上的窗口攒额度，单个动态秒可冲到标称的
   2 倍（实测 6,000 档：0.5s buffer → 11,118kbps，spread 82.5%）。
3. **画质上限 70**（关键一步）：即使 mean=max=标称、buffer=0.1s，MF 软编仍会在关键帧/场景切换
   用近无损质量，单秒冲到 **10,637kbps**（把 target 降到 40% 反而冲到 7,990kbps——说明尖峰
   不是「额度」，而是编码器对画质的自主选择）。给 `CODECAPI_AVEncCommonQuality=70` 后，
   编码器失去「花光一秒钟预算」的空间，逐秒恒定。
4. **filler NAL 补齐**：MF 的 CBR 只封顶不保底，静态桌面仍只有 ~494kbps。
   按「每秒预算 − 本秒已出字节」追加 H.264 filler NAL（type 12），解码器忽略。
   长度前缀必须覆盖「NAL 头 + 载荷」——差 1 字节会让 `mkv_probe` 判 AVCC 结构非法（已踩过）。
5. **流变更后重设码参**：`MF_E_TRANSFORM_STREAM_CHANGE` 后若直接用默认输出类型，
   码率会退回编码器默认值。抽出 `ConfigureOutputType()` 统一设置，并在重设后重新下发码控。

## 60 分钟体积（P0 指令要求）

- 命令: `sr-recorder --record --seconds 3600 --fps 30 --bitrate 6000 --out cbr60.mkv --log-level info --log-file cbr60.log`
- 产物: `verify/artifacts/cbr60/cbr60.mkv`（`summary.txt` 为 stdout + ffprobe 汇总）
- 实测: **3,021,931,339 字节（2.81 GiB）/ 3599.996s = 6,715 kbps（含音频）**；108,000 帧、丢帧 0
- 换算: 每小时 **≈2.8 GB**，是轮 31（实际 ~2.5Mbps → 1.1GB/h）的 **2.6 倍**

> 该次长跑使用的构建早于「画质上限 70」（其余参数相同）。终态配置下 filler 占比更高、
> 真实码率分量更小，总体积只会更贴近 6,000kbps 标称；LT-01 的 30min 段（终态二进制）
> 提供了同配置的实测体积，见 `tasks/G5/S1-006/SELFTEST.md`。

## M8-05 剩余时长估算是否失真

`EstimateRecordSeconds(free, stop, bitrate)` 用的是**标称**码率，所以修复前实际码率只有 2.5Mbps
时估算是**偏悲观**（能录 2.4 倍时长），不是偏乐观。修复后实际码率贴近标称，估算随之收敛。

实测对照（60min 那一次）：

| 项 | 值 |
| --- | --- |
| 启动时估算可录 | 247,126 s |
| 实测占用 | 839,425 字节/s（= 6,715kbps） |
| 按实测换算的可录时长 | 227,864 s |
| 偏差 | **+8.4%（估算偏乐观）**，判据 ≤20% |

结论：修复后 M8-05 的估算误差从「2.4 倍悲观」收敛到「8% 乐观」，没有出现「突然乐观 2 倍」。

## 复现环境

本机 / Windows 11 24H2（build 26100）/ MSVC 14.44 Release / ffmpeg 7.x
