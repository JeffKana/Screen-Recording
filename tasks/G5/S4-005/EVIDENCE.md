# EVIDENCE: S4-005（轮 45 · M3-04#1 选项 A）

> 口径声明：本卡**没有**达成「静态 3 + 活动 3 六次全过」，因此**不申请翻绿**；
> 交付的是「三选二逐个实测 + 结论：本机 MFT 不可约束」的证据链。
> 数据来源：2026-11-11 20:18~20:56 / `build-msvc` Release（MSVC VS2022）/ Win11 26200 / i5-13500 / i915 UHD 770 / 1920x1080 / DPI 100%。
> **`src/` 本轮净改动为 0**（实验代码已 `git checkout` 回滚），见 `HANDOFF.md`。

## M3-04#1 CBR 模式成片码率波动 ≤10%（5s 滑窗）+ 单秒 ≤2× 标称

### E1 · 红：当前构建在真实桌面活动下仍然失败（可复现）

- 验证方式：直接跑判据用例的 CBR 段（40s / 30fps / 6000kbps / cbr），**不注入任何模拟**，
  录制期间桌面处于真实活动（终端在输出、IDE 在重绘）。
- 命令：`pwsh -NoProfile -File verify/cases/g5_format.ps1`
- 输出：`verify/artifacts/format-20261111-201838/`（`cbr.mkv` / `cbr.out` / `summary.json`）
- 关键行（stdout 原样）：

```
FAIL cbr-window5-spread — 5s 滑窗（逐秒滑 1 桶，共 34 窗）6,000~7,356kbps avg=6,199 spread=21.9%（判据 ≤10%）
PASS cbr-single-second-cap — 单秒峰值 9,473kbps vs 硬上限 12,000kbps
PASS m3-04-cbr-nominal — avg=6,178kbps vs 标称 6000kbps（判据 ≤20%）
RESULT: 14 PASS / 1 FAIL / 0 SKIP
```

- 逐秒分布（`ffprobe packet=pts_time,size` 按秒分桶）：38 个有效秒中 35 个恰为 750,000 B，
  过冲秒为 sec12 **1,043,614 B**（8,349kbps）、sec13 **1,184,068 B**（9,473kbps）、
  sec15 869,643 B；`record_exit=0 frames_encoded=1200 frames_dropped=0`（排除取数/丢帧解释）。
- NAL 分解（`python verify/tools/cbr_nal_probe.py <mkv> <sec> --ffprobe <path>`）：

| 秒 | 总字节 | keyframe | filler | other | 说明 |
| --- | ---: | ---: | ---: | ---: | --- |
| 13 | 1,184,068 | 148,306 | 200,926 | **834,836** | 过冲来自 **P 帧**，不是 IDR |
| 12 | 1,043,614 | 185,415 | 257,349 | **600,850** | 同上 |
| 20 | 750,000 | 149,621 | 573,362 | 27,017 | 正常秒：filler 补齐到标称 |

- 结论：**不通过**。且过冲不是 IDR 造成的，所以「IDR 质量上限」这一类旋钮先天不解决问题。

### E2 · 三选二逐个实测：全部无效果

- 验证方式：把真实桌面样本（E1 的 `cbr.mkv`）用 ffplay 全屏回放作为**同一份像素输入**，
  在 `ApplyRateControl` 里用临时环境变量覆盖 MaxBitRate / BufferSize / MinQp / MeanBitRate，
  每次 25s CBR 6000 录制后按同一口径算 5s 滑窗 spread 与单秒峰值。
- 命令（实验期）：
  `pwsh -File verify/artifacts/r45-probe/sweep.ps1 -Tag base -MaxMult 1.0 -BufMs 100`
  （脚本参数即上表的 max/buffer/mean/minQP；脚本内容见 `artifacts/` 的说明，属一次性工具）
- 输出：`artifacts/sweep-summary.txt`（18 次逐次数据 + 病态内容压测）
- 结论摘要：

| 旋钮 | 设置 | 实测是否改变瞬时输出 |
| --- | --- | --- |
| ① `AVEncCommonMaxBitRate` | 1.0× / **1.5×** | 否（峰值 8,154 / 7,508kbps，落在基线抖动区间） |
| ② `AVEncCommonBufferSize` | 100 / 50 / 20 / 10 / 5 ms | 否（峰值 6,999~8,850kbps，重跑不复现） |
| ③ `AVEncVideoMinQP`（IDR/全帧 QP 下限） | 30 / 40 | 否（`apply=1`，病态内容仍 21.7~26.6Mbps） |
| 附：`AVEncCommonMeanBitRate` | 0.8× / 0.5× | 否（病态内容 mean 减半后输出不变） |
| 附：`AVEncVideoEncodeFrameTypeQP`（逐帧 QP） | 26 / 40 | **被拒**：`SetValue` 返回 `0x80070057` |

- 结论：**不通过**（本机 MFT 接受设置但不执行；逐帧 QP 接口直接被拒）。

### E3 · 静态 3 + 活动 3（轮 45 指定口径，同一套脚本）

- 验证方式：`g5_cbr_probe.ps1`（由 `tasks/G6/V1-017` 扩为 `static` / `active` 同脚本），
  active = `verify/lib/ActiveDesktop.ps1` 满屏滚动小字 + 每 250ms 整屏重排 + 光标划圆。
- 命令：`pwsh -NoProfile -File verify/cases/g5_cbr_probe.ps1 -Seconds 40 -Runs 3 -Mode static,active -OutDir tasks/G5/S4-005/artifacts/cbr-6runs`
- 输出：`artifacts/cbr-6runs/probe-summary.txt` + 逐次 `*.persec.csv` / `*.metrics.txt` / `*.summary.json`

| 场景 | 次 | capture_fps | 5s 窗 spread | 单秒峰值 | 判定 |
| --- | --- | ---: | ---: | ---: | --- |
| static | 1 | 1.0 | 0.0% | 6,000 | PASS |
| static | 2 | 2.0 | 0.0% | 6,000 | PASS |
| static | 3 | 1.0 | 0.0% | 6,000 | PASS |
| active | 1 | 51.0 | 0.5% | 6,098 | PASS |
| active | 2 | 49.0 | 0.6% | 6,085 | PASS |
| active | 3 | 48.0 | 0.6% | 6,086 | PASS |

- 结论：**6/6 通过，但这组数据不能作为翻绿依据**，理由必须写清楚（不是谦虚）：
  1. 逐次编码器真实输出只有 ~180KB/s（`bytes_written − cbr_filler_bytes`），
     而 E1 的真实桌面过冲秒是 1.18MB —— 本脚本的激励强度**明显弱于真实桌面**；
  2. 同一份 `src/`（本轮净改动 0）在 E1 里 21.9% FAIL，在 E3 里 3/3 PASS，
     说明差异来自**内容**而不是三轮任何改动；
  3. 按轮 45 指令「6 次全过才翻绿」，执行方本可据此报翻绿——但那是**拿软柿子当证据**，
     与轮 44 F1「只测安静桌面的数据不再作为依据」是同一个错误，故**不翻绿**，交验收方裁定激励强度。

## 复核项（轮 45 §3 #2 要求）

| 复核 | 命令 | 结果 |
| --- | --- | --- |
| VBR 两条未被打破 | `pwsh -File verify/cases/g5_format.ps1` 的 VBR 段 | `m3-04-vbr-motion-nominal` avg=5,972kbps 偏差 0.5% PASS；`m3-04-vbr-peak-any-content` 6,887/1,402 vs 9,000 PASS |
| M8-05 估算未被打破 | 同上（`g5_format` 全程未改码控参数） | **本轮 `src/` 净改动 0，编码参数与轮 44 完全一致**，故 VBR/估算不受影响；`g5_storage` 14/0 复跑见 `tasks/G6/V1-018/SELFTEST.md` |
| 画质/关键帧代价 | — | 不适用：本轮未改任何码控参数（改了也无效果，见 E2） |
