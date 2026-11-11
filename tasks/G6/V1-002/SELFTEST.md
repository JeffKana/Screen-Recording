# SELFTEST: V1-002（轮 41 · 用例稳定化 + 挂账 + 发布前准备）

## 0. 开工四项自查

| 检查项 | 结果 | 依据 |
| --- | --- | --- |
| 需求编号可检索 | 通过 | M1-27 / M8-01 / M6-10 / M6-11 / M3-15 / M6-04 / M8-05 在 `docs/01` 有原文；逐条核过验收要点 |
| 必读章节可定位 | 通过 | `docs/05 §7.2`/§3.1~3.3、`docs/06 §4.2`/§8.4、`docs/09` 头部、`docs/11 §6`、`CONTRACT §2`、`docs/12` 均在位 |
| 上游交付物可获取 | 通过 | 轮 40 三份交付包 + 两套构建产物 + `dist/*.zip` + `release-summary.json`；未用旧版本开工 |
| 文件域无交集 | 通过 | 同轮三卡目录互斥（V1-002 / V1-004 / S1-009），`domain_table_check` MISMATCH_COUNT=0 |

> 轮 41 下发时无预置卡，按验收方 `.codebuddy/plans/轮41指令.md` 建卡（沿用轮 40 S5-004 做法）。
> **R1 修改 `verify/cases/g5_storage.ps1` 是验收方指令 §2 R1 明确授权的用例修复**：
> 修的是「用 0 算误差」的测量缺陷，误差 ≤20% 判据一字未改，
> 且新增 `complete-fields-real-error-still-fails` 等护栏断言证明判据没被改软。
> 不属「为让测试通过而改用例」。

## 1. 自测命令与输出

| 命令 | 结果 | 时间戳 / 证据 |
| --- | --- | --- |
| `pwsh -File verify/cases/g5_estimate_selftest.ps1`（修复前） | `1 PASS / 4 FAIL`，子进程 `12 PASS / 1 FAIL` 含 `误差=100.0%` 假 FAIL | 11:47 · `artifacts/R1-red.txt` |
| `pwsh -File verify/cases/g5_estimate_selftest.ps1`（修复后） | `9 PASS / 0 FAIL` | `artifacts/R1-selftest-final.txt` |
| `pwsh -File verify/cases/g5_storage.ps1` ×5 | run1/2/4/5 = 13/0；**run3 = 12/1（真实 23.3%）** | 12:23–12:27 · `artifacts/R1-five-runs.txt`、`R1-run1..5.txt` |
| `pwsh -File verify/cases/g5_closeout.ps1`（首轮） | `15 PASS / 1 FAIL / 1 SKIP`（两处取数缺陷） | 首轮临时输出 |
| `pwsh -File verify/cases/g5_closeout.ps1`（修正后） | `18 PASS / 0 FAIL / 0 SKIP` | `artifacts/g5_closeout.txt` |
| `python verify/tools/coverage_report.py --write` | `written: docs/09-V1.0覆盖表.md`；99/1/16 | 见 `EVIDENCE.md §3` |
| `python verify/tools/coverage_report.py --check` | `coverage table is up to date` | 同上 |
| `python verify/tools/contract_check.py` | `8 项检查, 8 PASS, 0 FAIL` | 同上 |
| `python verify/tools/card_consistency_check.py` | 36 卡 `0 处冲突` | 同上 |
| `python verify/tools/domain_table_check.py` | `MISMATCH_COUNT=0` | 同上 |
| `pwsh -File verify/tools/build_release.ps1` | `17 PASS / 0 FAIL`，含四个 help 断言，SHA 自证 | `artifacts/build-release.txt` |
| 包内 exe `--help`（解压后） | 四参数齐全，`--window` 适用条件写清 | `artifacts/package-help.txt` |
| `Get-CimInstance Win32_OperatingSystem` / `Screen.AllScreens` | build 26200 / 双屏 1920×1080 | `docs/14 §4` |

## 2. 红-绿对照

| 新断言 | 旧实现实测 | 新实现实测 |
| --- | --- | --- |
| 字段缺失 → 「数据未取到」而非 100% 误差 | `FAIL m8-05-01 — 估算=0s 实测口径=0s 误差=100.0%`（四字段全 0） | `SKIP m8-05-01 — 数据未取到：…缺失（record_exit=n/a attempts=2 stale_after_clear=0）`；0 FAIL、退出码 0 |
| 齐全且达标 → PASS | 同 | `PASS … 误差=0.0%`（估算 19,200s / 实测 19,206s） |
| 齐全但真实超标 → 仍 FAIL（护栏） | 同 | `FAIL … 误差=30.x%`，退出码 1，输出不含「数据未取到」/「SKIP」 |
| 包内 `--help` 含四参数 | 轮 40 包 help 未列出（R4，验收方独立记录） | `PASS release-help-four-params` + `PASS extract-help-four-params`，缺 0 |
| 5 条无卡子句各有专属断言 | 无（21 条无卡） | `g5_closeout.ps1` 18/18；覆盖 99/1/16 |

## 3. 回归（2026-11-09 14:05:36 – 14:12:50 实测）

> 依据：`artifacts/run-regression.ps1` 跑三构建（MSVC Release/Debug、MinGW Release）
> 与判据 §3 #4 的关键用例；逐项完整输出在 `artifacts/regression-<case>.txt`，汇总在 `artifacts/regression.txt`。
> **以下全部是本轮实测值，不是历史数字。**

| 用例 | 判据期望 | 本轮实测 | 退出码 |
| --- | --- | --- | ---: |
| MSVC Release | 零告警 | `BUILD msvc-release exit=0 warnings=0` | 0 |
| MSVC Debug | 零告警 | `BUILD msvc-debug exit=0 warnings=0` | 0 |
| MinGW Release（Ninja） | 零告警 | `BUILD mingw-release exit=0 warnings=0`，重编译 `main.cpp.obj` 并链接 | 0 |
| `g5_window` | 24 PASS | **24 PASS / 0 FAIL / 0 SKIP** | 0 |
| `g5_uac` | 17 PASS / 1 SKIP | **17 PASS / 0 FAIL / 1 SKIP** | 0 |
| `g5_format` | 14 PASS | **14 PASS / 0 FAIL** | 0 |
| `g4_gate` | 12 PASS | **12 PASS / 0 FAIL** | 0 |
| `g3_recovery` | 9 PASS | **9 PASS / 0 FAIL** | 0 |
| `g3_playback` | 11 PASS | **11 PASS / 0 FAIL** | 0 |
| `g3_storage` | 11 PASS | **11 PASS / 0 FAIL** | 0 |
| `g5_encoder_list` | 17 PASS | **17 PASS / 0 FAIL** | 0 |
| `g5_tempfile` | 10 PASS | **10 PASS / 0 FAIL** | 0 |
| `ui_smoke` | 103 PASS | **103 PASS / 0 FAIL / 0 SKIP**（回归 runner 内 1 次） | 0 |
| `ui_smoke` ×3 连跑 | 3×103 PASS | **3 × 103 PASS / 0 FAIL / 0 SKIP**，run1/run2/run3 退出码均为 0 | 0 |
| 契约 8/8、卡面 0 冲突、域表 0 不匹配、`--check` | 通过 | 已实测，见 §1 | — |

> `ui_smoke` ×3 由 `artifacts/run-ui-smoke-x3.ps1` 在 14:13–14:20 单独连跑（避免与回归 runner 并发争用 UI），
> 汇总 `artifacts/ui-smoke-x3.txt`，逐次完整输出 `artifacts/ui-smoke-run1..3.txt`。
> `g5_storage` 连跑 5 次的结果见 §1 与 `EVIDENCE.md §1.1`（4 绿 1 红），**未混入本表的「全绿」结论**。

## 4. DoD 勾选

- [x] 开工四项自查全部通过并已记录
- [x] 任务卡每条需求 → 一条证据，含命令与输出（`EVIDENCE.md` §1~§6）
- [x] 红-绿对照表完整（旧值 + 新值）
- [ ] **「连跑 5 次 13/13」未达成**：4/5，run3 真实误差 23.3% → `docs/16` 裁定记录（轮 42 已裁定）；
  判据不放宽、对照口径不改、数据不隐瞒
- [x] Release + Debug 零告警：三构建 `warnings=0`（`artifacts/regression.txt`），发布构建 17/17 另证
- [x] 相关回归零失败：判据 §3 #4 全量清单 10 个用例全部符合期望、退出码 0（`artifacts/regression.txt`）
- [x] 无死代码、无调试残留：`PrintUsage()` 只增帮助文本；`g5_storage.ps1` 的
  `-SelfTestEstimate` / `-SimulateMissingFields` / `-EstimatePayload` 是**验收方要求的
  字段缺失分支用例**的可重跑入口，不是残留；打包脚本的两条 help 断言是判据要求
- [x] 改动未超出文件域（三卡目录互斥），`vcpkg.json` 未改，`CONTRACT.md` 未改
- [x] 接口无变更（无需契约变更单）
- [x] 文档已同步：`docs/05 §7.2.1`、`docs/07`（一行制 + 轮 41 记录）、`docs/09`（生成物）、
  `docs/14`、`docs/15`、`docs/16`；三张任务卡 `## 交付状态` 已写且与实际一致
- [x] `## 覆盖子句` 只列实际覆盖的子句（V1-002 / S1-009 写「无」，V1-004 列 5 条）
- [x] 已知限制已写明（含降级验证标注），见 `EVIDENCE.md §8` / `HANDOFF.md` 第三、四问
- [x] 未自评为已验收；轮 41 主表行留给验收方
