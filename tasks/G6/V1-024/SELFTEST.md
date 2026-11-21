# SELFTEST · V1-024（轮 52）

## §0 开工四项自查（`docs/06 §4.2`，缺一不开工）

| 项 | 结论 | 依据 |
| --- | --- | --- |
| 需求编号可检索 | **通过** | `docs/01` M3-09 原文：「容器格式：MKV（V1.0 默认）与 MP4（`--format mp4`）」，验收要点「**MP4 优先 faststart 布局**；MKV 支持多音轨与更长容错；切换格式不影响录制流程」。本轮只做「faststart 布局」的性能与判据口径，不改需求原文 |
| 必读章节可定位 | **通过** | `docs/26 §7`（`F1-51` moov 线性增长）、`docs/22 §1`（六条判据差距）、`docs/05 §7.1`（分级）/ `§7.2`（发布判据）、`docs/20`（分支/PR/CI/一行制）、`CONTRACT.md §7`（IPC，本轮未动）/ `§18.2`（退出码表）；`.codebuddy/plans/轮52指令.md`、`验收报告-轮51.md §R1~R4` 均存在 |
| 上游交付物可获取 | **通过** | `main` @ `91b96c1`（含 rc.1 发布记录 `docs/24`、`docs/27` 0.1 范围冻结）；`build-msvc` 与 `build-mingw` 可直接增量构建 |
| 文件域无交集 | **通过** | 本轮单卡：实现仅 `src/mux/Mp4Writer.*` + 接线 `src/recorder/{RecordingSession.*,main.cpp}`；用例 `verify/cases/g5_mp4.ps1` + 两个新用例；工具 `verify/tools/mp4_box_check.py`；文档 `docs/{07,22,26}`。无并行任务占用 |

## §1 本轮改动清单（越界自查）

| 文件 | 性质 |
| --- | --- |
| `src/mux/Mp4Writer.h` / `.cpp` | 预留区写入、moov 回填、8 字节对齐、回退路径、收尾耗时观测 |
| `src/recorder/RecordingSession.h` / `.cpp` | `Options.mp4_reserve_bytes`、`MuxerRouter` 转发、收尾 `faststart_*` 日志 |
| `src/recorder/main.cpp` | `--mp4-reserve <bytes>`（`SR_TEST_HOOKS=ON` 才编译） |
| `verify/cases/g5_mp4.ps1` | 判据拆分（通用/短素材）+ 预留命中 + 回退（日志/可播）+ 长素材通用门槛 |
| `verify/cases/g5_faststart_perf.ps1` | **新**：判据 ①②③ 的大素材实测 |
| `verify/cases/g5_f342_repro.ps1` | **新**：`F3-42` 三场景定向复现 |
| `verify/tools/mp4_box_check.py` | `moov-in-first-64k` 按 `mvhd.duration` 分流（>60s SKIP） |
| `docs/07`、`docs/22`、`docs/26` | 台账行 + 判据数字 + 缺陷表（`F3-42` 结论、`R1-52` 新发现） |

**未改**：`docs/01`（需求原文）、`docs/05`（判据）、`vcpkg.json`、`src/mux/MkvWriter.*`、`verify/` 下其他用例的断言、默认容器（仍 MKV）、MP4 崩溃恢复（V1.1）。

## §2 三构建（零 error 零告警）

```powershell
cmake --build build-msvc --config Release      # ✔ 见 EVIDENCE §0 的 exe
cmake --build build-msvc --config Debug        # ✔
cmake --build build-mingw                      # ✔
```

| 构建 | 结果 |
| --- | --- |
| MSVC Release | 全部目标成功；`Mp4Writer.cpp` / `RecordingSession.cpp` / `main.cpp` 重编译无 warning |
| MSVC Debug | `sr-recorder/ sr-ui/ srctl/ srprobe` 全部生成，无 warning |
| MinGW (Ninja) | `[12/12]` 全部链接成功，无 warning |

## §3 静态检查（四件套，CI 的 checks job 同口径）

```powershell
python verify/tools/contract_check.py          # 汇总: 8 项检查, 8 PASS, 0 FAIL
python verify/tools/coverage_report.py --check # coverage table is up to date
python verify/tools/card_consistency_check.py  # card_consistency: OK —— 扫描 65 张卡，0 处冲突
python verify/tools/domain_table_check.py      # MISMATCH_COUNT=0
```

## §4 用例门槛（原始输出见 `EVIDENCE.md §6`）

```powershell
pwsh -File verify/cases/g5_mp4.ps1            # RESULT: 23 PASS / 0 FAIL
pwsh -File verify/cases/g5_multiaudio.ps1     # RESULT: 10 PASS / 0 FAIL
pwsh -File verify/cases/g5_inject.ps1         # RESULT: 28 PASS / 0 FAIL
pwsh -File verify/cases/g4_gate.ps1           # RESULT: 12 PASS / 0 FAIL
pwsh -File verify/cases/g3_playback.ps1       # RESULT: 11 PASS / 0 FAIL
pwsh -File verify/cases/ui_smoke.ps1  (×3)    # RESULT: 103 PASS / 0 FAIL / 0 SKIP（×3）
pwsh -File verify/cases/g5_faststart_perf.ps1 # FASTSTART-PERF: 4 PASS / 0 FAIL
pwsh -File verify/cases/g5_f342_repro.ps1     # F342-REPRO: 未复现（静默退出 0 次）
```

> 注：`g5_mp4` 由 19 条变 23 条是**拆分 + 新增**，不是放宽（红端 20 PASS / 3 FAIL 见 `EVIDENCE.md §2.3`）。

## §5 先红后绿（`docs/06 §5`）

| 断言 | 红（改前） | 绿（改后） |
| --- | --- | --- |
| `r52-faststart-reserve-hit` | FAIL（旧 exe 无 `faststart_mode` 日志、顶层无 `free`） | PASS（`reserve=18960 moov=5200`、`free@5232+13760`） |
| `r52-faststart-fallback-log` | FAIL（旧 exe 不认 `--mp4-reserve`，exit 1） | PASS（`faststart_fallback=move`） |
| `r52-faststart-fallback-playable` | FAIL（回退产物不存在） | PASS（可播 + box check 43/0） |
| 判据 ①（1.15GB 收尾） | 旧实现 0.60s（轮 51 实测，0.52s/GB） | 0.976ms（`faststart_close_us=976`） |
| 判据 ②（8h 外推） | 旧实现 0.52s/GB × 21GB ≈ **10.9s > 10s（FAIL）** | 0.869ms/GB × 21GB = **0.02s（PASS）** |

> 旧实现 = `build-release/bin/Release/sr-recorder.exe`（2026-11-18，轮 51 打包产物）。
> 命令：`pwsh -File verify/cases/g5_mp4.ps1 -RecorderPath build-release\bin\Release\sr-recorder.exe`，
> 原始输出 `verify/artifacts/r52-g5_mp4-red-old-exe.txt`（`RESULT: 20 PASS / 3 FAIL`）。

## §6 Definition of Done 逐项

- [x] 任务卡每条需求都有对应证据（`EVIDENCE.md §1/§3/§4/§5`）
- [x] 新增断言先红后绿，旧值已记录（`§5`）
- [x] Release + Debug + MinGW 均编译通过，零告警（`§2`）
- [x] 相关回归零失败（`§4`；依据：全量用例在本机实跑，非抽样）
- [x] 无死代码 / 无调试残留（`--mp4-reserve` 是受 `SR_TEST_HOOKS` 保护的既有风格调试开关，已写 `--help`）
- [x] 改动未超出文件域（`§1`）
- [x] 接口无对外契约变更（`Mp4Writer` 是内部类；`--mp4-reserve` 是调试开关，`CONTRACT.md` 无需改）
- [x] 文档已同步（`docs/07` 行 + 记录、`docs/22` 判据数字、`docs/26` 缺陷表）
- [ ] **LT-01 第 3 次 8h 未出结果**（见 `HANDOFF.md §3`，不得计为通过）
- [ ] **issue 模板演练未完成**（`gh` 无法连通 GitHub API，见 `HANDOFF.md §3`）
