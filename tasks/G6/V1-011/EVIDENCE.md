# EVIDENCE: V1-011（轮 44 · M3-04#1 新判据落地 + 发布预演，主卡）

> 口径声明：本文件是**执行方证据**，不是验收结论；验收方独立重跑后判定 PASS/PARTIAL/FAIL。
> 数据来源：2026-11-11，`build-msvc/bin/Release`（SR_TEST_HOOKS=ON）；
> 本机 Win11 家庭版 build 26200 / i5-13500（14 核 20 线程）/ Intel UHD 770 /
> 双屏 1920×1080 / DPI 100%。全部文本证据在 `tasks/G6/V1-011/artifacts/` 与各子卡 artifacts。

## 0. 开工四项自查（`docs/06 §4.2`）

| 检查项 | 结果 | 依据 |
| --- | --- | --- |
| 需求编号可检索 | 通过 | `docs/01:106` M3-04（新判据）、`:111` M3-09、M6-01/04/05/08、M1-27 均有原文；逐条核过验收要点与 `docs/09` 子句表 |
| 必读章节可定位 | 通过 | `.codebuddy/plans/轮44指令.md` §1~§5、`验收报告-轮43.md` §裁定、`docs/05 §1/§2.1/§2.2/§7.2.1`、`docs/06 §4.2/§8.4`、`docs/11b §6`、`CONTRACT §2` 均在位 |
| 上游交付物可获取 | 通过 | 轮 43 七套交付包、`docs/17` 缺陷单与 5+5 次定性输出、三套构建产物、`dist/*.zip`；未用旧版本开工 |
| 文件域无交集 | 通过 | 5 张卡目录互斥（主卡 `docs/01/07/09/17/19`；V1-012 `g5_format.ps1`；V1-013 `g5_format_selftest.ps1`；V1-014 状态声明；V1-015 `docs/14/18`）。`domain_table_check` MISMATCH_COUNT=0 |

> **修改 `verify/` 用例的授权来源**：轮 44 指令 §1 第 2 点「`g5_format` 的
> `m3-04-cbr-spread` 拆两条」与 §2 P0「self-test 17→19」是**验收方明确要求的判据落地**，
> 由 V1-012 / V1-013 卡承接；不属「为让测试通过改尺子」。
> 判据数字 `10%` 与 `2×` 一个未动，且新增两条反向护栏专防改软。

---

## 1. P0 新判据落地（`docs/01` + 变更记录）

- 命令：`Select-String -Path docs/01-功能需求清单.md -Pattern '^## 变更记录|^\| M3-04 '`
- 输出：`docs/01:106` 现为
  「**CBR 模式下成片码率波动 ≤ 10%（5 秒滑窗口径），且单秒码率不超过标称的 2 倍**；
  VBR 按三条验收（…）」。
- `## 变更记录` 新增第 43 行（6 列表格）：轮次「43（验收方裁定，44 落地）」、
  日期 2026-11-11、依据写明「`01` 原文**未规定判据窗口**，「逐秒」是轮 32 测试侧自取口径；
  静态 4/5 超、运动 5/5 超；拒 D / 拒 B / 采 C 变体；A 登记 V1.5」，
  影响范围写明「仅判据窗口与上限明确化，**数值仍是 ≤10%**（未放宽）；新增 2× 是**收紧**；
  `docs/03` / `src/encode/` 未改」。
- `docs/17 §7` 补「轮 44 裁定结果」小节，把缺陷单从产品缺陷侧关闭、转为
  「判据口径变更 + V1.5 码控改进登记」。
- **本轮 `src/` 一字未动**（指令 §3 #1 不改码控），`docs/03` 未改、合并规则未改。

## 2. P0 两条新断言与滑窗口径（V1-012）

- 命令：`pwsh -File verify/cases/g5_format.ps1`（真实录制）；
  `pwsh -File verify/cases/g5_format.ps1 -SimulateCbrRates <csv>`（判据注入）。
- `m3-04-cbr-spread` → `cbr-window5-spread` + `cbr-single-second-cap`，
  共用 `Assert-CbrWindow`，正常录制与注入走**同一份实现**。
- **滑窗口径六条已写进用例头部注释**（指令 §3 #2）：
  ① 桶用 ffprobe `v:0` 的 `pts_time` 按 `Floor` 落整秒、累加 `size`、
  **跳第 0 秒桶与最后一个桶**，`bytes*8/1000`→kbps；
  ② 连续 **5 桶一窗、逐秒滑 1 桶**（不是 5 秒一跳），共 `max(0,N-4)` 窗、窗口数 0 判 FAIL；
  ③ 窗口值 = 5 桶**算术平均码率**；
  ④ `spread=(max-min)/avg ≤0.10`，沿用轮 32 定义只换输入序列；
  ⑤ 单秒挡板对**全部有效桶**逐个判 `≤ 2×6000=12000kbps`，与滑窗并列、**不许被稀释**；
  ⑥ 两条独立输出；`m3-04-cbr-nominal`、VBR 三条、60/15fps 断言与全部阈值字面量未动。
- 红-绿对照：

| 新断言 | 旧实现实测 | 新实现实测 |
| --- | --- | --- |
| `cbr-window5-spread` | 旧 `m3-04-cbr-spread` 在轮 43 静态 4/5 超、运动 5/5 超（最高 359%） | 注入 20 桶 → 16 窗 spread **18.2% FAIL**；真实 10 次 2.62%~7.30% PASS |
| `cbr-single-second-cap` | 无挡板，16,699kbps（2.8×）/ 31Mbps 级 IDR 秒无人拦 | 注入 9 桶含 16,699：滑窗 **0.0% PASS 但硬挡板 FAIL** |

## 3. P0 反向护栏（V1-013，17 → 19）

- 命令：`pwsh -File verify/cases/g5_format_selftest.ps1`
- 输出：`tasks/G6/V1-013/artifacts/selftest-19.txt` — **19 PASS / 0 FAIL**，退出码 0。
- 两条新增：
  - `r44-window-spread-fail`：10×6000 + 10×5000 → `FAIL cbr-window5-spread`（18.2%）、
    `PASS cbr-single-second-cap`、`SKIP=0`、`rc=1`。
  - `r44-single-second-cap-fail`：9 桶含 **16,699kbps** → 5 个滑窗全含尖峰、
    窗口值全等 8,140、`spread=0.0% PASS`，但 `FAIL cbr-single-second-cap`（16,699 > 12,000）、
    `SKIP=0`、`rc=1`。**这条直接兑现指令 §3 #3「不许以滑窗会稀释为由放过」**。
- 原 17 条一条未删；`f2-threshold-spread-10pct` 守卫随断言改名更新为
  `$w\.spread -le 0\.10`，仍强制文本含 `-le 0.10`（判据未改软）。

## 4. P0 10 次全过 + M3-04#1 翻绿（V1-014）

- 命令：`pwsh -File tasks/G6/V1-011/artifacts/run-r44-cbr10.ps1`（13:25:18 – 13:32:11）
- 输出：`artifacts/r44-cbr10.txt` → `ALL_10_PASS=True`；
  逐次 `*.persec.csv`（10 份）+ `*.out`（10 份）可独立重算。
- 口径与本卡 §2 完全一致；每次 `exit=0 frames=1200 dropped=0 buckets=38 windows=34`。

| 次 | 滑窗 spread | 单秒峰值 kbps | 次 | 滑窗 spread | 单秒峰值 kbps |
| --- | ---: | ---: | --- | ---: | ---: |
| static-01 | 3.79% | 7,153 | motion-01 | 4.69% | 6,824 |
| static-02 | 2.62% | 6,793 | motion-02 | 7.30% | 7,265 |
| static-03 | 5.87% | 7,804 | motion-03 | 5.19% | 6,904 |
| static-04 | 3.32% | 7,006 | motion-04 | 5.44% | 7,186 |
| static-05 | 4.94% | 7,504 | motion-05 | 7.14% | 7,016 |

- 判据：滑窗 ≤10%（最大 **7.30%**）；单秒 ≤12,000kbps（最大 **7,804**）。
  **没有一次需要靠滑窗稀释放过 IDR 失控**。
- 两次 `--check`（状态迁移）：
  - 第 1 次（起点/回退态 107/2/7）：`--write` 后 `--check` **PASS**；
    期间完成**状态移交**（V1-009 卡不再声明 M3-04#1，改由 V1-014 独占），
    消除最保守合并规则的「两卡声明矛盾」（轮 37 R5，实测 `--write` 曾非零退出）。
  - 第 2 次（翻绿）：V1-014 声明「已实现」→ `--write` → **108 / 1 / 7**
    → `--check` **PASS**；`contract_check` C8 8/8。
  - `docs/07`「轮 44 状态迁移留痕」两行备注对应两次 `--check`。

## 5. P0 无卡收口：**未达成，如实报 PARTIAL**（`docs/19`）

- 判据 §2「覆盖 ≥110 / 无卡 ≤4」与 §3 #4「真机/新容器继续挂账、不拿降级版本冒充」
  在本机**互相冲突**：M3-04#1 翻绿后为 108/1/7，要 ≤4 需真收 3 条，
  但剩余 7 条**全部**属于真机或新容器类。
- 命令与输出：`artifacts/r44-feasibility.txt`（本机 CPU/GPU/OS 实测、
  `src/mux` 文件清单、MP4 box 代码命中数 0、`--format/--container` 命中数 0、
  `MkvWriter` 单音轨三处行号、`docs/05` 参考机型与测量窗口原文）。
- 逐条阻塞证据与「已排除的看起来能收的做法」见 `docs/19-轮44-收口可行性与判据矛盾核查.md`；
  7 条在 `docs/09` 最后一列的挂账理由已由通用「未开工待排期」改为逐条证据并指向 `docs/19`。
- `docs/19 §4` 给出 A（补派真机/调口径）、B（判据改为 108/7 + 挂账齐备）、
  C（授权实现 MP4 muxer + 多音轨）三个待裁定方向；执行方**不自行取舍**，
  按 §3 #4 保持挂账、不假绿、不用降级凑数。
- 建议 B，但**不自行决定**；在回文前一律保持 7 条挂账。

## 6. P1 矩阵补 2 格 + M1-27 材料定稿（V1-015）

- 命令：`pwsh -File tasks/G6/V1-015/artifacts/run-r44-matrix.ps1`（13:32:45 – 13:34:54）
- 输出：`artifacts/r44-matrix.txt`，两格均 `PASS=True`：
  - **Win11 24H2 双屏 100%**（由「降级旁证」升级为三件套实测）：
    `backend=wgc`、60.05s / 1800 帧 / dropped=0 / 抽 3 帧非全黑 / 音画 **1ms** /
    duration 59.999s / 双轨 / 解码零错误。
  - **单屏 100% 第二后端 dxgi**：`--backend dxgi` → `backend=dxgi`（真强制）、
    60.03s / 1800 帧 / 1ms / 3 帧 / 双轨 / 零错误。
- **纠正轮 43 的一处错账**：`CaptureFactory.cpp:60-73` 只认 `wgc`/`dxgi`/`printwindow`；
  轮 43 用的 `dxgi-dd` 不匹配任何分支才走默认链（wgc 首选），
  **不是「自动回退」**。更正已同步 `docs/14 §3/§4` 与 `docs/07`。
- 其余 **10 格继续标未测**（150% DPI / Win10 / 23H2 / 三厂商 / 混合 DPI），按 §3 #4 不冒充。
- `docs/18` 定稿：新增 §0「EX-01 适用范围」（只覆盖黑屏一侧、不免除提示/入口与 §7.2 其余 5 条、
  「登记 ≠ 放宽判据」）与 §3.1「复测条件与判定门槛」（4 前提 + 3 判定），供轮 45 直接引用。
  **本轮未做 M1-27 终审/复测**（本机 UAC 不落安全桌面）。

## 7. 门判据检查

| 检查 | 命令 | 结果 |
| --- | --- | --- |
| 覆盖表 | `python verify/tools/coverage_report.py --check` | `up to date`；**108 / 1 / 7**（≥110 与 ≤4 未达成，见 §5） |
| 契约 C1~C8 | `python verify/tools/contract_check.py` | **8 PASS / 0 FAIL** |
| 卡面 | `python verify/tools/card_consistency_check.py` | **54 卡 0 冲突** |
| 域表 | `python verify/tools/domain_table_check.py` | `MISMATCH_COUNT=0` |
| 需求解析 | `coverage_model.load_requirements()` | **144 条**（`docs/01` 变更记录未干扰解析） |
| 新判据自检 | `pwsh -File verify/cases/g5_format_selftest.ps1` | **19 PASS / 0 FAIL** |
| 10 连测 | `artifacts/r44-cbr10.txt` | `ALL_10_PASS=True` |
| 三构建 / 全量回归 / 发布包 | `artifacts/r44-regression.txt`（13:37:01 – 13:49:30） | 三构建 `exit=0 warnings=0 errors=0`；判据 §3 #4 五项全绿（`g5_storage` 14、`g5_r43_closeout` 12、`g5_window` 24、`g5_uac` 17/1SKIP、`ui_smoke` 3×103）；`g5_format_selftest` 19、契约 8/8、54 卡 0 冲突、域表 0、发布包 17/0、`dist/` 只有 zip + summary。**两处 FAIL 及处置见 `artifacts/r44-regression-notes.txt` 与 `SELFTEST.md §3`** |

## 8. 已知限制（不得计为通过）

1. **判据 §2「覆盖 ≥110 / 无卡 ≤4」未达成**：实得 108/1/7；指令内部冲突已上报 `docs/19`，
   执行方保持挂账、不假绿。
2. 本轮**未改码控**（`src/encode/` 一字未动）；新判据是验收方裁定值，
   单秒 2× 硬挡板在这 10 次里从未被触发（最大 7,804 ≤ 12,000），
   它的判别力靠 V1-013 的两条反向护栏证明。
3. 10 连测是本机单台（Win11 build 26200 / i5-13500 / 双屏 100%）结果，未跨机型验证。
4. 矩阵单屏格是 `--display 0` 只采主屏的**降级验证**；双屏格是真实双屏。
5. M1-27 终审材料只是定稿，终审与人工复测**没做**。
6. **回归两处 FAIL（如实记录）**：
   - `g5_format` 的 `m3-04-mode-isolation` 漏改新口径（旧逐秒 spread 13.9% FAIL）——
     执行方遗漏，已修为同一套「5 秒滑窗 + 2× 挡板」，复跑 **15/0**、19 条自检仍 19/19。
   - `g5_encoder_list` 的 `m3-05-ui-listbox-exists`（`hwnd=0`）稳定复现；独立只读诊断
     `diag-encoder-list-timing.txt` 证明控件在 T+308ms 起存在且有效
     （`LIST_CONTROL_APPEARED=True`）、UI 日志三条断言全 PASS → **产品功能正常，
     用例取数时序竞态**。该用例**不在本轮 `verify/` 授权范围，未修改**，
     交验收方裁定；判据 §3 #4 五项不受影响。详见 `artifacts/r44-regression-notes.txt`。
7. **V1.0 仍不可发布**：7 条无卡、M1-27 待终审、M6-06 BLOCKED、矩阵 10 格未测、
   `docs/05 §7.2` 其余判据未跑完。
8. 执行方不自评为已验收；轮 44 主表行留给验收方。