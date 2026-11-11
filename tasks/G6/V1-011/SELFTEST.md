# SELFTEST: V1-011（轮 44 主卡）

## 0. 开工四项自查
| 检查项 | 结果 | 依据 |
| --- | --- | --- |
| 需求编号可检索 | 通过 | `docs/01:106` M3-04 新判据、M3-09 / M6-* / M1-27 原文 |
| 必读章节可定位 | 通过 | 轮44指令 §1~§5、验收报告-轮43 §裁定、`docs/05 §1/§2.1/§7.2.1`、`docs/06 §4.2`、`docs/11b §6` |
| 上游交付物可获取 | 通过 | 轮 43 七套交付包 + `docs/17` + 三套构建产物 + `dist/*.zip` |
| 文件域无交集 | 通过 | 5 张卡互斥；`domain_table_check` MISMATCH_COUNT=0 |

> 改 `verify/` 用例是轮 44 指令 §1/§2 明确授权的判据落地，由 V1-012 / V1-013 承接；
> 判据数字 10% / 2× 一字未动，另加 2 条反向护栏防改软。

## 1. 自测命令与输出
| 命令 | 结果 | 证据 |
| --- | --- | --- |
| `Select-String docs/01 -Pattern '^\| M3-04 |^## 变更记录'` | 新判据 + 第 43 行变更记录 | `docs/01` |
| `pwsh -File verify/cases/g5_format.ps1 -SimulateCbrRates <滑窗过10%>` | `FAIL cbr-window5-spread` 18.2%，rc=1 | `tasks/G6/V1-013/artifacts/` |
| `pwsh -File verify/cases/g5_format.ps1 -SimulateCbrRates <单秒16699>` | 滑窗 0.0% PASS 但 `FAIL cbr-single-second-cap`，rc=1 | 同上 |
| `pwsh -File verify/cases/g5_format_selftest.ps1` | **19 PASS / 0 FAIL** | `selftest-19.txt` |
| `pwsh -File artifacts/run-r44-cbr10.ps1` | **ALL_10_PASS=True**（10/10） | `r44-cbr10.txt` + 10 份 persec.csv |
| `python verify/tools/coverage_report.py --write` + `--check`（起点，第 1 次） | `up to date`，rc=0，107/2/7 | 终端 |
| `python verify/tools/coverage_report.py --write` + `--check`（翻绿，第 2 次） | `up to date`，rc=0，**108/1/7** | 终端 |
| `pwsh -File tasks/G6/V1-015/artifacts/run-r44-matrix.ps1` | 2 CELL 全 PASS | `r44-matrix.txt` |
| `python verify/tools/contract_check.py` | 8/8 PASS | 终端 |
| `python verify/tools/card_consistency_check.py` | 54 卡 0 冲突 | 终端 |
| `python verify/tools/domain_table_check.py` | 0 不匹配 | 终端 |

## 2. 红-绿对照
| 新断言 | 旧实现实测 | 新实现实测 |
| --- | --- | --- |
| `cbr-window5-spread` | 逐秒口径静态 4/5 超、运动 5/5 超（最高 359%） | 10 次 2.62%~7.30% 全 PASS；注入异常 18.2% FAIL |
| `cbr-single-second-cap` | 无挡板，16,699kbps（2.8×）无人拦 | 注入该值：滑窗 0.0% 仍 FAIL；10 次真实最大 7,804 |
| self-test 条数 | 17 | **19** |
| M3-04#1 状态 | 有卡未实现 | 已实现（108/1/7） |
| 矩阵双屏格 | 降级旁证 | 三件套实测 PASS |
| dxgi 后端 | 轮 43「自动回退、未测」（参数名用错） | `backend=dxgi` 真强制 + PASS |

## 3. 回归

> 汇总 `artifacts/r44-regression.txt`，逐项原始输出 `artifacts/reg-*.txt`；
> 三构建日志 `artifacts/build-*.txt`。**以该文件为准。**

| 用例 | 判据期望 | 本轮实测 |
| --- | --- | --- |
| 三构建 | 零告警 | **msvc-release / msvc-debug / mingw-release 全部 `exit=0 warnings=0 errors=0`** |
| `g5_storage` | 14 PASS | **14 / 0 / 0** |
| `g5_r43_closeout` | 12 PASS | **12 / 0 / 0** |
| `g5_window` | 24 PASS | **24 / 0 / 0** |
| `g5_uac` | 17 PASS / 1 SKIP | **17 / 0 / 1 SKIP** |
| `g4_gate` / `g3_recovery` / `g3_playback` / `g3_storage` | 全绿 | 12 / 9 / 11 / 11 |
| `g5_tempfile` / `g5_estimate_selftest` / `g5_longwindow_selftest` / `g5_f2_selftest` | 全绿 | 10 / 9 / 10 / 4 |
| `g5_format_selftest` | 19 | **19 / 0** |
| `g5_r42_closeout` | 12 | 12 / 0 |
| `g5_format` | 两条新断言 PASS | 回归首轮 **14/1**（漏改 `m3-04-mode-isolation`）→ 修后复跑 **15 / 0 / 0** |
| `g5_encoder_list` | 17 | **15 / 1**（`m3-05-ui-listbox-exists`，见下） |
| `ui_smoke` ×3 | 3×103 PASS | **3 × 103 / 0 / 0** |
| 契约 / 卡面 / 域表 / `--check` | 通过 | 8/8、54 卡 0 冲突、0 不匹配、up to date |
| 发布包 + `dist/` | 17 PASS；只有 zip + summary | **17 / 0**；`release-summary.json` + zip 两个文件 |

### 两处 FAIL 与处置（详见 `artifacts/r44-regression-notes.txt`）

1. **`g5_format` 14/1**：`m3-04-mode-isolation` 仍用旧逐秒 spread（13.9%）→
   同一需求两种判据，是执行方遗漏。已改为同一套 `Get-Window5Stats` 口径
   （5s 滑窗 ≤10% + 单秒 ≤12000 硬挡板），判据数字未动。
   复跑 `artifacts/rerun-g5-format.txt`：**15 PASS / 0 FAIL / 0 SKIP**，
   其中 `cbr-window5-spread` 3.4%、`cbr-single-second-cap` 7,022 vs 12,000、
   `m3-04-mode-isolation` 滑窗 4.2%（6 窗）/ 单秒 7,295。
   `g5_format_selftest` 同时复跑仍 **19/19**。
2. **`g5_encoder_list` 15/1**：`m3-05-ui-listbox-exists`（`hwnd=0`），复跑稳定复现。
   独立只读诊断 `artifacts/diag-encoder-list-timing.txt`：T+308ms 起
   `list107_hwnd=20063192 valid=True`、`LIST_CONTROL_APPEARED=True`；
   同用例三条 UI 日志断言全 PASS → **产品功能正常，用例找到窗口后立即取控件的竞态**。
   **该用例不在本轮 `verify/` 授权范围（指令 §1/§2 只授权 `g5_format*`），未修改**，
   交验收方裁定。判据 §3 #4 的五项不受影响。

## 4. DoD 勾选
- [x] 开工四项自查全部通过并已记录
- [x] 任务卡每条需求 → 一条证据，含可重跑命令与输出（`EVIDENCE.md §1~§6`）
- [x] 新增断言都先红后绿，旧值已记录（§2）
- [x] Release + Debug + MinGW 三构建零告警（`artifacts/r44-regression.txt`）
- [ ] **相关回归零失败：未完全达成** —— `g5_format` 漏改已修、复跑 15/0；
      `g5_encoder_list` 仍有 1 条用例侧 FAIL（产品侧诊断证明正常，未越权改用例，交验收方裁定）。
      判据 §3 #4 的五项 + `ui_smoke` 3×103 全绿。
- [x] 无死代码、无调试残留：`-SimulateCbrRates` 是验收方要求的反向护栏注入入口
- [x] 改动未超出文件域（5 卡互斥），`vcpkg.json` / `CONTRACT.md` / 合并规则 / `src/` 未改
- [x] 接口无变更，无需契约变更单
- [x] 文档已同步：`docs/01`、`docs/07`、`docs/09`、`docs/14`、`docs/17`、`docs/18`、新增 `docs/19`；
      5 张卡 `## 交付状态` 与实际一致
- [x] `## 覆盖子句` 只列实际声明的子句（仅 V1-014 声明 M3-04#1）
- [x] 已知限制已写明（含降级验证标注），见 `EVIDENCE.md §8` / `HANDOFF.md`
- [x] 未自评为已验收；轮 44 主表行留给验收方
- [x] 全量回归结果已回填 §3，两处 FAIL 如实标注并给出修复/裁定路径