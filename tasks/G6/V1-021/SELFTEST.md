# SELFTEST: V1-021（轮 48 主卡）

> 全部为**执行方自测**，不是验收结论。原始输出在 `artifacts/`（`.gitignore` 排除，命令可重跑再生）。
> 机器：Win11 build 26200 / i5-13500 / UHD 770 / ffprobe & ffmpeg 9.0.2 /
> PowerShell 7 / MSVC Release+Debug / MinGW Release。

## 0. 开工四项自查

| 检查项 | 结果 | 依据 |
| --- | --- | --- |
| 需求编号可检索 | **通过** | `docs/01`：`M3-04`:106、`M3-05`:107、`M3-09`:111、`M6-04`:199、`M1-27`:59、`M6-06`:201；原始输出 `artifacts/selftest-0-inputs.txt` |
| 必读章节可定位 | **通过** | `docs/05 §7.2`:214、`docs/06 §4.2`:177、`docs/20 §0`:8、`CONTRACT §18.2`:279、`docs/02 §7`:222、`docs/22 §1`:10、`docs/INDEX.md`:33；同上 |
| 上游交付物可获取 | **通过** | `main` @ `91bbbe2`（用户确认 PR #7 `1bc206a` / PR #8 `91bbbe2` 已合并），`round/48` 从该提交拉出；`git fetch` 因 TLS `unexpected eof` 未成功，但本地 `origin/main` = `main` = `91bbbe2`，与用户书面确认一致；轮 47 红端证据包 `tasks/G6/V1-020/artifacts/reg-g5_closeout.txt`（17/1）、`reg-g5_format*.txt`、旧二进制（`build-msvc/bin/Debug` 另存）、ffprobe/ffmpeg 均可取 |
| 文件域无交集 | **通过** | 本轮仅 `S4-006`（`src/mux/`、`src/recorder/`、`tasks/G6/S4-006/*`）与 `V1-021`（`verify/cases/g5_closeout.ps1` + 新增两个 verify 文件、`docs/07/22/INDEX.md`、`tasks/G6/V1-021/*`）；白名单逐项比对无重叠，无其它并行任务 |

> 规划侧只下发 `.codebuddy/plans/轮48指令.md`（无现成 `TASK.md`），按轮 47 `S4-008` / `V1-020` 的派生方式立卡；
> `g5_closeout.ps1` 的修改由轮 48 卡 §注意事项 #4 **明确授权**。

## 1. 自测命令与输出

| 命令 | 结果 | 产物 |
| --- | --- | --- |
| `pwsh -NoProfile -File artifacts/r48-closeout-red.ps1` | 红 1：`17 PASS / 1 FAIL / 0 SKIP`；红 2（变异体）：`19 PASS / 1 FAIL / 0 SKIP` | `artifacts/r48-closeout-red.txt` |
| `pwsh -NoProfile -File verify/cases/g5_closeout.ps1` | `RESULT: 20 PASS / 0 FAIL / 0 SKIP` | `artifacts/r48-closeout-green.txt` |
| `pwsh -NoProfile -File verify/cases/g5_mp4.ps1 -RecorderPath <轮47二进制>` | `RESULT: 0 PASS / 13 FAIL` | `artifacts/r48-mp4-red.txt` |
| `pwsh -NoProfile -File verify/cases/g5_mp4.ps1` | `RESULT: 13 PASS / 0 FAIL` | `artifacts/r48-mp4-green.txt` |
| `python verify/tools/mp4_box_check.py <r48.mp4>` | `RESULT: 42 PASS / 0 FAIL` | 用例产物 `box-check.txt` |
| `pwsh -NoProfile -File verify/cases/g5_format.ps1`（×3）/ `-CbrScene active`（×2） | 五次各 `15 PASS / 0 FAIL / 0 SKIP` | `artifacts/r48-format-1..5.txt`、`r48-format-5runs-summary.txt` |
| `pwsh -NoProfile -File verify/cases/g3_playback.ps1` / `g3_recovery.ps1` / `g4_gate.ps1` | `11/0`、`9/0`、`12/0` | `artifacts/reg-g3_playback.txt` 等 |
| `pwsh -NoProfile -File verify/cases/g5_encoder_list.ps1` | 首跑 `15/1` → 复跑 `17/0` | `artifacts/reg-g5_encoder_list.txt`、`reg-g5_encoder_list-run2.txt` |
| `pwsh -NoProfile -File verify/cases/ui_smoke.ps1` ×4 | run1 `102/1`；run2/3/4 连续 `103/0` | `artifacts/reg-ui_smoke-1..4.txt` |
| `pwsh -NoProfile -File verify/tools/build_release.ps1` | `RESULT: 17 PASS / 0 FAIL` | `artifacts/r48-release-build.txt` |
| `cmake --build build-msvc --config Debug` / `--config Release` / `cmake --build build-mingw` | 三个 `exit=0 warnings=0` | `artifacts/build-*.log` |
| `python verify/tools/coverage_report.py --check` / `contract_check.py` / `card_consistency_check.py` / `domain_table_check.py` | `up to date`（108/1/7）/ `8 PASS, 0 FAIL` / `62 卡 0 冲突` / `MISMATCH_COUNT=0` | `artifacts/r48-static-checks.txt`、`r48-final-verification.txt` |
| 交付前一次性自检 | `g5_mp4` 13/0、`g5_closeout` 20/0、静态四项全过 | `artifacts/r48-final-verification.txt` |

## 2. 红-绿对照

| 新断言 / 用例 | 旧值（红） | 新值（绿） |
| --- | --- | --- |
| `g5_closeout` 目标断言组 | 原 `m6-04-02-hw-unavailable-with-reason`（`==3`）在 4 行 unavailable 列表上 **FAIL**，`RESULT: 17 PASS / 1 FAIL` | 三条数量无关断言全 PASS，`RESULT: 20 PASS / 0 FAIL / 0 SKIP` |
| 反向用例的变异测试 | 守卫换成 `count >= 3` → `m6-04-02-reverse-blank-reason-fails` **FAIL**（`19 PASS / 1 FAIL`），证明 `>=3` 会放过「新加项没写原因」 | 正式守卫下反向断言 PASS（抹掉 1 行 reason → 守卫返回 False） |
| `g5_mp4` 全 13 条 | 轮 47 二进制：`0 PASS / 13 FAIL`（`--format` 未知参数、`exit=1`、无成片） | 新构建：`13 PASS / 0 FAIL`（含 box 42/0、两容器可播、exit 2） |
| `mp4_box_check.py` | 首版产品（PTS 差当 stts）：`format duration=7.366667`、`avg_frame_rate=4500/221`、ffmpeg 报 DTS 非单调 | 修正后（DTS 排序 + ctts）：`5.038730`、`30/1`、解码零 error、`42 PASS / 0 FAIL` |

覆盖状态：本卡与 `S4-006` 都**不声明** `## 覆盖子句`，`coverage_report.py --check` 保持
`108 / 1 / 7`。M3-09 三条留轮 49 收口后统一翻绿，起步切片不制造「结论宽于证据」。

## 3. 回归

| 用例 | 结果 |
| --- | --- |
| `g3_playback` / `g3_recovery` / `g4_gate` | `11/0` / `9/0` / `12/0` |
| `g5_encoder_list` | 首跑 `15/1`（UI 控件 hwnd=0 时序抖动），复跑 `17/0` |
| `ui_smoke` | 首跑 `102/1`（foreground_hwnd=0），run2/3/4 连续三次 `103/0` |
| `g5_closeout` / `g5_mp4` | `20/0/0` / `13/0` |
| `g5_format` 5 连（含 2 次 active） | 五次 `15/0/0`，偏差 3.2~3.5%、单秒峰值 6,000~6,158kbps |
| `build_release.ps1` | `17/0` |
| 静态四项 | 覆盖 `--check` PASS、契约 8/8、卡面 62 卡 0 冲突、域表 0 mismatch |
| 三构建 | MSVC Debug/Release、MinGW Release 零告警 |

> 依据：轮 48 卡 §通过判据 #5 的规定回归集全跑；附加 `build_release.ps1` 核发布包。
> 两处 UI 时序抖动逐次留档、未改判据，以复跑后连续三次绿作为结论依据。

## 4. DoD 勾选

- [x] 开工四项自查全部通过并已记录（§0 + `artifacts/selftest-0-inputs.txt`）
- [x] 任务卡每条需求 → 一条证据，含命令与输出（`EVIDENCE.md`）
- [x] 红-绿对照表完整（旧值 + 新值），旧二进制 SHA 留档
- [x] `g5_closeout` 只改目标断言组 + 反向用例，其它断言一字未动；未改成 `>=3`
- [x] `g5_format` 5 连原始输出逐次留档（≥2 次 active），未挑最好那次
- [x] 三构建零告警；规定回归集零失败（抖动已说明）
- [x] 未改 `verify/` 下判据与阈值；`g5_format.ps1` 本轮一字未动
- [x] 改动未超出白名单；`vcpkg.json` / 根 `CMakeLists.txt` / `CONTRACT.md` / `docs/01` 未改
- [x] 文档已同步（`docs/22` §1/§2/§3、`docs/INDEX.md`、`docs/07` 自述 + 主表留白）
- [x] O0 ⑤⑥ 拍板如实回写为「范围收窄的书面例外」，未冒充 PASS、未自行宣布可发布
- [x] `HANDOFF.md` 四问齐答，含「哪些需求没做完」与文档缺陷单
- [x] 覆盖子句只列实际覆盖的（无）；`## 交付状态` 与实际一致
- [x] 已知限制已写明（含降级验证：本机非 B 机、`--display 0` 单屏代理）
- [x] 未自评「已验收」
