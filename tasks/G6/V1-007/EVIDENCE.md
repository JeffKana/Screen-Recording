# EVIDENCE: V1-007（轮 43 · F1 CBR 过冲定性 + 发布预演，主卡）

> 口径声明：本文件是**执行方证据**，不是验收结论；验收方独立重跑后判定 PASS/PARTIAL/FAIL。
> 数据来源：2026-11-11，`build-msvc/bin/Release`（SR_TEST_HOOKS=ON）、
> 本机 Win11 家庭版 build 26200 / 双屏 1920×1080 / 默认 100% DPI / HVCI Enabled=1。
> 所有文本证据在 `tasks/G6/V1-007/artifacts/` 与 `tasks/G5/S4-004/artifacts/`，均为当次命令的真实输出。

## 0. 开工四项自查（`docs/06 §4.2`）

| 检查项 | 结果 | 依据 |
| --- | --- | --- |
| 需求编号可检索 | 通过 | M3-04:106、M1-24:56、M7-13:237、M1-27 均能在 `docs/01` 查到原文；逐条核过验收要点 |
| 必读章节可定位 | 通过 | `.codebuddy/plans/轮43指令.md` §1~§5、验收报告-轮42、`docs/05 §2.4/§7.2`、`docs/06 §4.2/§8.4`、`docs/11b §6`、`CONTRACT §2` 均在位 |
| 上游交付物可获取 | 通过 | 轮 42 六套交付包、`build-msvc`/`build-release`/`build-mingw` 三套产物、`dist/*.zip`、验收方 58% 现场样本 `verify/artifacts/format-20261110-183539/cbr.mkv` |
| 文件域无交集 | 通过 | 7 张卡目录互斥（主卡 verify 定性工具 + docs；S4-004 `src/encode/`；V1-008 `g5_format.ps1`；V1-009 纯状态声明；V1-010 收口用例；S1-011 `src/recorder/`；S5-006 `src/app/`）。`domain_table_check` MISMATCH_COUNT=0 |

> 本轮 F1（产品）与 F2（用例）**分两次独立改动**，各自留证据（轮 43 指令 §4.1）。
> F1 定性阶段未改任何产品代码，也未改 `g5_format` 判据。

---

## 1. P0 F1 定性：CBR 逐秒过冲（不是"负载抖动"）

- 命令：`pwsh -File verify/cases/g5_cbr_probe.ps1 -Seconds 40 -Runs 5 -Mode both`
- 输出目录：`tasks/G6/V1-007/artifacts/cbr-probe-20261111-102548/`（10:25:48 – 10:33:18）
  - 逐次 `*-*.persec.csv`（逐秒表）、`*-*.metrics.txt`（全量 metrics 39 行）、`*-*.out`（stdout 汇总）、
    `*-*.nal-*.json`、汇总 `probe-summary.txt`
- 逐秒口径与 `g5_format.ps1::Get-PerSecondStats` **完全一致**（按 `pts_time` 分桶、
  跳过第 0 秒与最后一秒、`spread=(max-min)/avg`、阈值 ≤10%）。

| 模式 | 5 次 spread | 超 10% |
| --- | --- | --- |
| 静态桌面 | 359.0% / 0.0% / 59.0% / 196.5% / 247.0% | **4 / 5** |
| 全屏运动源 testsrc2 | 15.2% / 17.5% / 26.4% / 16.9% / 12.1% | **5 / 5** |

- 每次 `record_exit=0`、`frames_encoded=1200`、`dropped=0`、metrics 39 行 → 排除取数失败。
- 判据（指令 §1）：静态 ≥2 次超 10% → **M3-04#1 回退 PARTIAL，成立**。
- 环境限制如实标注：静态的极端值（359% / 247%）受本机 IDE/终端窗口重绘影响，
  但运动源 5/5 稳定超 10% 与静态的 59%（吻合验收方复现的 58%）不依赖桌面活动。

### 1.1 关键帧秒字节分解（NAL 级，非推断）

- 命令：`python verify/tools/cbr_nal_probe.py <mkv> <second> --ffprobe <path> --json <out>`
- 工具按 ffprobe `packet.pos+4` 直读 MKV 的 AVCC 载荷，逐 NAL 分类
  （type 5 → `keyframe_bytes`、type 12 → `filler_bytes`、其余 → `other_bytes`），
  `decoded_bytes == probe_bytes` 才判解析完整（实测 30/30 包全覆盖）。

| 样本 | 秒 | 总字节 | kbps | keyframe | filler | other |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `static-02`（PASS 0.0%） | 3 | 750,000 | 6,000 | 426,434 | **283,762** | 39,804 |
| `static-01`（359%） | 10 | 3,899,710 | 31,198 | 301,063 | **0** | 3,598,647 |
| `static-03`（59%） | 21 | 1,199,369 | 9,595 | 330,113 | 10,228 | 859,028 |
| `motion-03`（26.4%） | 31 | 958,454 | 7,668 | 54,522 | **0** | 903,932 |

- **根因**（`src/encode/MfVideoEncoder.cpp::PadToTargetBitrate()` 修改前 :573-615）：
  按帧序号线性预算，关键帧/大 P 帧一旦把 `current` 顶过 `budget`，本秒余下帧全不补 filler；
  换秒时 `pad_bytes_in_window_=0`，「本秒已超」被直接丢弃、无结算。
- 工具开发中的两处取数缺陷（非产品问题，已修并留痕）：
  ① `ffprobe -read_intervals -show_data` 只取到 5/30 包 → 改按 `pos+4` 直读；
  ② `pos` 指向 MKV SimpleBlock 头（4 字节），需跳过才是 AVCC 载荷。

## 2. P0 F1 修复：**已尝试，实测不适用，已回滚**

- 实现（`tasks/G5/S4-004`）：按指令 §2 把补偿口径从「帧」改到「秒」——
  每帧按 `本秒剩余预算 / 本秒剩余帧数` 重算 filler 额度，换秒时显式记账超额；
  `ApplyRateControl()` 的 mean/max/buffer/quality **一个参数未改**。
- 三构建零告警后，在**同一组 5+5 次 CBR 40s** 上验证
  （`tasks/G5/S4-004/artifacts/cbr-after-fix/probe-summary.txt`，11:00:03 – 11:07:28）：

| 模式 | 定性前（旧口径） | 按秒结算后 |
| --- | --- | --- |
| 静态 5 次超 10% | 4/5 | 3/5（84.4% / 295.2% / 108.4% / 1.5% / 3.3%） |
| 运动 5 次超 10% | 5/5 | **5/5**（19.3% ~ 24.9%） |
| 运动 avg | 6,276 kbps（偏差 4.6%） | **8,039 kbps（偏差 34%）** ← 新回归 |

- **未达成「10 次全 ≤10%」，且引入新回归**：运动 avg 超标称 34%，会额外触发
  `m3-04-cbr-nominal`（≤20%）FAIL。
- 物理原因：filler 是**只增不减**的 H.264 NAL。某秒真实编码输出（IDR + 运动 P 帧）
  本身已超 750,000 B 时，任何 filler 口径都无法把已写入容器的字节削回来。
- **处置**：回滚 `PadToTargetBitrate()` 与相关字段到轮 42 原状（不保留让产品变差的改动），
  回滚后三构建再次零告警；`M3-04#1` 保持「有卡未实现」。
  10 次实测作为「指令指定的修法已尝试、经实测不适用」的红绿证据保留。
  缺陷单 `docs/17` 给出待验收方裁定的 A/B/C/D 四个方向（含是否允许动码控层）。
- **执行方不假绿、不为保住 104 拖着**（指令 §4.2）。

## 3. P0 F2 取数失败分离

- 归属卡 `tasks/G6/V1-008`；`artifacts/f2-selftest.txt` **17 PASS / 0 FAIL**。
- 判据（指令 §3 #2）：`g5_format.ps1 -SimulateNoStart` →
  `SKIP 数据未取到 — fps60：stdout 为空…（attempts=2）` + stdout 尾 20 行、
  `RESULT: 0 PASS / 0 FAIL / 1 SKIP`、退出码 **0**。
- 护栏：数据齐但 `record_exit=6` → **1 FAIL / 0 SKIP / 退出码 1**（产品失败不伪装 SKIP）；
  7 条「判据未改软」文本护栏全通过（≤10% / ≤20% / ≤102% / 逐秒公式 / 分桶公式）。
- 红证据（自然复现）：`verify/artifacts/format-20261110-183428/summary.json`
  `pass=4 fail=10`（`.part=108B`、`stdout=0B`）。
- 首轮自检 8/9 FAIL 的根因是 `$Args` 与 PowerShell 自动变量冲突导致 splatting 为空
  （用例真的启动了 recorder）；改名 `$CaseArgs` 后 17/17。属**自检取数缺陷**，判据未放宽。

## 4. P0 状态迁移：M3-04#1 回退（第 1 次 `--check`）

- `tasks/G6/V1-009/TASK.md` 声明「有卡未实现」→ `coverage_report.py --write` →
  统计 **104/1/11 → 103/2/11** → `--check` **PASS**、`contract_check` C8 **8/8 PASS**。
- 挂账理由手工填进 `docs/09` 最后一列（生成器 `load_existing_reasons` 保留该列）。
- 首次 `--write` 曾触发 R5「两张卡同状态不同理由」冲突（S4-004 与 V1-009 都声明），
  改为**状态由 V1-009 独占声明**、S4-004 写「无」后消除。
- 首次手工填理由后 `--check` 报 stale，再跑一次 `--write` 让生成器吸收后 PASS。
- **翻绿未执行**：F1 修复未达成 10 次全 ≤10%，按卡内条件保持回退。
  两次 `--check`（回退 + 翻绿）判据**只完成了第 1 次**，如实报 PARTIAL。

## 5. P1 无卡收口 4 条（覆盖 107 / 2 / 7）

- 命令：`pwsh -File verify/cases/g5_r43_closeout.ps1` →
  `tasks/G6/V1-007/artifacts/r43-closeout.txt` **12 PASS / 0 FAIL / 0 SKIP**。
- 归属卡 `tasks/G6/V1-010`（实现卡 S1-011 / S5-006 各自留域）。逐条：

| 子句 | 断言与实测值 |
| --- | --- |
| M1-24#1 | `--cursor off` → `include_cursor=0`；受控纯色全屏背景下成片中心 60×60 非背景像素 **0**，全画面抽样 **0 / 518,400**（开启态 35）。真实录制 + 成片像素级验证，非代理 |
| M1-24#2 | `--cursor on` 鼠标移到 A / B 两点各录一段：A 段 A 点 35、B 段 B 点 35、B 段 A 点 **0**（无残影）；`include_cursor=1` |
| M7-13#1 | 自绘托盘图标**像素自检** `tray_icon red_dot_pixel=0xFF0000FF base_pixel=0xFFC08020`；录制中边框窗口 `hwnd≠0 visible=True`，屏幕边缘 RGB=(255,0,0)；`--record-border off` → `visible=False` + 日志 `state=hidden border=0` |
| M7-13#2 | 暂停态边框 RGB=(240,240,240) 与录制态 (255,0,0) 可区分；日志 `state=paused border=1` |

- P1 用例开发中修的三处取数/实现缺陷（均留痕）：
  ① ffplay `-WindowStyle Hidden` 导致纯色窗口没显示 → 改独立 WinForms 进程（抽帧中心由
  (11,11,11) 变 (9,148,4) ≈ 绿）；② WGC 只在鼠标移动时刷新光标层 → 录 on 段前抖动一次；
  ③ `indicator_state` 初值 0 使 `--record-border off` 被判「无变化」不输出日志 → 改 -1。
- **降级/代理标注**：M7-13 的状态由 `--simulate-record-state` 注入，走同一条 UI 渲染分支
  （与轮 42 M6-11#2 同族），**非真实录制会话**；M1-24 是真实录制 + 像素级验证，
  但「全画面」为抽样（步长 2）而非逐像素，已在断言文案写明。
- 覆盖表：`--write` 后 **107 已实现 / 2 有卡未实现 / 7 无卡**，`--check` PASS，
  契约 8/8。判据「≥107 / ≤8」达成。

## 6. P1 矩阵补齐（100% 单屏 × 两个后端）

- 命令：`pwsh -File tasks/G6/V1-007/artifacts/run-r43-matrix.ps1`
- 输出：`artifacts/r43-matrix.txt`（见 `SELFTEST.md §3` 与 `docs/14 §3/§4`）。
- 轮 42 已实测「Win11 24H2 单屏 100%」一格（6/6，2026-11-09，默认 wgc 后端）；
  本轮补两个后端各 60s 三件套（录 60s → 抽 3 帧 → 音画偏差 → 双轨解码）。
- **只填能测的**：150% DPI、Win10、23H2、三厂商硬编等格继续标「未测」（指令 §4.5）。

## 7. P1 M1-27 终审材料

- `docs/18-轮43-M1-27终审材料.md`（一页纸）：提示/入口已实现证据、
  黑屏 BLOCKED 的环境依据（`ConsentPromptBehaviorAdmin=0` / `PromptOnSecureDesktop=0`、
  PrintWindow `frames=0` 的 UIPI 旁证）、6 步人工复测步骤、验收方核对清单。
- 明确「登记 EX-01 ≠ 放宽 `docs/05 §7.2` 六条判据」，M1-27#1 仍未翻绿。

## 8. 门判据检查

| 检查 | 命令 | 结果 |
| --- | --- | --- |
| 覆盖表 | `python verify/tools/coverage_report.py --check` | `up to date`；**107 / 2 / 7** |
| 契约 C1~C8 | `python verify/tools/contract_check.py` | **8 PASS / 0 FAIL** |
| 卡面（含 F2 规则） | `python verify/tools/card_consistency_check.py` | **49 卡 0 冲突** |
| 域表 | `python verify/tools/domain_table_check.py` | `MISMATCH_COUNT=0` |
| 三构建 | `artifacts/run-build.ps1` | msvc-release / msvc-debug / mingw-release 全部 `exit=0 warnings=0 errors=0` |
| F2 自检 | `verify/cases/g5_format_selftest.ps1` | 17 PASS / 0 FAIL |
| P1 收口 | `verify/cases/g5_r43_closeout.ps1` | 12 PASS / 0 FAIL / 0 SKIP |
| 全量回归 | `artifacts/r43-regression.txt`（11:27:19 – 11:44:04） | `g5_storage` 14、`g5_r42_closeout` 12、`g5_window` 24、`g5_uac` 17/1SKIP、`g4_gate` 12、`g3_*` 9/11/11、`g5_encoder_list` 17、`g5_tempfile` 10、`g5_estimate_selftest` 9、`g5_longwindow_selftest` 10、`g5_f2_selftest` 4、`g5_format_selftest` 17、`ui_smoke` 3×103 全绿；**`g5_format` 3 次 = 14/14、14/14、13/1（真实 CBR 过冲 154.7%）**；**`g5_r43_closeout` 回归首轮 11/1（用例时序，修后独立 3×12/12）**。见 `SELFTEST.md §3` |
| 发布包 | `pwsh -File verify/tools/build_release.ps1` | **17 PASS / 0 FAIL**，zip 346,340 B，包内/构建目录 SHA 相同 |

## 9. 已知限制（不得计为通过）

1. **F1 修复未达成，M3-04#1 保持「有卡未实现」**（回退后未翻绿）；
   指令 §3 #1 的「结论与覆盖率动作一致」已做（照实回退），但 §3 #3
   「`g5_format` 连跑 3 次全绿」与「修后 10 次全 ≤10%」**未达成**，判 PARTIAL。
   回归里 `g5_format` 第 3 次 `13/1` 的 `m3-04-cbr-spread`（逐秒 6,000~16,699kbps、
   spread 154.7%）是**真实产品失败**，不是取数问题 —— F2 已把取数失败分离成 SKIP，
   这条恰恰证明两件事被正确区分了。
1b. **`g5_r43_closeout` 回归首轮 11/1**：WGC 光标层在两段录制之间偶发不刷新，
   修法为 A/B 段各自录前抖动鼠标；独立连跑 **3×12/12 全绿**
   （`artifacts/r43-closeout-x3.txt`）。属用例时序缺陷，判据未放宽。
2. **状态迁移只做了回退一次 `--check`**，翻绿那次因修复不成立而未执行。
3. M7-13 是注入状态的代理验证；M1-24 全画面为抽样。
4. M3-04 的根因在码控层瞬时输出，需要验收方从 `docs/17 §6` 的 A/B/C/D 中裁定方向；
   执行方未自行改 CBR 模式实现、未自行改需求判据。
5. **V1.0 仍不可发布**：7 条无卡、M3-04#1 PARTIAL、M1-27 待终审、M6-06 BLOCKED、
   矩阵仍有 11 格未按三件套实测、`docs/05 §7.2` 其余判据未跑完。
6. 执行方不自评为已验收；轮 43 主表行留给验收方独立重跑后填写。
