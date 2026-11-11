# EVIDENCE: S4-004（F1 修复：按秒结算，实测不适用后回滚）

> 执行方证据，非验收结论。数据 2026-11-11；证据在
> `tasks/G5/S4-004/artifacts/cbr-after-fix/` 与 `tasks/G6/V1-007/artifacts/`。

## 一条需求一条证据

M3-04#1「CBR 模式下成片码率波动 ≤ 10%」。

- 修复前（红）：`tasks/G6/V1-007/artifacts/cbr-probe-20261111-102548/probe-summary.txt`
  —— 静态 5 次 4 次超 10%（最高 359%）、运动 5 次全超（12.1%~26.4%）；
  NAL 分解显示过冲秒 `filler=0`，正常秒 `filler=283,762`。
- 实现：按轮 43 指令 §2 把 `PadToTargetBitrate()` 的补偿口径从帧改到秒
  （每帧按 `本秒剩余预算 / 本秒剩余帧数` 重算 filler 额度 + 换秒显式记账），
  `ApplyRateControl()` 的 mean/max/buffer/quality 一个参数未改；三构建零告警。
- 修复后（绿？**否**）：`artifacts/cbr-after-fix/probe-summary.txt`（11:00:03 – 11:07:28）：

| 模式 | 修复前 | 修复后 |
| --- | --- | --- |
| 静态超 10% | 4/5 | 3/5（84.4% / 295.2% / 108.4% / 1.5% / 3.3%） |
| 运动超 10% | 5/5 | 5/5（19.3% ~ 24.9%） |
| 运动 avg | 6,276 kbps（偏差 4.6%） | **8,039 kbps（偏差 34%）** |

- **结论：未达成「10 次全 ≤10%」，且引入新回归**（avg 超标称 34%，会触发
  `m3-04-cbr-nominal` ≤20% FAIL）。根因：filler 是只增不减的 H.264 NAL，
  某秒真实编码输出（IDR + 运动 P 帧）本身超 750,000 B 时无法削回。
- **处置：回滚** `PadToTargetBitrate()` 与 `MfVideoEncoder.h/.cpp` 的轮 43 字段到轮 42 原状；
  回滚后三构建再次 `exit=0 warnings=0 errors=0`
  （`tasks/G6/V1-007/artifacts/build-summary.txt`）。保留 10 次实测作为红绿证据。
- 缺陷单 `docs/17` 给出待裁定方向（是否允许动码控层 / 改需求口径）。

## 已知限制
- 本卡**未交付修复**，`## 交付状态` 为「无」；M3-04#1 由 `tasks/G6/V1-009` 保持「有卡未实现」。
- 静态模式极端值受本机 IDE/终端窗口重绘影响，已在主卡 EVIDENCE 标注。
- 执行方不自评为已验收。