# HANDOFF: S2-001 控制闭环 → S5（UI）/ S6（恢复）

## 1. 接口变化

| 位置 | 变化 | 下游影响 |
| --- | --- | --- |
| `RecordingSession::Options` | 新增 `countdown_seconds` / `cancel_at_seconds` / `pause_at_list` / `pause_duration_list` / `fail_resume` | S5 接 UI 时复用同一组字段 |
| `RecordingSession::Stats` | 新增 `cancelled` / `state_sequence` / `pause_count` / `pause_after_stop_rejected` / `video_last_pts_ms` / `audio_last_pts_ms` / `write_errors` | UI 可直接展示状态序列与 A/V 偏差 |
| `RecordingSession::Run` | 返回值新增 14（取消）、15（运行期致命错误） | S5 必须区分这两类，不能一律当失败弹同一个框 |

## 2. 下游需要接什么

- **S5（UI）**：暂停/恢复目前由 `--pause-at/--pause-duration` 驱动，**未接 IPC**。
  接控制通道时，把 `RecorderCommand::Pause/Resume/Stop/Cancel` 映射到现有状态机即可，
  状态机已是产品路径的唯一状态源（`state_sequence` 可自证）。
- **S6（恢复）**：`write_errors > 0` 现在会让进程以 15 退出并正常收尾，
  恢复流程应能处理「非 0 退出但文件完整」的情形，不要一律当损坏。

## 3. 已知限制

| 限制 | 影响 | 计划 |
| --- | --- | --- |
| 未接 IPC/UI | 暂停只能靠参数驱动 | T-09 前置 |
| M7-02「按结束时间停止」未实现 | 只能按时长 | V1.5 |
| 暂停后 A/V 尾部差放大到 ~234ms | 长录制可能累积 | 观察项，超 300ms 判据即失败 |

## 4. 未完成项

- 控制通道（`PipeConnection::TryReceive` + `--control-pipe`）：本轮按边界不做，
  已在 `docs/08 §15` 与台账登记为 **T-09 前置**，不得计为完成。
