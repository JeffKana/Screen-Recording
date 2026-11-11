# HANDOFF: G5/S5-002（M5-06 列表）

## 我改了什么接口？

- 新增 CLI（不是 IPC 契约）：
  - `sr-recorder --list <dir> [--list-skip <prefix>]`
    输出 `list_dir=` / `count=` / 多条 `entry=kind=...\tstate=...\tsize=...\tmtime_ms=...\tduration_ms=...\twidth=...\theight=...\tname=...`
  - `--list-skip`：跳过以该前缀开头的 `.part`（正在写入的临时文件）
- `IRecoveryService` 未变；`MkvRecovery` 新增 `InspectHeader()`（只读 1MB 头）。
- `RecoveryItem` 新增只读展示字段 `duration_ms` / `width` / `height`（core 类型，无行为变化）。
- `src/app` 新增窗口类 `SrUiListWindow`（ListView），控件 ID `700`，托盘/主窗口命令 ID `309` / `208`。
- `CMakeLists` 给 `sr-ui` 增加 `comctl32`（系统库）。

## 下游需要接什么？

- **M5-07（重命名/删除）**：列表已给出条目名与路径前缀，右键菜单直接复用同一份 `--list` 输出；
  删除要走回收站（`SHFileOperation`/`IFileOperation`），不要自己 `remove`。
- **M5-09（损坏检测/修复）**：`state=recoverable` 的条目应挂「修复」入口，直接调
  `sr-recorder --repair <part>`；本卡只标注不修复。
- **G6 打包**：comctl32 是系统组件，安装包无需额外分发；但要在 Win10 以下确认 ListView v6 清单。

## 已知限制是什么？

1. 可恢复 `.part` 探测是**全量读**（沿用 `MkvRecovery::Probe`），大文件打开列表会有秒级停顿；
   正在录制的 `.part` 用 `--list-skip` 规避，不会再扫增长中的文件。
2. `--list` 的 `mtime_ms` 是 Unix 毫秒（由 Win32 FILETIME 换算），UI 再转本地时间；
   跨时区/夏令时由 `SystemTimeToTzSpecificLocalTime` 处理。
3. 文件名按 UTF-8 输出；若第三方工具用系统代码页解析会乱码（UI 侧已按 UTF-8 解）。
4. 列表不递归子目录（与 `--scan` 的扫描范围一致）。

## 哪些需求我**没**做完？

- **M5-06 三条子句全部完成**，但 M5-06 所属需求组仍缺 M5-07（重命名/删除）/M5-08（内置播放），
  它们是无卡状态，不在本卡范围。
