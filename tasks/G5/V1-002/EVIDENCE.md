# EVIDENCE: V1-002（覆盖模型「最保守」+ R5 纠偏 + docs/06 §2 对齐）

> 环境：本机 / Python 3.x / `verify/tools/coverage_model.py` + `coverage_report.py`

---

## 1 · 合并规则改「最保守」（判据 2）

### 1.1 改动

| 位置 | 改动 |
| --- | --- |
| `verify/tools/coverage_model.py` | 新增 `split_declaration()`（把声明拆成「状态词 + 理由」）、`merge_clause_state()`（**最保守**合并 + 冲突检测）；`build_rows()` 改用新合并函数，输出多一个 `conflicts` 字段；`summarize()` 多一个 `conflicts` 计数 |
| `verify/tools/coverage_report.py` | `main()` 三个模式（默认 / `--write` / `--check`）在有冲突时**以非零退出码拦下**并打印冲突清单；`render()` 在表尾追加「卡声明冲突」小节；`--self-test` 增加 4 个对抗场景 |

**合并规则（`merge_clause_state`）**：

1. 每张归属卡对本子句的声明 = 子句级声明，没有才回落到需求级声明，都没有按「有卡未实现」（轮 32 F2 的从严口径不变）。
2. **终态 = 已实现 ⟺ 全部归属卡都声明已实现**；任一张不是 → 「有卡未实现」。
   （旧规则是「任一卡声明已实现即已实现」——新卡对旧卡的纠正永远不生效，覆盖表只涨不跌。）
3. 非「已实现」的声明之间互相矛盾 → **报冲突**：状态词不同，或状态词相同但**理由都非空且不同**。
   「已实现」与「未实现」并存不算冲突——那是规则 2 的正常收敛。

### 1.2 对抗用例（先红后绿）

**红（真实现场，不是推演）**：轮 36 的覆盖表里 `M3-05#1/#2` = **已实现**，
而事实是未实现（`tasks/G3/S4-001/EVIDENCE.md` 自己写的是 `M3-05 | PARTIAL`，
全仓无「编码器列表 + 不可用原因」结构）——旧规则（`if declared.startswith("已实现"): implemented = True`，
即**任一卡写已实现即已实现**）把 `G5/S4-002` 的纠正直接覆盖掉了。

**绿（只改规则、不改任何卡）**：合并规则改「最保守」后立刻重跑生成器，
`M3-05` 从「已实现」翻成「有卡未实现」——此时全表变成 **72 已实现 / 19 有卡未实现 / 25 无卡**，
缺理由 15 条（即被旧卡的过时声明一票否决的 15 个子句）：

```text
需求库 144 条, V1.0 66 条 / 116 子句, 已实现 72, 有卡未实现 19, 无卡 25, 已挂账 29, 缺理由 15
缺理由: M3-03#1, M3-03#2, M3-04#1, M3-04#2, M3-05#1, M3-05#2, M5-04#1, M5-04#2, M5-06#1, M5-06#2, M5-06#3, M6-02#1, M6-09#1, M8-14#1, M8-14#2
```

→ 这一步同时暴露了「旧卡的过时声明会一票否决真实交付」，从而推出本卡 §2.2 的 5 处声明纠偏。

**对抗用例（4 个场景，写进 `coverage_report.py --self-test`）**：
用例在**临时目录里造真实卡片树**（两张 `TASK.md`），不是打桩：

```powershell
python verify/tools/coverage_report.py --self-test
```

```text
self_test ok: 三态 + 子句级优先判定正确（缺节默认未实现）
self_test ok: 轮 37 R5 对抗用例——最保守合并 + 冲突上报
```

| 场景 | 两张卡的声明 | 期望 | 实测 |
| --- | --- | --- | --- |
| 1（判据要求） | `已实现` + `有卡未实现` | 终态 = 有卡未实现，无冲突 | ✅ 有卡未实现、无冲突 |
| 2（判据要求） | `有卡未实现（甲理由）` + `有卡未实现（乙理由）` | 终态 = 有卡未实现，**报冲突** | ✅ 有卡未实现、1 条冲突 |
| 3 | `有卡未实现（同一理由）` ×2 | 有卡未实现、**不**报冲突（只有一条理由的补充不算矛盾） | ✅ 有卡未实现、无冲突 |
| 4 | `已实现` + `已实现` | 已实现、无冲突 | ✅ 已实现、无冲突 |

> 场景 2 的两个理由不同 → 冲突；场景 3 的理由相同 → 不冲突。
> 这条区分是必要的：`G3/S4-001` 与 `G5/S4-002` 对 M3-05 的声明（「有卡未实现（…）」与「有卡未实现」）
> 属于**同状态、不同详略**，不能报成冲突，否则每次纠偏都要改两张卡的理由文字。

## 2 · R5 记录纠偏（判据 1）

### 2.1 事实链（验收方轮 36 报告 §4）

- `tasks/G3/S4-001/TASK.md` 写 `M3-05: 已实现`；
- 同卡 `EVIDENCE.md` 写 `M3-05 | PARTIAL`（只枚举到 MF 软编 MFT，NVENC/oneVPL/AMF 需各自 SDK）；
- 全仓无「编码器列表 + 不可用原因」结构（`grep` 零命中）；
- 旧合并规则「任一卡声明已实现即已实现」把 `S4-002` 的纠正覆盖掉。

### 2.2 纠偏动作（声明一律带依据）

| 卡片 | 轮 36 的声明 | 轮 37 的声明 | 依据 |
| --- | --- | --- | --- |
| `tasks/G3/S4-001` | `- M3-05: 已实现` | `- M3-05: 有卡未实现（探测与回退已完成；列表与不可用原因见 S4-002）` | 验收方轮 36 裁定原文 |
| `tasks/G5/S1-003` | `- M6-02/M6-09: 有卡未实现` | `- M6-02/M6-09: 已实现（轮 36 由 S6-002/S6-003 交付 8h 单文件证据；本卡交付的是 6/12 段代理证据）` | 轮 36 的 8h 单文件产物 |
| `tasks/G5/S1-004` | `- M3-03/M3-04/M5-06: 有卡未实现` | `- …: 已实现（轮 32 由 S1-005/S1-007/S5-002 交付；本卡只搭了骨架）` | 轮 32 的三张交付卡 |
| `tasks/G5/S1-005` | `- M3-04: 有卡未实现` | `- M3-04: 已实现（#2 由 S1-007 交付）` | S1-007 的 EVIDENCE |
| `tasks/G1/S1-002` | `- M8-14: 有卡未实现` | `- M8-14: 已实现（轮 30 由 G5/S1-002 + G5/S6-001 交付；本卡交付的是 M6-03 诊断指标）` | 轮 30 的两张交付卡 |
| `tasks/G5/S4-003` | `- M5-04: 有卡未实现` | `- M5-04: 已实现` | 本轮新增 `verify/cases/g5_tempfile.ps1`（10/10 PASS） |

> **为什么必须改这些历史卡**：新合并规则是「任一卡声明非已实现 → 终态未实现」，
> 于是**旧卡的过时声明会一票否决**真实交付（实测：纠偏前若不改这 5 张卡，
> 已实现数会从 87 掉到 72，M6-02#1 / M6-09#1 / M3-03 / M3-04 / M5-06 / M8-14 全部被误判）。
> 验收方裁定的目标 85/6/25 正是「改完这 5 张卡 + R5 那一处」的结果（87−2=85、4+2=6）。
> 每处都写明**由谁交付**，不是空口改绿。

### 2.3 纠偏前后的覆盖表

```powershell
python verify/tools/coverage_report.py --write
```

| 时点 | 已实现 | 有卡未实现 | 无卡 | 缺理由 |
| --- | ---: | ---: | ---: | ---: |
| 轮 36 关闭时 | 87 | 0 | 29 | 0 |
| 只改合并规则、没改历史卡 | **72** | 19 | 25 | 15 |
| 改完 5 张历史卡（含 R5 那一处） | **85** | **6** | 25 | 0 |
| M3-05 落地后（`tasks/G5/S4-002`） | **87** | **4** | 25 | 0 |

```text
需求库 144 条, V1.0 66 条 / 116 子句, 已实现 87, 有卡未实现 4, 无卡 25, 已挂账 29, 缺理由 0
coverage table is up to date
```

→ **判据 1 达成**：R5 纠偏时点 = 85/6/25；M3-05 落地后 = 87/4/25（与验收方 §3 判据 1 一致）。

### 2.4 「数字下降不是回归」的说明

`docs/09-V1.0覆盖表.md` 顶部（由生成器写入，每次 `--write` 都在）：

```markdown
> **2026-10-29 变更说明（轮 37 · R5 纠偏）**：M3-05 的交付状态由「已实现」纠偏为
> 「有卡未实现」——G3/S4-001 卡写「已实现」但同卡 EVIDENCE 写 PARTIAL，且全仓无
> 「编码器列表 + 不可用原因」结构（旧合并规则「任一卡写已实现即已实现」把纠正覆盖掉了）。
> 该纠偏使**已实现 87→85、有卡未实现 4→6**：**属纠偏，不是回归**——G5 已于轮 36 关闭，
> M6-02#1 / M6-09#1 的 8h 单文件证据不变；M3-05 落地后恢复为 87/4/25。
```

## 3 · docs/06 §2 与 CONTRACT.md §2 对齐（判据 3）

```powershell
# 逐行比对：docs/06 §2 的 S1~S6 + V1 七行 vs CONTRACT.md §2 的同名七行
# （两份表的列数不同：docs/06 是 5 列，CONTRACT 是 3 列 → 取第一个含反引号的单元格）
python - <<'PY'
import re
def rows(path):
    out = {}
    for line in open(path, encoding='utf-8'):
        if not line.startswith('|') or '`' not in line:
            continue
        cells = [c.strip() for c in line.strip('|\n').split('|')]
        rid = cells[0].strip('* ')
        if rid not in ('S1','S2','S3','S4','S5','S6','V1'):
            continue
        for cell in cells[1:]:
            dirs = re.findall(r'`([^`]+)`', cell)
            if dirs:
                out[rid] = dirs
                break
    return out
a = rows('docs/06-子智能体执行规程.md')
b = rows('CONTRACT.md')
bad = 0
for k in ('S1','S2','S3','S4','S5','S6','V1'):
    same = a.get(k) == b.get(k)
    bad += 0 if same else 1
    print(k, 'docs06=', a.get(k), 'contract=', b.get(k), 'MATCH' if same else 'DIFF')
print('MISMATCH_COUNT=', bad)
PY
```

```text
S1 docs06= ['src/core/', 'src/ipc/', 'src/platform/', 'src/recorder/', 'src/tools/'] contract= ['src/core/', 'src/ipc/', 'src/platform/', 'src/recorder/', 'src/tools/'] MATCH
S2 docs06= ['src/capture/', 'src/compositor/'] contract= ['src/capture/', 'src/compositor/'] MATCH
S3 docs06= ['src/audio/'] contract= ['src/audio/'] MATCH
S4 docs06= ['src/encode/', 'src/mux/'] contract= ['src/encode/', 'src/mux/'] MATCH
S5 docs06= ['src/app/', 'ui/'] contract= ['src/app/', 'ui/'] MATCH
S6 docs06= ['src/resilience/', 'src/diag/'] contract= ['src/resilience/', 'src/diag/'] MATCH
V1 docs06= ['verify/'] contract= ['verify/'] MATCH
MISMATCH_COUNT= 0
```

- S1 行补齐 `src/platform/`、`src/recorder/`、`src/tools/`（`CONTRACT.md` §2 以这 5 个目录为准，
  轮 36 验收方裁定 `src/recorder/` 归 S1）；
- 根目录文件（`CMakeLists.txt`、`vcpkg.json`、`CONTRACT.md`）按 `CONTRACT.md` §2 的说明
  归 O0 维护，已从 S1 行移到 O0 行；
- `docs/06` §2 表下加了本次对齐的说明段（写明依据与「此后改文件域必须同时改两张表」）。

## 4 · 回归与自检

```powershell
python verify/tools/coverage_report.py --self-test   # 4 个对抗场景 + 三态自检
python verify/tools/coverage_report.py --check        # coverage table is up to date
python verify/tools/contract_check.py                 # C1~C8 8 PASS
```

```text
汇总: 8 项检查, 8 PASS, 0 FAIL
```

| 用例 | 结果 | 说明 |
| --- | --- | --- |
| `coverage_report.py --self-test` | PASS | 三态 + 子句级优先 + 4 个对抗场景 |
| `contract_check.py` | 8/8 PASS | 含 C8（未实现子句逐条挂账，缺理由 0） |
| `coverage_report.py --check` | PASS | 表与生成器一致 |
