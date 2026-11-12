# SELFTEST: V1-019（轮 46 · 主卡）

## 0. 开工四项自查

| 检查项 | 结果 | 依据 |
| --- | --- | --- |
| 需求编号可检索 | 通过 | `docs/01` 可查 M3-04（:106）、M3-09（:111）、M6-01（:196）、M6-04（:199）、M6-05（:200）、M6-08（:203）、M1-27（:59）；本卡不声明需求子句 |
| 必读章节可定位 | 通过 | `docs/21 §2/§4/§5`、`docs/19 §0`、`docs/18 §3.1`、`docs/05 §1/§2.1`、`docs/09`、`docs/20 §0`、`docs/06 §4.2`、`CONTRACT §2` 均可定位 |
| 上游交付物可获取 | 通过 | 分支从 `main` @ `ad65d7b` 拉出；`verify/lib/ActiveDesktop.ps1`（轮 45 / V1-017）、`g5_cbr_probe.ps1`、`tasks/G5/S4-005/EVIDENCE.md`、`docs/21` 均在分支内；`build-msvc/bin/Release/sr-recorder.exe` 在位且 `src/` 本轮未改 |
| 文件域无交集 | 通过 | 本卡白名单为 `docs/` 六份 + `verify/` 三用例 + 新增共用库 + `V1-014` 交付状态 + 本卡目录；本轮无并行任务分支，`src/encode/` / `vcpkg.json` / 其它任务卡未动 |

> 本卡明确授权改 `verify/cases/g5_format.ps1`、`g5_format_selftest.ps1`、`g5_cbr_probe.ps1`
> 并新增 `verify/lib/CbrCriteria.ps1`（`docs/21 §5` + 轮 46 指令 §P0-1/P0-3）；
> `verify/` 下其它用例与判据阈值（10% / 2×）未改。

## 1. 自测命令与输出（原始 RESULT 行）

| 命令 | 结果 | 时间戳 |
| --- | --- | --- |
| `pwsh -NoProfile -File verify/cases/g5_format_selftest.ps1`（旧实现，红） | `RESULT: 13 PASS / 9 FAIL` | 11:22 |
| `pwsh -NoProfile -File verify/cases/g5_format_selftest.ps1`（新实现，绿） | `RESULT: 22 PASS / 0 FAIL` | 11:22 |
| `pwsh -NoProfile -File verify/cases/g5_cbr_probe.ps1 -Seconds 40 -Runs 3 -Mode static,active -OutDir tasks/G6/V1-019/artifacts/cbr-6runs` | `MODE static runs=3 verdict_pass=3 avg_fail=0 cap_fail=0`；`MODE active runs=3 verdict_pass=3 avg_fail=0 cap_fail=0` | 11:24–11:29 |
| `pwsh -NoProfile -File verify/cases/g5_format.ps1 -CbrOnly -CbrFile .../active-01.mkv -CbrStdout .../active-01.out` | `RESULT: 2 PASS / 0 FAIL / 0 SKIP`；`F3_VERDICT=PASS`、相对差 `0.0006%` | 11:29 |
| `python verify/tools/coverage_report.py --write` → `--check` | `coverage table is up to date`；`已实现 108 / 有卡未实现 1 / 无卡 7` | 11:30 |
| `python verify/tools/contract_check.py` | `汇总: 8 项检查, 8 PASS, 0 FAIL` | 11:30 / 11:44 |
| `python verify/tools/card_consistency_check.py` | `card_consistency: OK —— 扫描 58 张卡，0 处冲突` | 11:30 / 11:44 |
| `python verify/tools/domain_table_check.py` | `MISMATCH_COUNT=0` | 11:22 / 11:44 |
| `pwsh -NoProfile -File tasks/G6/V1-019/run-regression.ps1` | 见 §3；`REGRESSION_START 11:30:56` → `REGRESSION_DONE 11:43:53` | 11:30–11:43 |
| `cmake --build build-msvc --config Release` / `--config Debug` / `cmake --build build-mingw` | `BUILD msvc-release exit=0 warnings=0` / `BUILD msvc-debug exit=0 warnings=0` / `BUILD mingw-release exit=0 warnings=0` | 11:44 |

原始输出：`artifacts/selftest-red.txt`、`selftest-green.txt`、`cbr-6runs/probe-summary.txt`、
`cbr-6runs/f3-tool-diff.txt`、`r46-regression.txt`、`build-summary.txt`（本地证据，未入库）。

## 2. 红-绿对照

| 新断言 / 新护栏 | 旧实现实测 | 新实现实测 |
| --- | --- | --- |
| `cbr-average-deviation`（全片 ≤10%） | 旧用例只有 `cbr-window5-spread`，注入 20 个 6000 桶 + 35,000,000B/40s 仍 `RESULT: 2 PASS / 0 FAIL`（平均 16.7% 超 10% 未被发现） | `FAIL cbr-average-deviation`，`RESULT: 1 PASS / 1 FAIL`，退出码 1 |
| `cbr-single-second-cap`（单秒 ≤2×，不被平均稀释） | 旧实现该条已在，但注入 16,699kbps + 平均 0.0% 时输出 `RESULT: 1 PASS / 1 FAIL`；新自检要求「平均仍 PASS」，旧实现无 `cbr-average-deviation` 可判 → `FAIL r46-single-second-cap-fail` | `FAIL cbr-single-second-cap` + `PASS cbr-average-deviation`，`RESULT: 1 PASS / 1 FAIL`，退出码 1 |
| `r46-average-source-wholefile`（不许逐秒再平均） | 旧实现无全片取数注入，护栏 `FAIL`（rc=0、无 `bytes_written=30000000`） | `PASS cbr-average-deviation`（逐秒均值 9000 但全片 6000），`RESULT: 2 PASS / 0 FAIL` |
| `r46-window5-criterion-removed`（滑窗判据删除） | 旧用例仍含 `Assert 'cbr-window5-spread'` 与 `Get-Window5Stats` → `FAIL` | 两者均已删除 → `PASS` |
| 自检总结果 | **13 PASS / 9 FAIL**（红） | **22 PASS / 0 FAIL**（绿，判据 ≥21） |
| 覆盖状态 | 轮 45 回退态 **107 / 2 / 7** | 6/6 全过后 **108 / 1 / 7**，`--check` PASS |

> 红证据是在**未修复**的 `g5_format.ps1` 上先写好新自检、先跑出来的（11:22:18），
> 不是事后补写；两次输出分别留在 `artifacts/selftest-red.txt` 与 `selftest-green.txt`。

## 3. 回归

| 用例 | 原始 RESULT 行 | 退出码 |
| --- | --- | --- |
| `g5_format`（默认静态） | `RESULT: 15 PASS / 0 FAIL / 0 SKIP` | 0 |
| `g5_format -CbrScene active` | `RESULT: 15 PASS / 0 FAIL / 0 SKIP` | 0 |
| `g5_format_selftest` | `RESULT: 22 PASS / 0 FAIL` | 0 |
| `g5_storage` | `RESULT: 14 PASS / 0 FAIL / 0 SKIP` | 0 |
| `g5_r43_closeout` | `RESULT: 12 PASS / 0 FAIL / 0 SKIP` | 0 |
| `g5_window` | `RESULT: 24 PASS / 0 FAIL / 0 SKIP` | 0 |
| `g5_uac` | `RESULT: 17 PASS / 0 FAIL / 1 SKIP` | 0 |
| `ui_smoke` run1 / run2 / run3 | `RESULT: 103 PASS / 0 FAIL / 0 SKIP` ×3 | 0 / 0 / 0 |
| `r46_matrix_hw`（P1 补格） | `RESULT: 6 PASS / 1 FAIL / 0 SKIP` | 1 |

依据：`artifacts/r46-regression.txt`（11:30:56–11:43:53，顺序执行 12 项）+ 每项独立 `*.txt`。
**唯一非绿项**是 P1 矩阵的 `r46-hw-encoder-really-hardware`：
强制 `--encoder mf-h264-hw` 后 stdout `encoder=mf-h264-sw`，属产品侧静默回退；
`src/encode/` 不在本卡文件域，未修、未删断言、未改用软编凑数，如实记 `PARTIAL`（`EVIDENCE.md` P1 段、`docs/14 §3/§5`）。
`g5_uac` 的 1 SKIP 是「UAC 安全桌面黑屏本机不可复现」的历史固定项，与本轮改动无关。

## 4. DoD 勾选

- [x] 开工四项自查全部通过并已记录
- [x] 任务卡每条需求 → 一条证据，含命令与输出（`EVIDENCE.md`）
- [x] 红-绿对照表完整（旧值 13/9 FAIL + 新值 22/0，含逐条断言）
- [x] `docs/01` 判据文本与变更记录行照抄 `docs/21 §2`，未自行润色
- [x] 6 次实测含 3 次「桌面有活动」，`ActiveDesktop.ps1` 用默认参数（命令行留证 `ACTIVE_DESKTOP_WEAKENED=False`）
- [x] 6 次逐次记录 `avg_ok` / `cap_ok` / `fail_kind`，6/6 全过（未触发 PARTIAL 分支）
- [x] Release + Debug + MinGW 三构建零告警
- [x] 判据 #6 五项回归零失败 + `g5_format` / selftest 全绿；唯一红项如实挂账
- [x] 无死代码、无调试残留；滑窗函数与断言已删除（非注掉）
- [x] 改动未超出文件域白名单，`vcpkg.json` 未改，`src/` 净改动 0，`verify/` 其它用例未改
- [x] 无产品接口变更 → 无需契约变更单；`CONTRACT.md` 未改
- [x] 文档已同步：`docs/01`、`docs/07`、`docs/09`（生成物）、`docs/14`、`docs/18`、`docs/19`、`V1-014` 交付状态
- [x] `HANDOFF.md` 四问齐答，含「哪些需求我没做完」
- [x] 任务卡 `## 交付状态` 已写且与实际一致（无子句声明）
- [x] `## 覆盖子句` 只列实际覆盖的子句（本卡为「无」）
- [x] 已知限制已写明（活动激励强度、全片含音频轨、逐秒方差不作判据、F3 同成片对照、M1-27 / 真机 BLOCKED、CI 不等于集成验收）
- [x] 未在任何交付说明里写「已验收」

## 5. 降级验证标注

- 6 次实测是 40s CBR 片段、同一二进制与同一桌面会话，不是 8h 单文件长录；`docs/21 §1` 的 8h 0.4% 偏差是历史实测，本轮未重跑。
- P1 矩阵用 `--display 0` 只采主屏代表单屏（物理双屏未禁用），属 `docs/14 §1 #4` 的降级验证；且硬编未命中，标 PARTIAL。
- F3 的 ≤2% 对照在同一成片上做（确定性）；独立 active 录制的 0.16% 差异属内容抖动，未冒充实现一致性。
