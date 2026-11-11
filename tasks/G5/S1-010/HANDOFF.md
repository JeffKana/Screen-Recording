# HANDOFF: S1-010（--force-cpu）

## 一、我改了什么接口？
无签名变更。新增：`Options::force_cpu`（main.cpp / RecordingSession.h）、
`--force-cpu` 解析与 help（SR_TEST_HOOKS 段）、`record_options.force_cpu` 透传、
`RecordingSession.cpp` 在 `Initialize` 前调用 `converter.ForceCpu()`。
`CONTRACT.md`、`vcpkg.json` 未改。

## 二、下游需要接什么？
1. 若将来要覆盖 M6-08#1（真实混合显卡跨 GPU 拷贝），需要新硬件用例；本卡的强制开关只是
   验证既有 CPU 中转能力，不代替真实故障。
2. `--force-cpu` 只在 hooks 构建存在；发布构建不会暴露（发布包 help 已断言）。

## 三、已知限制是什么？
- **降级验证**：强制开关触发，不是真实 GPU 故障/跨 GPU 拷贝场景。
- 三构建零告警，但只在 MSVC 构建上做了行为断言（MinGW 仅编译）。

## 四、哪些需求我没做完？
| 项 | 状态 | 原因 / 下一步 |
| --- | --- | --- |
| M6-08#2 | 已交付 | `converter=cpu` + 录制成功 + 可解码 |
| M6-08#1 | 没做，仍无卡 | 需核显+独显混合显卡机器 |
| V1.0 发布 | 仍不可发布 | 见主卡 HANDOFF |

## 五、交付包清单
`tasks/G5/S1-010/`：TASK.md、EVIDENCE.md、SELFTEST.md、HANDOFF.md。

**执行方不自评为已验收。**