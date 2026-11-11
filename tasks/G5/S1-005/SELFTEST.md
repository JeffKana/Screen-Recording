# SELFTEST: G5/S1-005（M3-04 CBR）

## 0. 开工四项自查

| 检查项 | 结果 | 依据 |
| --- | --- | --- |
| 需求编号可检索 | 通过 | `docs/01-功能需求清单.md:106` M3-04 原文「CBR 模式下成片码率波动 ≤ 10%；VBR 模式下标称值与实测值偏差 ≤ 20%」 |
| 必读章节可定位 | 通过 | `docs/03` §6 编码器路径；`docs/05` §2 码率口径 |
| 上游交付物可获取 | 通过 | `tasks/G5/S1-004`（轮 31）+ `verify/artifacts/format-20261023-175613/` |
| 文件域无交集 | 通过 | 本卡只碰 `src/encode`、`src/recorder/RecordingSession.*`、`g5_format.ps1`；`src/recorder/main.cpp` 归 S5-002 |

## 1. 自测命令与输出

| 命令 | 结果 | 时间戳 |
| --- | --- | --- |
| `pwsh -File verify/cases/g5_format.ps1 -BuildDir build-msvc` | **7 PASS / 0 FAIL**（spread **0.0%**、nominal 0.0%）×2 次 | 2026-10-26 15:17 / 15:19 |
| `python verify/tools/mkv_probe.py <cbr.mkv>` | `RESULT: 0 failure(s)`，NAL types 含 12（filler） | 2026-10-26 11:58 |
| `ffmpeg -v error -i cbr.mkv -f null -` | 无输出（零解码错误） | 2026-10-26 15:17 |
| `sr-recorder --record --seconds 3600 --bitrate 6000` | 3.02GB / 3599.996s，见 `verify/artifacts/cbr60/summary.txt` | 2026-10-26 12:18 |
| `pwsh -File verify/cases/g3_playback.ps1` | 11 / 11（CBR 成片可播，画质上限 70 未破坏解码） | 2026-10-26 15:20 |
| `pwsh -File verify/cases/g4_gate.ps1 -Seconds 60` | 12 / 12 | 2026-10-26 15:20 |

## 2. 红-绿对照

| 新断言 | 旧实现实测 | 新实现实测 |
| --- | --- | --- |
| `m3-04-cbr-mode`（ICodecAPI 是否生效） | 无该字段；只有 `MF_MT_AVG_BITRATE` | `rate_control=cbr`，回读 `AVEncCommonRateControlMode=0` |
| `m3-04-cbr-spread` | 逐秒 1,864~5,889kbps，spread **160.4%** | 逐秒 **6,000~6,000kbps**，spread **0.0%** |
| `m3-04-cbr-nominal` | avg 2,510kbps，偏差 **58%** | avg 6,000kbps，偏差 **0.0%** |
| 中间态 1（只加 ICodecAPI，0.5s buffer） | — | 单秒冲 11,118kbps，spread **82.5%** |
| 中间态 2（buffer 0.1s + filler） | — | 平均档跑 2.0%，但动态内容仍偶发 9,966kbps（spread 60.5%） |
| 中间态 3（降 target 到 40%） | — | 反而冲到 7,990kbps → 证明尖峰来自「画质自主选择」，不是额度 |
| 终态（画质上限 70） | — | 两次连跑均 6,000~6,000kbps、spread **0.0%** |
| filler 长度前缀（结构） | — | 旧写法差 1 字节 → `g4_control: fail-resume-container-valid` FAIL；修正后 32/32、mkv_probe 0 failure |

## 3. 回归

| 用例 | 结果 |
| --- | --- |
| `g5_format` | 7 / 7 |
| `g3_playback` / `g3_output` / `g3_recovery` / `g3_storage` | 11 / 15 / 9 / 11 |
| `g4_gate`（60s） | 12 / 12 |
| `g4_control` / `g4_channel` / `g4_elapsed` | 32 / 52 / 13 |
| `g5_fallback` / `g5_inject` / `g5_diag` | 25 / 28 / 36 |
| `g1_walk` / `g2_walk` | 40 / 40；14 PASS + 2 SKIP（环境） |
| `ui_smoke` | 101 / 101 |
| 契约 / 覆盖表 | 8 / 8；75 已实现 / 9 有卡未实现 / 32 无卡 |

## 4. DoD 勾选

- [x] 每条需求都有证据（#1 有实测；#2 明确写「未实现」）
- [x] 新断言先红后绿，旧值已记录（160.4% → 2.0%）
- [x] Release 零告警（MSVC `/W4`）
- [x] 相关回归零失败
- [x] 无死代码、无调试残留（实验用的环境变量开关已删除）
- [x] 未超出文件域
- [x] 接口无变更（`VideoEncoderConfig::rate_control` 是既有字段）
- [x] 文档已同步（docs/07 轮 32、docs/09 M3-04 挂账理由）

## 5. 已知限制

- 本机只有 `mf-h264-sw`，硬编分支的「不支持码控 → 降级」未被真实环境验证，仅有代码路径与日志字段。
- filler 补齐会让静态画面文件明显变大（6Mbps 档 60min ≈ 2.7GB），这是 CBR 的固有代价。
- VBR（#2）未实现，M3-04 整条仍为「有卡未实现」。
