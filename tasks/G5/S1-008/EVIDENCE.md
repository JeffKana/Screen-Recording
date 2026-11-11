# EVIDENCE: S1-008（窗口采集/尺寸跟随的 S1 侧改动承接）

## 0. 结论

| 项 | 结论 | 证据 |
| --- | --- | --- |
| S1 侧改动（recorder 入口 / ComPtr 持有 / 探针窗口）有据可查 | PASS | E1 |
| M1-08#1 窗口尺寸跟随 | PASS | E2 |
| M1-08#2 两档宽高比策略 | PASS | E3 |
| M1-09#1 被遮挡内容 | PASS | E4（由 S2-002 卡承担，本卡只承接工具侧） |

## 1. 唯一证据来源与可重跑命令

```
pwsh -NoProfile -File verify/cases/g5_window.ps1 -BuildDir build-msvc -Config Release
```

本轮实跑（2026-11-08，build-msvc / Release）：

```
--- 1 目标窗口被另一个窗口遮挡（fit=pad）---
  PASS m1-09-host-window — 自建窗口 hwnd=5773426
  PASS m1-09-record-exit — 录制退出码=0
  PASS m1-09-no-dropped — 丢帧=0 / 采集=256
  PASS m1-09-frame-read — 成片尺寸 800x600，抽帧结果：ok
  PASS m1-09-content-still-visible — 被遮挡时绿色占比=96.0%（期望 >50%）
  PASS m1-09-occluder-not-captured — 成片里遮挡物（红）占比=0.0%（期望 <2%）
  PASS m1-09-content-complete — 蓝色边框包围盒 x=[0,799] y=[0,599]；内容包围盒 x=[0,799] y=[0,599]
--- 2 窗口 800×600 → 1280×720（fit=pad）---
  PASS m1-08-host-window — 自建窗口 hwnd=8720026
  PASS m1-08-record-exit — 录制退出码=0
  PASS m1-08-fit-mode-applied — fit_mode=pad（期望 pad）
  PASS m1-08-source-follows — 源尺寸变化次数=1，最终源尺寸=1280×720（期望 ≥1 次、1280×720）
  PASS m1-08-no-dropped — 丢帧=0 / 采集=359
  PASS m1-08-frame-read — 成片 800×600，抽帧结果：ok
  PASS m1-08-content-complete — 蓝色边框包围盒 x=[0,799] y=[76,525]；内容包围盒 x=[0,799] y=[76,525]
  PASS m1-08-fit-pad-black-bars — pad 黑边 上=76px 下=74px（期望 75±2px；黑边占比=25.0%）
  PASS m1-08-no-black-frame-on-switch — 切换点 177~183 帧里的全黑帧：无（期望 0）
--- 3 同一场景（fit=stretch）---
  PASS m1-08-stretch-exit — 录制退出码=0
  PASS m1-08-stretch-fit-mode-applied — fit_mode=stretch（期望 stretch）
  PASS m1-08-stretch-source-follows — 源尺寸变化次数=1
  PASS m1-08-fit-stretch-no-bars — stretch 黑边占比=0.2%（期望 <2%）
  PASS m1-08-stretch-content-complete — stretch 蓝色边框包围盒 x=[0,799] y=[0,599]；内容包围盒 x=[0,799] y=[0,599]
--- 4 每秒切换一次尺寸（压「反复 resize」路径）---
  PASS m1-08-cycle-exit — 录制退出码=0（期望 0：反复 resize 不得崩）
  PASS m1-08-cycle-resizes — 尺寸变化次数=24（期望 ≥20 次）
  PASS m1-08-cycle-no-dropped — 丢帧=0 / 采集=765

RESULT: 24 PASS / 0 FAIL / 0 SKIP
```

（与验收方独立复跑结果一致：24 PASS / 0 FAIL / 0 SKIP。）

## 2. 一条子句一条证据

### E2 · M1-08#1（窗口尺寸跟随）

- **判据**：拖动窗口从 800×600 到 1280×720，成片画面始终完整包含窗口内容；反复 resize 不崩。
- **实测**：源尺寸变化 **1** 次、最终 **1280×720**；丢帧 **0/359**；
  蓝色边框包围盒 x=[0,799] y=[76,525] 与内容包围盒完全贴合（内容未被裁剪）；
  切换点 177~183 帧**无全黑帧**；连续 resize **24** 次、退出码 **0**、丢帧 **0/765**。
- **可重跑**：同上命令（断言 `m1-08-*`）。

### E3 · M1-08#2（填充+黑边 / 拉伸 两档策略）

- **判据**：宽高比变化时按「填充 + 黑边」处理，且可配置为「拉伸」。
- **实测**：`fit_mode=pad` 时黑边 上=**76px**、下=**74px**（理论 75，±2px 内），黑边占比 25.0%；
  `fit_mode=stretch` 时黑边占比 **0.2%**（<2%），内容包围盒与边框贴合。
- **可重跑**：同上命令（断言 `m1-08-fit-*`）。

### E4 · M1-09#1（被遮挡内容）

- **判据**：被遮挡时录到的仍是目标窗口内容。
- **实测**：绿色占比 **96.0%**（>50%）、遮挡物红色占比 **0.0%**（<2%）、丢帧 0/256。
- 说明：本子句的**实现**在 `src/capture/`（S2 卡），本卡只承接工具侧（探针窗口）。

## 3. 承接的改动清单与落点

| # | 改动 | 文件 | 本轮是否再改 |
| --- | --- | --- | --- |
| 1 | 会话用 `ComPtr` 持有上一帧纹理（0xC0000005 修复） | `src/recorder/RecordingSession.cpp/.h` | 否（轮 39 已改，本卡只是归位承接） |
| 2 | `--window <hwnd>` 入口 | `src/recorder/main.cpp` | 否 |
| 3 | `--fit <fit\|pad\|stretch>` 入口 | `src/recorder/main.cpp` | 否 |
| 4 | 探针窗口蓝色边框 + `--resize-cycle` | `src/tools/srprobe/window_host.cpp` | 否 |
| 5 | 探针窗口 `--hwnd-file`（M1-27 验收要用句柄指认高完整性目标窗口） | `src/tools/srprobe/window_host.cpp` | **是**（轮 40 新增，4 行改动） |

## 4. 已知限制

1. 探针的 `--hwnd-file` 写的是**十进制**句柄（提权实例的 stdout 拿不到）。
2. `--resize-cycle` 依赖探针自己定时改尺寸；录制的 recorder 必须与探针**同用户会话**运行。
