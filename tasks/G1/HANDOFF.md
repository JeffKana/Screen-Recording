# HANDOFF: G1 阶段交接（S1 → S2 / S3 / S4 / S5）

## 1. 我改了什么接口

| 位置 | 内容 | 影响下游 |
| --- | --- | --- |
| `src/core/types/IClock.h` | 新增 `IClock` 抽象，实现为 `sr::win::QpcClock` | 全链路时间戳必须经 `IClock::Now()` 获取，禁止直接调 QPC |
| `src/core/types/Frame.h` | `Tick` 以 `QpcClock` 构造时刻为原点（`value = 0` 表示无效） | S2/S3 产出的 PTS 必须来自同一 `IClock` 实例，否则原点不同 |
| `src/core/interfaces/*.h` | 8 个接口落地，签名与 `CONTRACT.md §4` 一致 | S2/S3/S4 各自实现一个，禁止改签名（走契约变更单） |
| `src/core/state/RecorderState.h` | `RecorderStateMachine::Apply` 返回 `Accepted/Rejected` | S5 只能经 IPC 下发命令，不得本地维护状态副本 |
| `src/ipc/Protocol.h` | 握手后服务端**额外主动推送一次 StateSnapshot** | S5 接入时注意：握手成功会先收到 ack + snapshot 两条消息 |
| `src/recorder/main.cpp` | 单实例锁文件 `%TEMP%\sr-recorder.lock`，第二实例退出码 4 | S5 启动 Recorder 时需能识别退出码 4 并转为「聚焦已运行实例」 |

## 2. 下游需要接什么

- **S2（采集）**：实现 `ICaptureSource`，`TryAcquire` 必须非阻塞、无新帧返回 `false`。
  纹理格式先按 `Bgra8Unorm` 产出，转 NV12 由 `ICompositor`/VideoProcessor 负责。
- **S3（音频）**：实现 `IAudioSource`，WASAPI Loopback 的 `MayStarveWhenSilent()` 必须返回 `true`，
  否则 M1-21 的静音填充不会被触发。
- **S4（编码封装）**：实现 `IVideoEncoder` / `IAudioEncoder` / `IMuxer`。
  `Open` 前必须先查 `Caps()`，参数面板以能力上限为准（M3-05）。
- **S5（应用界面）**：作为 IPC 客户端接入，复用 `src/ipc/PipeChannel.h`，不要另写一套协议。
  管道名规则：`\\.\pipe\sr.recorder.<recorder_pid>`。

## 3. 已知限制

| 限制 | 影响 | 处理计划 |
| --- | --- | --- |
| 编译验证只用过 MinGW-w64 GCC 16.2，**MSVC 未验证** | 契约要求 MSVC 2022，可能存在 `/permissive-` 相关差异（如 `min/max` 宏、`snprintf` 返回值） | 具备 VS Build Tools 的机器上补一次 Release+Debug 构建（台账 D-04） |
| 管道为**单客户端**模型，一次只服务一个 UI | 多 UI 实例同时控制会互相挤占 | G4 前改为多实例 + 会话隔离 |
| 无重叠 I/O，`Receive` 为阻塞读 | 控制线程阻塞期间无法感知进程退出 | 与 UI 进程生命周期绑定，UI 死则管道断开自然退出 |
| `session_elapsed_ms` 恒为 0 | 快照里的时长无意义 | S2/S4 接入后由真实会话时钟填充 |
| 未做 `--help` 之外参数校验 | 非法 token 直接断连、无提示 | 保持现状，属设计选择（不给攻击者反馈） |

## 4. 未完成的需求（本阶段刻意不做）

M2-01 的端到端时延、M8-14 的诊断包、以及 M2-10 中「不产生第二个文件」的**文件级**验证，
都需要真实录制链路存在后才可判定，当前只验证到状态机与协议层，不对外宣称已完成。
