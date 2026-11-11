# HANDOFF: S5-001 → 轮 25 / T-17

## 1. 新增 CLI

| 参数 | 作用 |
| --- | --- |
| `--control-pipe <任意非空>` | 启用控制通道，管道名固定为 `\\.\pipe\sr.recorder.<pid>` |
| `--control-wait <ms>` | accept 超时，超时后 `control=absent` 照常录 |

## 2. 新增统计字段

`control`（connected/absent/lost）、`commands_applied`、`commands_rejected`。

## 3. 下游（T-17 可观测）

- 诊断包应包含：`control`、`commands_applied/rejected`、`write_errors`、
  `fallback_source_pts_ms`、`backend_changed`。
- 丢帧统计（M6-03）尚未实现，是本轮未覆盖项。

## 4. 已知限制

| 限制 | 影响 | 计划 |
| --- | --- | --- |
| 单实例、单客户端 | 第二个 UI 连不上（`ERROR_PIPE_BUSY`） | 多实例属后续需求 |
| 断开后不重连 | UI 重启需重启录制进程 | T-17 或后续 |
| 手动暂停不设上限 | 长时间暂停仍占 muxer 与文件 | 可加 `--max-pause` |
