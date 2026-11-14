# EVIDENCE: V1-021（轮 48 主卡）

> 口径声明：本卡是「用例修正 / 稳定性复核 / 文档回写 / 发布预演」，**不写验收结论**；
> MP4 产品实现与五条判据的逐条证据在 `tasks/G6/S4-006/EVIDENCE.md`。
> **V1.0 仍不可发布**，路线图见 `docs/22`；O0 对 §7.2 ⑤⑥ 的拍板是范围收窄的书面例外，
> 不是判据达成。数据来源：2026-11-13/14 / `build-msvc/bin/Release` / Win11 build 26200 /
> ffprobe & ffmpeg 9.0.2。`artifacts/` 被 `.gitignore` 排除，全部命令可重跑并重新生成同名结构。

## P0-1 · `g5_closeout` 数量写死 → 数量无关 + 反向用例

- 验证方式：新增共用守卫 `Test-UnavailableRows`（三厂商 id 都在 unavailable + **每一行**都有非空 reason），
  把原 `m6-04-02-hw-unavailable-with-reason`（`==3`）换成三条断言：
  `m6-04-02-vendor-ids-unavailable`、`m6-04-02-every-unavailable-has-reason`、
  `m6-04-02-reverse-blank-reason-fails`。**没有**改成 `>=3`。
- 命令（红 1，轮 47 原始用例）：`pwsh -NoProfile -File tasks/G6/V1-021/artifacts/r48-closeout-red.ps1`
  （内部 `git show main:verify/cases/g5_closeout.ps1` 写到 `verify/artifacts/r48-closeout-old.ps1` 后运行）
- 输出：`artifacts/r48-closeout-red.txt` ——
  `FAIL m6-04-02-hw-unavailable-with-reason — 三厂商硬编 id 命中=3，逐条标注 available=0 + 非空 reason 的规范行=4；encoder=id=mf-h264-hw, ...`；
  **`RESULT: 17 PASS / 1 FAIL / 0 SKIP`**、`OLD_EXIT=1`。
- 命令（红 2，**变异体**）：同一脚本把守卫块替换成被轮 48 卡明令禁止的
  `return $rows.Count -ge 3`，再跑同一用例。
- 输出：`FAIL m6-04-02-reverse-blank-reason-fails — 抹掉 reason 的行数=1（期望 1），守卫对篡改列表=False（实际=False）`；
  **`RESULT: 19 PASS / 1 FAIL / 0 SKIP`**、`NAIVE_EXIT=1`。
  这条证明反向用例真有区分度：`>=3` 会让「新加项没写原因」溜过去，本卡的守卫不会。
- 命令（绿）：`pwsh -NoProfile -File verify/cases/g5_closeout.ps1`
- 输出：`artifacts/r48-closeout-green.txt` —— `RESULT: 20 PASS / 0 FAIL / 0 SKIP`，其中
  - `PASS m6-04-02-vendor-ids-unavailable — unavailable 行数=4（数量无关），三厂商命中=3/3，缺=`
  - `PASS m6-04-02-every-unavailable-has-reason — unavailable 行=4，缺 reason 的行=0`
  - `PASS m6-04-02-reverse-blank-reason-fails — 抹掉 reason 的行数=1，守卫对篡改列表=False`
- 结论：通过（数量无关 + 反向用例成立；其它断言一字未改，`RESULT` 从 18 条变 20 条）

## P0-4 · `g5_format` 5 连复核（M3-04#1 维持已实现）

- 命令：`pwsh -NoProfile -File verify/cases/g5_format.ps1`（run 1/3/5）、
  `pwsh -NoProfile -File verify/cases/g5_format.ps1 -CbrScene active`（run 2/4）
- 输出（逐次原始 `RESULT` 行，`artifacts/r48-format-1..5.txt`，汇总 `r48-format-5runs-summary.txt`）：

  | run | 场景 | 全片平均偏差 | 单秒峰值 | RESULT |
  | --- | --- | --- | --- | --- |
  | 1 | static | 3.3% | 6,000kbps | `15 PASS / 0 FAIL / 0 SKIP` |
  | 2 | **active** | 3.5% | 6,157kbps | `15 PASS / 0 FAIL / 0 SKIP` |
  | 3 | static | 3.2% | 6,000kbps | `15 PASS / 0 FAIL / 0 SKIP` |
  | 4 | **active** | 3.4% | 6,158kbps | `15 PASS / 0 FAIL / 0 SKIP` |
  | 5 | static | 3.2% | 6,000kbps | `15 PASS / 0 FAIL / 0 SKIP` |

- 活动场景可核验：`summary.json` 的 `cbr_scene=active`（run2/4），
  `cbr.out` 的 `frames_duplicated=253/255`、`capture_fps=45.3/51.2`，
  单秒峰值 6,157/6,158 vs static 6,000；目录 `verify/artifacts/format-20261113-234045` / `-234509`。
- 判据：`任意单秒 ≤ 12,000kbps（2× 标称 6,000）` 且 `全片平均偏差 ≤10%`；
  5 次全部满足，**未触发回退**，M3-04#1 维持覆盖表既有的「已实现」。
  原始输出逐次留档，没有只报最好那次；判据与阈值一字未改。
- 结论：通过（执行方自测；验收结论由验收方给）

## P1 · `docs/22` 归属轮次 + O0 ⑤⑥ 拍板 + INDEX + 台账

- 命令：`Get-Content -Encoding UTF8 'docs/22-轮47-V1.0差距盘点.md'`
- 输出：§1 六条判据的「归属轮次」列按轮 48 卡 §0 路线改为确定值：
  ① `M3-09#1/#3 轮 48 起步 → 轮 49 收口`、`M3-09#2 轮 49`、`M1-27#1 轮 50`、`4 条真机 轮 53 / 真机触发`；
  ② `轮 50`；③ `轮 51–52`；④ `轮 50`；⑤ `轮 53（v0.1 范围按 O0 例外收窄）`；⑥ `轮 53 / 真机触发`。
  §2 同步 EX-01 / M6-06 / M3-09 三条 / 4 条真机 / M3-05#1（已完成）/ 轮 47 两处红项。
- 命令：`Select-String -Path 'docs/22-...' -Pattern 'O0 拍板|v0.1|不改变|不冒充'`
- 输出：文件头新增
  `**O0 拍板（2026-11-13，用户书面决策）**：§7.2 的 ⑤ 兼容性矩阵与 ⑥ B 机性能基线依赖真机，v0.1 暂不做兼容性，仅以当前机器为准`；
  §1 ⑤⑥ 的「当前」列仍写 `未达成 / 缺真机`，并注明「**不改成 PASS、不拿降级版本冒充、不自行宣布可发布**」；
  §3 结论 `本轮之后 V1.0 可发布？ 否`，新增「v0.1 范围」「待 O0 裁定」两行。
  **没有**改 `docs/05 §7.2`、没有改 `docs/01` 版本标签、没有把 ⑤⑥ 翻绿。
- 命令：`Select-String -Path docs/INDEX.md -Pattern '\|\s*22\s*\|'`
- 输出：`| 22 | ... | 现行（轮 47 建表 / 轮 48 回写确定轮次 + O0 ⑤⑥ 例外） |` —— 轮 47 已登记，本轮只需标注回写，无需补行。
- 命令：`Select-String -Path 'docs/07-执行台账.md' -Pattern '^\| 48 \||轮 48 记录'`
- 输出：只有 `## 轮 48 记录（...）` 的执行方自述章节，主表**没有** `| 48 |` 行（留白给验收方）。
- 结论：通过

## 新增验收文件（本卡文件域，V1 名义域）

| 文件 | 证据 |
| --- | --- |
| `verify/cases/g5_mp4.ps1` | 红 `0 PASS / 13 FAIL` → 绿 `13 PASS / 0 FAIL`（`artifacts/r48-mp4-red.txt` / `r48-mp4-green.txt`）；13 条断言逐条覆盖轮 48 卡 §P0-2 五条判据 + §P0-3 三条 + box/IDR/默认名护栏 |
| `verify/tools/mp4_box_check.py` | `RESULT: 42 PASS / 0 FAIL`（`artifacts/r48-mp4-green.txt` 内 `box-check.txt` 引用）；逐条核 `stts/ctts/stsz/stsc/stco/stss` 计数、时长、偏移与 mdat 无填充 |

逐条判据的原始输出见 `tasks/G6/S4-006/EVIDENCE.md`（本卡不重复贴大段输出，但命令与 RESULT 行均可独立重跑）。

## 通过判据 #5 / #6 · 回归、静态、三构建

- 回归（命令均为 `pwsh -NoProfile -File verify/cases/<case>.ps1`，原始输出 `artifacts/reg-*.txt`）：

  | 用例 | RESULT |
  | --- | --- |
  | `g3_playback` | `11 PASS / 0 FAIL` |
  | `g3_recovery` | `9 PASS / 0 FAIL` |
  | `g4_gate` | `12 PASS / 0 FAIL` |
  | `g5_encoder_list` | 首跑 `15 PASS / 1 FAIL`（`m3-05-ui-listbox-exists` hwnd=0）→ 复跑 `17 PASS / 0 FAIL` |
  | `ui_smoke` ×3 | run2/3/4 连续三次 `103 PASS / 0 FAIL / 0 SKIP`（run1 `102/1`，`unfocused-hotkey` foreground_hwnd=0） |
  | `g5_closeout`（P0-1 修正后） | `20 PASS / 0 FAIL / 0 SKIP` |
  | `g5_mp4`（新增） | `13 PASS / 0 FAIL` |
  | `g5_format` 5 连 | 5×`15 PASS / 0 FAIL / 0 SKIP` |
  | `build_release.ps1`（附加） | `17 PASS / 0 FAIL` |

  **两处 UI 时序抖动如实保留**：`g5_encoder_list` 首跑与 `ui_smoke` run1 的原始输出都留档，
  未改任何用例/判据；规定回归集以复跑后的三次连续绿为结论依据。
- 静态（`artifacts/r48-static-checks.txt`、`r48-final-verification.txt`）：
  `python verify/tools/coverage_report.py --check` → `coverage table is up to date`；
  `coverage_report.py` → `已实现 108, 有卡未实现 1, 无卡 7, 已挂账 8, 缺理由 0`（**起步切片不翻绿，数字不变**）；
  `contract_check.py` → `汇总: 8 项检查, 8 PASS, 0 FAIL`；
  `card_consistency_check.py` → `扫描 62 张卡，0 处冲突`；
  `domain_table_check.py` → `MISMATCH_COUNT=0`。
- 三构建（`artifacts/build-*.log`）：`cmake --build build-msvc --config Debug` /
  `--config Release` / `cmake --build build-mingw` → 三个 `exit=0 warnings=0`。
- 结论：规定回归集零失败；静态四项全过；三构建零告警。

## 已知限制（不得计为通过）

1. **V1.0 仍不可发布**：M3-09#1 faststart / #2 多音轨未做（轮 49），LT/ER 三次（轮 50/51–52）、
   EX-01 与 M6-06 终审（轮 50）、矩阵与 B 机基线（轮 53 / 真机）均未闭环。
2. O0 拍板只收窄 **v0.1 的兼容性/性能基线范围**；`docs/22` 仍如实记 ⑤⑥ 未达成，执行侧不宣布可发布。
3. `docs/01` M3-09 原文「MP4（默认）与 MKV」与轮 48 卡判据 5「默认 mkv」矛盾 → 文档缺陷单交 O0，
   不改需求原文。
4. `--format` 非法值 `exit 2` 由轮 48 卡授权，但 `CONTRACT.md §18.2` 退出码表尚无码 2
   （根目录文件属 O0），已上报回写。
5. MP4 的 moov 后置、崩溃恢复未适配、UI 仍固定 `.mkv` 输出名，见 `S4-006/HANDOFF.md`。
6. 执行方不写验收结论；台账主表轮 48 一行留白。
