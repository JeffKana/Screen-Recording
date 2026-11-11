# EVIDENCE: S5-005（画质档位联动/可见 + 背压可见标识）

> 口径：执行方证据，非验收结论。数据 2026-11-09，`build-msvc/bin/Release`。

## M3-10#1 切换档位后各参数联动变化且可在高级面板单独覆盖

- 命令：`pwsh -File verify/cases/g5_r42_closeout.ps1`（M3-10#1 段）
- 输出：`tasks/G6/V1-003/artifacts/g5_r42_closeout.ps1.txt` — 3 条断言全 PASS：
  1. `m3-10-01-preset-links-three-params`：四档联动值互不相同
     （极致 60fps/12000kbps/q90、高清 30/6000/70、标准 30/4000/60、省空间 15/2000/50），
     每次都写 `preset_saved` 日志且退出码 0；INI 由产品 API 落盘并回读校验。
  2. `m3-10-01-preset-values-monotonic`：极致三项都高于省空间。
  3. `m3-10-01-advanced-panel-override`：`--set-fps 45` 单独把帧率改成 45 并落盘，
     档位名仍在配置中、未被预设强行回写。
- 实现：`AppConfig::ApplyQualityPreset()` / `SaveQualityPreset()`；设置页 `CBN_SELCHANGE`
  联动 fps/码率/画质控件并写 `preset_linked` 日志；`RecorderController` 写
  `preset_dispatched fps=… bitrate_kbps=…` 证明联动值真的下发到录制进程。

## M3-10#2 预设值在界面可见

- 输出：3 条断言全 PASS：
  1. `m3-10-02-preset-seed-ok`：产品 API 落盘档位（退出码 0 + `preset_saved name=极致` 日志）。
  2. `m3-10-02-preset-visible`：设置页只读文本（控件 109）
     「当前预设：极致 · 60 fps · 12000 kbps · 画质 90（高级面板可单独覆盖）」。
  3. `m3-10-02-preset-name-not-garbled` + `m3-10-02-preset-combo-present`：
     档位名正确显示（非乱码）、下拉（控件 108）选中序号 0。
- 首轮实测到乱码「鏋佽嚧」与选中序号 4：根因是用例用 UTF8 手写中文 INI，
  Windows profile API 按 ANSI 读。改为由产品自己的 `--set-preset` 落盘后正常，
  并新增「非乱码」断言防回归。**这是用例取数缺陷，产品行为本身正确**。

## M6-11#2 出现背压时有可见标识

- 输出：2 条断言全 PASS：
  1. `m6-11-02-backpressure-visible`：`--simulate-backpressure 7` → 状态栏首行
     「⚠ 背压（丢帧 7）」。
  2. `m6-11-02-no-false-backpressure`：不注入时状态栏无「背压」。
- 实现：`UpdateUi()` 用 `snapshot.metrics.frames_dropped + backpressure_inject` 判断是否追加标识；
  注入只用于触发**同一条 UI 渲染分支**，不是把日志文案冒充界面。
  正常录制（dropped=0）状态栏两行文本保持不变，`g5_closeout` 的 M6-11#1 正则不受影响（回归已验）。

## 已知限制

- M3-10#1 联动 fps / 码率 / 画质三项，**分辨率不随档位缩放**（需求描述提到分辨率，
  但验收要点原文只要求「各参数联动变化」）；该限制在 TASK.md / HANDOFF 显式标注。
- M6-11#2 是**代理验证**：注入丢帧走同一 UI 分支，产品无队列深度字段，非真实队列积压测量。
- 三构建零告警；设置页窗口高度 500 → 570 以容纳新控件，`ui_smoke` 3×103 回归见主卡。
- 执行方不自评为已验收。