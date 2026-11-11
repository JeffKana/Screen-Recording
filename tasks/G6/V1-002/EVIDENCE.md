# EVIDENCE: V1-002（轮 41 · 用例稳定化 + 挂账 + 发布前准备）

> 口径声明：本文件是**执行方证据**，不是验收结论。验收方独立重跑后判定 PASS/PARTIAL/FAIL。
> 降级/代理验证逐条显式标注；两处口径冲突另提裁定申请（`docs/15`、`docs/16`），不静默解释判据。
> 数据来源：2026-11-09，`build-msvc/bin/Release`（SR_TEST_HOOKS=ON）与
> `build-release/bin/Release`（SR_TEST_HOOKS=OFF），本机 Win11 家庭版 build 26200 / 双屏 1920×1080 / 默认 100% DPI。
> 所有文本证据在 `tasks/G6/V1-002/artifacts/` 下，均为当次命令的真实输出。

## 0. 开工四项自查（`docs/06-子智能体执行规程.md` §4.2）

| 检查项 | 结果 | 依据 |
| --- | --- | --- |
| 需求编号可检索 | 通过 | M1-27、M8-01、M6-10、M6-11、M3-15、M6-04、M8-05 均能在 `docs/01-功能需求清单.md` 查到原文；逐个核过验收要点（见 `tasks/G5/V1-004/TASK.md` 需求范围表） |
| 必读章节可定位 | 通过 | `docs/05 §7.2`、§3.1~3.3；`docs/06 §4.2`/§8.4；`docs/09` 头部；`docs/11 §6`；`CONTRACT.md §2`；`docs/12`（裁定先例）；`verify/lib/Cleanup.ps1` 均在位 |
| 上游交付物可获取 | 通过 | 轮 40 三份交付包（`tasks/G5/S5-003`、`tasks/G5/S5-004`、`tasks/G6/V1-001`）、两套构建产物、`dist/*.zip`、`dist/release-summary.json` 齐全；未用旧版本开工 |
| 文件域无交集 | 通过 | 同轮三卡目录互斥：V1-002 = `verify/` + `docs/` + `dist/` + `tasks/G6/V1-002/`；V1-004 = `verify/cases/g5_closeout.ps1` + `tasks/G5/V1-004/` + `docs/15`；S1-009 = `src/recorder/main.cpp` + `tasks/G5/S1-009/`。`domain_table_check` MISMATCH_COUNT=0 |

> 轮 41 下发时仓库无任务卡，按验收方 `.codebuddy/plans/轮41指令.md` 建卡，
> 沿用轮 40 `S5-004`「执行方按验收方指令建卡」的做法；指令原文是规划侧输入，未增删需求。

---

## 1. R1 `g5_storage` 假失败（P0）

- 验证方式：先写断言 → 在**未修复**实现上跑出红 → 实现修复 → 再跑出绿 → 连跑 5 次。
- 命令：`pwsh -File verify/cases/g5_estimate_selftest.ps1`
- 红：`artifacts/R1-red.txt` — 旧实现喂空输出后 `RESULT: 12 PASS / 1 FAIL / 0 SKIP`，
  其中 `FAIL m8-05-01-estimate-error-20pct — 估算=0s 实测口径=0s 误差=100.0%
  （实测码率=0 B/s, free=0 B, bytes_written=0）`；新写的 4 条语义断言全 FAIL，子进程 `EXIT=1`。
  **旧语义的 100% 是自然复现，不是人为构造**（残留/单实例锁导致字段全 0）。
- 绿：`artifacts/R1-selftest-final.txt` — `RESULT: 9 PASS / 0 FAIL`，逐条：
  `legacy-empty-estout-yields-100pct`（缺陷复刻）、`missing-fields-reports-not-acquired`、
  `missing-fields-no-100pct-error`、`missing-fields-marked-skip`、`missing-fields-zero-fail`、
  `complete-fields-pass`、`complete-fields-real-error-still-fails`、`real-error-shows-ratio`、
  `real-error-not-masked-as-skip`。
- 字段缺失分支实测输出：`SKIP m8-05-01-estimate-error-20pct — 数据未取到：
  estimated_record_seconds, disk_free_bytes, bytes_written, wall_seconds 缺失
  （record_exit=n/a attempts=2 stale_after_clear=0）→ 标 SKIP，不计入 PASS/FAIL，不用 0 算误差`。
  `attempts=2` 证明「重试一次→仍缺」链路真实走过。
- 修复内容：`verify/cases/g5_storage.ps1` — ① M8-05 段开跑前 `Clear-SrProcesses` + 等 ≤2s
  确认无残留；② `Read-EstimateFields` 逐字段校验（空串/非数值都算缺失）；③ 缺失重试一次、
  仍缺 SKIP；④ `record_exit` / `attempts` / `stale_after_clear` 进断言文案。**误差 ≤20% 判据一字未改**。
- 红-绿对照：

| 新断言 | 旧实现实测 | 新实现实测 |
| --- | --- | --- |
| 缺失字段→「数据未取到」而非 100% 误差 | `FAIL ... 误差=100.0%`（4 字段全 0） | `SKIP ... 数据未取到`，0 FAIL，退出码 0 |
| 齐全且达标 → PASS | 同 | `PASS ... 误差=0.0%` |
| 齐全但真实超标 → 仍 FAIL（护栏） | 同 | `FAIL ... 误差=30.x%`，退出码 1，**未被伪装成 SKIP** |

### 1.1 连跑 5 次（判据「5 次 13/13」）—— **未达成，如实记录**

- 命令：`pwsh -File tasks/G6/V1-002/artifacts/R1-five-runs.ps1`（逐次完整输出 `artifacts/R1-run1..5.txt`）
- 汇总：`artifacts/R1-five-runs.txt`

| 次 | 时间 | 结果 | 估算 | 实测口径 | 误差 | 实测码率 |
| --- | --- | --- | ---: | ---: | ---: | ---: |
| 1 | 12:23:32 | 13 PASS / 0 FAIL | 19,333s | 18,463s | 4.7% | 810,465 B/s |
| 2 | 12:24:29 | 13 PASS / 0 FAIL | 19,361s | 19,036s | 1.7% | 787,201 B/s |
| 3 | 12:25:25 | **12 PASS / 1 FAIL** | 19,314s | 15,661s | **23.3%** | 954,577 B/s |
| 4 | 12:26:22 | 13 PASS / 0 FAIL | 19,290s | 17,487s | 10.3% | 853,764 B/s |
| 5 | 12:27:18 | 13 PASS / 0 FAIL | 19,292s | 17,033s | 13.3% | 876,651 B/s |

- **run3 不是假失败**：字段齐全、`record_exit=0 attempts=1 stale_after_clear=0`，23.3% 是真实计算值。
  根因是测法口径方差（产品按配置码率 ≈774 KB/s 估算，用例用 10s 短窗口实测写入速率
  787~955 KB/s 作对照，波动 ±21% 跨过 20% 阈值），**不是产品缺陷，也不是 R1 要修的族**。
- 处置：不放宽 ≤20%、不改对照口径、不隐瞒 run3；提 `docs/16-轮42-M8-05估算测法裁定记录.md`
  交验收方裁定。**R1 的假失败已消除，但「连跑 5 次全绿」判未达成**，M8-05#1 仍按 PARTIAL + 不稳定记。

## 2. M1-27 挂账与发布例外（P0）

- 验证方式：文本一致性比对 + 例外清单条目存在性。
- 命令：`Select-String -Path docs/09-V1.0覆盖表.md,docs/05-非功能需求与验收.md -Pattern 'ConsentPromptBehaviorAdmin=0|EX-01'`
- 输出：`docs/09:54` 理由列为「提示/入口已实现；黑屏需在开启安全桌面的机器复测（本机
  `ConsentPromptBehaviorAdmin=0` 不可复现）…已登记为发布例外，见 `docs/05 §7.2`」；
  `docs/05 §7.2.1` 有 `EX-01 | M1-27#1 黑屏一侧 | 提示/入口已实现；黑屏需在开启安全桌面的机器复测
  （本机 ConsentPromptBehaviorAdmin=0 不可复现） | … | 轮 41 登记 / 轮 42 终审`。
- 结论：**两处文本一致**，例外清单有条目，且写明「登记 ≠ 放宽 6 条判据」「轮 42 终审」，
  不再作为待办反复出现（轮 41 指令 §4.2）。M1-27#1 覆盖表状态仍为「有卡未实现」，**未翻绿**。

## 3. 无卡收口 5 条（P0）

- 验证方式：`tasks/G5/V1-004/EVIDENCE.md`（逐条一需求一证据）。
- 命令：`pwsh -File verify/cases/g5_closeout.ps1`
- 输出：`artifacts/g5_closeout.txt` — `RESULT: 18 PASS / 0 FAIL / 0 SKIP`，`CLOSEOUT_EXIT=0`。
- 覆盖表：`python verify/tools/coverage_report.py --write` → `written: docs/09-V1.0覆盖表.md`；
  `--check` → `coverage table is up to date`；统计行 `已实现 99 / 有卡未实现 1 / 无卡 16 / 缺理由 0`。
- 五条：`M8-01#1`、`M6-10#1`、`M6-11#1`、`M3-15#1`、`M6-04#2` 均显示 `已实现`，
  归属 `tasks/G5/V1-004/TASK.md`。**目标 ≥99 / ≤16 达成**。
- 首轮两处取数缺陷（非放宽判据）：`artifacts/g5_closeout.txt` 是修复后的重跑；
  首轮 `15 PASS / 1 FAIL / 1 SKIP` 的根因是 `--list-encoders` 汇总串误当逐条行、UI 日志
  `ReadAllText` 撞共享写句柄；改用 `Read-TextSafe` 与规范单行后 18/18 全绿，断言**收紧**未放宽。

## 4. R4 发布包 `--help` 四参数（P1）

- 验证方式：包内 exe（不是构建目录 exe）的 `--help` 原文 + 打包脚本内置断言。
- 命令：`pwsh -File verify/tools/build_release.ps1`（源码改动见 `tasks/G5/S1-009/EVIDENCE.md`）
- 输出：`artifacts/build-release.txt` — `RESULT: 17 PASS / 0 FAIL`，含
  `release-help-four-params`（缺 0）、`extract-help-four-params`（缺 0）、
  `release-help-no-debug-switch`（残留 0）、`zip-*-sha-matches-build` 两组 SHA 相同、
  `sr-recorder.exe SHA256=4BCEE50E…E12`（构建目录 = 包内同值）。
- 包内 help 原文：`artifacts/package-help.txt` 第 13~17 行：

```
  --record     直接录制一段（不启动 UI），配合 --seconds/--fps/--out 使用
  --window     <hwnd> 按窗口录制，hwnd 支持十进制或 0x 十六进制；
               仅窗口源下有效，指定后忽略 --region/--display（窗口源优先）
  --fit        pad|stretch 窗口宽高比变化时的适配策略（配合 --window 使用）
  --region     x,y,w,h 按区域录制；与 --window 不可同时生效（--window 优先）
```

- 结论：四个串齐全，`--window` 适用条件与互斥关系写清（轮 41 指令 §4.4）。
  复现：`Expand-Archive dist/ScreenRecorder-0.1.0-win-x64.zip` 后跑 `bin\sr-recorder.exe --help`。
- **口径**：验收方轮 40 已证伪「包陈旧」假设（包内 exe 能 `--record`、`--window` 串都在）；
  本卡只补 CLI 可发现性，不声称「此前功能缺失」。

## 5. 兼容性矩阵草案（P1）

- 验证方式：文件存在 + 表头/已测项标注 + 未测格显式留空。
- 命令：`Get-Content docs/14-轮41-兼容性矩阵草案.md`
- 输出：14 格矩阵；本机唯一一格 `Win11 24H2（及之后）× 双屏 × 100%` 标
  **「☑降级旁证（非矩阵实测）」**并写明「未按三件套录 60s 抽帧比对，轮 42 补」；
  其余 11 格 `☐未测`；§5 明确「必测 100% 未达成、抽测 ≥80% 未达成、`docs/05 §7.2` 第 5 条未满足」。
- 环境实测：`Get-CimInstance Win32_OperatingSystem` → build 26200；
  `[System.Windows.Forms.Screen]::AllScreens` → `\\.\DISPLAY1 1920×1080`、`\\.\DISPLAY2 1920×1080`。
- 结论：只出表头 + 已测项标注，未实测、未借机器（轮 41 指令 §4.5）。**V1.0 仍不可发布**。

## 6. 台账一行制（P1）

- 验证方式：主表轮次唯一性 + 7 列结构（C6）+ 执行方自述位置。
- 命令：`python verify/tools/contract_check.py` → `C6 执行台账存在且结构完整 … 表头 7 列 PASS`；
  `Select-String -Path docs/07-执行台账.md -Pattern '^\| 41 \|'` → **无匹配**（轮 41 主表零行）。
- 处置：`docs/07:96` 口径注改为「自轮 41 起主表每轮只保留一行 V1 验收行，由验收方独立重跑后填写；
  执行方自述并入该行动作列（备注），不另起 S1/S2/S6 行」；执行方自述落在
  `## 轮 41 记录` 小节。**执行方不预填验收结论、不自评**；待验收方写唯一一行。
- 结论：轮 41 主表无重复行；历史轮 34~39 的双行是既有记录，未改动（验收方已补口径说明）。

## 7. 门判据检查

| 检查 | 命令 | 结果 |
| --- | --- | --- |
| 覆盖表 up to date | `python verify/tools/coverage_report.py --check` | `coverage table is up to date` |
| 契约 C1~C8 | `python verify/tools/contract_check.py` | `8 项检查, 8 PASS, 0 FAIL` |
| 卡面一致性 | `python verify/tools/card_consistency_check.py` | 扫 36 张卡，`0 处冲突` |
| 文件域表 | `python verify/tools/domain_table_check.py` | `MISMATCH_COUNT=0` |
| 发布包自检 | `pwsh -File verify/tools/build_release.ps1` | `17 PASS / 0 FAIL`，SHA 自证 |
| 三构建 / 用例回归 | `pwsh -File tasks/G6/V1-002/artifacts/run-regression.ps1` | 14:05–14:12 实测：三构建 `exit=0 warnings=0`；`g5_window` 24/0、`g5_uac` 17/0/1SKIP、`g5_format` 14/0、`g4_gate` 12/0、`g3_recovery` 9/0、`g3_playback` 11/0、`g3_storage` 11/0、`g5_encoder_list` 17/0、`g5_tempfile` 10/0、`ui_smoke` 103/0，全部退出码 0。见 `artifacts/regression.txt`、`SELFTEST.md` §3 |
| `ui_smoke` ×3 连跑 | `pwsh -File tasks/G6/V1-002/artifacts/run-ui-smoke-x3.ps1` | run1/run2/run3 均 `103 PASS / 0 FAIL / 0 SKIP`，退出码 0。见 `artifacts/ui-smoke-x3.txt` |

## 8. 已知限制（不得计为通过）

1. R1「连跑 5 次全绿」**未达成**（4/5），run3 真实误差 23.3% → `docs/16` 裁定记录（轮 42 已裁定）。
2. M6-04#2 的 01 原文点名 x264，03 选型为 MF 软编 → `docs/15` 裁定记录（轮 42 已裁定）；验收方可判 PARTIAL 并回退覆盖 −1。
3. M8-01#1 用 12s 强杀**代理** 5min 长录；M3-15#1 用注入丢帧**代理** GPU 压力；
   M6-11#1 的面板只呈现三项指标（采集/编码 fps、丢帧），码率/队列深度/写盘速度/编码器状态未呈现。
4. 兼容性矩阵 12 格未测；M6-06（150% DPI）环境 BLOCKED；M1-27 黑屏例外待轮 42 终审。
5. 发布例外登记**不等于**放宽 `docs/05 §7.2` 的 6 条判据。V1.0 仍不可发布。
