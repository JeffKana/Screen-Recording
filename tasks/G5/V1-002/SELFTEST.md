# SELFTEST: V1-002（覆盖模型「最保守」+ R5 纠偏 + docs/06 §2 对齐）

## 0. 开工四项自查（06 §4.2）

| 检查项 | 结果 | 依据 |
| --- | --- | --- |
| 需求编号可检索 | ✅（无需求子句） | 本卡是工具与记录类工作；被纠正的 M3-05#1/#2 见 `tasks/G5/S4-002` |
| 必读章节可定位 | ✅ | `.codebuddy/plans/验收报告-轮36.md` §4、`.codebuddy/plans/轮37指令.md` §1/§3/§4、`CONTRACT.md` §2、`docs/06` §2/§4.2、`docs/09` 均存在 |
| 上游交付物可获取 | ✅ | 轮 36 的覆盖表与 `verify/tools/coverage_model.py`（改前版本已在 `EVIDENCE.md` 的差异表里逐条列出） |
| 文件域无交集 | ✅ | 本卡只动 `verify/tools/`、`docs/06`、`docs/09`、历史卡的 `## 交付状态` 声明行；`src/` 一行未动 |

## 1. 命令与输出

```powershell
python verify/tools/coverage_report.py --self-test
python verify/tools/coverage_report.py --write
python verify/tools/coverage_report.py --check
python verify/tools/contract_check.py
```

```text
self_test ok: 三态 + 子句级优先判定正确（缺节默认未实现）
self_test ok: 轮 37 R5 对抗用例——最保守合并 + 冲突上报
需求库 144 条, V1.0 66 条 / 116 子句, 已实现 87, 有卡未实现 4, 无卡 25, 已挂账 29, 缺理由 0
coverage table is up to date
汇总: 8 项检查, 8 PASS, 0 FAIL
```

文件域表逐行比对（脚本见 EVIDENCE §3）：**MISMATCH_COUNT= 0**（S1~S6 + V1 七行全 MATCH）。

## 2. 红绿对照

| 时点 | 现象 | 见 EVIDENCE |
| --- | --- | --- |
| 红 | 旧规则：M3-05#1/#2 = 已实现（与事实不符），新卡的纠正被旧卡覆盖 | §1.2 |
| 绿（只改规则） | M3-05 翻成「有卡未实现」，全表 72/19/25、缺理由 15 | §1.2 |
| 绿（改完 5 处声明） | 85/6/25、缺理由 0 → **验收方判据 1 的 R5 纠偏时点** | §2.3 |
| 绿（M3-05 落地后） | 87/4/25 → **验收方判据 1 的终态** | §2.3 |

## 3. 回归

| 项 | 结果 | 说明 |
| --- | --- | --- |
| `coverage_report.py --self-test` | PASS | 三态 + 4 个对抗场景 |
| `contract_check.py` | 8/8 PASS | 含 C8（缺理由 0） |
| `coverage_report.py --check` | PASS | 表与生成器一致 |
| 产品用例 | 不适用 | 本卡不动 `src/`，产品行为零变化（`g5_format`/`g4_gate`/`ui_smoke` 的回归见 `tasks/G5/S4-002` 的 SELFTEST） |

## 4. 交付前自检

- [x] 开工四项自查全部通过并已记录
- [x] 合并规则改为最保守 + 对抗用例（4 场景，含验收方点名的两个）
- [x] 冲突上报：非零退出码 + 表尾「卡声明冲突」小节
- [x] R5 纠偏：85/6/25 → M3-05 落地后 87/4/25
- [x] `docs/09` 顶部有「因纠偏 87→85，非回归」说明（生成器写入，不会被 `--write` 冲掉）
- [x] `docs/06 §2` 与 `CONTRACT.md §2` 逐行对齐（MISMATCH_COUNT=0）
- [x] 改动未超出文件域（`src/` 一行未动；只改历史卡的 `## 交付状态` 声明行，并逐处写明依据）
- [x] `vcpkg.json` 未改；`verify/cases/` 未改（只改 `verify/tools/` 的工具）
- [x] HANDOFF 四问齐答（含「哪些需求我没做完」）
- [x] `## 交付状态` 已写
- [x] `## 覆盖子句`：本卡不覆盖需求子句，已在卡里写明原因
