# EVIDENCE: S5-006（M7-13 托盘红点 + 可选屏幕边框）

> 执行方证据，非验收结论。数据 2026-11-11。

## 一条需求一条证据（配合 V1-010 的行为断言）

M7-13「录制中状态提示：托盘红点、屏幕边缘录制边框」；
验收要点「录制中托盘图标带红点，可选显示屏幕边框；暂停时边框样式与录制中可区分」。

- 改动（`src/app/main.cpp` 单文件）：
  - `MakeRedDotIcon()`：32×32 32bpp DIB + `CreateIconIndirect` 自绘蓝色圆底 + 右上红点，
    **并在函数内做像素自检日志**（`tray_icon red_dot_pixel=0xFF0000FF base_pixel=0xFFC08020`），
    不拿文案冒充功能；
  - `SrRecordBorderWindow`：`WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_TOPMOST|WS_EX_TOOLWINDOW`
    + `LWA_COLORKEY` 黑透明；WM_PAINT 画屏幕边缘矩形，
    录制中 `RGB(255,0,0)` / `PS_SOLID` / 6px，暂停 `RGB(255,200,0)` / `PS_DASH` / 2px；
  - `UpdateRecordIndicator()`：状态变化时切换显隐并输出日志；
  - `--record-border on|off`（产品开关，默认 on）、`--simulate-record-state recording|paused`
    （`SR_TEST_HOOKS` 注入，走同一条 UI 渲染分支）、help 两行。
- 命令与实测值（`tasks/G6/V1-007/artifacts/r43-closeout.txt`）：

| 断言 | 实测 |
| --- | --- |
| `m7-13-01-tray-red-dot-pixel` | `tray_icon red_dot_pixel=0xFF0000FF base_pixel=0xFFC08020` |
| `m7-13-01-border-visible-recording` | `hwnd=16256406 visible=True`，日志 `state=recording border=1 red_dot=1` |
| `m7-13-01-border-red` | 屏幕左侧边缘 RGB=(255,0,0) |
| `m7-13-02-border-distinct-when-paused` | 暂停态 RGB=(240,240,240) vs 录制态 (255,0,0)，日志 `state=paused border=1` |
| `m7-13-01-border-optional-off` | `--record-border off` → `visible=False` + `state=hidden border=0` |

- 红-绿对照：旧实现 `icon_active=LoadIconW(nullptr, IDI_INFORMATION)`（无红点）、
  无边框窗口 → 新实现自绘红点 + 可选边框 + 暂停区分。
- 修复的一处实现缺陷：`indicator_state` 初值 0 导致「默认关闭」被判无变化、
  不输出日志；改 -1 后 `--record-border off` 可验。
- 三构建零告警；正常态状态栏文本未改（`ui_smoke` 由主卡回归验证）。

## 已知限制
- **代理验证**：状态由 `--simulate-record-state` 注入，走同一条 UI 渲染分支，
  非真实录制会话；真实录制下托盘/边框切换未专项实测。
- 暂停态屏幕像素实测为 (240,240,240)（细虚线 + 透明采样点落在间隙），
  与录制态红色可区分；断言判的是「两态颜色不同 + 日志 state=paused」，
  不是「黄色像素必然命中某一点」。
- 执行方不自评为已验收。