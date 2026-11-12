# SELFTEST: V1-018（轮 45 · 主卡）

## 0. 开工四项自查

| 检查项 | 结果 | 依据 |
| --- | --- | --- |
| 需求编号可检索 | 通过 | 本卡不声明需求子句；涉及的 M3-04#1 / M3-09#* / M6-* 均在 `docs/01` 可查 |
| 必读章节可定位 | 通过 | `docs/09`（覆盖模型）、`docs/07`（一行制）、`docs/14`（矩阵口径）、`docs/20`（SOP）均存在 |
| 上游交付物可获取 | 通过 | `S4-005`（实测未达成）、`V1-017`（用例侧）均在本分支 |
| 文件域无交集 | 通过 | 本卡只写 `docs/` 六份 + `V1-014` 交付状态 + 本卡目录；`src/` 归 `S4-005`，`verify/` 归 `V1-017` |

## 1. 自测命令与输出（原始 RESULT 行）

| 命令 | 结果 | 时间戳 |
| --- | --- | --- |
| `python verify/tools/contract_check.py` | `汇总: 8 项检查, 8 PASS, 0 FAIL` | 21:02 |
| `python verify/tools/coverage_report.py --check` | `coverage table is up to date` | 21:02 |
| `python verify/tools/card_consistency_check.py` | `card_consistency: OK —— 扫描 56 张卡，0 处冲突` | 21:02 |
| `python verify/tools/domain_table_check.py` | `MISMATCH_COUNT=0` | 21:02 |
| `pwsh -File verify/cases/g5_storage.ps1` | `RESULT: 14 PASS / 0 FAIL / 0 SKIP` | 20:56 |
| `pwsh -File verify/cases/g5_r43_closeout.ps1` | `RESULT: 12 PASS / 0 FAIL / 0 SKIP` | 20:55 |
| `pwsh -File verify/cases/g5_window.ps1` | `RESULT: 24 PASS / 0 FAIL / 0 SKIP` | 20:56 |
| `pwsh -File verify/cases/g5_uac.ps1` | `RESULT: 17 PASS / 0 FAIL / 1 SKIP` | 20:57 |
| `pwsh -File verify/cases/ui_smoke.ps1` ×3 | `RESULT: 103 PASS / 0 FAIL / 0 SKIP` ×3 | 20:58~21:00 |
| `pwsh -File verify/cases/g5_format_selftest.ps1` | `RESULT: 19 PASS / 0 FAIL` | 20:54 |
| `cmake --build build-msvc --config Debug` | exit=0，`warning C` 计数 0 | 21:00 |
| `cmake --build build-mingw` | exit=0，`warning:` 计数 0 | 21:01 |
| `pwsh -File verify/tools/build_release.ps1` | `RESULT: 17 PASS / 0 FAIL`（346,342 字节） | 21:05 |

## 2. 红-绿对照

本卡不含产品断言，无「先红后绿」项；状态迁移的红-绿记录在 `tasks/G5/S4-005/SELFTEST.md` §2。

覆盖表侧的对照（同一命令、两个状态）：

| 状态 | `coverage_report --write` 结果 | `--check` |
| --- | --- | --- |
| 轮 44 末（M3-04#1 已实现） | 108 / 1 / 7 | PASS |
| 轮 45 回退（M3-04#1 有卡未实现） | **107 / 2 / 7** | PASS |

## 3. 回归

见 §1。全部用例零 FAIL，唯一非绿项是 `g5_format` 的 `cbr-window5-spread`（产品侧真失败，
已在 `S4-005` 如实上报，**未**通过改判据掩盖）。

## 4. DoD 勾选

- [x] 四项静态检查全过
- [x] 回归 §4 全部复跑并贴原始 RESULT 行
- [x] 三构建零告警
- [x] 发布包 17/17，`dist/` 只有 zip + summary
- [x] 四件套齐全，`HANDOFF` 四问答齐
- [x] 未提交 `dist/` / `build-*/` / `artifacts/`
