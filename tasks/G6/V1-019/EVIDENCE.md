# EVIDENCE: V1-019（轮 46 · 主卡）

> 口径声明：本文件是**执行方自测证据**，不是验收结论；「通过」仅表示本机按命令重跑得到该输出，
> 不代表验收方判 PASS。6 次实测用的是同一台机器、同一 `build-msvc/bin/Release/sr-recorder.exe`
> 与 40s CBR 片段，**不是** 8h 单文件长录；`src/` 本轮净改动 0。
> 数据来源：2026-11-12 11:22~11:44 / `build-msvc`（MSVC Release）/ Win11 家庭版 build 26200 /
> Intel UHD 770 + 2×1920×1080（含虚拟显示器）/ ffprobe 9.0.2 / PowerShell 7。
> 本地原始输出在 `tasks/G6/V1-019/artifacts/`（`.gitignore` 排除 `tasks/**/artifacts/`，
> 重跑下列命令即可再生同名结构；关键行已逐条抄进本文，可脱离 artifacts 核验）。

---

## P0-1 #1 · `docs/01` M3-04#1 判据与变更记录（判据文本照抄 `docs/21 §2`）

- 验证方式：文本比对 + 可检索性检查 + 覆盖模型重新生成。
- 命令：
  ```powershell
  Select-String -Path docs/01-功能需求清单.md -Pattern '不作判据|单秒 ≤2×|全片平均码率'
  python verify/tools/coverage_report.py --write
  python verify/tools/coverage_report.py --check
  ```
- 输出：
  - `docs/01:106` = `| M3-04 | ... | CBR 模式下**全片平均码率**与标称偏差 ≤10%，且**任意单秒**码率不超过标称的 2 倍。VBR 按三条验收... | V1.0 |`
  - `docs/01` 变更记录新增行（前 5 格照抄 `docs/21 §2`，第 6 格 `—` 占位）：
    `| 46 | 2026-11-12 | M3-04 验收要点第 1 句 | 「波动 ≤10%（原按逐秒/5s 滑窗判定）」→「全片平均偏差 ≤10% + 单秒 ≤2×」 | \`docs/21\`、\`tasks/G5/S4-005/EVIDENCE.md\`；MFT 不执行码率约束、过冲来自 P 帧 | — |`
  - 变更记录下方引用块逐字照抄 `docs/21 §2` 两行：
    `> CBR 模式下**全片平均码率**与标称偏差 ≤10%，且**任意单秒**码率不超过标称的 2 倍。`
    `> （逐秒方差与滑窗方差受编码器实现限制，**不作判据**；单秒 2× 为硬挡板，不可放宽。）`
  - `coverage table is up to date`；`docs/09` 中 M3-04#1 验收要点列已变为新文本。
- 结论：通过（本机文本与生成物一致）。`docs/21` 未被修改（裁定原文只读）。
- 复现环境：任意 shell，`python` 3.12。

## P0-1 #2 · `g5_format.ps1` 两条独立断言

- 验证方式：真实录制 40s CBR（静态 + 活动各一次）+ `-CbrOnly` 分析已有成片。
- 命令：
  ```powershell
  pwsh -NoProfile -File verify/cases/g5_format.ps1
  pwsh -NoProfile -File verify/cases/g5_format.ps1 -CbrScene active
  pwsh -NoProfile -File verify/cases/g5_format.ps1 -CbrOnly `
      -CbrFile tasks/G6/V1-019/artifacts/cbr-6runs/active-01.mkv `
      -CbrStdout tasks/G6/V1-019/artifacts/cbr-6runs/active-01.out
  ```
- 输出（`artifacts/g5_format.txt` / `g5_format_active.txt` / `cbr-6runs/f3-g5-format-active01.txt`）：
  - 静态：`PASS cbr-average-deviation — 全片平均 6,195kbps vs 标称 6,000kbps，偏差 3.3%（判据 ≤10%）；bytes_written=30999850 / wall_seconds=40.03，非逐秒再平均（逐秒均值 6,000kbps 仅作对照）`；`PASS cbr-single-second-cap — 单秒峰值 6,000kbps vs 硬上限 12,000kbps（2× 标称 6,000；有效桶 38 个，平均 PASS 也不稀释）`；`RESULT: 15 PASS / 0 FAIL / 0 SKIP`
  - 活动：`PASS cbr-average-deviation — 全片平均 6,207kbps ... 偏差 3.4% ...`；`PASS cbr-single-second-cap — 单秒峰值 6,114kbps vs 12,000kbps`；`RESULT: 15 PASS / 0 FAIL / 0 SKIP`
  - `-CbrOnly`：`PASS cbr-average-deviation — 全片平均 6,197kbps ... bytes_written=31025930 / wall_seconds=40.05`；`PASS cbr-single-second-cap — 单秒峰值 6,099kbps ... 有效桶 38 个`；`RESULT: 2 PASS / 0 FAIL / 0 SKIP`
  - 模拟注入（护栏）：`FAIL cbr-average-deviation`（35,000,000B/40s = 7,000kbps，偏差 16.7%）/ `FAIL cbr-single-second-cap`（16,699kbps > 12,000），见 `artifacts/selftest-green.txt`。
- 结论：通过（两条断言各自独立输出、各含实测值；平均来自全片 `bytes_written/wall_seconds`）。
- 复现环境：本机 MSVC Release 产物 + ffprobe；`-CbrScene active` 调 `ActiveDesktop.ps1` 默认参数。

## P0-1 #3 · 两条反向护栏（19 → ≥21 PASS，先红后绿）

- 验证方式：先把新自检跑在**未修复**的 `g5_format.ps1` 上（红），实现后再跑同一命令（绿）。
- 命令：
  ```powershell
  pwsh -NoProfile -File verify/cases/g5_format_selftest.ps1
  ```
- 输出：
  - 红（旧实现，`artifacts/selftest-red.txt`）：`RESULT: 13 PASS / 9 FAIL`；
    其中 `FAIL r46-average-deviation-fail`、`FAIL r46-single-second-cap-fail`、
    `FAIL r46-average-source-wholefile`、`FAIL r46-window5-criterion-removed`。
  - 绿（新实现，`artifacts/selftest-green.txt` 与 `artifacts/r46-regression.txt`）：`RESULT: 22 PASS / 0 FAIL`；
    `PASS r46-average-deviation-fail — ... FAIL=1 SKIP=0 rc=1`（平均超 10% 必须 FAIL）；
    `PASS r46-single-second-cap-fail — ... FAIL=1 ...`（单秒 2.8× 但平均 0.0% 仍 FAIL）；
    `PASS r46-average-source-wholefile — ... FAIL=0 rc=0`（逐秒均值 9000 但全片 6000，不许逐秒再平均）。
- 结论：通过（22 ≥ 21；两条反向护栏缺一不可，旧值/新值见 `SELFTEST.md §2`）。
- 复现环境：不启动 recorder 的模拟分支，`pwsh -File verify/cases/g5_format.ps1 -SimulateCbrRates ... -SimulateCbrBytes ...`。

## P0-1 #4 · 静态 3 + 活动 3 共 6 次全过 → M3-04#1 转「已实现」

- 验证方式：同一脚本、同一 40s CBR 6000kbps 配置，静态与活动各 3 次；活动用 `ActiveDesktop.ps1` **默认参数**。
- 命令：
  ```powershell
  pwsh -NoProfile -File verify/cases/g5_cbr_probe.ps1 -Seconds 40 -Runs 3 `
      -Mode static,active -OutDir tasks/G6/V1-019/artifacts/cbr-6runs
  # 活动激励进程命令行核验（不传任何弱化开关）
  Get-CimInstance Win32_Process -Filter "Name='pwsh.exe'" |
    Where-Object { $_.CommandLine -like '*ActiveDesktop.ps1*' } |
    Select-Object -ExpandProperty CommandLine
  ```
- 输出（`artifacts/cbr-6runs/probe-summary.txt`，逐次 `*.summary.json` 同值）：

  | 场景 | 全片平均(kbps) | 偏差 | 单秒峰值 / 上限(kbps) | VERDICT | capture_fps |
  | --- | ---: | ---: | ---: | --- | ---: |
  | static-01 | 6,193.8 | 3.23% | 6,000 / 12,000 | PASS | 1.0 |
  | static-02 | 6,246.1 | 4.10% | 6,948 / 12,000 | PASS | 13.0 |
  | static-03 | 6,239.2 | 3.99% | 6,938 / 12,000 | PASS | 1.0 |
  | active-01 | 6,197.4 | 3.29% | 6,099 / 12,000 | PASS | 47.0 |
  | active-02 | 6,197.0 | 3.28% | 6,097 / 12,000 | PASS | 43.0 |
  | active-03 | 6,196.9 | 3.28% | 6,096 / 12,000 | PASS | 40.0 |

  `MODE static runs=3 verdict_pass=3 avg_fail=0 cap_fail=0`；
  `MODE active runs=3 verdict_pass=3 avg_fail=0 cap_fail=0`；`fail_kind=-,-,-`（**没有一次平均超，也没有一次单秒超**）。
  活动命令行（`artifacts/cbr-6runs/active-desktop-params.txt`）：
  `"C:\Program Files\PowerShell\7\pwsh.exe" -NoProfile -File "verify/lib/ActiveDesktop.ps1"`，`ACTIVE_DESKTOP_WEAKENED=False`；
  共用库 `verify/lib/CbrCriteria.ps1::Start-CbrActiveDesktop` 的 `-ArgumentList` 只有 `-NoProfile -File <脚本>`，无 `-Seconds`/`-Windowed`/`-BurstSec`。
  `capture_fps` 静态 1.0~13.0 vs 活动 40.0~47.0，证明三次活动不是静态桌面冒充。
  static-02/03 的逐秒 spread 15.7%/15.5% 按 `docs/21 §2` **不作判据**，未用于判定。
- 翻绿证据：
  ```powershell
  python verify/tools/coverage_report.py --write
  python verify/tools/coverage_report.py --check
  python verify/tools/coverage_report.py
  ```
  → `coverage table is up to date`；`已实现 108, 有卡未实现 1, 无卡 7, 缺理由 0`；
  `docs/09` 中 `| M3-04#1 | ... | 已实现 | — |`；`tasks/G6/V1-014/TASK.md` 交付状态为 `已实现（轮 46：...6/6 全过...）`。
- 结论：通过（6/6；**若有 1 次不过会保持 PARTIAL 并写明是平均超还是单秒超**，本轮未触发该分支，
  `g5_cbr_probe.ps1` 的 `fail_kind` 字段已把两种失败分开记录）。
- 复现环境：本机桌面会话，需要可见 GUI（活动激励是 WinForms 满屏窗口）。

## P0-2 · `docs/19` 拆表（真机阻塞 / 范围工作）

- 验证方式：文本检索两表标题、B 表轮次/卡号、模糊词检查；`docs/09` 挂账理由同步。
- 命令：
  ```powershell
  Select-String -Path docs/19-轮44-收口可行性与判据矛盾核查.md `
    -Pattern '0.A 表|0.B 表|真机阻塞|范围工作|轮 47|S4-006|S4-007'
  Select-String -Path docs/09-V1.0覆盖表.md -Pattern '排轮 47'
  ```
- 输出：`docs/19 §0.A` 4 行（M6-01#1 / M6-04#1 / M6-05#1 / M6-08#1）各含「缺什么（机型/OS/显卡组合）」与「复测条件」；
  `§0.B` 3 行（M3-09#1/#2/#3）各含「实现工作量」「轮 47 + `S4-006`/`S4-007` 独立卡」；
  `docs/09` 三条挂账理由以 `**V1.0 未开工（可做）→ 排轮 47` 开头；
  `§0` 现行表述无「新容器」「待排期」（`§1` 之后的轮 44 原文保留为历史依据链，文件头已声明不用于现行挂账）。
- 结论：通过（B 表给的是轮次与卡号，不是缺机器；MP4 muxer 未塞进收口轮）。
- 复现环境：任意文本检索。

## P0-3 · 两工具口径合一与同场景 ≤2% 对照（F3）

- 验证方式：`verify/lib/CbrCriteria.ps1` 为两个用例唯一的分桶 / 判据 / 激励源实现；
  用同一 active-01 成片分别让两个工具出结论，逐字段比对。
- 命令：
  ```powershell
  Select-String -Path verify/cases/g5_format.ps1,verify/cases/g5_cbr_probe.ps1 `
    -Pattern 'CbrCriteria\.ps1|Get-CbrVerdict|Get-CbrSecondBuckets|Start-CbrActiveDesktop'
  pwsh -NoProfile -File verify/cases/g5_format.ps1 -CbrOnly `
      -CbrFile tasks/G6/V1-019/artifacts/cbr-6runs/active-01.mkv `
      -CbrStdout tasks/G6/V1-019/artifacts/cbr-6runs/active-01.out
  ```
- 输出（`artifacts/cbr-6runs/f3-tool-diff.txt`，比对 `verify/artifacts/format-20261112-112933/cbr-verdict.json`
  与 `artifacts/cbr-6runs/active-01.summary.json`）：

  ```
  F3_TOOL_A_g5_format: whole_avg_kbps=6197.4 avg_dev_pct=3.29 peak_kbps=6099 verdict=True
  F3_TOOL_B_cbr_probe: whole_avg_kbps=6197.4 avg_dev_pct=3.29 peak_kbps=6099 verdict=True
  F3_DIFF: whole_avg_kbps=0.0392 relative_pct=0.0006 avg_dev_pp=0 peak_kbps_rounded=0 verdict_same=True
  F3_VERDICT=PASS (判据：同场景两工具结论差 ≤2%)
  ```

  注：probe 的 `summary.json` 对 `whole_avg_kbps` / `max_kbps` 按 1 位小数 / 整数取整，
  故峰值按四舍五入后比较（未取整时差 0.48kbps，同属取整误差）。
  另有独立 active 录制对照：`g5_format -CbrScene active` 全片 6,207kbps vs probe active 6,197.4kbps，差 0.16%（≤2%）。
- 结论：通过（同场景相对差 **0.0006%**，两工具结论同为 PASS）。
- 复现环境：需先跑 P0-1 #4 生成 `active-01.mkv` / `active-01.out`。

## P1 · 矩阵补 1 格（100% 单屏 × 硬编 `--encoder mf-h264-hw`）

- 验证方式：三件套（录 60s / 抽帧 / 音画偏差 + 可解码）+ 额外「硬编真命中」断言。
- 命令：`pwsh -NoProfile -File tasks/G6/V1-019/r46-matrix-hw.ps1`
- 输出（`artifacts/r46_matrix_hw.txt`）：`RESULT: 6 PASS / 1 FAIL / 0 SKIP`；
  `PASS r46-hw-record-60s — record_exit=0 wall=60.03s frames_encoded=1800 frames_dropped=0 size=46510745 B`；
  `FAIL r46-hw-encoder-really-hardware — encoder=mf-h264-sw（必须真命中硬编，不是静默回退 mf-h264-sw）`；
  抽帧 3/3 非全黑、尺寸稳定 6,220,800B、音画偏差 1ms、duration 59.999s、双轨、解码零错误；
  `MATRIX_DATE=2026-11-12`。
  旁证（`artifacts/hw-diag/hw5.out`）：`sr-recorder --list-encoders` 报告
  `encoder=id=mf-h264-hw hardware=1 available=1 reason=`，但 5s 强制 `--encoder mf-h264-hw` 仍输出 `encoder=mf-h264-sw`；
  源码 `src/encode/MfVideoEncoder.cpp:434-479` 在协商 / 码控失败时会重建软编 MFT（强制指定路径未拦住）。
- 结论：**部分通过（PARTIAL）**。三件套 6/6 全过，但硬编未真命中，该格在 `docs/14` 记为 `PARTIAL`，
  **不计为硬编实测 PASS**；未放宽断言、未改 `src/encode/`（不属本卡文件域），
  修复需规划侧另立卡。见 `docs/14 §3/§5`、`docs/07` 轮 46 P1。

## P1 · M1-27 终审材料（`docs/18 §3.1` 门槛）

- 验证方式：按 §3.1 四条前提逐条核验本机可核验项。
- 命令：
  ```powershell
  Get-CimInstance Win32_OperatingSystem | Select-Object Caption,BuildNumber
  Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System' `
    -Name ConsentPromptBehaviorAdmin,PromptOnSecureDesktop
  ```
- 输出：`Microsoft Windows 11 家庭版 中文版 build 26200`；`ConsentPromptBehaviorAdmin=0`、`PromptOnSecureDesktop=0`。
  `docs/18 §7` 四条门槛逐条记为「不满足 / 未执行」。
- 结论：**未达成（BLOCKED）**，EX-01 继续有效，`docs/05 §7.2.1` 措辞未改，**不得判 PASS、不得翻绿**。

## P1 · 台账与回归 / 三构建

- 命令与输出（`artifacts/r46-regression.txt`、`artifacts/build-summary.txt`）：

  | 用例 | 原始 RESULT 行 | 退出码 |
  | --- | --- | --- |
  | `g5_format`（静态） | `RESULT: 15 PASS / 0 FAIL / 0 SKIP` | 0 |
  | `g5_format -CbrScene active` | `RESULT: 15 PASS / 0 FAIL / 0 SKIP` | 0 |
  | `g5_format_selftest` | `RESULT: 22 PASS / 0 FAIL` | 0 |
  | `g5_storage` | `RESULT: 14 PASS / 0 FAIL / 0 SKIP` | 0 |
  | `g5_r43_closeout` | `RESULT: 12 PASS / 0 FAIL / 0 SKIP` | 0 |
  | `g5_window` | `RESULT: 24 PASS / 0 FAIL / 0 SKIP` | 0 |
  | `g5_uac` | `RESULT: 17 PASS / 0 FAIL / 1 SKIP` | 0 |
  | `ui_smoke` ×3 | `RESULT: 103 PASS / 0 FAIL / 0 SKIP` ×3 | 0 / 0 / 0 |
  | `r46_matrix_hw` | `RESULT: 6 PASS / 1 FAIL / 0 SKIP`（P1，见上） | 1 |
  | 三构建 | `BUILD msvc-release exit=0 warnings=0` / `BUILD msvc-debug exit=0 warnings=0` / `BUILD mingw-release exit=0 warnings=0` | 0 |

  静态检查：`汇总: 8 项检查, 8 PASS, 0 FAIL`、`coverage table is up to date`、
  `card_consistency: OK —— 扫描 58 张卡，0 处冲突`、`MISMATCH_COUNT=0`。
- 结论：判据 #6 的五个回归项 + `g5_format` / selftest 全绿；**唯一红项是 P1 矩阵的硬编断言**，
  按真实结果保留，未删除、未改尺子、未冒充 PASS。
- 台账：`docs/07` 新增「轮 46 记录」执行方自述，**主表一行留给验收方**，不自评。

---

## 覆盖子句与交付状态

- 本卡 `## 覆盖子句`：无；`## 交付状态`：无（不声明需求子句）。
- M3-04#1 的翻绿由独占声明卡 `tasks/G6/V1-014/TASK.md` 承载，符合轮 37 R5 最保守合并规则。
- `docs/09` 三态：**108 / 1 / 7**，未实现子句挂账 8、缺理由 0。
