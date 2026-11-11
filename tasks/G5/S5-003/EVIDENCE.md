# EVIDENCE: S5-003（M1-27 UAC 提权窗口采集限制提示）

## 0. 结论

| 验收点（docs/01 M1-27#1） | 结论 | 证据 | 实测要点 |
| --- | --- | --- | --- |
| ① 非管理员运行时录制 UAC 提权窗口，成片该窗口区域为黑屏 | **BLOCKED（本机不可复现）** | E3 | 实测**不是黑屏**：WGC 采到高完整性窗口真实内容（黑帧 1.0%、绿色 96.0%）；同窗口 PrintWindow 兜底取 0 帧 |
| ② 录制前或录制中有明确提示 | PASS | E1 | 开录前自检项 `preflight_check name=elevation ok=0 blocking=0` + `uac_preflight_warning`；录制中 `uac_warning phase=recording`；界面常驻标签（控件 210）文本可读 |
| ③ 界面有「以管理员身份重启」入口 | PASS | E2 | 主窗口按钮（控件 209）+ 托盘菜单项；走 `ShellExecuteW(runas)`，真实拉起的高完整性实例自报 `elevated=1` |

用例总结果：**17 PASS / 0 FAIL / 1 SKIP**（SKIP 即 ①，附实测数据）。

## 1. 唯一证据来源与可重跑命令

```
pwsh -NoProfile -File verify/cases/g5_uac.ps1 -BuildDir build-msvc -Config Release
```

本轮实跑（2026-11-08，build-msvc / Release）：

```
--- A/B 提权检测 + 开录前/录制中提示 ---
  PASS ui-started — main_hwnd=52955634
  PASS elevation-state-logged — 非提权进程被正确识别（elevated=0）
  PASS preflight-elevation-item — 开录前自检列出提权项，且标记为不阻断
  PASS preflight-warning-logged — 开录前给出明确提示（日志行）
  PASS preflight-not-blocking — 提示不阻断开录（自检仍通过）
  PASS recording-warning-logged — 录制中同样给出提示（日志行）
  PASS uac-label-visible — 标签文本：⚠ 当前非管理员运行：UAC 提权窗口等高权限窗口可能黑屏或无画面
  PASS elevate-entry-exists — 按钮文本：以管理员身份重启
--- D 「以管理员身份重启」命令构造 ---
  PASS elevate-dry-run-exit — exit=0
  PASS elevate-command-uses-runas — 动词为 runas
  PASS elevate-command-has-exe — 命令行含自身 exe 路径
  PASS elevate-command-keeps-args — 命令行保留原参数，并给提权实例单独日志
--- E 入口真实拉起（ShellExecute runas）---
  PASS elevate-launch-exit — exit=0
  PASS elevate-launch-ok — 提权请求已发出
  PASS elevated-instance-is-elevated — 提权实例自报 elevated=1
--- F 高完整性目标窗口采集实测 ---
  PASS target-window-is-elevated — owner_pid=21096（目标窗口必须属于高完整性进程）
  [实测] WGC: backend=wgc frames=196 black=1.0% green=96.0%
  [实测] PrintWindow: backend=printwindow frames=0 record_exit=4
  SKIP uac-target-black — 实测未黑屏 → 本机 ConsentPromptBehaviorAdmin=0 / PromptOnSecureDesktop=0，UAC 对话框不落安全桌面，「UAC 提权窗口黑屏」不可复现；同一高完整性窗口走兜底 PrintWindow 时 frames=0（UIPI 拦截，取不到帧）
  PASS uac-target-printwindow-blocked — frames=0 record_exit=4

RESULT: 17 PASS / 0 FAIL / 1 SKIP
```

## 2. 一条断言一条证据

### E1 · 提示（开录前 + 录制中）

- **判据**：非管理员运行时，开录前或录制中必须有明确提示，且**提示不阻断开录**。
- **实测**：
  - 启动即判定：`uac_elevation_state elevated=0`（`src/app/Elevation.h` 的 `IsProcessElevated()`：
    `OpenProcessToken` + `TokenElevation`）。
  - 开录前自检新增 `elevation` 项：`preflight_check name=elevation ok=0 blocking=0`
    ——「有这项、但不是阻断项」，随后 `uac_preflight_warning elevated=0 reason=not_elevated
    action=restart_as_admin`；自检整体 `preflight_pass=1`（录制照常开始）。
  - 录制中：`uac_warning phase=recording elevated=0 reason=not_elevated action=restart_as_admin`。
  - 界面：常驻标签（控件 210）文本「⚠ 当前非管理员运行：UAC 提权窗口等高权限窗口可能黑屏或无画面」，
    断言同时检查 `IsWindowVisible`（不是「文案写了但看不见」）。
- **可重跑**：同上命令（断言 `elevation-state-logged` / `preflight-elevation-item` /
  `preflight-warning-logged` / `preflight-not-blocking` / `recording-warning-logged` / `uac-label-visible`）。

### E2 · 「以管理员身份重启」入口

- **判据**：界面有入口；入口真实可用（拉起的高完整性实例 `elevated=1`）。
- **实测**：
  - 入口存在：主窗口按钮（控件 209）文本「以管理员身份重启」，`IsWindowVisible` + `IsWindowEnabled` 均为真；
    托盘菜单也有同名项（仅未提权时出现）。
  - 命令构造（`--elevate-dry-run`）：日志
    `uac_elevate_dry_run verb=runas command="…\sr-ui.exe" --config … --log "….elevated"`
    ——动词 `runas`、自身 exe 路径、原参数全部保留、提权实例用单独日志文件。
  - 真实拉起（`--elevate-launch`）：父进程日志 `uac_elevate_launch=ok`；提权实例的日志
    `<log>.elevated` 中 `uac_elevation_state elevated=1` → **确实拉起了高完整性实例**。
  - 交互约束：提权请求**只由用户确认触发**（按钮/菜单/CLI），自动化**不**自动弹 UAC；
    `--elevate-launch` 用 `ShellExecuteW(runas)`，UAC 对话框由系统弹出，脚本只等待并读日志，
    不点对话框 → 不会把测试机会卡死。
- **可重跑**：同上命令（断言 `elevate-entry-exists` / `elevate-dry-run-*` / `elevate-launch-*` /
  `elevated-instance-is-elevated`）。

### E3 · 「成片该窗口区域为黑屏」——BLOCKED（本机不可复现）

- **判据**（docs/01 M1-27#1 原文）：非管理员运行时录制 UAC 提权窗口，**成片该窗口区域为黑屏**。
- **实测（与判据相反，如实记录）**：
  - 目标窗口：用 `srprobe windowhost` 以 **runas** 启动（高完整性），断言其宿主进程
    `TokenElevation=1`（`target-window-is-elevated` PASS，owner_pid=21096）。
  - WGC 路径：录 6s → `frames=196`、`black=1.0%`、`green=96.0%` → **不是黑屏**，
    采到的是高完整性窗口的真实内容（绿色底 + 蓝色边框）。
  - 兜底 PrintWindow 路径：`frames=0`、`record_exit=4` → **取不到帧**（UIPI 拦截），
    这一侧是稳定可复现的失败。
- **为什么不可复现**：本机 UAC 策略为 `ConsentPromptBehaviorAdmin=0`、
  `PromptOnSecureDesktop=0` → UAC 对话框**不落安全桌面**，因此不存在
  「安全桌面上的提权窗口」这一采集场景；WGC 在桌面切换期间能继续取帧 → 不黑屏。
- **缺什么（BLOCKED 的具体内容）**：
  1. 一台 `PromptOnSecureDesktop=1` 的机器（UAC 对话框落安全桌面），或在
     `ConsentPromptBehaviorAdmin=5`（同意前不提示）下由**人工**点一次 UAC 的取样；
  2. 取样素材：真实 UAC 对话框（`Credential UI Xaml Host`）窗口句柄，
     用同一台**非提权** recorder 去采该窗口；
  3. 断言：成片该窗口区域黑帧占比 ≥ 95%（判据口径）且 UI 提示与入口同时出现。
- **本轮做了什么替代**：产品侧把「为什么可能黑屏」变成**用户可见的原因 + 可执行的出口**
  （提示 + 入口 + 两条可 grep 的日志），并在 E2 里证明入口确实能拉起高完整性实例；
  验收方要求的「黑屏证据」在本机无法产出，**没有**用文案结案。

## 3. 实现落点（供验收方对账）

| 需求点 | 文件 | 位置 |
| --- | --- | --- |
| 提权检测 | `src/app/Elevation.h`（新增） | `IsProcessElevated()` |
| 启动即判定 + 日志 | `src/app/main.cpp` | `wWinMain`（`uac_elevation_state`） |
| 开录前自检项 | `src/app/main.cpp` | `RunPreflight`（`name=elevation`、`blocking=false`） |
| 录制中提示 | `src/app/main.cpp` | `StartRecording`（`uac_warning phase=recording`） |
| 常驻提示标签 | `src/app/main.cpp` | `WM_CREATE`（控件 210） |
| 提权入口（按钮） | `src/app/main.cpp` | `WM_CREATE` 控件 209 + `WM_COMMAND` |
| 提权入口（托盘） | `src/app/main.cpp` | `ShowTrayMenu`（`kTrayElevate`） |
| 提权动作 | `src/app/Elevation.h` | `RestartAsAdmin` → `ShellExecuteW(runas)` |
| 命令构造/拉起钩子 | `src/app/main.cpp` | `--elevate-dry-run` / `--elevate-launch`（仅 `SR_TEST_HOOKS` 构建） |
| 目标窗口句柄传递 | `src/tools/srprobe/window_host.cpp` | `--hwnd-file`（提权实例 stdout 拿不到句柄） |

## 4. 已知限制

1. **黑屏一侧未闭环**（§E3）：本机不可复现，缺一台 UAC 落安全桌面的机器 + 人工确认取样。
2. 提权实例与原实例**并存**：本卡做的是「提示 + 入口」，不做「自动拉起并交接」，
   避免用户没确认就被换进程。
3. `--elevate-launch` 在**已经提权**的进程里是空操作（记 `already_elevated` 后退出）——
   防止提权实例再拉一个提权实例（无限循环）。
4. 提权实例写**独立日志**（`<log>.elevated`）：两个实例写同一个日志文件会互相覆盖。
5. 提示是**常驻**的（不只在开录前弹一次）：提示消失 = 用户看不到原因；
   代价是主窗口多一行常驻文字。

## 5. 先红后绿对照（改前必 FAIL）

仓库无版本控制（`git` 不存在），「改前」用**改动前的二进制**制造：`build-mingw/bin/sr-ui.exe`
（构建于 2026-10-29，早于 M1-27 的全部改动，因此**没有**提权检测/自检项/常驻提示/提权入口/相关日志行）。
用同一条用例跑这个二进制 → 新断言必须 FAIL；再用改动后的 `build-msvc` 跑 → 必须 PASS。

| 用例 | 改后（绿） | 改前（红，`build-mingw` 的 10-29 构建） |
| --- | --- | --- |
| `verify/cases/g5_uac.ps1` | **17 PASS / 0 FAIL / 1 SKIP** | **4 PASS / 10 FAIL / 2 SKIP** |

红态明细（改前）：

```
  FAIL elevation-state-logged — 非提权进程被正确识别（elevated=0）
  FAIL preflight-elevation-item — 开录前自检列出提权项，且标记为不阻断
  FAIL preflight-warning-logged — 开录前给出明确提示（日志行）
  FAIL recording-warning-logged — 录制中同样给出提示（日志行）
  FAIL uac-label-visible — 标签文本：（空）
  FAIL elevate-entry-exists — 按钮文本：（空）
  FAIL elevate-command-uses-runas — 动词为 runas
  FAIL elevate-command-has-exe — 命令行含自身 exe 路径
  FAIL elevate-command-keeps-args — 命令行保留原参数，并给提权实例单独日志
  FAIL elevate-launch-ok — 提权请求已发出
```

即：**本卡新增的 10 条断言在改前全部变红**（含「提示标签/按钮根本不存在」这两条），
改后全部变绿（只剩「黑屏」那一条因环境不可复现而 SKIP）。

## 6. 跨构建验证（MinGW）

同一用例对 **MinGW 构建**（WGC 不可用 → 走 PrintWindow 兜底）复跑：

```
[实测] WGC: backend=auto frames=0 black=-100.0% green=-100.0%
[实测] PrintWindow: backend=printwindow frames=0 record_exit=4
RESULT: 17 PASS / 0 FAIL / 1 SKIP
```

即：**提示与入口两条在 MinGW 构建上同样成立**（17 PASS），
而「高权限窗口取不到帧」在只有 PrintWindow 的构建上是**必然**的（frames=0）。
MinGW 重建（`cmake --build build-mingw`）**0 warning / 0 error**。
