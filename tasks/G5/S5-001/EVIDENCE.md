# EVIDENCE: S5-001 控制通道（T-17a）

锚点：M2-02（暂停/恢复经控制通道）、M8-02（恢复幂等）、C11（真实降级）。

## 复现

```powershell
pwsh -NoProfile -File verify/cases/g4_channel.ps1  -BuildDir build-msvc -Config Release   # 14/14
pwsh -NoProfile -File verify/cases/g5_fallback.ps1 -BuildDir build-msvc -Config Release   # 24/24
```

## 六场景实测

| 场景 | 期望 | 实测 |
| --- | --- | --- |
| 未连接超时 | `control=absent`，照常录 | duration=3.999，exit 0 |
| 连发 8 条 | 全部生效 | `commands_applied=8`，序列含 4 次 Paused→Recording |
| 重复 Pause | 幂等 | 序列只出现 1 次 Paused |
| 非法命令 | 拒绝且不中断 | `commands_rejected=1`，成片可播 |
| 中途断开 | `control=lost`，继续录 | duration=5.999，exit 0 |
| 拆帧发送 | 半帧不消费 | 恰解析 1 条 |

## 关键实现

- `TryReceive`：`PeekNamedPipe` 探 12 字节头 → `DecodeHeader` 得 `payloadLen` →
  `available >= 12+len` 才 `ReadFile`。半帧不动缓冲区。
- `WaitForClient(timeout)`：临时 `PIPE_NOWAIT` 轮询 `ConnectNamedPipe`，连上后切回阻塞。
  无线程、无 `OVERLAPPED`、线格式未改。
- `ClientSeen()`：区分「连上又断」与「从未连接」。

## D-34（第三次覆盖事故）

预置 16B `.part.repaired` → 修复后应不覆盖。

- 修复前：哨兵被 1343596B 整体覆盖。
- 修复后：哨兵仍 16B，新文件落到 `rec.mkv.part_001.repaired`。

收敛：新增 `src/core/util/FreePath.h`，三处调用点（`OutputPath` / `MkvRecovery::Repair` /
`RepairIntoFallback`）统一改为调用它，语义固定为「全占即失败，绝不复用已存在路径」。

## 兜底两个此前未跑的入口

| 入口 | 实测 |
| --- | --- |
| 无 `--fallback-dir` | exit 15，保留 `.part`，无成品 |
| `.part` 不可修复 | exit 7（非 0），兜底目录 0 文件 |

## 回归

`g4_control` 32/32、`g5_inject` 28/28、`g3_output` 15/15、`g3_storage` 11/11、
`g3_recovery` 9/9、契约 7/7；MSVC 与 MinGW 均零告警。
