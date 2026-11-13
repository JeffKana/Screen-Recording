# EVIDENCE: V1-020（轮 47 主卡）

> 口径声明：本卡是「用例 / 文档 / 台账 / 回归」类工作，**不写验收结论**；产品实现在
> `tasks/G6/S4-008`。V1.0 **仍不可发布**，差距表见 `docs/22`；硬编抽测格不冒充 PASS。
> 数据来源：2026-11-13 / `build-msvc/bin/Release`（MSVC Release，本机 Win11 build 26200 /
> i5-13500 / UHD 770 / 双屏 1920×1080 / DPI 100%）。
> 红端基线：轮 46 二进制另存 `%TEMP%\sr-r47-baseline\sr-recorder.exe`（SHA 见 `artifacts/baseline-sha256.txt`）。
> `artifacts/` 被 `.gitignore` 排除，全部命令可重跑并重新生成同名结构。

## P0-1 正向 + 反向用例各 1 条，先红后绿（M3-05#1）

- 验证方式：新用例 `verify/cases/g5_encoder_consistency.ps1` 逐项读取 `--list-encoders`，
  对 `available=1` 的项强制指定并比对 stdout `encoder=<id>`（正向），对 `available=0` 的项
  要求 `exit 6` + 非空原因 + 无成片（反向），再注入 `--fail-encoder hardware` 验证列表变诚实。
- 命令（红，旧二进制）：
  `pwsh -NoProfile -File verify/cases/g5_encoder_consistency.ps1 -RecorderPath "$env:TEMP\sr-r47-baseline\sr-recorder.exe"`
- 输出：`artifacts/r47-red.txt` —— **`RESULT: 4 PASS / 8 FAIL`**
  - `FAIL r47-forward-available-items — 可用项=2；mf-h264-hw: exit=0 encoder=<[record] encoder=mf-h264-sw ...>`（**抓到静默回退**）
  - `FAIL r47-no-silent-fallback — 是否出现软编：True`
  - `FAIL r47-injected-hw-unavailable — 注入后 exit=1`（旧实现不接受带参数的 `--fail-encoder`）
  - `FAIL r47-injection-is-real-probe` / `r47-injected-forced-exit6` / `r47-repeat-state-consistent` /
    `r47-own-resource-no-leak` / `r47-vendor-handle-bounded`
- 命令（绿，新构建）：`pwsh -NoProfile -File verify/cases/g5_encoder_consistency.ps1`
- 输出：`artifacts/r47-green.txt` —— **`RESULT: 12 PASS / 0 FAIL`**
  - `PASS r47-forward-available-items — 可用项=1；mf-h264-sw: exit=0 encoder=<[record] encoder=mf-h264-sw ...> 成片=True`
  - `PASS r47-no-silent-fallback — 是否出现软编：False`
  - `PASS r47-backward-unavailable-items — 不可用项=4；mf-h264-hw/nvenc-h264/qsv-h264/amf-h264: exit=6 原因=True 无成片=True`
  - `PASS r47-injected-hw-unavailable — mf-h264-hw available=0 reason=MFT 真激活失败（Open: ... injected: hardware encoder unavailable）`
  - `PASS r47-injection-is-real-probe` / `r47-injected-forced-exit6` / `r47-probe-retry-not-cached`
  - `PASS r47-repeat-state-consistent — 1/0,1/0,1/0,1/0,1/0`
  - `PASS r47-own-resource-no-leak — 跳过硬件 MFT 激活后 5 轮句柄：204,204,204,204,204；增长=0`
  - `PASS r47-vendor-handle-bounded — 真实激活 5 轮句柄：268,271,274,277,280；增长=12（上限 16）`
- 结论：**部分通过→通过**（先红后绿成立；验收判定由验收方独立重跑给）
- 复现环境：本机 / Win11 build 26200 / MSVC Release；旧二进制路径见上

## P0-1 补充：列表与开录结果一致的直接实测

- 命令：`.\build-msvc\bin\Release\sr-recorder.exe --list-encoders`
- 输出（关键行）：`encoder=id=mf-h264-hw hardware=1 available=0 reason=MFT 真激活失败（Open: encoder 'mf-h264-hw' unavailable: MFT 协商失败（encoder rejected NV12 input type））`；
  `mf-h264-sw available=1`；`encoder_count=5`。
- 命令：`.\build-msvc\bin\Release\sr-recorder.exe --record --seconds 2 --fps 30 --bitrate 2000 --out $env:TEMP\sr-r47-green-hw.mkv --encoder mf-h264-hw`
- 输出：`[record] encoder open failed: encoder 'mf-h264-hw' unavailable: MFT 协商失败（encoder rejected NV12 input type）`，
  `record_exit=6`，无成片。
- 命令：同上 `--encoder mf-h264-sw` → `exit=0`、`encoder=mf-h264-sw`、成片存在。
- 结论：通过（列表与开录结果一致，构造上共用 `MfVideoEncoder::Open` 同一条实现）

## P0-2 `docs/19` 0.B 三条有轮次

- 命令：`Select-String -Path 'docs/19-轮44-收口可行性与判据矛盾核查.md' -Pattern '排轮 48|排轮 49|0.B 表'`
- 输出：`### 0.B 表 · MP4/MKV 实现工作（3 条，已排轮 48–49，不缺任何机器）`；
  `M3-09#1 → 排轮 48 · S4-006`（交付物：`Mp4Writer` box 布局 + faststart + `--format mp4`）、
  `M3-09#2 → 排轮 49 · S4-007`（交付物：双音轨成片 ffprobe 两条 audio track）、
  `M3-09#3 → 排轮 48 · 随 S4-006 同卡收口`。
- 命令：`Select-String -Path 'docs/09-V1.0覆盖表.md' -Pattern '^\| M3-09#'`
- 输出：三行挂账理由分别为「排轮 48（`S4-006`）」「排轮 49（`S4-007`）」「排轮 48（随 `S4-006` 同卡收口，依赖 #1）」；
  旧「排轮 47」已不存在。
- 命令：`python verify/tools/coverage_report.py --check`
- 输出：`coverage table is up to date`（`artifacts/coverage-final-check.txt`）
- 结论：通过（选「给轮次」，未自行裁定移出 V1.0；`docs/01` 版本标签未改，符合轮 47 指令 §P0-2 二选一）

## P0-3 V1.0 差距盘点表

- 命令：`Get-Content 'docs/22-轮47-V1.0差距盘点.md'`
- 输出：`docs/22 §1` 六行表（判据 / 当前 / 差距 / 归属轮次），逐条对应 `docs/05 §7.2` 六条；
  §2 例外清单 EX-01 / M6-06 / M3-09 三条 / 4 条真机 BLOCKED / M3-05#1；
  §3 结论「**否**，V1.0 不可发布」；§4 依据链。`docs/INDEX.md` 已登记编号 22。
- 命令：`Get-ChildItem docs -File -Filter '*.md' | Where-Object { $_.Name -ne 'INDEX.md' } | ForEach-Object { ($_.Name -split '-')[0] } | Group-Object | Where-Object Count -gt 1`
- 输出：无输出（编号唯一）
- 结论：通过（四列齐备、含归属轮次、不写成散文）

## P1 矩阵补格 / F3 文案 / 台账

- 矩阵：命令 `Get-Content 'tasks/G6/V1-020/artifacts/r47-matrix-hw.txt'` →
  `process_exit=6`、`成品存在=False`、`error=encoder 'mf-h264-hw' unavailable: MFT 协商失败（encoder rejected NV12 input type）`、
  「三件套不可执行：开录前 exit 6，无成片可抽帧」。`docs/14 §3/§5` 已改为
  「不可计硬编 PASS（轮 47 复核）」。**结论：部分通过**——轮 47 指令写「可转 PASS」，
  本机硬件 MFT 真激活失败、没有硬编成片，按 `docs/05 §7.3` 不冒充 PASS，如实保留。
- F3：命令 `Select-String -Path 'verify/cases/g5_format.ps1' -Pattern 'cbr-average-deviation|仅对照' | Select-Object -First 3` →
  输出含 `（逐秒均值 {5:N0}kbps (仅对照)）`；`g5_format` 复跑 **15 PASS / 0 FAIL**，
  `cbr-average-deviation` 输出「全片平均 6,192kbps … 偏差 3.2% … 逐秒均值 6,000kbps (仅对照)」
  （`artifacts/reg-g5_format-run2.txt`）。阈值 10% / 2× 未改。**结论：通过**。
- 台账：命令 `Select-String -Path 'docs/07-执行台账.md' -Pattern '轮 47 记录|^\| 47 \|'` →
  输出只有 `## 轮 47 记录（...）`，主表**无 47 行**（留白给验收方）；执行方自述单列。**结论：通过**。

## 通过判据 #2 覆盖 108/1/7

- 命令：`python verify/tools/coverage_report.py --write` → `written: docs/09-V1.0覆盖表.md`（`artifacts/coverage-final.txt`）
- 命令：`python verify/tools/coverage_report.py` →
  `已实现 108, 有卡未实现 1, 无卡 7, 已挂账 8, 缺理由 0`（`artifacts/coverage-final-stats.txt`）
- 命令：`python verify/tools/coverage_report.py --check` → `coverage table is up to date`
- 回退留痕：`python verify/tools/coverage_report.py --write`（`S4-008` 声明「有卡未实现」时）→
  `已实现 107, 有卡未实现 2, 无卡 7`（`artifacts/coverage-rollback.txt`、`coverage-rollback-stats2.txt`），
  `contract_check.py` 8/8（`artifacts/contract-rollback.txt`）。**先回退、先修列表、用例全绿、再翻绿**，没有直接标已实现。
- 结论：通过（数字由生成器给出，非手工改状态列）

## 通过判据 #5 / #6 回归、静态检查与三构建

- 回归（命令均为 `pwsh -NoProfile -File verify/cases/<case>.ps1`，原始输出见 `artifacts/reg-*.txt`）：

  | 用例 | RESULT |
  | --- | --- |
  | `g5_encoder_list` | `17 PASS / 0 FAIL` |
  | `g5_format`（清场复跑） | `15 PASS / 0 FAIL / 0 SKIP` |
  | `g5_format_selftest` | `22 PASS / 0 FAIL` |
  | `g5_storage` | `14 PASS / 0 FAIL / 0 SKIP` |
  | `ui_smoke` ×3 | 三次 `103 PASS / 0 FAIL / 0 SKIP` |
  | `g5_encoder_consistency`（新增） | `12 PASS / 0 FAIL` |
  | `g5_inject` / `g5_r43_closeout`（附加） | `28 PASS / 0 FAIL` / `12 PASS / 0 FAIL / 0 SKIP` |

- **如实保留的红项**：
  1. `g5_format` 首次 `14 PASS / 1 FAIL`：`cbr-single-second-cap` 单秒 **12,799kbps > 12,000**
     （`artifacts/reg-g5_format.txt`）；清场复跑 15/0（`reg-g5_format-run2.txt`）。与轮 44/45 记录的
     「单秒过冲随桌面活动漂移」同源，两次输出都留档，未改判据。
  2. 附加抽样 `g5_closeout` `17 PASS / 1 FAIL`：`m6-04-02-hw-unavailable-with-reason` 把
     「不可用行 == 3」写死，轮 47 诚实列表为 4（多出 `mf-h264-hw`）。属**用例侧过期假设**，
     本卡文件域未授权改该用例，未改、未放宽；remedy 见 `HANDOFF.md §2`。
- 静态检查（命令 → 输出，`artifacts/*-final.txt`）：
  `contract_check.py` → `汇总: 8 项检查, 8 PASS, 0 FAIL`；
  `card_consistency_check.py` → `扫描 60 张卡，0 处冲突`；
  `domain_table_check.py` → `MISMATCH_COUNT=0`；
  `coverage_report.py --check` → `coverage table is up to date`。
- 三构建（命令 → 输出，`artifacts/build-*.log`）：
  `cmake --build build-msvc --config Debug` / `--config Release` / `cmake --build build-mingw`
  → 三个 `exit=0`，`warning C\d+|warning:` 命中数均为 **0**。
- 结论：规定回归集零失败；附加抽样两处红项已如实上报，未改尺子。

## 已知限制（不得计为通过）

1. **V1.0 仍不可发布**：`docs/22` 六条判据中 ①③④⑤⑥ 未闭环，EX-01 未终审。
2. M3-09 三条未实现（只落 48–49 轮次）；M6-06 / M1-27 本轮不碰。
3. 硬编抽测格**未**转 PASS（本机 `mf-h264-hw` 真激活失败），三件套不可执行。
4. 探测器不创建 D3D device / video processor：本机重复激活第三方硬件 MFT 有 **+3 句柄/次**的
   驱动侧开销（5 轮 268→280），进程退出即回收；产品路径每次探测都在一次性子进程完成
   （UI 设置页 spawn `sr-recorder --list-encoders`），不累积。详见 `HANDOFF.md §3`。
5. 执行方不写验收结论；CI 三绿与全部 RESULT 行由验收方独立重跑后判定。
