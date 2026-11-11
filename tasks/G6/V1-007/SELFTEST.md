# SELFTEST: V1-007（轮 43 主卡）

## 0. 开工四项自查

| 检查项 | 结果 | 依据 |
| --- | --- | --- |
| 需求编号可检索 | 通过 | M3-04:106、M1-24:56、M7-13:237、M1-27 在 `docs/01` 有原文，逐条核过验收要点 |
| 必读章节可定位 | 通过 | 轮43指令 §1~§5、验收报告-轮42、`docs/05 §2.4/§7.2`、`docs/06 §4.2/§8.4`、`docs/11b §6`、`CONTRACT §2` |
| 上游交付物可获取 | 通过 | 轮 42 六套交付包 + 三套构建产物 + `dist/*.zip` + 验收方 58% 现场样本；未用旧版本开工 |
| 文件域无交集 | 通过 | 7 张卡目录互斥，见 `EVIDENCE.md §0`；`domain_table_check` MISMATCH_COUNT=0 |

> F1（产品）与 F2（用例）分两次独立改动，各自留证据（指令 §4.1）。
> F1 定性阶段未改产品、未改 `g5_format` 判据。

## 1. 自测命令与输出

| 命令 | 结果 | 证据 |
| --- | --- | --- |
| `pwsh -File verify/cases/g5_cbr_probe.ps1 -Seconds 40 -Runs 5 -Mode both` | 静态 4/5 超 10%、运动 5/5 超 | `artifacts/cbr-probe-20261111-102548/probe-summary.txt` |
| `python verify/tools/cbr_nal_probe.py <mkv> <sec>` | `decoded_bytes == probe_bytes`，过冲秒 `filler=0` | `*.nal-fixed-sec*.json` |
| 三构建编译（按秒结算版） | `exit=0 warnings=0 errors=0` | `artifacts/build-summary.txt` |
| `pwsh -File artifacts/run-f1-after.ps1` | 10 次：静态 3/5 超、运动 5/5 超、运动 avg 8,039 kbps | `tasks/G5/S4-004/artifacts/cbr-after-fix/probe-summary.txt` |
| 回滚后三构建 | `exit=0 warnings=0 errors=0` | `artifacts/build-summary.txt` |
| `pwsh -File verify/cases/g5_format_selftest.ps1` | **17 PASS / 0 FAIL** | `tasks/G6/V1-008/artifacts/f2-selftest.txt` |
| `pwsh -File verify/cases/g5_r43_closeout.ps1` | **12 PASS / 0 FAIL / 0 SKIP** | `artifacts/r43-closeout.txt` |
| `pwsh -File artifacts/run-r43-matrix.ps1` | wgc PASS；dxgi-dd 自动回退到 wgc → 标未测 | `artifacts/r43-matrix.txt` |
| `python verify/tools/coverage_report.py --write` / `--check` | **107 / 2 / 7**，`up to date` | 终端输出 |
| `python verify/tools/contract_check.py` | **8 PASS / 0 FAIL** | 终端输出 |
| `python verify/tools/card_consistency_check.py` | **49 卡 0 冲突** | 终端输出 |
| `python verify/tools/domain_table_check.py` | `MISMATCH_COUNT=0` | 终端输出 |

## 2. 红-绿对照

| 新断言 | 旧实现实测 | 新实现实测 |
| --- | --- | --- |
| F1 定性（5+5 次） | 验收方 5 次运行四种断言，被判「不是抖动」 | 静态 4/5、运动 5/5 稳定超 10%；NAL 分解给出根因 |
| F1 按秒结算 | 运动 avg 6,276（偏差 4.6%）、5/5 超 | 运动 avg **8,039（偏差 34%）**、5/5 超 → **回滚** |
| F2 造假不启动 | 14 条断言照跑，刷 10 条假 FAIL（4/10） | **1 SKIP / 0 FAIL / exit 0**，`attempts=2` |
| 产品 `record_exit=6` | 与取数失败混在一起 | 判 FAIL，不伪装 SKIP |
| `--cursor off` 成片指针像素 | 无 CLI 入口，恒含光标 | 中心 0、全画面抽样 0 / 518,400 |
| 托盘红点 | 系统 `IDI_INFORMATION` | 自绘 `red_dot_pixel=0xFF0000FF` |
| 屏幕边框 | 无 | 录制红框 RGB(255,0,0)、暂停可区分、可关 |

> 三处首轮取数/实现缺陷（NAL 工具 seek 不全、ffplay 被隐藏、WGC 静止不刷新光标层）
> 与一处 R5 卡面冲突、一处 stale 都已修并留痕，**判据未放宽**。

## 3. 回归（2026-11-11 11:27:19 – 11:44:04 实测）

> 汇总 `artifacts/r43-regression.txt`，逐项原始输出 `artifacts/reg-*.txt`。
> **以下全部是本轮实测值，不是历史数字。**

| 用例 | 判据期望 | 本轮实测 | 退出码 |
| --- | --- | --- | ---: |
| `g5_storage` | 14 PASS（双断言） | **14 / 0 / 0** | 0 |
| `g5_r42_closeout` | 12 PASS | **12 / 0 / 0** | 0 |
| `g5_window` | 24 PASS / 0 SKIP | **24 / 0 / 0** | 0 |
| `g5_uac` | 17 PASS / 1 SKIP | **17 / 0 / 1 SKIP** | 0 |
| `g4_gate` / `g3_recovery` / `g3_playback` / `g3_storage` | 全绿 | 12 / 9 / 11 / 11 | 0 |
| `g5_encoder_list` / `g5_tempfile` | 17 / 10 | 17 / 10 | 0 |
| `g5_estimate_selftest` / `g5_longwindow_selftest` / `g5_f2_selftest` | 9 / 10 / 4 | 9 / 10 / 4 | 0 |
| `g5_format_selftest` | 17 | **17 / 0** | 0 |
| `g5_r43_closeout` | 12 | **11 / 1 FAIL**（见下） | 1 |
| `g5_format` ×3 | 3 次全绿（指令 §3 #3） | **14/14、14/14、13/1**（见下） | 0 / 0 / 1 |
| `ui_smoke` ×3 | 3×103 PASS | **3 × 103 / 0 / 0** | 0 |
| 契约 / 卡面 / 域表 / `--check` | 通过 | 8/8、49 卡 0 冲突、0 不匹配、up to date | 0 |
| 发布包 `build_release.ps1` | 17 PASS | **17 / 0 FAIL**，zip 346,340 B | 0 |

### 两处未绿，如实记录（不放宽判据、不藏成 SKIP）

1. **`g5_format` 第 3 次 13/1**：
   `FAIL m3-04-cbr-spread — 逐秒 6,000~16,699kbps avg=6,916 spread=154.7%（判据 ≤10%）`。
   这是 **F1 未修复的真实产品失败**，不是取数问题（F2 已把取数失败分离成 SKIP）。
   判据 §3 #3「连跑 3 次全绿」**未达成**，判 PARTIAL；M3-04#1 保持「有卡未实现」。
2. **`g5_r43_closeout` 11/1**：`m1-24-02-pointer-follows-position` 在回归上下文里
   B 段 B 点非背景像素 = 0，根因是 WGC 只在指针移动时刷新光标层、两段之间偶发不刷新。
   修法：A/B 段各自录前抖动一次鼠标。修后独立连跑 **3 × 12/12 全绿**
   （`artifacts/r43-closeout-x3.txt`）。属用例时序缺陷，判据未放宽。

> 回归 runner 的 `TOOL coverage_report.py` 行取到的是脚本帮助文本末行（取行逻辑），
> 退出码 0；直接在终端跑 `--check` 为 `coverage table is up to date`。

## 4. DoD 勾选

- [x] 开工四项自查全部通过并已记录
- [x] 任务卡每条需求 → 一条证据，含命令与输出（`EVIDENCE.md §1~§7`）
- [x] 新增断言都有旧值/新值对照（§2）
- [x] Release + Debug + MinGW 三构建零告警（含回滚后）
- [ ] **相关回归零失败：未达成** —— `g5_format` 第 3 次 13/1（真实产品过冲）、
      `g5_r43_closeout` 回归首轮 11/1（用例时序，修后 3×12/12）。
      其余 19 项 + `ui_smoke` 3×103 全绿；均未放宽判据、未藏成 SKIP。
- [x] 无死代码：按秒结算的两个统计字段随回滚一并移除；模拟入口均为验收方要求的可重跑分支
- [x] 改动未超出文件域（7 张卡互斥），`vcpkg.json` / `CONTRACT.md` / 合并规则未改
- [x] 接口无签名变更（新增 CLI 参数与内部方法），无需契约变更单
- [x] 文档已同步：`docs/07`（轮 43 记录 + 状态迁移备注）、`docs/09`（生成物 + 挂账理由）、
      `docs/14`（矩阵）、`docs/17`（缺陷单）、`docs/18`（M1-27 终审材料）、7 张卡 `## 交付状态`
- [x] `## 覆盖子句` 只列实际覆盖的子句
- [x] 已知限制已写明（含降级/代理验证标注），见 `EVIDENCE.md §9` / `HANDOFF.md`
- [x] 未自评为已验收；轮 43 主表行留给验收方
- [x] 全量回归结果已回填 §3；两处未绿如实标注并给出修复/裁定路径
