# TASK: S1-007 G5 关门轮 · 码控/帧率/文件操作/覆盖模型（轮 33）

阶段：G5｜角色：S1｜轮次：33｜来源：验收方轮 33 指令（P0-2 / P0-3 / P1）

## 需求范围

| 需求 | 子句 | 验收要点原文（docs/01） | 证据形式 |
| --- | --- | --- | --- |
| M3-04 | #2 | VBR 模式下标称值与实测值偏差 ≤ 20% | `g5_format.ps1` 逐秒码率 avg vs 标称、peak vs 上限、`rate_control=vbr` |
| M3-03 | #1 | 选 60fps 录制动态画面，成片用帧率检测工具测得 59~60fps | `ffprobe avg_frame_rate` + `encode_fps` |
| M3-03 | #2 | 选 15fps 时实际帧数不超过配置值 | `ffprobe` 帧率 + 帧数上限 |
| M5-07 | #1 | 重命名后磁盘文件名同步变更 | `g5_fileops.ps1`：磁盘文件 + 列表 `list_entry` 同步 |
| M5-07 | #2 | 删除进回收站而非直接抹除 | `FOF_ALLOWUNDO`，回收站 COM 命名空间可查 |
| M5-07 | #3 | 批量操作有二次确认 | 多选 + 确认框；自动化断言 `confirm=1` 日志 |

维护项（不占需求子句，但阻塞本轮验收）：
R1 覆盖模型支持子句级交付声明（`M3-04#1: 已实现`）并重生成 `docs/09`；
R2 `g5_diag_real` 导出后断言源日志 intact；R3 画质上限 70/85 的 PSNR/SSIM 对照；
R5 `g5_list` 断言「大小」列与磁盘字节一致。

## 必读章节

- `docs/01-功能需求清单.md` M3-03 / M3-04 / M5-07
- `docs/02-系统架构.md` §5（录制会话）、§7（IPC）
- `docs/03-技术选型.md` §6（MF 编码器路径）
- `docs/05-非功能需求与验收.md` §2（码率/帧率口径）、§6（故障注入）
- `docs/06-子智能体执行规程.md` §4.2（开工自查）、§6.1（量化结论）
- `CONTRACT.md` §7（IPC 快照）、§18（G5 判据）

## 文件域

- `src/encode/MfVideoEncoder.{h,cpp}`
- `src/core/types/EncodingConfig.h`
- `src/recorder/RecordingSession.{h,cpp}`、`src/recorder/main.cpp`
- `src/app/AppConfig.{h,cpp}`、`src/app/RecorderController.{h,cpp}`、`src/app/main.cpp`
- `src/app/FileOps.{h,cpp}`（新增）、`src/app/CMakeLists.txt`
- `verify/cases/g5_format.ps1`、`g5_list.ps1`、`g5_diag_real.ps1`、新增 `g5_fps.ps1` / `g5_fileops.ps1` / `g5_quality.ps1`
- `verify/tools/coverage_model.py`、`verify/tools/coverage_report.py`
- `docs/09-V1.0覆盖表.md`、`docs/07-执行台账.md`
- `tasks/G5/S1-005/TASK.md`（只补 `## 交付状态` 的子句级回填一行）

## 上游交付

轮 32 末构建（`build-msvc/bin/Release`）：CBR `g5_format` 7/7、`g5_list` 24/24、
`g5_diag_real` 7/7；覆盖表 75 已实现 / 9 有卡未实现 / 32 无卡。
基线产物：`verify/artifacts/format-20261026-*`、`list-20261026-*`。

## 截止门

G5（关门轮）。本卡不覆盖 M6-02#1 / M6-09#1 的 8h 单文件，那部分在 `tasks/G5/S6-002`。

## 约束

- 不得改动 `mf-h264-sw` 回退链；硬编不支持码控时必须降级而不是拒绝录制。
- CBR 的既有行为（mean=max=标称、0.1s HRD、quality 70、filler 补齐）不得被 VBR 分支覆盖。
- 不得引入新三方依赖；`vcpkg.json` 只读。
- 不得修改既有断言以迁就实现；新增断言必须先红后绿。
- 文件操作必须走 Shell（`SHFileOperationW` + `FOF_ALLOWUNDO`），不得用 `std::filesystem::remove`。
- 删除/重命名前必须确认目标不是正在录制的 `.part`。

## 完成定义（DoD）

- [ ] `rate_control=vbr` 可回读，`max_bitrate` 生效，`cbr_filler_bytes=0`
- [ ] VBR 与 CBR 连续跑互不覆盖（输出文件按 M5-15 避让，模式各自生效）
- [ ] 帧率六档（15/24/25/30/60/自定义）可设置、重启保留、录制生效
- [ ] 重命名磁盘同步 + 列表同步；删除进回收站；批量有确认
- [ ] R1 覆盖模型子句级生效（先红后绿），`docs/09` 由工具重生成，C8 8/8
- [ ] R2/R5 断言落地；R3 给出 70/85 的 PSNR/SSIM 实测
- [ ] `g5_format` / `g5_fps` / `g5_list` / `g5_fileops` 全绿，抽样回归零失败
- [ ] MSVC Release + Debug 零告警

## 覆盖子句

- M3-04#2
- M3-03#1
- M3-03#2
- M5-07#1
- M5-07#2
- M5-07#3

## 交付状态

- M3-04: 已实现
- M3-03: 已实现
- M5-07: 已实现

> 轮 35 回填：M3-04#2 的测法已由验收方裁定（`docs/01` M3-04、`docs/05` §2.4 三条），
> `g5_format` 断言一一对应（运动源均值 / 任意内容峰值 / 静态只记录），本地 **14/14**。
> 仍需验收方独立复跑；G5 关闭不以本卡自测为准。
