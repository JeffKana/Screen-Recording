# EVIDENCE: V1-015（P1 矩阵补 2 格 + M1-27 终审材料定稿）

> 执行方证据，非验收结论。数据 2026-11-11。
> 命令：`pwsh -File tasks/G6/V1-015/artifacts/run-r44-matrix.ps1`
> 输出：`tasks/G6/V1-015/artifacts/r44-matrix.txt`（13:32:45 – 13:34:54）。

## 一需求一证据（两条 P1 项各一条）

### ① 矩阵补格

- 环境实测：`OS=Microsoft Windows 11 家庭版 中文版 build=26200 DPI=100%
  main=1920x1080 screens=2`（`r44-matrix.txt` 的 ENV 行）。
- **格 1：Win11 24H2 × 双屏 × 100%**（轮 42 是「降级旁证」，本轮升级为三件套实测）：

| 项 | 实测值 |
| --- | --- |
| 录制 | 不传 `--display`（整屏采集），`backend=wgc`、`record_exit=0`、wall=**60.05s**、frames_encoded=**1800**、frames_dropped=0 |
| 抽帧 | n=30/900/1770 三帧非全黑 **3/3** |
| 音画 | `video_last_pts_ms=59999` / `audio_last_pts_ms=60000` → 尾偏差 **1ms** |
| 容器 | duration=**59.999s**、video+audio 双轨、`ffmpeg -f null` **零错误** |
| 结论 | **PASS（2026-11-11）** |

- **格 2 / 单屏 100% 的第二后端 `dxgi`**：`--display 0 --backend dxgi` →
  `backend=dxgi`（**真的强制命中**）、`record_exit=0`、60.03s、1800 帧、dropped=0、
  1ms、3 帧非全黑、duration 59.999s、双轨、解码零错误 → **PASS（2026-11-11）**。
- **纠正轮 43 的一处错误结论**（重要，不留错账）：轮 43 写「`--backend dxgi-dd`
  自动回退至 wgc，该后端未测」。实际是**参数名用错**：
  `src/capture/CaptureFactory.cpp:60-73` 的 `Equals(preferred, ...)` 只认
  `wgc` / `dxgi` / `printwindow`；`dxgi-dd` 不匹配任何分支 → 落到
  `else { add_wgc(); add_dxgi(); }` 默认链（wgc 首选）→ 返回 `backend=wgc`。
  本轮用正确的 `dxgi` 后一次命中。更正已同步 `docs/14 §3/§4` 与 `docs/07` 轮 44 记录。
- 判据 §2 P1「每格有实测值 + 日期，或明确未测」：两格都有 2026-11-11 实测值；
  其余 **10 格继续标 `未测`**（150% DPI / Win10 / 23H2 / 三厂商 / 混合 DPI 等），
  按指令 §3 #4 不拿降级版本冒充。
- 降级标注：单屏格用 `--display 0` 只采主屏代表单屏（本机物理双屏）；
  双屏格是本机真实双屏，不属降级。

### ② M1-27 终审材料定稿（`docs/18`）

- 命令/产物：`docs/18-轮43-M1-27终审材料.md`（轮 44 定稿）。
- 新增 **§0「EX-01 适用范围」**：明确只覆盖「UAC 安全桌面弹窗黑屏一侧」，
  **不免除** ① 未提权提示 ② 管理员重启入口 ③ `docs/05 §7.2` 其余 5 条 ④ M1-27#1 覆盖表状态；
  写明「登记 ≠ 放宽判据、≠ PASS、≠ 翻绿」，并给出适用环境边界
  （`ConsentPromptBehaviorAdmin=0` / `PromptOnSecureDesktop=0`）。
- 新增 **§3.1「复测条件与判定门槛」**：4 项前提（干净机且 UAC 落安全桌面、
  使用发布包 `test_hooks=0`、弹窗期间确实在录、抽帧覆盖三阶段）+ 3 条判定
  （复现黑屏 → EX-01 不成立；弹窗可见且入口有效 → 交验收方裁定；前提不满足 → 维持 BLOCKED）。
- 原 §1~§4（一页纸结论、环境依据、6 步人工复测、终审清单）保留。
- 供轮 45 发布复核直接引用；**执行方未做终审、未翻绿 M1-27#1**。

## 已知限制
- 本机只有 Win11 build 26200 一个 OS 版本、Intel UHD 770 一个物理 GPU，
  矩阵其它格的环境前提不成立；继续标未测。
- M1-27 的 §3 人工复测需安全桌面机器，本轮**没有执行**（本机
  `ConsentPromptBehaviorAdmin=0` / `PromptOnSecureDesktop=0` 不可复现）。
- 本卡不声明需求子句；矩阵 runner 只在 `tasks/G6/V1-015/artifacts/`，未改 `verify/cases/`。
- 执行方不自评为已验收。