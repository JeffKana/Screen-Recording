# TASK: S5-006 轮 43 · M7-13 录制中状态提示（托盘红点 + 可选屏幕边框）

阶段：G5｜角色：S5｜轮次：43｜来源：`.codebuddy/plans/轮43指令.md` §1 P1

## 需求范围
M7-13（`docs/01:237`）：「录制中状态提示：托盘红点、屏幕边缘录制边框」；
验收要点「录制中托盘图标带红点，可选显示屏幕边框；暂停时边框样式与录制中可区分」。

## 文件域（白名单）
- `src/app/main.cpp`（自绘 HICON、分层边框窗口类、`UpdateRecordIndicator`、
  `--record-border on|off`、验收注入 `--simulate-record-state`、help）
- `tasks/G5/S5-006/*`（本卡四件套）

> 不改 `verify/`（断言在 `tasks/G6/V1-010`）、不改 `CONTRACT.md`、`vcpkg.json`。
> 不引入三方依赖（自绘图标用 GDI）。

## 约束
1. 托盘「红点」要有**像素级自检日志**（`tray_icon red_dot_pixel=0xFF0000FF`），
   不拿文案冒充功能。
2. 边框窗口 `WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_TOPMOST`，黑透明；
   录制中红色粗实线、暂停黄色细虚线，两态颜色/样式可区分。
3. 「可选」由 `--record-border off` 控制；默认 on。
4. `--simulate-record-state` 只在 `SR_TEST_HOOKS` 构建暴露，走**同一条 UI 渲染分支**；
   验收证据须标注「代理验证：注入状态，非真实录制会话」。
5. 三构建零告警；`ui_smoke` 3×103 不回归（正常态状态栏文本不变）。

## 完成定义（DoD）
- [ ] `g5_r43_closeout.ps1` 的 M7-13 四条断言全绿（含红点像素、边框可见/红色、
      暂停可区分、可关闭）
- [ ] 三构建零告警、`ui_smoke` 3×103 通过
- [ ] 四件套齐全

## 覆盖子句

无（实现卡，状态由 `tasks/G6/V1-010` 统一声明）。

## 交付状态

无（本卡不声明需求子句）。