# SELFTEST: S2-002（轮 39 · M1-08#1/#2 + M1-09#1）

## 0. 开工四项自查（06 §4.2）

| 检查项 | 结果 | 依据 |
| --- | --- | --- |
| 需求编号可检索 | ✅ | `docs/01` M1-07~M1-09，验收要点原文已抄进 `TASK.md` |
| 必读章节可定位 | ✅ | `docs/01` M1-08/M1-09、`docs/02` §4/§7、`docs/03` §4、`docs/05` §5 AC-01、`CONTRACT.md` §4 |
| 上游交付物可获取 | ✅ | 轮 38 的实现（`--window/--fit`、转换器策略、`source_size_changes` 观测量、`srprobe windowhost`）+ 验收方轮 38 报告的 R1~R3 结论 |
| 文件域无交集 | ✅ | 本轮动 `src/capture/`（S2）、`src/compositor/`（S2）、`src/recorder/`（S1）、`src/tools/srprobe/`（S1）、`verify/cases/g5_window.ps1`；与并行的 `tasks/G5/V1-003`（只动 verify/tools + docs + 历史卡）不重叠 |

## 1. 构建

```powershell
cmake --build build-msvc --config Release   # 0 error / 0 warning
cmake --build build-msvc --config Debug     # 0 error / 0 warning
cmake --build build-mingw --config Release   # 0 error / 0 warning
```

## 2. 先红后绿（E1 → E2 → 修复）

| 阶段 | 做法 | 结果 |
| --- | --- | --- |
| E1（红） | 删掉自设护栏 + **不**重建 VideoProcessor | Blt 不报错、**也不裁剪**（画面正确）→ 证伪了「CONTENT_DESC 绑定输入尺寸导致失败」这一假设；也说明真正的机制是「护栏挡住了缩放分支」 |
| E2 | 删掉护栏 + 按新源尺寸重建 enumerator/processor | 画面正确、`converter=gpu-videoprocessor`（无 CPU 回退）、丢帧 0 |
| 修复后 | 采集侧按 `ContentSize()` 重建帧池+纹理；会话侧用 ComPtr 持有上一帧纹理 | 24 PASS / 0 FAIL / 0 SKIP |

**崩溃的红**：轮 38 的 0xC0000005（5 次里 2 次）→ ComPtr 持有后 **24 次 resize 退出码 0** ✓。

## 3. 回归

| 用例 | 结果 |
| --- | --- |
| `g5_window`（本轮） | 24 PASS / 0 FAIL / 0 SKIP |
| `g5_format` / `g4_gate` / `g3_recovery` | 14 / 12 / 9 全 0 FAIL |
| `g5_encoder_list` / `g5_tempfile` | 17 / 10 全 0 FAIL |
| `ui_smoke` | 103 PASS / 0 FAIL（一次 102/1，失败项 `unfocused-hotkey` 是前台窗口探测的环境敏感项，重跑即过；本轮未改热键代码） |
| 覆盖表 / 契约 / 卡面一致性 | 90/1/25、缺理由 0、`--check` 通过；C1~C8 8/8；`card_consistency_check` 0 冲突 |

## 4. 交付前自检

- [x] 开工四项自查全部通过并已记录
- [x] **先实验后改码**：E1/E2 都留了可重跑的命令与输出（E1 的构建方式见 EVIDENCE §1）
- [x] 每条断言都有可重跑的命令与实际输出
- [x] 三构建零告警
- [x] 相关回归零失败（依据已说明：§3）
- [x] 无死代码：轮 38 的自设护栏已删除；不再有「恒假开关」；`ContentSize()` 分支是真实生效的路径
- [x] 改动未超出文件域（`src/core/`、`src/ipc/`、`src/mux/`、`src/encode/` 未动 → 无需契约变更单）
- [x] 文档已同步（`TASK.md` 交付状态、`EVIDENCE.md` §0 归因更正、`docs/07` 台账轮 39 行、`docs/09` 覆盖表）
- [x] HANDOFF 四问齐答
- [x] `## 交付状态` 与实际一致（三条均「已实现」）
- [x] `## 覆盖子句` 只列实际覆盖的子句
- [x] 已知限制已写明（含「E1 证明重建处理器非必需」「只覆盖 WGC 源」）
