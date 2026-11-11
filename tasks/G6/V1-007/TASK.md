# TASK: V1-007 轮 43 · F1 CBR 过冲定性 + 发布预演（主卡）

阶段：G6｜角色：V1（验收侧）｜轮次：43｜来源：`.codebuddy/plans/轮43指令.md` §1/§2/§3

> 本卡是**定性测量与记录类**主卡：F1 定性阶段**不改任何产品代码、不改 g5_format 判据**。
> F1 的产品修复在 `tasks/G5/S4-004`、F2 的用例修复在 `tasks/G6/V1-008`，
> 两次改动彼此独立、各自留证据（轮 43 指令 §4.1：不要把 F1 和 F2 混在一个改动里）。

## 需求范围

| 项 | 判据（轮 43 指令 §1/§3） |
| --- | --- |
| P0 F1 定性 | 静态桌面与运动源**各 5 次** CBR 40s，输出逐秒表；对比关键帧秒与普通秒的字节构成（`keyframe_bytes` / `filler_bytes` / `other_bytes`）。若静态 5 次中 ≥2 次 spread >10% → **M3-04#1 回退 PARTIAL** 并出缺陷单 |
| P1 矩阵补齐 | 能测的格子填实测值 + 日期（建议 100% 单屏 × 两个后端）；测不了的继续标「未测」 |
| P1 M1-27 终审材料 | 一页纸：提示/入口已实现 + 黑屏 BLOCKED 的环境依据 + 人工复测步骤，供轮 44 终审 |
| 通过判据 | 覆盖 ≥107 / 无卡 ≤8、`--check` 通过；回归 `g5_storage` 14、`g5_r42_closeout` 12、`g5_window` 24、`ui_smoke` 3×103、契约 8/8；三构建零告警 |

## 文件域（白名单）

- `verify/cases/g5_cbr_probe.ps1`（新增，F1 定性：5+5 次 40s + 逐秒表 + metrics）
- `verify/tools/cbr_nal_probe.py`（新增，NAL 级字节分解）
- `docs/07-执行台账.md`（轮 43 记录、状态迁移两行备注）
- `docs/09-V1.0覆盖表.md`（生成物；挂账理由列只手工填 M3-04#1 回退那一行）
- `docs/14-轮41-兼容性矩阵草案.md`（矩阵补齐）
- `docs/17-轮43-M3-04-CBR过冲缺陷单.md`（新增，定性成立时）
- `docs/18-轮43-M1-27终审材料.md`（新增）
- `tasks/G6/V1-007/*`（本卡四件套 + artifacts）

> 与其它卡的边界：`src/encode/` 由 `tasks/G5/S4-004` 独占；
> `verify/cases/g5_format.ps1` 及其自检由 `tasks/G6/V1-008` 独占；
> M3-04#1 的**状态声明**由 `tasks/G6/V1-009` 独占（本卡只承接 docs 写入）；
> P1 收口用例由 `tasks/G6/V1-010` 独占。
> **`docs/01`、`docs/03`、`docs/05` 本卡只读不改**；`CONTRACT.md` / `vcpkg.json` 不改。

## 上游交付

- 轮 42 六套交付包（`tasks/G6/V1-003`、`V1-005`、`V1-006`、`tasks/G5/S1-010`、`S2-003`、`S5-005`）
- `build-msvc/bin/Release`（SR_TEST_HOOKS=ON）、`build-release/`、`build-mingw/`
- `src/encode/MfVideoEncoder.cpp::PadToTargetBitrate()`（定性对象，只读）
- `verify/cases/g5_format.ps1::Get-PerSecondStats`（逐秒口径基准，只读）
- `verify/artifacts/format-20261110-183539/cbr.mkv`（验收方 58% 现场样本）

## 约束

1. **定性阶段的口径与 `g5_format.ps1` 完全一致**：按 `pts_time` 分桶、跳过第 0 秒与最后一秒，
   `spread=(max-min)/avg`、阈值 ≤10%。不得为了让结论好看换口径。
2. 静态桌面必须最小化干扰（清理 ffplay / sr-* 进程、不做任何 UI 动作、等 2s）。
3. 每次保留 `metrics` 全量行（capture_fps / encode_fps / dropped）与 recorder stdout。
4. 逐秒表要能定位具体秒号并标出该秒是否含关键帧（ffprobe `flags` 含 `K`）。
5. **不改 `g5_storage` 的口径**（轮 43 指令 §4.4，它刚稳定 14/14 双断言）。
6. 不改合并规则；`docs/09` 只手工填最后一列挂账理由。
7. 若定性成立（静态 ≥2 次超 10%），**照实回退 M3-04#1**，不为了保住 104 拖着（指令 §4.2）。

## 完成定义（DoD）

- [ ] F1 定性有 5+5 次数据，逐秒表 + 关键帧秒字节分解齐备
- [ ] 结论与覆盖率动作一致（回退或保留），缺陷单写了根因
- [ ] 矩阵 100% 单屏 × 两个后端各格有实测值 + 日期；测不了的仍标「未测」
- [ ] M1-27 一页纸终审材料
- [ ] 覆盖 ≥107 / 无卡 ≤8；`--check` 通过
- [ ] 回归清单全绿、三构建零告警
- [ ] 四件套齐全

## 覆盖子句

无（本卡是定性测量 + 记录/文档类工作，**不声明需求子句**）。
M3-04#1 由 `tasks/G6/V1-009` 声明；P1 收口子句由 `tasks/G6/V1-010` 声明。

## 交付状态

无（本卡不覆盖需求子句）。逐条状态见 `tasks/G6/V1-009`、`tasks/G6/V1-010`。