# TASK: S1-008 窗口采集/尺寸跟随的 S1 侧改动承接（ComPtr 持有 + recorder 入口 + 探针窗口）

阶段：G5｜角色：S1｜轮次：40｜来源：验收方轮 40 指令 P1-F（文件域归位 F6）

## 背景（为什么开这张卡）

轮 39 实现 M1-08 时，`src/recorder/` 与 `src/tools/srprobe/` 的改动是**在 S2-002 卡名下做的**，
但按 `CONTRACT.md` §2，`src/recorder/` 与 `src/tools/` 属 **S1** 文件域 → 属**双向越权**。
轮 40 把 S2-002 卡的文件域改回 `src/capture/` + `src/compositor/`（见该卡文件域节的更正说明），
并由**本卡**承接 S1 侧的实际改动，使每一处代码改动都落在正确的角色卡下。

## 承接的改动清单

| # | 改动 | 文件 | 需求子句 |
| --- | --- | --- | --- |
| 1 | 会话用 `ComPtr` 持有上一帧纹理（修 0xC0000005：重建纹理释放旧纹理而会话手里只有裸指针） | `src/recorder/RecordingSession.cpp` / `.h` | M1-08#1 |
| 2 | recorder 的窗口采集入口 `--window <hwnd>` | `src/recorder/main.cpp` | M1-08#1 |
| 3 | recorder 的宽高比策略入口 `--fit <fit\|pad\|stretch>` | `src/recorder/main.cpp` | M1-08#2 |
| 4 | 探针窗口加蓝色边框 + `--resize-cycle`（每 N 秒改一次尺寸，用于连续缩放跟随） | `src/tools/srprobe/window_host.cpp` | M1-08#1 |
| 5 | 探针窗口支持 `--hwnd-file`（提权实例 stdout 拿不到窗口句柄，M1-27 验收要用来指认目标窗口） | `src/tools/srprobe/window_host.cpp` | M1-27#1（工具侧） |

## 必读章节

- `docs/01-功能需求清单.md` M1-08、M1-27
- `CONTRACT.md` §2（文件域分工）、§4（`CaptureRequest` / `ICaptureSource` 契约）
- `docs/02` §7 接口契约

## 文件域

- `src/recorder/RecordingSession.cpp`、`src/recorder/RecordingSession.h`、`src/recorder/main.cpp`
- `src/tools/srprobe/window_host.cpp`
- `tasks/G5/S1-008/*`

## 约束

- `src/core/` 属 S1 但本卡**不改**（`fit mode` / `window_handle` 的契约变更在轮 36~39 已随
  S1 的变更单落地，见 `CONTRACT.md` §4）。
- 窗口缩放时**不得**重建编码器（成片分辨率要稳定）。

## 完成定义（DoD）

- [x] `src/recorder/` 与 `src/tools/srprobe/` 的实际改动在本卡下有据可查
- [x] `verify/cases/g5_window.ps1` 重跑通过（窗口尺寸跟随 / 两档策略 / 被遮挡）
- [x] `verify/cases/g5_uac.ps1` 的工具侧依赖（`--hwnd-file`）可用
- [x] 不再出现「改了 S1 的目录却记在 S2 卡下」的归位问题

## 覆盖子句

- M1-08#1
- M1-08#2

## 交付状态

- M1-08#1: 已实现
- M1-08#2: 已实现
