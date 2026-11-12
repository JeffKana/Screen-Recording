# SELFTEST: S4-005（轮 45）

## 0. 开工四项自查

| 检查项 | 结果 | 依据 |
| --- | --- | --- |
| 需求编号可检索 | 通过 | `docs/01:106` M3-04#1 原文可查（含轮 44 裁定的 5s 滑窗 + 2× 口径、`## 变更记录` 第 43 行） |
| 必读章节可定位 | 通过 | `docs/01` M3-04、`docs/17`（§7 轮 44 裁定）、`docs/02` 编码数据流、`docs/05 §2.4` 均存在 |
| 上游交付物可获取 | 通过 | 验收报告-轮44（三次实测）、`g5_format.ps1` 双断言、`g5_cbr_probe.ps1` 均在 main |
| 文件域无交集 | 通过 | 本轮 `src/encode/` 只有本卡；`verify/` 由 `V1-017` 独占；`docs/` 由 `V1-018` 独占 |

## 1. 自测命令与输出

| 命令 | 结果 | 时间戳 |
| --- | --- | --- |
| `pwsh -NoProfile -File verify/cases/g5_format.ps1` | `RESULT: 14 PASS / 1 FAIL / 0 SKIP`（FAIL=`cbr-window5-spread` 21.9%） | 2026-11-11 20:18 |
| `pwsh -NoProfile -File verify/cases/g5_cbr_probe.ps1 -Seconds 40 -Runs 3 -Mode static,active -OutDir tasks/G5/S4-005/artifacts/cbr-6runs` | 6 次全部 VERDICT=PASS（static 0.0%/0.0%/0.0%；active 0.5%/0.6%/0.6%） | 2026-11-11 20:38 |
| 三选二 A/B 扫描（18 次 25s 录制） | 见 `artifacts/sweep-summary.txt`：无旋钮把 spread 稳定压到 ≤10% | 2026-11-11 20:20~20:50 |
| 病态内容压测（全屏噪声） | mean×0.5 / MinQP=30/40 输出不变；固定 QP 被拒 `0x80070057` | 2026-11-11 20:47~20:52 |
| `cmake --build build-msvc --config Release` | 0 error / 0 warning | 2026-11-11 20:58 |
| `cmake --build build-msvc --config Debug` | 0 error / 0 warning | 2026-11-11 21:00 |
| MinGW Release 构建 | 0 error / 0 warning（`grep -c 'warning:'` = 0） | 2026-11-11 21:01 |

## 2. 红-绿对照

| 新断言（判据不变） | 旧实现实测 | 本轮改动后实测 |
| --- | --- | --- |
| `cbr-window5-spread ≤10%`（真实桌面 40s） | **21.9% FAIL**（单秒 9,473kbps） | **21.9% FAIL 不变**——三选二旋钮实测无效果，`src/` 净改动 0 |
| `cbr-single-second-cap ≤12000` | PASS（9,473） | PASS（不变） |
| 同断言在固定激励（static/active 各 3 次） | static 0.0%、active 0.5~0.6% | 同左（不变） |

> 本轮**没有绿**：这是「先红、实施候选修复、实测无效、回滚并如实报 PARTIAL」的闭环，
> 与轮 43 S4-004 同一种诚实路径。**不把 E3 的 6/6 当翻绿依据**（理由见 EVIDENCE E3）。

## 3. 回归

| 用例 | 结果 | 归属 |
| --- | --- | --- |
| `g5_format`（VBR 两条 / nominal / mode-isolation） | 14 PASS / 1 FAIL（唯一 FAIL 就是 M3-04#1 的滑窗） | 本卡 |
| `g5_storage` / `g5_r43_closeout` / `g5_window` / `g5_uac` / `ui_smoke`×3 | 见 `tasks/G6/V1-018/SELFTEST.md`（全绿） | 主卡 |

## 4. DoD 勾选

- [x] 红-绿对照：旧值（真实桌面 21.9% / 9,473kbps）与新值同表
- [x] 三选二逐个实测，给出「有无效果」的定量结论
- [x] 静态 3 + 活动 3 共 6 次，逐次给 win5 spread 与单秒峰值
- [x] VBR 两条 / M8-05 复核结论（`src/` 净改动 0，参数与轮 44 一致）
- [x] 未达成 → 明确写 PARTIAL 且 `docs/09` 保持 107/2/7
- [x] 四件套齐全（TASK / EVIDENCE / SELFTEST / HANDOFF + artifacts）
