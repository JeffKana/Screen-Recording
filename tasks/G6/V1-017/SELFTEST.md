# SELFTEST: V1-017（轮 45）

## 0. 开工四项自查

| 检查项 | 结果 | 依据 |
| --- | --- | --- |
| 需求编号可检索 | 通过 | `docs/01` M3-04#1 / M3-05#1 / M3-05#2 均有原文 |
| 必读章节可定位 | 通过 | `docs/05 §2.4`、`§7.2`、`docs/14`、`docs/17 §7` 均存在 |
| 上游交付物可获取 | 通过 | `g5_format.ps1` 双断言、`run-r44-matrix.ps1` 均在 main |
| 文件域无交集 | 通过 | 本卡只碰 `verify/cases/g5_cbr_probe.ps1`、`g5_encoder_list.ps1`、`verify/lib/ActiveDesktop.ps1`；`docs/` 归 `V1-018`，`src/` 归 `S4-005` |

## 1. 自测命令与输出

| 命令 | 结果 | 时间戳 |
| --- | --- | --- |
| `pwsh -File verify/cases/g5_cbr_probe.ps1 -Seconds 40 -Runs 3 -Mode static,active -OutDir tasks/G5/S4-005/artifacts/cbr-6runs` | 6/6 VERDICT=PASS（static 0.0%×3、active 0.5/0.6/0.6%） | 2026-11-11 20:38 |
| `pwsh -File verify/cases/g5_encoder_list.ps1` ×5 | 5 × `17 PASS / 0 FAIL` | 2026-11-11 20:50 |
| `pwsh -File tasks/G6/V1-017/artifacts/run-r45-matrix.ps1` | `dual-screen-100-dxgi ... PASS=True` | 2026-11-11 20:53 |
| `pwsh -File verify/cases/g5_format_selftest.ps1` | `19 PASS / 0 FAIL`（反向护栏未因本轮改动失效） | 2026-11-11 20:54 |

## 2. 红-绿对照

| 新断言 | 旧实现实测 | 新实现实测 |
| --- | --- | --- |
| `g5_encoder_list` 的 `m3-05-ui-listbox-count`（≤2s 轮询后相等） | 轮 44 一次 15/1（`ui-listbox-count` 读到 0）；验收方复跑 17/0 未复现 | 连跑 5 次 17/0，5/5 稳定 |
| `g5_cbr_probe` 的 `VERDICT`（win5 ≤10% 且单秒 ≤2×） | 旧脚本只有逐秒 spread，无 5s 滑窗与单秒挡板，跑不出轮 44 判据 | 6 次逐次给出 VERDICT 与 capture_fps |
| 矩阵 `dual-screen-100-dxgi` | 该组合此前未测（双屏只测过 wgc） | PASS（2026-11-11，三件套齐） |

> 说明：`g5_encoder_list` 属**取数竞态**，红证据是轮 44 的现场（15/1），本轮不是在同一份现场上复现红；
> 因此本条按「加法式稳定化」记，不按「先红后绿」记，避免夸大。

## 3. 回归

| 用例 | 结果 |
| --- | --- |
| `g5_format_selftest`（19 条反向护栏） | 19 PASS / 0 FAIL |
| `g5_format`（VBR / nominal / mode-isolation 未受影响） | 14 PASS / 1 FAIL（唯一 FAIL 是 M3-04#1 滑窗，属产品侧，见 `S4-005`） |

## 4. DoD 勾选

- [x] `g5_cbr_probe.ps1` 一条命令跑 `-Mode static,active`，逐次给判定
- [x] `g5_encoder_list.ps1` 5 连绿 17/0
- [x] 矩阵新增 1 格（三件套 + 日期 + 后端名）
- [x] 四件套齐全
