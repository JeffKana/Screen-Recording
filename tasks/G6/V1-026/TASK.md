# TASK: V1-026 轮 54 · `R1-52` 原子占位 + 0.1.1 + 长稳收口

阶段：G6｜角色：V1（主卡：缺陷修复 / 补丁发布 / 长稳判定）｜轮次：54
来源：`.codebuddy/plans/轮54指令.md`（判据以此为准）+ `验收报告-轮52-53.md §1/§4`（`R1-52` 定级严重、0.1.1 必修）
上游：`main` @ `e5b7f24`（#19/#20/#21 已合并）——**从最新 main 拉**（不再叠分支，`docs/20 §1` 本轮补的条款）

## 需求范围

| # | 项 | 要求 | 证据形式 |
| --- | --- | --- | --- |
| P0-1 | `R1-52` 同名 `.part` 原子占位 | 选名后立刻 `CREATE_NEW` 独占创建；撞车顺延，最多 999 次；**不许只加锁不改选名** | `EVIDENCE.md §1`；`g3_output` +2 条（先红后绿） |
| P0-2 | 0.1.1 发布 | `channel` 扩成四态：`-rc.`→rc / `0.1.0`→mvp / **`0.1.x`→mvp-patch** / 其余→ga；`ga_ready=0`；包 17/17；`docs/24` 补行；`gh release create v0.1.1`（非 prerelease） | `EVIDENCE.md §2/§4` |
| P0-3 | LT-01 第 3 次判定 | 六项判据逐条核；通过 → `docs/22` ③ 改 **3/3**；漂移给**最差点** | `EVIDENCE.md §3` |
| P1 | `docs/20` 补「串行依赖必须 rebase」/ `docs/26` 归档 `R1-52` / 台账一行留白 | 见 `EVIDENCE.md §5` | — |

## 判据（照抄轮 54 卡「通过判据」）

1. 20 次抢同一路径 **0 损坏**，且「只有一个成功 / 另一个顺延」行为稳定；
2. 回归：`g3_output` 17（15+2）、`g5_fileops` 9、`g5_format` 15、`g5_mp4` 23、`ui_smoke` 3×103；
3. 0.1.1 包与 release 落地，`channel=mvp-patch`；
4. LT-01 第 3 次有结论（通过则 ③ 3/3）；
5. CI 三绿；契约 8/8；三构建零告警。

## 文件域

| 类别 | 文件 |
| --- | --- |
| 产品 | `src/recorder/OutputPath.{h,cpp}`、`src/recorder/RecordingSession.{h,cpp}`、`src/recorder/main.cpp`、`src/mux/MkvWriter.{h,cpp}`、`src/mux/Mp4Writer.{h,cpp}` |
| 用例 | `verify/cases/g3_output.ps1`（+2 条；其余用例不动） |
| 工具/文档 | `verify/tools/build_release.ps1`（channel 四态）、`docs/07`、`docs/20`、`docs/22`、`docs/24`、`docs/26`、`README.md`、`RELEASE-NOTES-0.1.1.md`（新建） |
| 交付包 | `tasks/G6/V1-026/**`（`artifacts/` 不入库） |

**禁止**：改 `verify/` 既有断言来迁就新行为（`g3_output` 的 `residue-avoided-name`、A/B/C/D 段一律不动）；
改 `g5_mp4` / `g5_fileops` / `g5_format`；把 `ga_ready` 改成 1；删除 0.1.0 的已发布资产或 tag。

## 交付状态

本轮不改任何覆盖子句的状态（`M5-15` / `M8-07` 早已完成，本轮修的是它们**没覆盖到的并发窗口**）；
`coverage_report --check` PASS、111 / 1 / 4 不变即为证据。**不自评「已验收」**。
