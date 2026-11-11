# EVIDENCE: S2-001 控制闭环（暂停 / 倒计时 / 自动停止）

锚点按 2026-10-21 裁定：**M2-02 / M2-04 / M7-02（按时长部分）**。

复现命令：

```powershell
pwsh -NoProfile -File verify/cases/g4_control.ps1 -BuildDir build-msvc -Config Release
```

结果：**32 PASS / 0 FAIL**。

## A 倒计时取消（M2-04，轮 21 新增）

| 判据 | 实测 |
| --- | --- |
| 取消退出码 | `record_exit=14` |
| 取消标记 | `cancelled=1` |
| 目标目录 | **未创建**（倒计时前移到 `PrepareOutputPath` 之前） |
| 残留 | 无 `.part` |
| 状态终点 | `Idle,Preflight,Countdown,Idle` |

## B 暂停账目（M2-02）

| 判据 | 实测 |
| --- | --- |
| 暂停不计入成片 | 6s 内容 + 4s 暂停 → `duration=5.999s`、`frames=180`、`wall=10.07s` |
| 暂停期间不写盘 | `part_bytes_at_pause == part_bytes_at_resume`，增长 0 B |
| 恢复无长空洞 | 最大帧间隔 **34ms**（1 帧 33ms） |

## C 音轨对齐（M2-02）

| 场景 | 视频末 PTS | 音频末 PTS | 差 |
| --- | ---: | ---: | ---: |
| 无暂停 | 4966ms | 4913ms | **53ms** |
| 暂停 4s | 5966ms | 5732ms | **234ms** |

判据 ≤300ms。暂停会把尾部差放大约 4 倍，列为观察项（见 HANDOFF）。

## D 倒计时（M2-04）

`countdown_ms=3002`，成片仍为 `2.999s` —— 倒计时不进成片。

## E 自动停止（M7-02 前半）

`--seconds 8` → 240 帧 / `7.999s`。**按结束时间停止未实现**，属 V1.5。

## F 双后端（M2-02）

WGC 与 DXGI 各跑一遍暂停流程：`backend_changed=0`，均 `3.999s`。

## G 错误传播（D-28）

`--fail-resume` 注入：

```
record_exit=15
resume failed (injected by --fail-resume)
output=...\failresume.mkv  duration=1.999s  decode clean  container RESULT: 0 failure
```

即：**以非零码退出，但已录内容完整落盘**。此前该路径 `break` 后照常 `return 0`，
属于「中途哑火却报成功」，会污染故障注入结论。

## H 状态机与多段暂停（M2-02）

```
state_sequence = Idle,Preflight,Countdown,Recording,Paused,Recording,Paused,Recording,Stopping,Idle
pause_count = 2
pause_after_stop_rejected = 1
```

`RecorderStateMachine` 已接入产品路径（此前只活在 G1 单元测试里）。
Stop 之后下发 Pause 被状态机拒绝，本项由进程自证，不依赖外部断言。

## 轮 21 修掉的缺陷

| 编号 | 现象 | 根因 |
| --- | --- | --- |
| D-28 | 恢复失败静默成功（exit 0） | 三处 `break` 后照常收尾，`Run` 恒返回 0 |
| D-29 | 取消会留下目录与 `.part` | 倒计时放在 `muxer.Open` 之后，物理上已创建 |
| — | 暂停只支持一次 | `pause_pending=false` 后不再触发；改为列表驱动 |

## 回归

`g3_storage` 11/11、`g3_output` 15/15、`g3_recovery` 9/9、`contract_check` 7/7、
MinGW 与 MSVC 均零告警。

## 未完成（不得计为通过）

- 控制通道（`--control-pipe` / `PipeConnection::TryReceive`）：登记为 **T-09 前置**。
- M7-02「按结束时间停止」：V1.5。
