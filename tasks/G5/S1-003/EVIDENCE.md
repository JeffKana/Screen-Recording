# EVIDENCE: LT-01/LT-02 代理（轮 31）

> **第一行口径：代理 ≠ LT-01，本轮未证明单文件 8 小时。**
> 所有数字来自 2026-10-23 本机实测（MSVC Release）。

## 1. LT-01 代理

### 1.1 链路自检（4 段 × 20s，`verify/artifacts/lt01-20261023-155401`）

| 段 | 退出码 | 时长 | mem_peak | handles | 丢帧 | 可播 |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | 0 | 20.000s | 543.4MB | 387 | 0 | 是 |
| 2 | 0 | 19.999s | 544.1MB | 387 | 0 | 是 |
| 3 | 0 | 19.999s | 543.7MB | 387 | 0 | 是 |
| 4 | 0 | 19.999s | 543.2MB | 387 | 0 | 是 |

轮汇总：内存 +0.1%（判据 ≤5%）、句柄 +0%（判据 ≤10%）。**20s 段的绝对内存主要来自
GPU/编码器初始化，不能外推到 30min。**

### 1.2 30 分钟段（`verify/artifacts/lt01-20261023-164647`）

| 段 | 退出码 | 时长 | mem_peak | handles | 丢帧 | 可播 |
| --- | --- | --- | --- | --- | --- | --- |
| r1-s1 | 0 | 1799.998s | 546.7MB | 382 | 0 | 是 |
| r1-s2 | 0 | 1799.998s | 543.5MB | 382 | 0 | 是 |

段间：内存 **-0.6%**（≤+5%）、句柄 **0%**（≤10%）；段内 15/20min 采样 WS 543MB、
handles 481 均持平。文件 487MB / 431MB。

**未完成**：计划 12 段，本会话完成 2 段后于 17:47 主动停止（避免占机 6h）。
`-Rounds 3 -Segments 4 -Seconds 1800` 可续跑；未跑完不构成「LT-01 通过」。

采集口径：`sr-recorder --record --seconds 1800 --fps 30 --log-level info`，
不挂 UI；每段进程退出后再起下一段；内存取 `mem_peak_mb`（PeakWorkingSet64）。

## 2. LT-02 代理（`verify/artifacts/lt02-20261023-155903`）

30 分钟静音段 + 30s 恢复段（第 5s 起播 1kHz/10s），8/8 PASS：

| 检查 | 实测 | 判据 |
| --- | --- | --- |
| 静音段 mean_volume | **-62.1 dB** | ≤ -50 dB |
| 静音段 max_volume | -8.0 dB | 参考项：期间有一次瞬时声音（系统提示音），均值仍达标 |
| 恢复段 mean / max | -38.7 dB / -28.7 dB | 有声、不削波（≤ -1 dB） |
| 起播点（silencedetect） | 4.9999s | 与 5s 计划一致 |
| 起播窗口 max（4.9~5.4s） | -28.9 dB | 无满幅尖峰（爆音） |
| 时长 | 29.999s | 30±1s |

20s 预演（`lt02-20261023-155530`）：静音 -91dB、恢复 -36.9/-28.8dB、onset 4.82s，
与 30min 结果一致 → 判据不依赖时长。

## 3. 诊断包时间窗（P1）

合成 2h 日志（721 行，每 10s 一行）：

```
diag_window=ok
bytes_in=26484  bytes_out=2288
span_ms=600000   dropped_lines=660
```

`Export` 现在先 `TruncateByAge(…, 10min)` 再 `TruncateTail(…, 400KB)`；
`config.ini` 与 `version.txt` 不受影响。

## 4. U1 取证链路（构造失败验证）

`Save-FailureContext -Tag probe -WorkDirs <fake>` →

```
context=True  workCopy=True  hasProcSection=True  tail=True
rec.mkv.part 1024 bytes  ui.log 59 bytes
```

即：失败现场包含进程表、命令行、工作目录清单、`.part` 与日志尾部；
本轮 10 连跑未触发真实失败，故用构造失败验证。

## 5. 未达成

- 单文件 8 小时（M6-02#1/M6-09#1）：未做 → 覆盖表保持「有卡未实现」。
- LT-01 12 段全量：未跑完，见 `artifacts/lt01-*/summary.json`。
