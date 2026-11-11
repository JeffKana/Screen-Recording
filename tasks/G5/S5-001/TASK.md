# TASK: S5-001 控制通道（T-17a）

- 角色: S5（UI/集成），协作 S1（IPC）、S2（录制主循环）、V1
- 阶段: G5（轮 24，计划 2 轮）
- 锚点: M2-02、M8-02、C11

## 文件域

| 文件 | 归属 | 说明 |
| --- | --- | --- |
| `src/ipc/PipeChannel.{h,cpp}` | S1 | `TryReceive`、`WaitForClient(timeout)`、`ClientSeen` |
| `src/core/util/FreePath.h` | S1 | 新增，全仓唯一避让实现 |
| `src/recorder/RecordingSession.{h,cpp}` | S2 | 控制轮询、手动暂停、兜底缺口 |
| `src/resilience/MkvRecovery.cpp` | S6 | D-34 修复 |
| `verify/cases/g4_channel.ps1` | V1 | 新增 |

## 硬边界遵守

| 约束 | 遵守 |
| --- | --- |
| 不引线程做阻塞读 | 是（全部 `PeekNamedPipe` 轮询） |
| 不用关句柄打断阻塞读 | 是（没有阻塞读） |
| 不改 CONTRACT §7 线格式 | 是 |
| accept 用 `--control-wait`，不改异步 | 是（默认 5000ms） |

## 交付状态

> 轮 32（F2）补节：覆盖模型默认值已翻成「有卡未实现」，历史卡必须显式声明。
> 以下状态与 `docs/09` 当前结论一致（门已关闭且证据在对应轮次的台账里）。

- M2-02: 已实现
- M8-02: 已实现
