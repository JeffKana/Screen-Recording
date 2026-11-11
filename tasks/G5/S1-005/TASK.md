# TASK: S1-005 M3-04 码率控制（CBR）修复（轮 32）

阶段：G5｜角色：S1｜轮次：32｜来源：验收方轮 32 指令 P0-2

## 需求范围

| 需求 | 子句 | 验收要点原文（docs/01） | 证据形式 |
| --- | --- | --- | --- |
| M3-04 | #1 | CBR 模式下成片码率波动 ≤ 10% | `g5_format.ps1` 逐秒码率表（min/max/avg/spread）+ 60min 体积 |
| M3-04 | #2 | VBR 模式下标称值与实测值偏差 ≤ 20% | 轮 32 当时不做（无质量档位参数），当时保持「有卡未实现」；#2 已由 `tasks/G5/S1-007` 交付 |

数值判据（任务卡口径，与 05 一致）：spread = (max-min)/avg ≤ 10%；nominal = |avg-标称|/标称 ≤ 20%。

## 必读章节

- `docs/01-功能需求清单.md` M3-04
- `docs/03-技术选型.md` §6（编码器路径）
- `docs/05-非功能需求与验收.md` §2（码率口径）
- `docs/06-子智能体执行规程.md` §6.1（量化结论必须给实测值）

## 文件域

- `src/encode/MfVideoEncoder.{h,cpp}`
- `src/recorder/RecordingSession.{h,cpp}`（只加 `rate_control`/`filler_bytes` 统计字段与日志）
- `src/core/interfaces/IVideoEncoder.h`、`src/core/types/EncodingConfig.h`（只读）
- `verify/cases/g5_format.ps1`

> `src/recorder/main.cpp` 归 `tasks/G5/S5-002`（它要加 `--list`），两卡文件域无交集。

## 上游交付

轮 31 的 `tasks/G5/S1-004`：CBR 实测 spread 160.4%、avg 2,510kbps（标称 6,000）。
基线产物：`verify/artifacts/format-20261023-175613/`。

## 约束

- 不得改动 `mf-h264-sw` 回退链：硬编协商失败仍必须回退软编（M3-06）。
- 硬编不支持 rate control 时必须降级而不是拒绝录制。
- 不得引入新三方依赖；`vcpkg.json` 只读。
- 不得修改 `verify/` 下的既有断言以迁就实现。

## 完成定义（DoD）

- [x] `rate_control=cbr`（ICodecAPI 回读确认），不是只设 `MF_MT_AVG_BITRATE`
- [x] `g5_format` 7/7：spread ≤10%、nominal ≤20%
- [x] 60min 体积实测并写进 EVIDENCE
- [x] 成片可解码、`mkv_probe.py` 0 failure（filler NAL 不破坏 AVCC 结构）
- [x] 全量回归零失败（含 g3_playback / g4_gate）

## 覆盖子句

- M3-04#1

## 交付状态

- M3-04: 已实现（#2 由 S1-007 交付）
- M3-04#1: 已实现

> 变更：轮 37 / S1（声明纠偏）/ 依据：#1 由本卡达标（复跑实测逐秒 6,000~6,000kbps、spread 0.0%）；
> #2（VBR）由 `tasks/G5/S1-007` 交付，`g5_format` 复跑 14/14（VBR 运动源偏差 0.8%、峰值 7,512<9,000、
> filler=0）。「本卡当时只覆盖 #1」这条事实保留在正文（不再与状态行冲突）。
