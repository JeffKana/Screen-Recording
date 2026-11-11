# HANDOFF: V1-002（轮 41 · 用例稳定化 + 挂账 + 发布前准备）

## 一、我改了什么接口？

**没有接口变更**，`CONTRACT.md` 未动，无需契约变更单。

| 改动 | 性质 |
| --- | --- |
| `verify/cases/g5_storage.ps1` | 用例取数前置与失败语义（验收方 R1 明确授权）。新增 `-SelfTestEstimate` / `-SimulateMissingFields` / `-EstimatePayload` 三个**测试入口参数**，供字段缺失分支用例重跑；误差 ≤20% 判据未改 |
| `verify/cases/g5_estimate_selftest.ps1`（新增） | 字段缺失分支用例，9 条断言 |
| `verify/cases/g5_closeout.ps1`（新增） | 5 条无卡子句的 18 条专属行为断言 |
| `verify/tools/build_release.ps1` | 新增 `release-help-four-params` / `extract-help-four-params` 两条自检断言；原有 15 条不变 |
| `src/recorder/main.cpp`（S1-009 卡） | `PrintUsage()` 无条件段增加 6 行帮助文本；`ParseArgs` 未动、接口签名未动 |
| `docs/05 §7.2.1` | 新增发布例外清单小节（EX-01） |
| `docs/07` | 台账口径注改为一行制；新增「轮 41 记录」小节（执行方自述） |
| `docs/09` | 由 `coverage_report.py --write` 重生成（状态变了，规则没动）+ M1-27#1 挂账理由改写 |
| `docs/14` / `docs/15` / `docs/16` | 新增：矩阵草案 / x264 口径裁定申请 / M8-05 测法裁定申请 |
| `tasks/G6/V1-002/*`、`tasks/G5/V1-004/*`、`tasks/G5/S1-009/*` | 三张卡与四件套；`dist/` 产物由 `build_release.ps1` 重建 |

## 二、下游需要接什么？

1. **验收方独立重跑**（不自评）：
   `pwsh -File verify/cases/g5_estimate_selftest.ps1`（9/9）、
   `pwsh -File verify/cases/g5_closeout.ps1`（18/18）、
   `pwsh -File verify/cases/g5_storage.ps1`（连跑 5 次，**预期 4 绿 1 红，见第三问**）、
   `pwsh -File verify/tools/build_release.ps1`（17/17）、
   `Expand-Archive dist/…zip` 后跑包内 `sr-recorder.exe --help`。
2. **两份裁定记录（轮 42 已裁定）需要回文**：`docs/15`（M6-04#2 的 x264 口径）、`docs/16`（M8-05 估算测法）。
   两份都给了选项和回退路径，执行方没有自行定判据。
3. **台账主表**：轮 41 主表**故意留空**，按一行制由验收方写唯一一行 V1 验收行；
   执行方自述已并入 `## 轮 41 记录`。
4. **轮 42 发布复核**：`docs/05 §7.2.1` 的 EX-01 终审、`docs/14` 矩阵实测、
   `docs/05 §7.2` 六条判据逐条。

## 三、已知限制是什么？

1. **R1 的「连跑 5 次 13/13」未达成**：实测 4/5。run3 是**真实误差 23.3%**，
   不是假失败（字段齐全、`record_exit=0 attempts=1 stale=0`）。
   根因：产品按配置码率（≈774 KB/s）估算，用例用 10s 短窗口实测写入速率
   （5 次 787~955 KB/s，波动 ±21%）作对照，20% 阈值正好落在波动区间内 → 判据不可复现。
   **我没有放宽 ≤20%、没有改对照口径、没有挑数据**，全部写进 `docs/07` 与 `docs/16`，
   `g5_estimate_selftest.ps1` 的护栏断言还证明「真实超标仍会 FAIL、不会被伪装成 SKIP」。
   → 建议验收方在 `docs/16` 的 A/B/C/D 里裁定；若判 D，M8-05#1 回退有卡未实现、覆盖 −1。
2. **M6-04#2 术语冲突**：`docs/01` 点名 x264，`docs/03` 因 GPL 明确改用 MF 软编。
   本轮按既有软编等价口径给行为证据并暂记「已实现」，但**验收方可判 PARTIAL**；
   若裁定「x264 为字面硬要求」，下一轮回退为有卡未实现、覆盖 99 → 98。
3. **降级/代理验证**：
   - M8-01#1：12s 强杀代理需求原文的 5min 长录（loss=2.550s ≤10s）；
   - M3-15#1：`--inject-drop-every 4` 代理真实 GPU 压力；音画同步用「双轨 + 全片解码零错误」代理，
     不是逐帧 A/V 偏差实测；
   - M6-11#1：面板只呈现三项指标（采集/编码 fps、丢帧），需求标题另列的
     码率/队列深度/写盘速度/编码器状态**未呈现**；#2 背压可见标识仍无卡。
4. **兼容性矩阵**：12 格 `未测`，本机唯一一格标「降级旁证（非矩阵实测）」；
   M6-06（150% DPI）环境 BLOCKED。必测 100% / 抽测 ≥80% **未达成**。
5. **回归**：`artifacts/regression.txt` 是权威结果，14:05–14:12 实测三构建零告警、
   10 个关键用例全部符合期望（`ui_smoke` 103/0）。判据要求 `ui_smoke` 3×103，
   另用 `artifacts/run-ui-smoke-x3.ps1` 连跑，run1/run2/run3 均 `103 PASS / 0 FAIL / 0 SKIP`
   （`artifacts/ui-smoke-x3.txt`）；`docs/07:518` 的 3×103 是轮 40 历史实测，**未冒充本轮**。
6. **发布例外 ≠ 放宽判据**：EX-01 登记的是环境阻塞事实，`docs/05 §7.2` 六条仍逐条生效。

## 四、哪些需求我**没**做完？（最重要）

| 项 | 状态 | 原因 / 下一步 |
| --- | --- | --- |
| **P0 R1「连跑 5 次全绿」** | **未达成（4/5）** | run3 真实误差 23.3%，属测法口径方差；已提 `docs/16` 裁定记录（轮 42 已裁定），未放宽判据。假失败本身已消除（红绿 + 9/9 自检可证） |
| **P0 无卡收口 5 条** | 已交付，但 1 条待裁定 | 18/18 断言、覆盖 99/1/16 达成；M6-04#2 的 x264 口径待 `docs/15` 裁定，验收方可判 PARTIAL |
| **P0 M1-27 挂账** | 已交付 | 两处文本一致、EX-01 已登记；黑屏本身仍 BLOCKED，需安全桌面机器 + 人工取样，轮 42 终审 |
| **P1 `--help` 补全** | 已交付 | 包内 help 四参数齐全、17/17 自检通过；只补可发现性，未新增功能 |
| **P1 兼容性矩阵** | **仅草案** | 实测放轮 42，本轮按指令不借机器、不实测 |
| **P1 台账一行制** | 已交付 | 轮 41 主表零行，留给验收方写唯一 V1 行；执行方自述在「轮 41 记录」 |
| `ui_smoke` ×3 连跑 | 已交付 | 3×103 PASS / 0 FAIL，见 `artifacts/ui-smoke-x3.txt` |
| M6-04#2 的 x264 字面实现 | **没做，也不该做** | `docs/03` 因 GPL 明确排除 libx264；等裁定，不做选项 B |
| M8-05 对照口径调整 | **没做** | 等 `docs/16` 裁定，不自行放宽 |
| 16 条剩余无卡子句 | **没收口** | 本轮只做 5 条清账；其余需真机/新能力，按 `docs/01` 判据逐条排期 |
| M6-06（150% DPI） | BLOCKED | 环境无 150% DPI 显示器，不因实现困难放宽 |
| **V1.0 发布** | **仍不可发布** | 16 条无卡、M1-27 黑屏例外待终审、M6-06 BLOCKED、矩阵未实测、§7.2 其余判据未跑完 |

## 五、交付包清单

```
tasks/G6/V1-002/  TASK.md EVIDENCE.md SELFTEST.md HANDOFF.md + artifacts/（红/绿/5连跑/收口/构建/包help/回归）
tasks/G5/V1-004/  TASK.md EVIDENCE.md SELFTEST.md HANDOFF.md
tasks/G5/S1-009/  TASK.md EVIDENCE.md SELFTEST.md HANDOFF.md
```

**执行方不自评为已验收**；所有结论待验收方独立重跑判定。
