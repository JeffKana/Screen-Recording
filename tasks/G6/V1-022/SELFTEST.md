# SELFTEST · V1-022（轮 49 主卡）

## §0 开工四项自查

| 检查项 | 实测 | 结论 |
| --- | --- | --- |
| 需求编号可检索 | `docs/01:111` M3-09（本轮唯一改动的需求原文行） | ✅ |
| 必读章节可定位 | `docs/05 §7.2`(:214) / `docs/09` / `docs/22 §1§2` / `CONTRACT §18.2`(:279) | ✅ |
| 上游交付物可获取 | `main` @ `ed8767a`；`S4-007` 同步交付 | ✅ |
| 文件域无交集 | 本卡只动 `docs/` + `tasks/`；`S4-007` 只动 `src/` + `verify/cases|tools` | ✅ |

## §1 文档与台账（命令 + 输出）

```powershell
python verify/tools/coverage_report.py --check     # coverage table is up to date
python verify/tools/contract_check.py              # 汇总: 8 项检查, 8 PASS, 0 FAIL
python verify/tools/card_consistency_check.py      # OK —— 扫描 64 张卡，0 处冲突
python verify/tools/domain_table_check.py          # MISMATCH_COUNT=0
git diff --stat ed8767a -- docs/05-非功能需求与验收.md   # 空
git diff --stat ed8767a -- vcpkg.json                    # 空
```

覆盖统计：`已实现 111, 有卡未实现 1, 无卡 4, 已挂账 5, 缺理由 0`（116 子句）。

## §2 用例与回归（原始 RESULT 行）

```powershell
pwsh -File verify/cases/g5_mp4.ps1
pwsh -File verify/cases/g5_multiaudio.ps1
pwsh -File verify/cases/g5_ui_format.ps1
pwsh -File verify/cases/g3_playback.ps1
pwsh -File verify/cases/g3_recovery.ps1
pwsh -File verify/cases/g4_gate.ps1
pwsh -File verify/cases/ui_smoke.ps1   # ×3
```

```
RESULT: 19 PASS / 0 FAIL ; 产物 %TEMP%\sr-mp4
RESULT: 10 PASS / 0 FAIL ; 产物 %TEMP%\sr-multiaudio
RESULT: 13 PASS / 0 FAIL ; 产物 %TEMP%\sr-ui-format
RESULT: 11 PASS / 0 FAIL
RESULT:  9 PASS / 0 FAIL
RESULT: 12 PASS / 0 FAIL
RESULT: 103 PASS / 0 FAIL / 0 SKIP   （3 次相同）
```

## §3 交付包自检

```powershell
Get-ChildItem tasks/G6/S4-007, tasks/G6/V1-022 -File | Select-Object FullName
```

```
tasks/G6/S4-007/TASK.md / EVIDENCE.md / SELFTEST.md / HANDOFF.md
tasks/G6/V1-022/TASK.md / EVIDENCE.md / SELFTEST.md / HANDOFF.md
```

## §4 交付前确认（本卡维度）

- [x] 每条 P1 都有对应证据（`EVIDENCE.md` 一节一条）
- [x] `docs/05` 未改、`docs/01` 只按裁定改默认值（未自造解释）、未改版本标签、未宣布可发布
- [x] `docs/09` 由生成器重写（非手工编辑），`--check` 通过
- [x] 台账轮 49 行结论留白（不自评「已验收」）
- [x] 用例只收紧不放宽，阈值一个未动
- [x] 改动未超出白名单（`git status --short` 逐项核对）
