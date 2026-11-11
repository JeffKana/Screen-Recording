# 轮 27 · UI-1 人工验收清单（不计入自动化 PASS）

自动化部分见 `verify/cases/ui_smoke.ps1`（24 条）。本清单只覆盖需要人眼确认的托盘/窗口行为，
截图放在同目录，文件名以 `round27-` 开头。

## 1. 托盘图标与菜单

| 步骤 | 期望 | 证据 |
| --- | --- | --- |
| 启动 `sr-ui.exe`（不带 `--hidden`） | 通知区域出现应用图标；悬停提示「Screen Recorder — 就绪」 | `round27-main-idle.png` 为主窗口；托盘图标在通知区域（可能收进溢出区） |
| 右键托盘图标 | 菜单：开始录制 / 暂停 / 停止 / 打开目录 / 设置热键… / 显示主窗口 / 退出 | `round27-tray-menu.png`（菜单在光标处） |
| Idle 时看菜单 | 「开始录制」可点；「暂停」「停止」置灰 | 同 `round27-main-idle.png` 的按钮状态 |
| 开始录制后看菜单 | 「开始录制」置灰；「暂停」「停止」可点 | `round27-tray-menu.png`（菜单的「开始录制」为灰） |
| 暂停后看菜单 | 「暂停」变成「继续」；「停止」仍可点 | 人工点一次暂停后复看 |
| 点击「打开目录」 | 资源管理器打开成品所在目录 | 人工确认 |
| 点击「设置热键…」 | 弹出三行热键设置；输入已被占用的组合并保存 | 弹窗「该热键已被占用」，窗口不关闭；换成空闲组合保存成功 |
| 运行中修改 `config.ini`（或从命令行 `--set-hotkey`） | 1s 内重载并重新注册，无需重启 | 自动化 E 段已断言 `hotkeys_reloaded` + 新热键立即触发录制 |

> 菜单状态与 `state_sequence` 末态一致：菜单每次弹出都从最新 `StateSnapshot` 重建；
> `round27-tray-menu.png` 对应日志中的 `snapshot state=Recording` → `preview_tray_menu`。

## 2. 最小化到托盘（M2-13）

| 步骤 | 期望 | 证据 |
| --- | --- | --- |
| 点窗口的最小化按钮，或点「最小化到托盘」按钮 | 主窗口从任务栏消失；托盘图标保留 | `round27-minimized.png`（桌面已无主窗口）+ 日志 `minimized_to_tray` |
| 双击托盘图标（或菜单「显示主窗口」） | 窗口恢复原位 | 人工确认；自动化用 `ShowWindow(SW_SHOW)` 验证可恢复（`visible_after_restore=True`） |

本次截图对应的日志片段：

```
t=... minimized_to_tray
t=... exit_timer_fired
t=... app_exit code=0
```

## 3. 热键冲突可见（人工复核）

自动化已断言日志 `hotkey_register_failed id=start ... error=1409`，并确认 pause/stop 仍注册。
人工复核：启动时若某个默认热键被占用，主窗口状态栏第二行显示
`⚠ Ctrl+Alt+Fxx 注册失败（已被占用）`，托盘提示追加「（热键冲突）」。

## 4. 录制中关闭应用

自动化 D 段已覆盖：`--exit-after-ms` 触发的 `WM_CLOSE` 会先发 Stop、等 Recorder 收尾、
再退出；日志出现 `shutdown_done exit=0 output_exists=1`，成品可播，无 `sr-recorder` 残留。

## 5. 不在本清单的项

- 开始/暂停/恢复/停止热键、M2-01 时延、成品可播、无残留 → `ui_smoke.ps1` 自动断言。
- 保存路径/命名模板/主窗口计时排版 → 轮 28（UI-2）。
- 开录前自检、区域选择、联调 → 轮 29（UI-3）。
