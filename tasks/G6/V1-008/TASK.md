# TASK: V1-008 轮 43 · F2 取数失败与断言失败分离

阶段：G6｜角色：V1（验收侧）｜轮次：43｜来源：`.codebuddy/plans/轮43指令.md` §1 P0 / §4.3

> **本卡只改用例，不改产品**（轮 43 指令 §4.1）。F1 的产品修复在 `tasks/G5/S4-004`。
> 本卡修改 `verify/cases/g5_format.ps1` 是**验收方轮 43 指令 §1 P0 F2 明确授权的用例修复**：
> 修的是「取数失败拿 0 当指标算」的测量缺陷，与轮 41 R1 同族；判据（≤10% / ≤20% / 峰值 ≤102%）
> **一字不改**，且新增护栏断言证明「真实产品失败仍判 FAIL、不得伪装成 SKIP」。

## 需求范围

判据（轮 43 指令 §1 / §3 #2）：`g5_format` 每段先校验 `record_exit` 与关键字段；
缺失重试一次，仍缺失判「数据未取到（SKIP）」并附 stdout 尾 20 行；
**人为造一次 recorder 不启动，输出应是 1 条 SKIP 而非 10 条 FAIL**。

## 现状（只读结论）

`verify/cases/g5_format.ps1:67-89` `Record-Segment` 返回 `Read-TextSafe $stdout`，
不读 `$proc.ExitCode`、不查 `record_exit`、不验成片；`Field()`（:61-65）缺字段返回 `''`，
PowerShell 转数值为 `0` → 14 条断言照跑，刷出 10 条假 FAIL
（现场 `verify/artifacts/format-20261110-183428/summary.json`：`fps60.mkv.part=108B`、`fps60.out=0B`）。

## 文件域（白名单）

- `verify/cases/g5_format.ps1`
- `verify/cases/g5_format_selftest.ps1`（新增，取数失败分支自检）
- `tasks/G6/V1-008/*`（本卡四件套）

> 与 `tasks/G5/S4-004`（`src/encode/`）、`tasks/G6/V1-007`（定性用例 + docs）、
> `tasks/G6/V1-010`（P1 收口用例）目录互斥。

## 约束

1. **区分两种失败，不许把产品失败藏成 SKIP**：
   - 数据未取到（stdout 空 / `record_exit` 字段缺失 / 成片不存在或过小）→ **SKIP「数据未取到」**；
   - 数据齐但 `record_exit != 0`（产品明确报错）→ **FAIL**（真实产品失败）。
2. 每段校验失败先**重试一次**；仍失败则输出 1 条 SKIP + stdout 尾 20 行，
   **终止整个用例**（不继续跑其余断言），退出码 0（SKIP 不计 PASS/FAIL）。
3. `-SimulateNoStart` / `-SimulateFieldMissing` 是验收方要求的**可重跑分支入口**，
   不是调试残留；发布构建不含这些参数（它们是 PowerShell 用例参数，与产品无关）。
4. 不改任何断言的阈值与判据；`Get-PerSecondStats` 的逐秒口径一字不动。

## 完成定义（DoD）

- [ ] `g5_format_selftest.ps1` 全绿，含：造假不启动 → 1 SKIP / 0 FAIL / 退出码 0；
      真实 `record_exit!=0` → 判 FAIL 不伪装 SKIP；缺字段重试一次；正常段不受影响
- [ ] `g5_format` 正常模式 14 PASS / 0 FAIL（F1 修复后）
- [ ] 红绿对照：旧实现在「不启动」下 10 条 FAIL，新实现 1 条 SKIP
- [ ] 四件套齐全

## 覆盖子句

无（本卡是用例侧工作，不声明需求子句）。

## 交付状态

无（本卡不覆盖需求子句）。