# EVIDENCE: V1-010（P1 无卡收口 4 条）

> 执行方证据，非验收结论。数据 2026-11-11。
> 命令：`pwsh -File verify/cases/g5_r43_closeout.ps1`
> 输出：`tasks/G6/V1-007/artifacts/r43-closeout.txt` — **12 PASS / 0 FAIL / 0 SKIP**。

## 一需求一证据

| 子句 | 验收要点原文 | 实测值 |
| --- | --- | --- |
| M1-24#1 | 关闭后成片任何位置都不出现鼠标指针 | `--cursor off` → `record_exit=0 include_cursor=0`；受控纯色全屏背景下成片中心 60×60 非背景像素 **0**、全画面抽样 **0 / 518,400**（容差 ±36）；开启态中心 35 |
| M1-24#2 | 开启后指针位置与操作一致、无残影 | `--cursor on`：`include_cursor=1`；鼠标移到 A 点录一段（A 区域非背景 35），移到 B 点再录一段（B 区域 35、**A 区域 0**） |
| M7-13#1 | 录制中托盘图标带红点，可选显示屏幕边框 | 自绘图标像素自检日志 `tray_icon red_dot_pixel=0xFF0000FF base_pixel=0xFFC08020`；录制中边框窗口 `hwnd=16256406 visible=True`，屏幕边缘 RGB=(255,0,0)；`--record-border off` → `visible=False` + `state=hidden border=0` |
| M7-13#2 | 暂停时边框样式与录制中可区分 | 暂停态 RGB=(240,240,240) 与录制态 (255,0,0) 不同；日志 `state=paused border=1`；代码上暂停用黄色细虚线、录制用红色粗实线 |

## 实现与判据

- 产品改动：`tasks/G5/S1-011`（`--cursor on|off` 透传到 `CaptureRequest::include_cursor`）、
  `tasks/G5/S5-006`（自绘 HICON + 分层边框窗口 + `--record-border` + 注入开关）。
- 判据未被改软：像素阈值（背景容差 ±36）、非背景计数、窗口可见性、颜色区分都是本轮新增的
  **硬断言**；没有放宽任何既有判据。
- 覆盖表：`--write` → **107 已实现 / 2 有卡未实现 / 7 无卡**；`--check` PASS、契约 8/8。
  判据「≥107 / ≤8」达成。

## 用例开发中修的四处缺陷（留痕，非产品缺陷）
1. ffplay `-WindowStyle Hidden` 让纯色窗口没显示 → 抽帧中心 (11,11,11)，全部像素判定失效；
   改独立 WinForms 进程（`-STA`）后中心 (9,148,4) ≈ 绿，判定生效。
2. WGC 只在指针移动时刷新光标层：`--cursor on` 静止鼠标录到 0 个非背景像素，
   而移动过的 A/B 段各 35；录 on 段前抖动一次触发重绘（判据未变）。
3. `indicator_state` 初值 0 使 `--record-border off` 被判「无变化」不输出日志 → 改 -1。
4. **回归首轮 `11 PASS / 1 FAIL`**：`m1-24-02-pointer-follows-position` 的 B 段 B 点 = 0，
   根因仍是 WGC 光标层在两段之间偶发不刷新。修法：A/B 段**各自**录前抖动一次。
   修后独立连跑 **3 × 12 PASS / 0 FAIL**（`tasks/G6/V1-007/artifacts/r43-closeout-x3.txt`）。
   判据未放宽。

## 已知限制（降级/代理标注）
- **M7-13 两条是代理验证**：状态由 `--simulate-record-state` 注入，走同一条 UI 渲染分支
  （与轮 42 M6-11#2 同族），**不是真实录制会话**；真实录制下的托盘/边框切换未专项实测。
- M1-24 的「全画面」是**抽样**（步长 2）而非逐像素；受控纯色背景下该抽样覆盖 518,400 点。
- 光标开关仅 WGC 桌面源支持（`supports_cursor_toggle=true`）；DXGI / 窗口源不支持，
  已在 `--help` 写明。
- 执行方不自评为已验收。