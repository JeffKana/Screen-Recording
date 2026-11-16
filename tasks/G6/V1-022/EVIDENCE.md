# EVIDENCE · V1-022（轮 49 主卡：默认值收口 / 文档回写 / 用例与回归 / 交付包）

> 每条 P1 一条证据，含可重跑命令与实际输出。产品实现的逐条证据在 `tasks/G6/S4-007/EVIDENCE.md`，
> 本卡不重复贴，只引用结论行。

---

## P1-1 `docs/01` M3-09 默认值改口 + 变更记录

```powershell
Select-String -Path docs/01-功能需求清单.md -Pattern 'M3-09 \||49（验收方裁定）'
git diff --stat ed8767a -- docs/05-非功能需求与验收.md
git diff ed8767a -- docs/01-功能需求清单.md | Select-String -Pattern '^[-+]' | Select-Object -First 8
```

```
    111  | M3-09 | 容器格式：MKV（V1.0 默认）与 MP4（`--format mp4`） | MP4 优先 faststart 布局；MKV 支持多音轨与更长容错；切换格式不影响录制流程 | V1.0 |
    300  | 49（验收方裁定） | 2026-11-14 | M3-09 需求描述（默认容器） | 「容器格式：MP4（默认）与 MKV」→「容器格式：**MKV（V1.0 默认）**与 MP4（`--format mp4`）」… |
（docs/05 的 diff 为空）
```

- 验收要点三句**一字未改**（faststart / 多音轨 / 切换格式），因此 `docs/09` 的子句总数与切分不变。
- 变更记录写明「MP4 恢复适配后（V1.1）再切默认」，并写明「不宣布可发布」。
- 本轮**未**改 `docs/01` 的版本标签，**未**宣布可发布。

## P1-2 `docs/22` 登记「MP4 崩溃恢复 = V1.1 前置」+ §1 归属轮次

```powershell
Select-String -Path docs/22-轮47-V1.0差距盘点.md -Pattern 'V1.1|111|轮 49 收口'
```

```
> 覆盖数字以 `coverage_report.py` 当前输出 **111 / 1 / 4** 为准（轮 49 `S4-007` 收口 M3-09#1/#2/#3）；
> **MP4 崩溃恢复登记为 V1.1 前置**（本轮不做、不冒充）：faststart 后 moov 在 `Close()` 才生成…
| MP4 崩溃恢复（V1.1 前置） | … `probe_repairable=0`，原因写明「iso-bmff (mp4) part: moov missing」，**原文件保留** … | 需要 moov 落 `.part` 尾部再回填或分片 moov 的完整设计；属 V1.1 前置 | **V1.1**（不排入 V1.0） |
```

§1 ① 行：剩 **4 无卡 + 1 有卡未实现**（`M1-27#1`），4 条无卡全为真机；
M3-09#1/#2/#3 三条在「归属轮次」列标为「轮 48 起步 → 轮 49 收口」。

## P1-3 `docs/14` 硬编格口径保持

```powershell
git diff --stat ed8767a -- docs/14-轮41-兼容性矩阵草案.md
Select-String -Path docs/14-轮41-兼容性矩阵草案.md -Pattern '不可计硬编 PASS'
```

```
（git diff 为空——本轮未改 docs/14）
docs/14:51  … **不可计硬编 PASS**（2026-11-13 / 轮 47 复核）… 无硬编成片可抽帧，三件套不可执行，按 docs/05 §7.3 不拿软编冒充硬编、也不把报错冒充 PASS
```

轮 49 卡 P1 要求「硬编格保持『不可用路径实测、不计硬编 PASS』」→ 逐字核对在位，未改写。

## P1-4 台账轮 49 一行留白

```powershell
Select-String -Path docs/07-执行台账.md -Pattern '^\| 49 \|'
```

```
| 49 | G6 | V1 | M3-09#1/#2/#3 收口（faststart / 多音轨 / UI 扩展名）+ `01` 默认值 | （待验收方独立重跑后填写） | （待填） | **执行方自述（不是结论）**：… |
```

结论列与 findings 列均为占位文本，等待验收方填写；另新增「轮 49 记录」节（P0 表 / 红-绿对照 /
回归与静态 / 未达成 5 条）。

## P1-5 覆盖表重生成

```powershell
python verify/tools/coverage_report.py --write
python verify/tools/coverage_report.py --check
```

```
written: docs/09-V1.0覆盖表.md
coverage table is up to date
需求库 144 条, V1.0 66 条 / 116 子句, 已实现 111, 有卡未实现 1, 无卡 4, 已挂账 5, 缺理由 0
```

108 → **111** 的来源只有 `S4-007` 的 `## 交付状态`（`M3-09#1/#2/#3` 已实现），没有靠正文顺带提及凑数。
`M3-09` 的三行在 `docs/09` 里状态均为「已实现」，挂账理由列为 `—`。

## P1-6 用例补强 + 红-绿

```powershell
git diff --stat ed8767a -- verify/
```

```
 verify/cases/g5_mp4.ps1       |  96 +++++++++++++++-
 verify/tools/mp4_box_check.py |  19 +++-
 verify/cases/g5_multiaudio.ps1 | 新增
 verify/cases/g5_ui_format.ps1  | 新增
```

- `g5_mp4` 原有 13 条一条未删（`git diff` 里 `r48-*` 断言文本零改动），新增 `r49-faststart-*` 2 条 +
  `r49-part-*` 4 条；`mp4_box_check.py` 的 `moov-after-mdat`（轮 48 起步期临时口径）改为
  `moov-before-mdat` 并加 `moov-in-first-64k` —— **收紧，不是放宽**。
- 红端（`ed8767a` 构建）：`g5_mp4` **15/4**、`g5_multiaudio` **0/10**；`g5_ui_format` 红端不可自动执行，
  以源码事实替代（`ed8767a:src/app/main.cpp:376` 写死 `.mkv`）。详见 `S4-007/EVIDENCE.md §5`。
- 未改 `verify/lib/**`、`g5_closeout.ps1`、`g5_format.ps1` 及任何阈值。

## P1-7 回归

```powershell
pwsh -File verify/cases/g5_mp4.ps1
pwsh -File verify/cases/g5_multiaudio.ps1
pwsh -File verify/cases/g5_ui_format.ps1
pwsh -File verify/cases/g3_playback.ps1
pwsh -File verify/cases/g3_recovery.ps1
pwsh -File verify/cases/g4_gate.ps1
pwsh -File verify/cases/ui_smoke.ps1     # 连跑 3 次
```

```
RESULT: 19 PASS / 0 FAIL        g5_mp4
RESULT: 10 PASS / 0 FAIL        g5_multiaudio
RESULT: 13 PASS / 0 FAIL        g5_ui_format
RESULT: 11 PASS / 0 FAIL        g3_playback
RESULT:  9 PASS / 0 FAIL        g3_recovery
RESULT: 12 PASS / 0 FAIL        g4_gate
RESULT: 103 PASS / 0 FAIL / 0 SKIP  ×3   ui_smoke
```

## 附：三构建零告警与静态四检

```
debug_exit=0 / mingw_exit=0 / msvc Release 全量 exit=0，warning 命中 0
汇总: 8 项检查, 8 PASS, 0 FAIL          (contract_check.py)
card_consistency: OK —— 扫描 64 张卡，0 处冲突
MISMATCH_COUNT=0                        (domain_table_check.py)
```
