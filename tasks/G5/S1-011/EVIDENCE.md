# EVIDENCE: S1-011（M1-24 光标采集开关 CLI 透传）

> 执行方证据，非验收结论。数据 2026-11-11。

## 一条需求一条证据（配合 V1-010 的行为断言）

M1-24「光标采集开关：可选择是否将鼠标指针写入成片」。

- 现状普查：`src/core/types/CaptureRequest.h:21` 已有 `bool include_cursor = true`；
  `src/capture/WgcCaptureSource.cpp:158` 已调用
  `impl_->session.IsCursorCaptureEnabled(request.include_cursor)`；
  `supports_cursor_toggle=true`（WGC），DXGI/窗口源为 false。缺的只有 CLI 入口与透传。
- 改动（4 处，均在 `src/recorder/`）：
  1. `main.cpp` `Options` 加 `bool include_cursor = true`；
  2. `main.cpp` 解析 `--cursor on|off`（非法值 `return false`，不静默回落）；
  3. `main.cpp` help 加一行 + 汇总 stdout 加 `include_cursor=%d`；
  4. `RecordingSession.h` Options 加字段、`RecordingSession.cpp:188` 设
     `request.include_cursor = options.include_cursor`。
- 命令与实测值（`tasks/G6/V1-007/artifacts/r43-closeout.txt`）：
  - `--cursor off` → `record_exit=0 include_cursor=0`，受控纯色背景下成片中心 60×60
    与全画面抽样非背景像素均 **0**；
  - `--cursor on` → `record_exit=0 include_cursor=1`，中心 35，指针随 A/B 两点移动、
    旧位置无残影（B 段 A 点 0）。
- 红-绿对照：旧实现无 CLI 入口、`include_cursor` 恒 true（无法关闭）→
  新实现可关可开，且成片像素级可验。
- 三构建 `exit=0 warnings=0 errors=0`；发布包 help 含 `--cursor`（见主卡回归）。

## 已知限制
- 仅 WGC 桌面源支持开关；`--help` 已写明。DXGI / 窗口源 `supports_cursor_toggle=false`。
- 本卡不声明需求子句，状态由 `tasks/G6/V1-010` 统一声明。
- 全画面为抽样（步长 2），非逐像素扫描。
- 执行方不自评为已验收。