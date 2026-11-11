# SELFTEST: V1-006

## 0. 开工四项自查
| 检查项 | 结果 | 依据 |
| --- | --- | --- |
| 需求编号可检索 | 通过 | `docs/01:205` M6-10 验收要点第 2 句 |
| 必读章节可定位 | 通过 | 轮42指令 §1 P1/§4.4、`docs/11b §5`、`packaging/install.ps1` |
| 上游交付物可获取 | 通过 | `tasks/G6/V1-001` 发布包、`dist/*.zip` |
| 文件域无交集 | 通过 | 本卡 `verify/cases/g5_r42_hvci.ps1` + `docs/14` + 本卡目录；其余卡不写这些 |

## 1. 自测命令与输出
| 命令 | 结果 |
| --- | --- |
| `pwsh -File verify/cases/g5_r42_hvci.ps1` | 7 PASS / 0 FAIL / 0 SKIP |
| `python verify/tools/contract_check.py` | 8/8 PASS |
| `python verify/tools/coverage_report.py --check` | up to date |

## 2. 红-绿对照
| 断言 | 旧实现实测 | 新实现实测 |
| --- | --- | --- |
| `m6-10-02-ui-runs-under-hvci` | FAIL（按 GUI stdout 判定，拿不到输出） | PASS（退出码 0 + 日志 `test_hooks=0`） |

## 3. 回归
全量回归见 `tasks/G6/V1-003/artifacts/r42-regression.txt`；本用例在其中重跑。

## 4. DoD 勾选
- [x] 开工四项自查通过
- [x] 一条需求一条证据，含命令与实测数值
- [x] 首轮 FAIL 已定位为用例取数缺陷并修正，判据未放宽
- [x] 环境判据为真实 HVCI，非模拟
- [x] 未改 `src/` / `CONTRACT.md` / `vcpkg.json`
- [x] 四件套齐全；未自评为已验收