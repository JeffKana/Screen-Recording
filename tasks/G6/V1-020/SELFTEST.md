# SELFTEST: V1-020（轮 47 主卡）

> 全部为**执行方自测**，不是验收结论。原始输出在 `artifacts/`（`.gitignore` 排除，命令可重跑再生）。
> 机器：Win11 build 26200 / i5-13500 / UHD 770 / 双屏 1920×1080 / DPI 100% /
> PowerShell 7 / MSVC + Ninja / MinGW。时间：2026-11-13。

## 0. 开工四项自查

| 检查项 | 结果 | 依据 |
| --- | --- | --- |
| 需求编号可检索 | **通过** | `docs/01` 原文：`M3-05`:107、`M3-06`:108、`M3-09`:111、`M1-27`:59、`M6-06`:201；原始输出 `artifacts/selftest-0-inputs.txt` |
| 必读章节可定位 | **通过** | `docs/05 §7.2`:214、`docs/06 §4.2`:177、`docs/02 §7`:222、`CONTRACT §2`:24、`docs/20 §0`:8、`docs/19 §0.B`、`docs/09`、`docs/14 §3/§5`；同上 |
| 上游交付物可获取 | **通过** | `main` @ `560087a`（Merge PR #6 `acceptance/r46`），`round/47` 从该提交拉出；`verify/tools/*`、`verify/cases/g5_encoder_list.ps1`、`build-msvc/bin/Release/sr-recorder.exe`（轮 46 基线另存 `%TEMP%\sr-r47-baseline\`，SHA256 `795CE22F…5125`）均可取；`git rev-parse HEAD` 原始输出同上 |
| 文件域无交集 | **通过** | 本轮只有 `S4-008`（`src/encode/`、`src/recorder/`、`tasks/G6/S4-008/*`）与 `V1-020`（`verify/cases/g5_encoder_consistency.ps1`、`g5_format.ps1` 仅文案、`docs/*`、`tasks/G6/V1-020/*`、`S4-008/TASK.md` 仅状态移交）两张卡，白名单逐项比对无重叠；无其它并行任务 |

> 规划侧只下发 `.codebuddy/plans/轮47指令.md`（无现成 `TASK.md`），两张卡按该指令派生，
> 与轮 46 `V1-019` 的派生方式一致；`TASK.md` 字段齐备（需求范围/必读/文件域/上游/约束/DoD/覆盖子句/交付状态）。

## 1. 自测命令与输出

| 命令 | 结果 | 时间戳 / 产物 |
| --- | --- | --- |
| `pwsh -NoProfile -File verify/cases/g5_encoder_consistency.ps1 -RecorderPath "$env:TEMP\sr-r47-baseline\sr-recorder.exe"` | `RESULT: 4 PASS / 8 FAIL`（红：`mf-h264-hw` 强制后 `encoder=mf-h264-sw`） | `artifacts/r47-red.txt` |
| `pwsh -NoProfile -File verify/cases/g5_encoder_consistency.ps1` | `RESULT: 12 PASS / 0 FAIL`（绿） | `artifacts/r47-green.txt` |
| `.\build-msvc\bin\Release\sr-recorder.exe --list-encoders` | `mf-h264-sw available=1`；`mf-h264-hw available=0 reason=MFT 真激活失败（… encoder rejected NV12 input type）`；耗时 278ms | `EVIDENCE.md §P0-1 补充` |
| `.\build-msvc\bin\Release\sr-recorder.exe --record --seconds 60 --fps 30 --bitrate 6000 --display 0 --out <tmp> --encoder mf-h264-hw` | `process_exit=6`、`成品存在=False`、三件套不可执行 | `artifacts/r47-matrix-hw.txt` |
| `.\build-msvc\bin\Release\sr-recorder.exe --list-encoders --probe-repeat 5` | 状态 `1/0` ×5 一致；句柄 268/271/274/277/280 | `artifacts/r47-green.txt`（run11） |
| `.\build-msvc\bin\Release\sr-recorder.exe --list-encoders --probe-repeat 5 --fail-encoder hardware` | 句柄 204/204/204/204/204（增长 0） | `artifacts/r47-green.txt`（run12） |
| `python verify/tools/coverage_report.py --write`（回退态） | `已实现 107, 有卡未实现 2, 无卡 7` | `artifacts/coverage-rollback.txt`、`coverage-rollback-stats2.txt` |
| `python verify/tools/coverage_report.py --write`（翻绿后） | `已实现 108, 有卡未实现 1, 无卡 7, 已挂账 8, 缺理由 0` | `artifacts/coverage-final.txt`、`coverage-final-stats.txt` |
| `python verify/tools/coverage_report.py --check` | `coverage table is up to date` | `artifacts/coverage-final-check.txt` |
| `python verify/tools/contract_check.py` | `汇总: 8 项检查, 8 PASS, 0 FAIL` | `artifacts/contract-final.txt` |
| `python verify/tools/card_consistency_check.py` | `扫描 60 张卡，0 处冲突` | `artifacts/cards-final.txt` |
| `python verify/tools/domain_table_check.py` | `MISMATCH_COUNT=0` | `artifacts/domain-final.txt` |
| `cmake --build build-msvc --config Debug` / `--config Release` / `cmake --build build-mingw` | 三个 `exit=0 warnings=0` | `artifacts/build-msvc-Debug.log`、`build-msvc-Release.log`、`build-mingw-Release.log` |
| `pwsh -NoProfile -File verify/cases/g5_format.ps1`（F3 文案后） | 首次 `14 PASS / 1 FAIL`（单秒 12,799kbps）；清场复跑 `15 PASS / 0 FAIL` | `artifacts/reg-g5_format.txt`、`reg-g5_format-run2.txt` |
| 交付前一次性自检 | 覆盖/契约/卡面/域表/一致性用例全通过 | `artifacts/r47-final-verification.txt` |

## 2. 红-绿对照

| 新断言 | 旧实现实测（轮 46 二进制） | 新实现实测（轮 47 构建） |
| --- | --- | --- |
| `r47-forward-available-items` | **FAIL**：可用项=2；`mf-h264-hw` 强制后 `exit=0`、`encoder=mf-h264-sw`、有软编成片 | **PASS**：可用项=1（`mf-h264-sw`），`exit=0`、`encoder=mf-h264-sw`、有成品 |
| `r47-no-silent-fallback` | **FAIL**：`--encoder mf-h264-hw` 输出出现 `encoder=mf-h264-sw`（True） | **PASS**：不出现（False） |
| `r47-backward-unavailable-items` | **PASS**（3 个厂商项） | **PASS**（4 项，多出 `mf-h264-hw`，`exit=6` + 原因 + 无成片） |
| `r47-injected-hw-unavailable` | **FAIL**：`--list-encoders --fail-encoder hardware` → `exit=1`（旧 CLI 不接受参数） | **PASS**：`exit=0`、`mf-h264-hw available=0`、原因含 `injected: hardware encoder unavailable` |
| `r47-injection-is-real-probe` | **FAIL**（无 injected 原因、软件项也拿不到） | **PASS**：硬件原因含 `injected`、软件仍 `available=1` |
| `r47-injected-forced-exit6` | **FAIL**：`exit=1` | **PASS**：`exit=6`、含 `unavailable`、无成片 |
| `r47-probe-retry-not-cached` | PASS（回归护栏，在旧实现上也成立） | **PASS**：撤注入后原因不含 `injected`，规范行与首轮逐字一致 |
| `r47-repeat-state-consistent` | **FAIL**：无 `probe_iteration` 行 | **PASS**：5 轮 `1/0` 完全一致 |
| `r47-own-resource-no-leak` | **FAIL**：无行、增长 `-1` | **PASS**：跳过硬件激活后句柄 `204×5`，增长 0 |
| `r47-vendor-handle-bounded` | **FAIL**：无行、增长 `-1` | **PASS**：真实激活 `268→280`，增长 12 ≤ 16 |
| **合计** | **`RESULT: 4 PASS / 8 FAIL`** | **`RESULT: 12 PASS / 0 FAIL`** |

覆盖状态迁移（也是一次红-绿）：`108/1/7`（仓库起点，未落回退）→ 回退态
`107/2/7`（`S4-008` 声明「有卡未实现」）→ 用例全绿后翻绿 `108/1/7`。
两次 `--write` 输出都在 `artifacts/`，**不是为了凑 108 直接标已实现**。

## 3. 回归

| 用例 | 结果 |
| --- | --- |
| `g5_encoder_list` | `17 PASS / 0 FAIL` |
| `g5_format` | 首次 `14 PASS / 1 FAIL`（`cbr-single-second-cap` 12,799kbps > 12,000）；复跑 `15 PASS / 0 FAIL / 0 SKIP` |
| `g5_format_selftest` | `22 PASS / 0 FAIL` |
| `g5_storage` | `14 PASS / 0 FAIL / 0 SKIP` |
| `ui_smoke` ×3 | `103 PASS / 0 FAIL / 0 SKIP` ×3 |
| `g5_encoder_consistency`（新增） | `12 PASS / 0 FAIL` |
| `g5_inject` / `g5_r43_closeout`（附加） | `28 PASS / 0 FAIL` / `12 PASS / 0 FAIL / 0 SKIP` |
| `g5_closeout`（附加抽样） | **`17 PASS / 1 FAIL`**：`m6-04-02-hw-unavailable-with-reason` 硬编码「不可用行 == 3」，轮 47 诚实列表为 4。用例侧过期假设，未授权改、未改；remedy 已在 `HANDOFF.md §2` 上报 |
| 静态四项 | 覆盖 `--check` PASS / 契约 8/8 / 卡面 60 卡 0 冲突 / 域表 0 mismatch |
| 三构建 | MSVC Debug、MSVC Release、MinGW Release 均 `exit=0 warnings=0` |

> 依据说明：**规定回归集**（轮 47 指令 §通过判据 #5）零失败；另跑了 4 个附加用例，
> 其中两处红项（首次 g5_format、g5_closeout）如实保留，未改任何判据或阈值。

## 4. DoD 勾选

- [x] 开工四项自查全部通过并已记录（§0 + `artifacts/selftest-0-inputs.txt`）
- [x] 任务卡每条动作 → 一条证据，含命令与输出（`EVIDENCE.md`）
- [x] 红-绿对照表完整（旧值 + 新值），旧二进制 SHA 留档
- [x] Release + Debug + MinGW 零告警（三份 build 日志）
- [x] 规定回归集零失败；依据与附加红项已说明（§3）
- [x] 无死代码、无调试残留、无注掉的块（临时分段诊断代码已删除，见 `git diff src/recorder/main.cpp`）
- [x] 改动未超出文件域（`git status --short` 全部落在两张卡白名单）
- [x] `vcpkg.json` 未改、`CMakeLists.txt` 未改、无新三方依赖
- [x] 未修改 `verify/` 下判据与阈值：`g5_format.ps1` 只改一处文案 `(仅对照)`，新增用例是独立文件
- [x] 文档已同步（`docs/07/09/14/19/22/INDEX.md`、两张 `TASK.md` 的 `## 交付状态`）
- [x] `HANDOFF.md` 四问齐答，含「哪些需求没做完」
- [x] 覆盖子句只列实际覆盖的（本卡「无」；M3-05#1 由 `S4-008` 独占声明）
- [x] 已知限制已写明（含「降级验证」：本机硬件 MFT 不可用、矩阵 `--display 0` 单屏降级）
- [x] 未自评「已验收」；台账主表轮 47 一行留白给验收方
