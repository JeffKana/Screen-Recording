# DEFECT-01：8h 单文件在 7h22m 被并发用例杀死

- 严重度：**Blocker（对 G5 结论）** / 产品无缺陷
- 发现时间：2026-10-27 18:27（轮 34 冻结现场时）
- 影响需求：M6-02#1、M6-09#1、LT-01
- 结论：**8h FAIL**（实际 26,521s < 28,790s），不是产品崩溃，是验收编排互相踩踏。

## 1. 时间线（精确到秒）

| 时间 | 事件 | 证据 |
| --- | --- | --- |
| 10:50:18 | 启动 8h 单文件录制；driver pid 7672、recorder pid 34308 | `lt01-8h.out`、`driver2.out` |
| 18:11:19 | 倒数第 2 条采样：t=26,460,815ms，WS 364.0MB，handles 481 | `samples.jsonl` |
| 18:12:19 | 最后一条采样：t=26,521,136ms，WS 364.2MB，handles 481 | `samples.jsonl` |
| 18:12:30 | `lt01-8h.mkv.part` 最后写入时间；此后不再增长 | 文件 mtime |
| 18:12:30 后 | `sr-recorder` pid 34308 消失；无 Application Error / WER 事件 | `Get-Process`；事件日志 18:10–18:14 为空 |
| 18:12:3x | driver 检测到进程退出，开始收尾分析 | `driver2.out` |

最后一条 recorder 日志是 `metrics ... captured=439684`，**没有** `error=` / `fatal` / `record_exit`。
静默消失 + 无崩溃事件 = 被 `TerminateProcess` 杀死。

## 2. 根因

验收方在 18:11–18:12 复跑 `g5_fileops`（其报告写明「我复跑 g5_fileops 9/9」）。
该用例开头（以及 `ui_smoke` / `g5_format` / `g5_fps` 等 12+ 个用例）都有：

```powershell
Get-Process sr-ui, sr-recorder -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
```

8h 的录制进程镜像名就是 `sr-recorder`，于是被一并 `TerminateProcess`。
这不是「用例写错」——用例需要清理上一轮残留；错在**长跑进程与普通用例共用镜像名、且没有互斥锁**。

## 3. 影响

- 时长 26,521s < 28,790s → `lt01-duration` 必 FAIL。
- `.part` 未 finalize，没有 Segment Duration；内存/句柄/丢帧虽在截止前健康，
  但不能用于 8h 判据。
- 首轮（10:28）已因 D-58 漂移算法作废；本轮又因 D-60 作废，**两次都不是产品问题**。

## 4. 修复（本轮已做）

| 编号 | 修复 | 验证 |
| --- | --- | --- |
| D-60 | 长跑改用独立镜像 `sr-recorder-lt01.exe`（复制到 work 目录再启动），普通用例按 `sr-recorder` 名字清理不再命中 | 45s 冒烟：`image=sr-recorder-lt01`，正常退出 |
| D-60 | 启动时写 `verify/artifacts/.lt01-running.json`（pid/out/started），收尾或 trap 删除；验收方看到该文件即不得开跑任何用例 | 文件已生成；本轮 45s 冒烟后自动删除 |
| D-59 | 异常退出时不再 probe 不存在的 `.mkv`：改为探测 `.part`、上报 `aborted=true` / `span_s`，stdout 未 flush 时从日志最后 `captured=` 回填 | 45s 正常路径通过；代码分支已加 |

## 5. 复现

1. 启动 8h：`pwsh -File verify/cases/g5_lt01_real.ps1 -Seconds 28800 -SampleSeconds 60`；
2. 另开一个 shell 跑任一旧用例（如 `g5_fileops.ps1`）→ 旧版立即杀死 8h；新版不受影响；
3. 新版如果被 `Stop-Process -Name sr-recorder-lt01` 显式杀死，summary 会给出
   `aborted=true`、`span_s` 与 `.part` 路径，不再出现 `duration_s=0` 的误导性结果。

## 6. 冻结现场

`verify/artifacts/FROZEN-20261027-1812/`：

- `summary.json`（旧的误导性 3 PASS / 6 FAIL，保留原样）
- `lt01-8h.log`、`lt01-8h.out`、`samples.jsonl`（442 条）
- `lt01-8h.mkv.part`（20,824,813,676 字节，SHA256 `10C30648…5213`）
- `manifest.json`（含 `sr-recorder.exe` SHA256 `9B9E2015…8720`、终止时间、结论）

**不得**用这份 `.part` 交 8h 结论；它只用于证明「跑到 7h22m 是健康的，死因是外部终止」。
