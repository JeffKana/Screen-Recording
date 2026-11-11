# EVIDENCE: V1-003（轮 42 · 两项裁定落地 + 发布预演，主卡）

> 口径声明：本文件是**执行方证据**，不是验收结论；验收方独立重跑后判定 PASS/PARTIAL/FAIL。
> 数据来源：2026-11-09，`build-msvc/bin/Release`（SR_TEST_HOOKS=ON）、
> `build-release/bin/Release`（OFF）、`dist/ScreenRecorder-0.1.0-win-x64.zip`；
> 本机 Win11 家庭版 build 26200 / 双屏 1920×1080 / 默认 100% DPI / **HVCI Enabled=1**。
> 所有文本证据在 `tasks/G6/V1-003/artifacts/`，均为当次命令的真实输出。

## 0. 开工四项自查（`docs/06 §4.2`）

| 检查项 | 结果 | 依据 |
| --- | --- | --- |
| 需求编号可检索 | 通过 | M8-05:255、M6-04:199、M6-08:203、M6-10:205、M6-11:206、M3-10:112 均能在 `docs/01` 查到原文；逐条核过验收要点 |
| 必读章节可定位 | 通过 | `.codebuddy/plans/轮42指令.md` §1~§5、`验收报告-轮41.md` §2/§3/§5、`docs/05 §2.1/§7.2`、`docs/06 §4.2/§8.4`、`docs/11b §6`、`CONTRACT §2/§18.2` 均在位 |
| 上游交付物可获取 | 通过 | 轮 41 四套交付包、`build-msvc` / `build-release` / `build-mingw` 三套产物、`dist/*.zip` + `release-summary.json`；未用旧版本开工 |
| 文件域无交集 | 通过 | 主卡 `verify/`（部分用例与工具）+ `docs/` + `tasks/G6/V1-003/`；S1-010 独占 `src/recorder/`、S2-003 独占 `src/compositor/Nv12Converter.h`、S5-005 独占 `src/app/`、V1-005 独占 `tasks/G5/V1-005/`、V1-006 独占 `verify/cases/g5_r42_hvci.ps1` + `docs/14`；`domain_table_check` MISMATCH_COUNT=0 |

> 轮 42 下发时仓库无预置任务卡，按验收方 `.codebuddy/plans/轮42指令.md` 建卡
> （沿用轮 40 S5-004 / 轮 41 V1-002 做法），未增删需求。

---

## 1. P0 M8-05 新口径（裁定 A + 60s 长窗口护栏）

- 验证方式：先写护栏断言 → 在旧口径上证明会假 FAIL → 新口径 5 连绿 → 独立性自检证明「护栏不是复读机」。
- 命令：`pwsh -File verify/cases/g5_longwindow_selftest.ps1`
- 输出：`artifacts/selftest-g5_longwindow_selftest.ps1.txt` — **10 PASS / 0 FAIL**，逐条：
  `legacy-10s-window-false-fail`（红：旧 10s 口径在同组真值上误差 23.3% → FAIL，复现轮 41 run3）、
  `configured-bps-main-criterion-pass`（绿：新口径 0.00%）、`configured-bps-formula-is-6192kbps`、
  `guard-rejects-10s-window`、`guard-catches-real-30pct-overshoot`、`guard-data-missing-fails-not-skip`、
  `both-assertions-present`、`both-assertions-independent-pass`、
  `guard-fails-while-main-passes`（**主判据 PASS 但长窗口真超标 → 套件仍 FAIL**）、`guard-fail-is-not-skip`。
- 两条断言的实现：
  - ① 主判据 `m8-05-01-estimate-error-20pct`：`expected = (free − stop) / (configured_kbps×1000/8)`，
    其中 `configured_kbps = 6000（用例传给 --bitrate）+ 192（src/core/util/DiskEstimate.h:8 音频常量）= 6192` → 774,000 B/s。
    **配置码率由用例自己的参数构造，不读产品的任何估算字段**。10s 短窗口实测速率只记录、不参与判定。
  - ② 独立护栏 `m8-05-longwindow-bitrate-20pct`：`verify/lib/LongWindow.ps1` 用
    **文件系统字节数 / 独立 Stopwatch 墙钟**算平均写入速率，与配置码率比 ≤20%；
    数据缺失或窗口 <60s 一律 **FAIL 而非 SKIP**（验收方拒 C：SKIP 会藏真超标）。
    护栏不消费 `estimated_record_seconds`，不是产品公式的复读机。
- 5 连绿：`pwsh -File tasks/G6/V1-003/artifacts/run-five-runs.ps1`
  → `artifacts/R2-five-runs.txt`（19:00:19 – 19:10:10），**5 次全部 `14 PASS / 0 FAIL / 0 SKIP`**：

| 次 | 主判据 | 独立护栏（≥60s） |
| --- | --- | --- |
| 1 | 估算 13,033s / 期望 13,033s / 误差 0.0% | wall=62.43s, 48,053,508 B, 平均 769,730 B/s, 偏差 0.55% |
| 2 | 13,002s / 13,002s / 0.0% | 62.49s, 48,053,507 B, 769,020 B/s, 0.64% |
| 3 | 13,016s / 13,016s / 0.0% | 62.45s, 48,053,829 B, 769,532 B/s, 0.58% |
| 4 | 13,009s / 13,009s / 0.0% | 62.43s, 48,053,526 B, 769,683 B/s, 0.56% |
| 5 | 13,014s / 13,014s / 0.0% | 62.43s, 48,053,523 B, 769,670 B/s, 0.56% |

- `g5_estimate_selftest.ps1` **仍 9/9**（`artifacts/selftest-g5_estimate_selftest.ps1.txt`），
  含「齐全但真超标仍 FAIL」，三档取数语义未被改软。
- 红-绿对照：

| 断言 | 旧实现实测 | 新实现实测 |
| --- | --- | --- |
| 主判据用配置码率 | 10s 实测 954,577 B/s → 误差 23.3% FAIL（假失败） | 774,000 B/s → 0.00% PASS |
| 长窗口护栏 | 断言不存在（套件不会发现写入率超标） | 注入 29.2% 超标 → FAIL；真实 0.56% → PASS |
| 两条同时存在 | 只留主判据时「主判据 PASS + 护栏超标」仍全绿（空测） | 同一场景下套件退出码 1（护栏有判别力） |

## 2. P0 M8-05#1 回退 → 5 连绿 → 翻绿

- 回退：`tasks/G5/V1-005/TASK.md` 声明「有卡未实现」→ `--write` → 覆盖表 **98/2/16** →
  `--check` **PASS（第 1 次）**；挂账理由写入 `docs/09` 最后一列。
- 翻绿：5 连绿后该卡改「已实现」→ `--write` → **99/1/16** → `--check` **PASS（第 2 次）**。
- `docs/07` 两行备注（「轮 42 状态迁移留痕」）：回退一行、翻绿一行，注明「计划内状态迁移，不是回归」。
- 生成器 `coverage_report.py` 只在 `render()` 增加两行轮 42 说明文字，**未动合并规则**
  （`coverage_model.py` / `build_rows` / `merge_clause_state` 一字未改）。

## 3. P0 M6-04#2 措辞落 `docs/01`

- 命令：`Select-String -Path docs/01-功能需求清单.md -Pattern '不引入 GPL|^## 变更记录'`
- 输出：`docs/01:199` 现为「均失败时落到**内置软件编码器（MF H.264 软编，不引入 GPL 组件）**且录制成功」；
  `docs/01` 末尾新增 `## 变更记录`，列明轮次 41（裁定）/ 42（落地）、日期 2026-11-09、
  依据 `docs/03 §6`（软编回退首选 MF H.264 软件 MFT、libx264 GPLv2）、`§8`（`--disable-gpl`）、
  `§10`（闭源分发需隔离 GPL），影响范围写明「仅措辞与许可边界，M6-04#2 仍已实现，`docs/03` 未改」。
- `docs/03` 未改（可核对文件 mtime / 全文无轮 42 字样）。
- `--write` 后覆盖表 M6-04#2 文本同步、状态仍「已实现」，`--check` PASS。

## 4. P0 无卡收口 5 条

- 命令：`pwsh -File verify/cases/g5_r42_closeout.ps1`、`pwsh -File verify/cases/g5_r42_hvci.ps1`
- 输出：`artifacts/g5_r42_closeout.ps1.txt` **12 PASS / 0 FAIL / 0 SKIP**；
  `artifacts/g5_r42_hvci.ps1.txt` **7 PASS / 0 FAIL / 0 SKIP**。
- 逐条证据（一需求一条证据，含可重跑命令与实测值）：

| 子句 | 归属卡 | 行为断言与实测值 |
| --- | --- | --- |
| M6-08#2 | S1-010（+S2-003 提供 `ForceCpu()`） | `--force-cpu` → `record_exit=0 converter=cpu frames_encoded=90`；成片 duration=2.999s、video+audio、ffmpeg 解码零错误；对照组 `converter=gpu-videoprocessor`。**降级验证**：强制开关触发既有 CPU 中转路径，非真实 GPU 故障 |
| M6-10#2 | V1-006 | 真实环境 `HVCI.Enabled=1`、OS=Win11 家庭版、build 26200；包内驱动类文件 0；`install.ps1 -InstallDir <tmp>` 退出码 0 且产物存在；包内 exe `record_exit=0`、成片 2,326,391 B、双轨解码零错误；包内 sr-ui `--version` 退出码 0 + 日志 `test_hooks=0`。**真实 HVCI 环境实测，非模拟** |
| M3-10#1 | S5-005 | 四档各自联动值：极致 60fps/12000kbps/q90、高清 30/6000/70、标准 30/4000/60、省空间 15/2000/50；四组互不相同且每次写 `preset_saved` 日志；高级面板 `--set-fps 45` 单独覆盖后落盘 fps=45，档位名仍在、未被预设强行回写 |
| M3-10#2 | S5-005 | 设置页控件 109 文本「当前预设：极致 · 60 fps · 12000 kbps · 画质 90（高级面板可单独覆盖）」；档位名无乱码、下拉（控件 108）选中序号 0 |
| M6-11#2 | S5-005 | `--simulate-backpressure 7` → 状态栏首行「⚠ 背压（丢帧 7）」；不注入时状态栏无「背压」。**代理验证**：注入走与真实丢帧相同的 UI 渲染分支 |

- 首轮实测发现并修复的两处真实缺陷（不是放宽判据）：
  1. M3-10#2 首轮显示「鏋佽嚧」乱码、下拉选中 4（自定义）：用例用 UTF8 手写中文 INI，
     而 Windows profile API 按 ANSI 读。改为由产品自己的 `--set-preset` 落盘，并新增
     `m3-10-02-preset-name-not-garbled` 断言（档位名正确 + 序号 0）。
  2. `g5_r42_hvci.ps1` 首轮 `m6-10-02-ui-runs-under-hvci` FAIL：sr-ui 是 GUI 子系统，
     `--version` 写父控制台不进管道。改用退出码 + 日志 `test_hooks=0` 判定（取数修复，判据未放宽）。
- 未收口的 M7-13#1/#2、M1-24#1/#2 等：需真实 UI 绘制或成片像素级验证，本轮不拿日志文案冒充功能，
  在 `HANDOFF.md` 第四问挂账。

## 5. P1 矩阵 1 格实测

- 命令：`pwsh -File verify/cases/g5_r42_matrix.ps1`
- 输出：`artifacts/r42-matrix.txt` — **6 PASS / 0 FAIL / 0 SKIP**，`MATRIX_DATE=2026-11-09`。
- 三件套（`docs/11b §6`）实测值：`--display 0` 单屏录 60s → `record_exit=0`、wall=60.04s、
  frames_encoded=1800、frames_dropped=0、成片 46,498,959 B；抽首/中/末三帧（n=30/900/1770）
  非全黑像素各 21,348、原始尺寸稳定 6,220,800 B/帧；音画尾偏差 **1ms**（v=59999/a=60000）；
  duration=59.999s、双轨、ffmpeg 全片解码零错误。
- **降级验证**：本机物理双屏 1920×1080×2，用 `--display 0` 只采主屏代表「单屏」，非真实单屏机。
- `docs/14 §3/§4` 已填实测值 + 日期；其余 **11 格仍标未测**，双屏 100% 格仍是「降级旁证」不计 PASS。

## 6. P1 F2 校验 + F3 编号唯一

- F2：`python verify/tools/card_consistency_check.py --tasks-dir <tmp>`（假卡）。
  `artifacts/f2-selftest.txt` — **4 PASS / 0 FAIL**：
  ① 假卡「`docs/17-轮42-假需求裁定申请.md`（待裁定）+ 已实现」→ 退出码 1 且输出含 ASCII 标记
  `[F2_PENDING_REF]`；② 同一引用 + 有卡未实现 → 退出码 0；③ 已裁定归档措辞 + 已实现 → 退出码 0
  （不误伤历史事实）；④ 全仓真实卡 0 冲突。规则只认 `docs/1\d-…裁定申请` 具体路径，
  豁免「已裁定/裁定记录/裁定结果」。
- **F2 编码缺陷（回归首轮真实发现）**：首轮全量回归里 `g5_f2_selftest` 报 2 PASS / 2 FAIL，
  但单独跑是 4/4。根因是断言用中文文本（「F2 待裁定项先翻绿」「0 处冲突」）匹配 Python 的 UTF-8
  输出，而 runner 捕获时按 ANSI 解码成乱码 → 假 FAIL。修法：工具输出加 ASCII 标记
  `[F2_PENDING_REF]`，断言只依赖**退出码 + ASCII 标记**（与编码无关）。同时把 runner 的
  工具调用设 `PYTHONIOENCODING=utf-8` + `Console.OutputEncoding=UTF8`，让中文汇总可读。
  复跑验证：直接跑 4/4、模拟 runner 捕获 4/4（`artifacts/f2-in-runner.txt`）。
  **这是用例取数缺陷，F2 规则本身与判据未放宽。**
- F3：`artifacts/f3-rename.txt`（两个 `11-` → `11a`/`11b`、9 处精确引用 + docs 裸引用同步）、
  `artifacts/f2-archive.txt`（三份已裁定申请归档为「裁定记录」）、`docs/INDEX.md`（编号登记表）。
  `Get-ChildItem docs -Filter *.md` 前缀分组无重复。

## 7. 门判据检查

| 检查 | 命令 | 结果 |
| --- | --- | --- |
| 覆盖表 | `python verify/tools/coverage_report.py --check` | `coverage table is up to date`；**104 已实现 / 1 有卡未实现 / 11 无卡** |
| 契约 C1~C8 | `python verify/tools/contract_check.py` | **8 PASS / 0 FAIL** |
| 卡面（含 F2 新规则） | `python verify/tools/card_consistency_check.py` | **42 卡 0 冲突** |
| 域表 | `python verify/tools/domain_table_check.py` | `MISMATCH_COUNT=0` |
| 三构建 | `artifacts/run-build.ps1` | `msvc-release / msvc-debug / mingw-release` 全部 `exit=0 warnings=0 errors=0` |
| 发布包 | `pwsh -File verify/tools/build_release.ps1` | **17 PASS / 0 FAIL**；包内/构建目录 SHA256 相同；`--help` 四参数齐全、无调试开关 |
| `dist/` | `Get-ChildItem dist -File` | 只有 zip + summary |
| 5 连绿 | `artifacts/R2-five-runs.txt` | 5×14/14 |
| 全量回归 | `artifacts/r42-regression.txt` | 见 `SELFTEST.md §3`；**`g5_format` 有一次 13/1 FAIL（真实但偶发），见下 §9** |

## 8. 已知限制（不得计为通过）

1. **V1.0 仍不可发布**：11 条无卡、M1-27 黑屏例外待终审、M6-06（150% DPI）BLOCKED、
   矩阵 11 格未测、`docs/05 §7.2` 其余判据未跑完。
2. 矩阵首格是**降级验证**（物理双屏用 `--display 0` 只采主屏）。
3. M6-08#2 用 `--force-cpu` 强制开关触发既有 CPU 路径，不是真实混合显卡跨 GPU 故障；
   M6-11#2 用注入丢帧走同一 UI 渲染分支，不是真实队列积压测量（产品无队列深度字段）。
4. M3-10#1 联动 fps / 码率 / 画质三项，**分辨率不随档位缩放**（需求描述提到分辨率，
   但验收要点原文只要求「各参数联动变化」）；该限制在 S5-005 卡与 `HANDOFF` 显式标注。
5. `g5_storage` 的 60s 护栏让单次套件约 2 分钟；5 连绿共约 10 分钟。
6. 执行方不自评为已验收；轮 42 主表行留给验收方独立重跑后填写。

## 9. 回归中 `g5_format` 的偶发 FAIL（真实、不隐瞒、不放宽判据）

- 全量回归 `artifacts/r42-regression.txt` 里 `g5_format.ps1` **13 PASS / 1 FAIL**：
  `m3-04-vbr-peak-any-content — ② 运动源峰值 10,204 / 静态峰值 1,419 vs 上限 9000kbps（判据 ≤102%）`。
  第一次回归（21:22 那轮）是 14/14；同一二进制的另一次单独复跑出现过
  `m3-03-60fps-rate — encode_fps=41 frames=900 wall=16.22`。
- **判定**：这是真实 FAIL，不是假失败；两次是**不同断言**，指向本机负载抖动（VBR 瞬时峰值、60fps 编码吞吐），
  与轮 42 改动的文件域（`--force-cpu` 默认关闭；UI 档位/背压标识在 `src/app/`），
  以及 `g5_format` 的码控/帧率路径。执行方**没有**改 `g5_format` 用例、没有放宽 ≤102% 判据。
- 独立复跑证据：`pwsh -File artifacts/run-format-x3.ps1` → `artifacts/format-x3.txt`：
  **3 次全部 14 PASS / 0 FAIL**（21:53、21:55、21:57，无并发重负载）。
  首轮全量回归（21:22）也是 14/14。
- **结论：判据 §3 #5「回归零失败」在本轮**未稳定达成**——`g5_format` 不稳定，报 PARTIAL，
  交验收方独立重跑裁定；不下「全绿」结论。历史同类不稳定见 `docs/07:80`（ui_smoke 92/9→98/3→101/0）。
- 该项与本轮 P0 五项无关（P0 的 5 连绿、覆盖 104/1/11、双断言、两次 `--check` 均有各自独立证据）。
