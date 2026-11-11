# SELFTEST: V1-003（卡面清理 + 一致性检查脚本）

## 0. 开工四项自查

| 检查项 | 结果 | 依据 |
| --- | --- | --- |
| 需求编号可检索 | ✅（无需求子句） | 记录与工具类工作；被纠正的子句见 `tasks/G5/S1-003` 等卡 |
| 必读章节可定位 | ✅ | `.codebuddy/plans/验收报告-轮37.md` §4 R1、`.codebuddy/plans/轮38指令.md` §1/§3、`docs/06` §4.2/§8.4 |
| 上游交付物可获取 | ✅ | 轮 37 的 5 张卡改写结果（`tasks/G5/S1-003` 等）、`docs/09` 覆盖表 |
| 文件域无交集 | ✅ | 只动 `verify/tools/`（新增脚本）、`tasks/**/TASK.md` 的声明段、`docs/07`；与 `tasks/G5/S2-002`（src/ + verify/cases）不重叠 |

## 1. 命令与输出

```powershell
python verify/tools/card_consistency_check.py
python verify/tools/coverage_report.py --check
python verify/tools/contract_check.py
```

```text
card_consistency: OK —— 扫描 30 张卡，0 处冲突
coverage table is up to date
汇总: 8 项检查, 8 PASS, 0 FAIL
```

## 2. 红绿对照

| 场景 | 结果 |
| --- | --- |
| 红：临时造一张「状态说已实现、正文说未实现且未标注历史」的卡 | `card_consistency: FAIL —— 1 处冲突`，退出码 **1**，并给出 `卡片:行号 [需求] 原文` ✓ |
| 绿：真实仓库 30 张卡 | `card_consistency: OK —— 0 处冲突`，退出码 **0** ✓ |

## 3. 回归

| 项 | 结果 | 说明 |
| --- | --- | --- |
| `coverage_report.py --check` | PASS | 表与生成器一致 |
| `contract_check.py` | 8/8 PASS | 含 C8（缺理由 0） |
| 产品用例 | 不适用 | 本卡不动 `src/`（产品行为零变化） |

## 4. 交付前自检

- [x] 开工四项自查全部通过并已记录
- [x] 5 张历史卡的过期说明已清除，且状态行后统一有 `> 变更：轮 N / 角色 / 依据` ✓
- [x] 脚本有**自证**（红：造冲突卡 → 退出码 1；绿：真实仓库 → 0 冲突）✓
- [x] 改动未超出文件域（只改声明段与历史表述，证据段与需求原文未动）✓
- [x] `vcpkg.json` 未改；`verify/cases/` 未改（只新增 `verify/tools/card_consistency_check.py`）✓
- [x] HANDOFF 四问齐答
- [x] `## 交付状态` 已写
- [x] `## 覆盖子句`：本卡不覆盖需求子句，已在卡里写明原因
