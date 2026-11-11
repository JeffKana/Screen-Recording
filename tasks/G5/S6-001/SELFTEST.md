# SELFTEST: G5/S6-001（诊断包真实截断）

## 0. 开工四项自查

| 检查项 | 结果 | 依据 |
| --- | --- | --- |
| 需求编号可检索 | 通过 | `docs/01-功能需求清单.md` M8-14「一键导出含版本、硬件信息、最近日志、当前配置的诊断包」 |
| 必读章节可定位 | 通过 | `docs/05` §3 诊断包体积/脱敏；`docs/06` §6.1 |
| 上游交付物可获取 | 通过 | `tasks/G5/S1-002`（T-17）的 `TruncateByAge`；真实 60min 日志 `verify/artifacts/cbr60/cbr60.log` |
| 文件域无交集 | 通过 | 只碰 `src/recorder/main.cpp` 的 export-diag 分支 + 新用例；S5-002 已交付完毕 |

## 1. 自测命令与输出

| 命令 | 结果 | 时间戳 |
| --- | --- | --- |
| `pwsh -File verify/cases/g5_diag_real.ps1 -LogFile verify\artifacts\cbr60\cbr60.log` | **7 PASS / 0 FAIL**（600s / 596 行 / 56KB） | 2026-10-26 13:19 |
| `pwsh -File verify/cases/g5_diag.ps1` | 36 / 36（含三态自检，未回归） | 2026-10-26 12:05 |
| `sr-recorder --diag-window-test <合成2h日志>` | `span_ms=600000`（函数级，保留） | 轮 31 已有 |

## 2. 红-绿对照

| 新断言 | 旧实现实测 | 新实现实测 |
| --- | --- | --- |
| `diag-has-recorder-section` + 数据 | `log.txt` 141B，正文 `(recorder log unavailable)` | 56,153B，含最近 10 分钟 596 行 |
| `diag-span-within-10min` | 无从测量（没有内容） | 600s |
| `diag-keeps-newest` | 无 | 末条时间戳与源一致 |
| 源日志是否被破坏 | **被截成 0 字节** | 原样保留（30min 日志 159KB 未变） |

## 3. 回归

| 用例 | 结果 |
| --- | --- |
| `g5_diag` | 36 / 36 |
| `g5_diag_real` | 7 / 7 |
| `ui_smoke`（UI 的 `--export-diag` 同款修复） | 101 / 101 |

## 4. DoD 勾选

- [x] 真实 ≥30min 日志验证（60min，3582 行）
- [x] 跨度 ≤10min、保留最新、≤400KB
- [x] 同一缺陷（输入被清空）已修，且写进 EVIDENCE/HANDOFF
- [x] 既有 `g5_diag` 36 条未改动
- [x] Release 零告警

## 5. 已知限制

- `log.txt` 里的 recorder 段只取源文件**尾部 128KB**，再按 10 分钟窗口裁。若 10 分钟内的日志
  超过 128KB（debug 级 + 多路指标），尾部截断会先于时间窗生效——此时跨度可能小于 10 分钟，
  但不会超。已在 `DiagPack.cpp` 注释里说明，属既有设计。
- 本轮没有改脱敏逻辑（`SanitizeText`），只改「谁有权打开日志文件」。
