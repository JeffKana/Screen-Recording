# TASK: V1-003 轮 42 · 两项裁定落地 + 发布预演（主卡）

阶段：G5/G6｜角色：V1（验收侧/工具与文档）｜轮次：42｜来源：`.codebuddy/plans/轮42指令.md`、验收报告-轮41（S=92.1）

> 轮 42 下发时仓库无预置任务卡，按验收方指令建卡（沿用轮 40 S5-004 / 轮 41 V1-002 做法）。
> 执行方不自评为已验收；全部结论待验收方独立重跑。

## 需求范围（判据以 `docs/01` 验收要点原文为准）

| 项 | 验收方判据 | 证据形式 |
| --- | --- | --- |
| P0 M8-05 新口径 | ① 主判据 `expected=(free−stop)/(configured_kbps×1000/8)`，±20% 不变；② **独立**断言「≥60s 录制平均写入速率 vs 配置码率 ≤20%」；两条同时存在、各自独立输出；`g5_storage` 连跑 5 次全绿；`g5_estimate_selftest` 仍 9/9 | 双断言红绿 + 5 连绿原始输出 |
| P0 M8-05#1 回退再翻绿 | 先「有卡未实现」→ 5 连绿 → 翻回；两次 `--check` 都过；回退与翻绿各留一行台账备注 | 两次 `--check` 输出 + 台账两行 |
| P0 M6-04#2 措辞落 01 | `docs/01` 可检索「不引入 GPL」+ 变更记录（轮 41，依据 03 §6）；只改 01 不改 03；M6-04#2 仍已实现 | 01 原文 diff + `--check` |
| P0 无卡收口 5 条 | 覆盖 ≥103 已实现 / 有卡 ≤1 / 无卡 ≤11；每条有卡 + 专属行为断言 | `g5_r42_closeout.ps1` 逐条断言 |
| P1 兼容性矩阵 1 格 | Win11 100% 单屏格有实测值 + 日期；其余 10 格仍标未测 | `docs/14` 表格 + artifacts |
| P1 F2 校验 | `card_consistency_check` 新增：正文引用 `docs/1x-*裁定申请` 的子句若为「已实现」→ 报警；假卡可触发 | 假卡红绿 + 36 卡 0 冲突 |
| P1 F3 docs 重号 | 两个 `11-` 编号唯一 + 索引登记 | `docs/INDEX.md` + 目录清单 |

## 必读章节

- `.codebuddy/plans/轮42指令.md` §1~§5、`.codebuddy/plans/验收报告-轮41.md` §2/§3/§5（F1/F2/F3）
- `docs/01-功能需求清单.md` M8-05:255、M6-04:199、M1-24:56、M3-10:112、M6-08:203、M6-10:205、M6-11:206、M7-13:237
- `docs/03-技术选型.md` §编码选型（GPL 排除）、`docs/05 §2.1/§7.2`
- `docs/06 §4.2`（开工四项自查）、`docs/06 §8.4`（薄切片）、`docs/11-轮36-G6交付方案.md §6`（矩阵三件套）
- `docs/15`、`docs/16`（轮 41 两份裁定记录（轮 42 已裁定））、`docs/12`（轮 34 同类先例）
- `CONTRACT.md §2`（文件域）、`§18.2`（退出码表）

## 文件域（白名单）

- `verify/cases/g5_storage.ps1`（**验收方裁定 A 明确授权改判据口径**，修的是测法缺陷，不是为让测试通过改尺子；轮 41 R1 同款授权）
- `verify/cases/g5_longwindow_selftest.ps1`（新增，护栏独立性自检）
- `verify/cases/g5_r42_closeout.ps1`（新增，5 条收口专属断言）
- `verify/tools/card_consistency_check.py`（**验收方 F2 明确要求**加规则）
- `verify/tools/coverage_report.py`（只在 `render()` 加一行轮 42 说明，**不动合并规则**）
- `docs/01`、`docs/05`、`docs/07`、`docs/09`（生成物 + 挂账理由列）、`docs/12`、`docs/14`、`docs/15`、`docs/16`、`docs/INDEX.md`（新增）
- `tasks/G6/V1-003/*`、`tasks/G5/V1-005/*`（新建卡与四件套）
- `tasks/G6/V1-002/*`、`tasks/G5/V1-004/*`（**仅限**按 F2/F3 更新历史卡里的文档引用路径措辞，不改 `## 交付状态`、不改需求号）

> **同轮文件域互斥**：`tasks/G5/S1-010` 独占 `src/recorder/` + `src/compositor/`；
> `tasks/G5/S5-005` 独占 `src/app/`。本卡不写 `src/`。
> `dist/` 由 `build_release.ps1` 重建（未传 `-SkipBuild`）。

## 上游交付

- 轮 41 交付包 `tasks/G6/V1-002/`、`tasks/G5/V1-004/`、`tasks/G5/S1-009/`
- `build-msvc/bin/Release`（SR_TEST_HOOKS=ON）、`build-release/bin/Release`（OFF）、`dist/*.zip`
- `verify/lib/Cleanup.ps1` 的 `Read-TextSafe`、`verify/cases/g5_closeout.ps1` 的断言范式
- 60s 探针：`tasks/G6/V1-002/artifacts/r42-longprobe.txt`（774,699 B/s，偏差 0.09%）

## 约束

1. **不改合并规则**：`coverage_model.py` / `build_rows` / `merge_clause_state` 一律不动；`coverage_report.py` 只加说明文本。
2. **不碰 `FreePath`、不重跑 8h**（轮 42 指令 §5）。
3. 主判据与长窗口护栏**必须同时存在、各自独立输出**；护栏字段缺失判 **FAIL**（拒 C：SKIP 会藏真超标），不得用 SKIP 伪装通过。
4. 5 条收口**必须真实有产品能力 + 行为断言**，不得用日志/文案冒充实测、不得把「引擎级/局部达成」标已实现。
5. 待裁定项先挂「有卡未实现」，裁定后再翻（F2 流程）。
6. `vcpkg.json` 只读；`CONTRACT.md` 不改（无接口签名变更）；不引三方依赖。
7. 台账：主表轮 42 只留一行 V1 验收行，执行方自述并入备注（一行制）；回退/翻绿各一行备注。
8. `docs/09` 是生成物，只手工填最后一列「挂账理由」；其余靠 `--write`。

## 完成定义（DoD）

- [ ] `g5_storage` 双断言各自独立输出，5 连绿全绿；`g5_estimate_selftest` 9/9
- [ ] 回退与翻绿各有一次 `--check` 通过 + 一行台账备注
- [ ] `docs/01` 含「不引入 GPL」+ 变更记录；`--check` 通过
- [ ] 覆盖 ≥103 / 有卡 ≤1 / 无卡 ≤11；`--check` 通过
- [ ] 矩阵 1 格有实测值 + 日期，其余 10 格标未测
- [ ] F2 假卡触发报警、真实卡 0 冲突；F3 编号唯一 + 索引
- [ ] 契约 8/8、三构建零告警、判据 §3 #5 全量回归、`dist/` 只有 zip + summary
- [ ] 四件套齐全（每张卡 EVIDENCE/SELFTEST/HANDOFF + artifacts）

## 覆盖子句

无（本卡是工具/文档/用例侧工作，**不声明需求子句**）。
6 条子句（M8-05#1 + 5 条收口）由 `tasks/G5/V1-005/TASK.md` 统一声明并逐条写 `## 交付状态`；
实现卡 S1-010 / S5-005 也写「无」，避免「最保守」合并规则把新卡拖成「有卡未实现」。

## 交付状态

无（本卡不覆盖需求子句）。逐条状态见 `tasks/G5/V1-005/TASK.md`。
