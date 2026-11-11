# HANDOFF: S1-001

## 我改了什么接口

- 无对外接口变更。`OutputPath.h` 签名未动（`PrepareOutputPath` / `CommitOutput` / `OutputPlan` 保持原样）。
- 新增内部自由函数 `FindFreePath`（`OutputPath.cpp` 匿名命名空间）：`base` 本身优先，其次 `_001…_999`；全占返回 false。
- 行为变更 2 处：
  1. `CommitOutput` 在 `rename` **之前**调用 `FindFreePath`，目标被占用（含录制期间被创建）时换名；`rename` 仍失败则继续换名重试。
  2. `PrepareOutputPath` 序号耗尽时返回失败（上层 `record_exit=13`），不再返回已存在的 `_999`。

## 下游需要接什么

- **T-15（控制闭环）**：`RecordingSession::Run` 的返回码语义不变，13 = 路径不可用；暂停/倒计时改造不要动 `PrepareOutputPath` 的调用时机（必须在倒计时之后、`muxer.Open` 之前）。
- **T-16（故障注入）**：`CommitOutput` 现在保证「不会覆盖已存在的成品」，但**不负责**把 `.part` 搬到备用目录；M8-06 后半需要新增「写失败 → 停止 → 换目录提交」路径，可直接复用 `CommitOutput`（只换 `OutputPlan.final_path`）。
- **用例**：`g3_output.ps1` 从 11 条扩到 15 条，脚本会清空 `build-msvc/output`，不要在它之后依赖该目录里的旧产物。

## 已知限制

1. 查空位与 `rename` 之间存在极小 TOCTOU 窗口；未采用「准备阶段占位」是因为崩溃会留下 0 字节文件，与 G5-S3 冲突。
2. `_001~_999` 全占的拒绝分支无自动化用例（成本高），仅实现保证。
3. `ProbeWritable` 使用固定探针名 `.sr_write_probe.tmp`，两个 `--record` 进程同时开录可能互删探针（不影响成品，未修）。
4. 未改动 `MkvWriter::WriteVideo/WriteAudio` 的返回值处理 —— 写失败仍不被感知，属 T-16 范围（F2）。

## 哪些需求我**没**做完

- **M8-06 后半**（录制中权限被收回 → 停止并保存到备用路径）仍未实现，本轮保持 PARTIAL，挂账 T-16。
- M5-15 的「命名模板 `{date}_{app}_{index}`」（05 AC-10 的另一半）不在本卡范围，属 UI/命名模板（T-09）。
