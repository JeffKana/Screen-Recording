# HANDOFF: V1-003（轮 42 主卡）

## 一、我改了什么接口？

**无接口签名变更**，无需契约变更单，`CONTRACT.md` / `vcpkg.json` 未改。

| 改动 | 性质 |
| --- | --- |
| `verify/cases/g5_storage.ps1` | **验收方裁定 A 明确授权的用例口径修正**：主判据改配置码率（≤20% 判据一字未动），新增独立 60s 长窗口护栏断言；三档取数语义与 `g5_estimate_selftest` 9/9 不变 |
| `verify/lib/LongWindow.ps1`（新增） | 护栏的纯计算函数：文件系统字节数 / 墙钟 → 平均写入速率；数据缺失/短窗口判 FAIL |
| `verify/cases/g5_longwindow_selftest.ps1`（新增） | 10 条自检，含「主判据 PASS 但护栏真超标 → 套件 FAIL」 |
| `verify/tools/card_consistency_check.py` | **F2 规则**：正文引用待裁定 `docs/1x-*裁定申请` + 子句已实现 → 报警；新增 `--tasks-dir` 供假卡自检 |
| `verify/tools/coverage_report.py` | 只在 `render()` 加两行轮 42 说明文字，**合并规则一字未动** |
| `docs/01` | M6-04 验收要点措辞（x264 → 内置软编，不引入 GPL 组件）+ `## 变更记录` |
| `docs/09` | 生成物；回退/翻绿各 `--write` 一次；挂账理由列只手工填 M8-05#1 回退那一行 |
| `docs/07` | 两行状态迁移备注 + 轮 42 记录；主表仍留白给验收方 |
| `docs/14` | 矩阵首格实测值 + 日期，其余 11 格仍未测 |
| `docs/INDEX.md`（新增） | F3 编号登记与引用约定 |
| `verify/cases/g5_r42_closeout.ps1` / `g5_r42_hvci.ps1` / `g5_r42_matrix.ps1` / `g5_f2_selftest.ps1`（新增） | 收口与 P1 专属断言 |
| `src/recorder/main.cpp`、`RecordingSession.h/.cpp` | `--force-cpu`（SR_TEST_HOOKS）令既有 CPU 中转路径可达；`record_options.force_cpu` 透传；见 S1-010 |
| `src/compositor/Nv12Converter.h` | 新增 `ForceCpu()` 内联方法（S2-003） |
| `src/app/AppConfig.h/.cpp`、`main.cpp`、`RecorderController.cpp` | 画质档位联动/保存/界面可见、状态栏背压标识、`preset_dispatched` 日志；见 S5-005 |
| `docs/11a`、`docs/11b`、三份「裁定记录」 | F2/F3 文档归档（改名，内容未删） |
| 6 张任务卡 + 四件套 | `tasks/G6/V1-003`、`V1-005`、`V1-006`、`G5/S1-010`、`S2-003`、`S5-005` |
| `dist/*.zip` + `release-summary.json` | 由 `build_release.ps1` 重建（源码改动后未用 `-SkipBuild`） |

## 二、下游需要接什么？

1. **验收方独立重跑**（执行方不自评）：
   ```
   pwsh -File verify/cases/g5_estimate_selftest.ps1          # 9/9
   pwsh -File verify/cases/g5_longwindow_selftest.ps1        # 10/10
   pwsh -File tasks/G6/V1-003/artifacts/run-five-runs.ps1    # 5×14/14（约 10 分钟）
   pwsh -File verify/cases/g5_r42_closeout.ps1               # 12/12
   pwsh -File verify/cases/g5_r42_hvci.ps1                   # 7/7
   pwsh -File verify/cases/g5_r42_matrix.ps1                 # 6/6（60s 录制）
   pwsh -File verify/cases/g5_f2_selftest.ps1                # 4/4
   python verify/tools/coverage_report.py --check
   python verify/tools/contract_check.py
   python verify/tools/card_consistency_check.py
   python verify/tools/domain_table_check.py
   ```
2. **两份裁定文书已归档为「裁定记录」**（`docs/15`、`docs/16`、`docs/12`）：F2 规则只对
   **待裁定**的 `docs/1x-*裁定申请` 报警；若下游新建待裁定申请并引用在已实现卡里，会触发卡面冲突——
   这正是 F2 的意图（先挂「有卡未实现」，裁定后再翻）。
3. **待裁定项流程**：轮 41 F2 的教训已落到工具里，本轮 M6-04#2 是**先有验收方裁定、再落 01 措辞**，
   没有再出现「先翻绿再追认」。
4. **矩阵 1 格已实测**，轮 43 需补齐其余 11 格 + 双屏 100% 那格从「降级旁证」升级为实测。

## 三、已知限制是什么？

1. **M8-05 主判据本身几乎等于产品公式**（验收方已指出）：真正兜底的是 60s 长窗口护栏。
   两条必须同时存在；`g5_longwindow_selftest` 的 `guard-fails-while-main-passes` 断言专门守这一点。
   护栏让单次 `g5_storage` 约 2 分钟，5 连绿约 10 分钟。
2. **降级/代理验证**：
   - M6-08#2：`--force-cpu` 强制开关触发既有 CPU 中转路径，非真实混合显卡跨 GPU 故障。
   - M6-11#2：`--simulate-backpressure` 注入丢帧走同一 UI 渲染分支，产品无队列深度字段。
   - M6-10#2：真实 HVCI 环境（`Enabled=1`）实测，**不是模拟**。
   - 矩阵首格：物理双屏用 `--display 0` 只采主屏代表「单屏」，非真实单屏机。
   - M3-10#1：联动 fps / 码率 / 画质三项，**分辨率不随档位缩放**（需求描述提到分辨率，
     验收要点原文只要求「各参数联动变化」）。
3. **首轮两处 FAIL 已修**（用例取数缺陷，非产品缺陷、非判据放宽）：
   M3-10#2 的 INI 中文编码（产品 API 落盘）、HVCI 用例对 GUI 应用 stdout 的误判。
4. **`g5_format` 不稳定**：回归里出现 1 次 13/1 FAIL（VBR 峰值 10,204 vs 上限 9,000kbps），
   独立 3 连跑全 14/14。真实但偶发，与本轮改动文件域无关；未改用例、未放宽判据，报 PARTIAL。
5. **V1.0 仍不可发布**：11 条无卡、M1-27 黑屏例外待终审、M6-06（150% DPI）BLOCKED、
   矩阵 11 格未测、`docs/05 §7.2` 其余判据未跑完。

## 四、哪些需求我**没**做完？（最重要）

| 项 | 状态 | 原因 / 下一步 |
| --- | --- | --- |
| **P0 M8-05 新口径** | 已交付 | 5 连绿 14/14 ×5；两条断言各自独立输出；`g5_estimate_selftest` 9/9；`g5_longwindow_selftest` 10/10 |
| **P0 M8-05#1 回退再翻绿** | 已交付 | 回退 98/2/16 → 翻绿 99/1/16，两次 `--check` 均 PASS，各一行台账备注 |
| **P0 M6-04#2 措辞落 01** | 已交付 | 01 含「不引入 GPL」+ 变更记录；03 未改；M6-04#2 仍已实现 |
| **P0 无卡收口 5 条** | 已交付 | 覆盖 **104/1/11**（≥103、≤11）；`g5_r42_closeout` 12/12、`g5_r42_hvci` 7/7 |
| **P1 矩阵 1 格实测** | 已交付 | `g5_r42_matrix` 6/6 + 日期 2026-11-09；**其余 11 格未测**，属计划内（指令 §4.4 只要求先把表跑通） |
| **P1 F2 / F3** | 已交付 | 假卡触发报警、42 卡 0 冲突；docs 编号唯一 + `INDEX.md` |
| `M7-13#1 / #2`（托盘红点、屏幕边框、暂停区分） | **没做，仍无卡** | 需自绘图标 + 分层边框窗口 + 像素级验证；不拿日志文案冒充功能验收，留轮 43 |
| `M1-24#1 / #2`（光标开关） | **没做，仍无卡** | 引擎有 `include_cursor`，但 CLI/UI 入口与「成片任意位置无指针」的像素判据未落地，留轮 43 |
| `M3-09#1/#2/#3`、`M3-10` 的分辨率联动、`M6-01#1`、`M6-04#1`、`M6-05#1`、`M6-08#1` | **没做，仍无卡** | 分别需要新容器/多音轨、缩放输出、4核8线程参考机型、三厂商真机、三个 Windows 版本、混合显卡；均不因实现困难放宽判据 |
| 矩阵其余 11 格 / `M6-06` | **没做** | 环境/真机不具备，标未测或 BLOCKED |
| **全量回归「零失败」** | **未稳定达成，报 PARTIAL** | `g5_format` 在回归里 13/1 FAIL（`m3-04-vbr-peak-any-content`，运动源峰值 10,204 vs 上限 9,000）；另一次复跑出现 `m3-03-60fps-rate`（encode_fps=41）。两次不同断言 → 本机负载抖动；独立连跑 3 次全 14/14、首轮回归也 14/14。**未改该用例、未放宽 ≤102% 判据、不用全绿冒充**，交验收方裁定。其余 17 个用例 + `ui_smoke` 3×103 全绿 |
| **V1.0 发布** | **仍不可发布** | 见第三问；另加 `g5_format` 不稳定 |

## 五、交付包清单

```
tasks/G6/V1-003/  TASK.md EVIDENCE.md SELFTEST.md HANDOFF.md + artifacts/（33+ 个原始输出）
tasks/G5/V1-005/  TASK.md + 四件套（M8-05#1 状态迁移）
tasks/G6/V1-006/  TASK.md + 四件套（M6-10#2 HVCI）
tasks/G5/S1-010/  TASK.md + 四件套（--force-cpu）
tasks/G5/S2-003/  TASK.md + 四件套（Nv12Converter::ForceCpu）
tasks/G5/S5-005/  TASK.md + 四件套（画质档位 / 背压标识）
```

**执行方不自评为已验收**；所有结论待验收方独立重跑判定。
