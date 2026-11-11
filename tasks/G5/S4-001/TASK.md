# TASK: S4-001 兜底提交与后端降级（T-16b）

- 角色: S4（编码/封装），协作 S6（恢复）、V1
- 阶段: G5（轮 23，计划 2 轮）
- 锚点: M8-06（后半）、M8-02、C11、M8-08

## 文件域

| 文件 | 归属 | 说明 |
| --- | --- | --- |
| `src/mux/MkvWriter.{h,cpp}` | S4 | `SetFailWriteAfter`、`Close` 失败时关闭句柄 |
| `src/encode/MfVideoEncoder.cpp` | S4 | `fail_all_encoders` 注入下沉到 `CreateTransform` |
| `src/resilience/MkvRecovery.cpp` | S6 | 产物命名避让、修复后源 `.part` 改名 |
| `src/recorder/RecordingSession.{h,cpp}` | S2 | 兜底提交、降级注入、`integrity_lost` 判定 |
| `src/capture/CaptureChain.{h,cpp}` | S2 | `excluded_backend` 枚举期排除 |
| `src/core/types/{CaptureRequest,StorageConfig}.h` | S1 | 新增字段（向后兼容） |
| `verify/cases/g5_fallback.ps1` | V1 | 新增 |

## 判据

见 `CONTRACT.md §17`（G5-F1 ~ G5-F8）。

## 约束

- 注入必须落在真实实现内部，禁止在调用方短路（F2 整改）
- 降级必须是链路真实行为，禁止硬编码 `backend_changed=1`
- 兜底产物必须经 `MkvRecovery` 截断修复，直接 copy `.part` 不算
- 原目录不得留下成品或 0 字节文件

## 交付状态

> 轮 32（F2）补节：覆盖模型默认值已翻成「有卡未实现」，历史卡必须显式声明。
> 以下状态与 `docs/09` 当前结论一致（门已关闭且证据在对应轮次的台账里）。

- M8-02: 已实现
- M8-06: 已实现
- M8-08: 已实现
