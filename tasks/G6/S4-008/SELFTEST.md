# SELFTEST: S4-008（轮 47 产品实现卡）

> 执行方自测，非验收结论。共用主卡 `tasks/G6/V1-020` 的用例与原始输出目录；
> 机器：Win11 build 26200 / i5-13500 / UHD 770 / MSVC Release+Debug / MinGW Release。

## 0. 开工四项自查

| 检查项 | 结果 | 依据 |
| --- | --- | --- |
| 需求编号可检索 | **通过** | `docs/01:107` M3-05、`:108` M3-06、`:111` M3-09 原文；明细见 `tasks/G6/V1-020/artifacts/selftest-0-inputs.txt` |
| 必读章节可定位 | **通过** | `docs/05 §7.2/AC-08`、`docs/02 §7`、`docs/06 §4.2`、`CONTRACT §2/§6/§18.2`、`docs/14 §3`、`docs/19 §0.B`、`docs/20 §0`；同上 |
| 上游交付物可获取 | **通过** | `main` @ `560087a`，`round/47` 从该提交拉出；`g5_encoder_list.ps1` 17 断言、`g5_inject`/`g5_closeout` 的 `--fail-encoder` 无参用法、轮 46 基线二进制（SHA256 `795CE22F…5125`）均可取 |
| 文件域无交集 | **通过** | 本轮仅 `S4-008`（`src/encode/`、`src/recorder/`、`tasks/G6/S4-008/*`）与 `V1-020`（`docs/`、`verify/cases/`、`tasks/G6/V1-020/*`）；无重叠、无其它并行任务 |

## 1. 自测命令与输出

| 命令 | 结果 | 产物 |
| --- | --- | --- |
| `cmake --build build-msvc --config Release` / `--config Debug` / `cmake --build build-mingw` | 三个 `exit=0 warnings=0` | `tasks/G6/V1-020/artifacts/build-*.log` |
| `.\build-msvc\bin\Release\sr-recorder.exe --list-encoders` | `mf-h264-sw available=1`；`mf-h264-hw available=0` + 非空原因；278ms | `EVIDENCE.md §P0-1 #1` |
| `... --record --seconds 2 --out <tmp> --encoder mf-h264-hw` | `record_exit=6`，`encoder open failed: ... unavailable: MFT 协商失败（encoder rejected NV12 input type）`，无成片 | `EVIDENCE.md §P0-1 #2` |
| `... --list-encoders --fail-encoder hardware` | `mf-h264-hw available=0`、原因含 `injected`；`mf-h264-sw available=1` | `tasks/G6/V1-020/artifacts/r47-green.txt` run8 |
| `... --list-encoders --probe-repeat 5` | 5 轮 `1/0` 一致；句柄 268→280 | 同上 run11 |
| `... --list-encoders --probe-repeat 5 --fail-encoder hardware` | 句柄 204×5，增长 0 | 同上 run12 |
| `pwsh -NoProfile -File verify/cases/g5_encoder_consistency.ps1 -RecorderPath <旧二进制>` | `RESULT: 4 PASS / 8 FAIL` | `artifacts/r47-red.txt` |
| `pwsh -NoProfile -File verify/cases/g5_encoder_consistency.ps1` | `RESULT: 12 PASS / 0 FAIL` | `artifacts/r47-green.txt` |
| `pwsh -NoProfile -File verify/cases/g5_encoder_list.ps1` | `17 PASS / 0 FAIL`（UI 日志与 recorder 规范行逐字一致） | `artifacts/reg-g5_encoder_list.txt` |
| `pwsh -NoProfile -File verify/cases/g5_inject.ps1` / `g5_r43_closeout.ps1` | `28 PASS / 0 FAIL` / `12 PASS / 0 FAIL / 0 SKIP`（`--fail-encoder` 无参兼容） | `artifacts/reg-g5_inject.txt`、`reg-g5_r43_closeout.txt` |

## 2. 红-绿对照

| 新断言 | 旧实现实测（轮 46 二进制） | 新实现实测 |
| --- | --- | --- |
| `r47-forward-available-items` | FAIL：`mf-h264-hw` 列表 `available=1`，强制后 `exit=0 / encoder=mf-h264-sw`（静默回退） | PASS：可用项只有 `mf-h264-sw`，`exit=0 / encoder=mf-h264-sw` |
| `r47-no-silent-fallback` | FAIL：出现 `encoder=mf-h264-sw` | PASS：不出现 |
| `r47-backward-unavailable-items` | PASS（3 厂商项） | PASS（4 项，多出 `mf-h264-hw`，全部 `exit=6` + 原因 + 无成片） |
| `r47-injected-hw-unavailable` | FAIL：`exit=1`（旧 CLI 不接受 `hardware` 参数） | PASS：`available=0` + 原因含 `injected` |
| `r47-injection-is-real-probe` / `r47-injected-forced-exit6` | FAIL / FAIL | PASS / PASS |
| `r47-repeat-state-consistent` / `r47-own-resource-no-leak` | FAIL（无 `probe_iteration` 行） | PASS（状态 1/0 ×5；跳过硬件激活时句柄 204→204） |
| **合计** | **4 PASS / 8 FAIL** | **12 PASS / 0 FAIL** |

覆盖状态迁移：回退态 `107/2/7` → 用例全绿后 `108/1/7`（两次 `--write` 输出在
`tasks/G6/V1-020/artifacts/coverage-rollback*`、`coverage-final*`）。

## 3. 回归

| 用例 | 结果 |
| --- | --- |
| `g5_encoder_list` | `17 PASS / 0 FAIL` |
| `g5_inject`（`--fail-encoder` 无参全失败路径） | `28 PASS / 0 FAIL` |
| `g5_r43_closeout` | `12 PASS / 0 FAIL / 0 SKIP` |
| `g5_format` / `g5_format_selftest` / `g5_storage` / `ui_smoke` ×3 | `15/0`（首次 14/1，已复跑）、`22/0`、`14/0`、`3×103/0` |
| `g5_encoder_consistency`（新增） | `12 PASS / 0 FAIL` |
| `g5_closeout`（附加抽样） | `17 PASS / 1 FAIL`：用例硬编码「不可用行 == 3」，未授权改、未改；已在 `HANDOFF.md` 上报 |
| 静态四项 | 覆盖 `--check` PASS、契约 8/8、卡面 60 卡 0 冲突、域表 0 mismatch |

> 依据：产品改动集中在 `src/encode/` 与 `src/recorder/`，按「相关集成用例 + 规定回归集」
> 抽样并全跑；三构建零告警。附加抽样 `g5_closeout` 的红项如实保留。

## 4. DoD 勾选

- [x] 正向：`available=1` 项强制指定 `exit 0` 且 `encoder=<id>`
- [x] 反向：`available=0` 项强制指定 `exit 6` 且原因非空、无成片
- [x] 注入：`--fail-encoder hardware` 使 `mf-h264-hw` 变 `unavailable`；撤注入后恢复
- [x] 新增断言先红后绿，旧值已记录（旧二进制 SHA 留档）
- [x] MSVC Release / MSVC Debug / MinGW Release 零告警；既有回归零失败（附加红项已说明）
- [x] 未改 `verify/` 下任何判据与用例；未改 `vcpkg.json` / `CMakeLists.txt` / `src/core/`
- [x] 自动选择的 M3-06 回退语义未变；`--fail-encoder` 无参语义未变
- [x] 探测器无 D3D device / processor 持有；失败可重试（句柄与状态护栏实测）
- [x] 无死代码、无调试残留（临时分段诊断已删除）
- [x] 四件套齐全，HANDOFF 四问答齐，`## 交付状态` 与实际一致
- [x] 未自评「已验收」
