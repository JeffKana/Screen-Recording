# SELFTEST: V1-003（轮 42 主卡）

## 0. 开工四项自查

| 检查项 | 结果 | 依据 |
| --- | --- | --- |
| 需求编号可检索 | 通过 | M8-05、M6-04、M6-08、M6-10、M6-11、M3-10 在 `docs/01` 有原文，逐条核过验收要点 |
| 必读章节可定位 | 通过 | 轮42指令 §1~§5、验收报告-轮41 §2/§3/§5、`docs/05 §2.1/§7.2`、`docs/06 §4.2/§8.4`、`docs/11b §6`、`CONTRACT §2` |
| 上游交付物可获取 | 通过 | 轮 41 四套交付包 + 三套构建产物 + `dist/*.zip`；未用旧版本开工 |
| 文件域无交集 | 通过 | 6 张卡目录互斥，见 `EVIDENCE.md §0`；`domain_table_check` MISMATCH_COUNT=0 |

## 1. 自测命令与输出

| 命令 | 结果 | 证据 |
| --- | --- | --- |
| `pwsh -File verify/cases/g5_longwindow_selftest.ps1` | **10 PASS / 0 FAIL** | `artifacts/selftest-g5_longwindow_selftest.ps1.txt` |
| `pwsh -File verify/cases/g5_estimate_selftest.ps1` | **9 PASS / 0 FAIL** | `artifacts/selftest-g5_estimate_selftest.ps1.txt` |
| `pwsh -File artifacts/run-five-runs.ps1` | **5×14/14 全绿** | `artifacts/R2-five-runs.txt` |
| `python verify/tools/coverage_report.py --write` + `--check`（回退） | `--check` PASS 第 1 次 | `docs/07` 状态迁移留痕 |
| `python verify/tools/coverage_report.py --write` + `--check`（翻绿） | `--check` PASS 第 2 次 | 同上 |
| `pwsh -File verify/cases/g5_r42_closeout.ps1` | **12 PASS / 0 FAIL / 0 SKIP** | `artifacts/g5_r42_closeout.ps1.txt` |
| `pwsh -File verify/cases/g5_r42_hvci.ps1` | **7 PASS / 0 FAIL / 0 SKIP** | `artifacts/g5_r42_hvci.ps1.txt` |
| `pwsh -File verify/cases/g5_r42_matrix.ps1` | **6 PASS / 0 FAIL / 0 SKIP** | `artifacts/r42-matrix.txt` |
| `pwsh -File verify/cases/g5_f2_selftest.ps1` | **4 PASS / 0 FAIL** | `artifacts/f2-selftest.txt` |
| `pwsh -File artifacts/run-f3-rename.ps1` | 两个 `11-` → `11a/11b`，引用同步 | `artifacts/f3-rename.txt` |
| `pwsh -File artifacts/run-f2-archive.ps1` | 三份申请 → 裁定记录 | `artifacts/f2-archive.txt` |
| `pwsh -File artifacts/run-build.ps1` | 三构建 `exit=0 warnings=0 errors=0` | `artifacts/build-summary.txt` |
| `pwsh -File verify/tools/build_release.ps1` | **17 PASS / 0 FAIL** | `artifacts/r42-release.txt` |
| `python verify/tools/contract_check.py` | **8 PASS / 0 FAIL** | 终端输出 |
| `python verify/tools/card_consistency_check.py` | **42 卡 0 冲突**（含 F2 新规则） | 终端输出 |
| `python verify/tools/domain_table_check.py` | `MISMATCH_COUNT=0` | 终端输出 |
| `python verify/tools/coverage_report.py --check` | `coverage table is up to date`（104/1/11） | 终端输出 |

## 2. 红-绿对照

| 新断言 | 旧实现实测 | 新实现实测 |
| --- | --- | --- |
| 配置码率主判据 | 10s 实测 954,577 B/s → 23.3% FAIL（假失败） | 774,000 B/s → 0.00% PASS |
| 60s 长窗口护栏 | 断言不存在；只留主判据时套件永远绿 | 注入 29.2% 超标 → FAIL；真实 0.56% → PASS |
| 两断言独立性 | — | `guard-fails-while-main-passes`：主判据 PASS 但护栏超标 → 退出码 1 |
| F2 待裁定报警 | 工具无此规则，待裁定项可先翻绿 | 假卡触发退出码 1 + `[F2_PENDING_REF]`；真实卡 0 冲突 |
| `--force-cpu` 路径切换 | 无稳定触发入口 | `converter=cpu` vs 对照 `gpu-videoprocessor` |
| 背压可见标识 | 状态栏无标识 | 注入 7 → 「⚠ 背压（丢帧 7）」；不注入 → 无标识 |
| 档位名显示 | 首轮 UTF8 手写 INI → 乱码「鏋佽嚧」、下拉序号 4 | 产品 API 落盘 → 「极致」正确、序号 0 |
| HVCI 包内 UI 可执行 | 首轮按 stdout 判定 → FAIL（GUI 应用不进管道） | 退出码 0 + 日志 `test_hooks=0` → PASS |
| F2 自检在 runner 捕获下 | 全量回归首轮 2 PASS / 2 FAIL（中文输出按 ANSI 解码成乱码） | 改 ASCII 标记 + 退出码判定 → 4/4 |

> 三处首轮 FAIL 都是**用例取数缺陷**（INI 编码、GUI stdout、Python UTF-8 输出解码），
> 修复后判据未放宽；另加 `preset-name-not-garbled` 断言防止回归。三构建首次编译因
> `RefreshPresetValues(*context)` 对引用再次解引用报 C2100，改为传引用后三构建零告警。

## 3. 回归

| 用例 | 判据期望 | 本轮实测 |
| --- | --- | --- |
| `g5_closeout` | 18 PASS | **18 PASS / 0 FAIL / 0 SKIP** |
| `g5_window` | 24 PASS / 0 SKIP | **24 / 0 / 0** |
| `g5_uac` | 17 PASS / 1 SKIP | **17 / 0 / 1 SKIP** |
| `g5_format` | 14 PASS | **13 PASS / 1 FAIL**（不稳定，见下） |
| `ui_smoke` ×3 | 3×103 PASS | **3×103 / 0 / 0** |
| `g4_gate` / `g3_recovery` / `g3_playback` / `g3_storage` | 全绿 | 12 / 9 / 11 / 11，全绿 |
| `g5_encoder_list` / `g5_tempfile` | 17 / 10 PASS | 17 / 10 |
| `g5_estimate_selftest` / `g5_longwindow_selftest` / `g5_f2_selftest` | 9 / 10 / 4 | 9 / 10 / 4，全绿 |
| `g5_r42_closeout` / `g5_r42_hvci` | 12 / 7 | 12 / 7 |
| 契约 / 卡面 / 域表 / `--check` | 通过 | 8/8、42 卡 0 冲突、0 不匹配、up to date |

> 回归原始输出在 `artifacts/reg-*.txt` 与 `artifacts/reg-ui-smoke-{1,2,3}.txt`；
> 汇总 `artifacts/r42-regression.txt`（21:37:48 – 21:48:41）。

### `g5_format` 不稳定的处理（不放宽判据）

- 回归里 `13/1` 的 FAIL 是 `m3-04-vbr-peak-any-content`（运动源峰值 10,204 vs 上限 9,000kbps，
  判据 ≤102%）。另一次单独复跑出现 `m3-03-60fps-rate`（encode_fps=41）。两次是**不同断言**，
  指向本机负载抖动；与本轮改动文件域无关（`--force-cpu` 默认关闭、`src/app/` 的 UI 改动）。
- 独立连跑 3 次（`artifacts/format-x3.txt`）：**3×14 PASS / 0 FAIL**；首轮全量回归（21:22）也 14/14。
- **判据 §3 #5「回归零失败」未稳定达成 → 报 PARTIAL**，交验收方独立重跑裁定；
  执行方未改 `g5_format` 用例、未放宽 ≤102% 判据、未用全绿冒充。详见 `EVIDENCE.md §9`、`HANDOFF.md` 第四问。

## 4. DoD 勾选

- [x] 开工四项自查全部通过并已记录
- [x] 任务卡每条需求 → 一条证据，含命令与输出（`EVIDENCE.md §1~§6`）
- [x] 新增断言都先红后绿，旧值已记录（§2）
- [x] Release + Debug + MinGW 三构建零告警
- [ ] **相关回归零失败：未稳定达成**（`g5_format` 偶发 13/1 FAIL，独立 3 连跑 14/14；判 PARTIAL，见 §3）。
      其余 17 个用例 + `ui_smoke` 3×103 全绿。
- [x] `g5_format` 如实标注不稳定，未改该用例、未放宽 ≤102% 判据
- [x] 无死代码、无调试残留：`--force-cpu` / `--set-preset` / `--simulate-backpressure` 是
      验收方要求的**可重跑测试入口**（分别在 `SR_TEST_HOOKS` 块 / 调试 help 段），不是残留；
      发布包 `--help` 实测无调试开关
- [x] 改动未超出文件域（6 张卡目录互斥），`vcpkg.json` / `CONTRACT.md` 未改
- [x] 接口无签名变更（`Nv12Converter::ForceCpu()` 是新增可选方法；`AppConfig` 新增函数与字段；
      `SaveQualityPreset` 新增函数），无需契约变更单
- [x] 文档已同步：`docs/01`（措辞 + 变更记录）、`docs/07`（两行状态迁移备注 + 轮 42 记录）、
      `docs/09`（生成物）、`docs/14`（矩阵实测）、`docs/INDEX.md`（F3 索引）、
      6 张任务卡 `## 交付状态` 与实际一致
- [x] `## 覆盖子句` 只列实际覆盖的子句
- [x] 已知限制已写明（含降级/代理验证标注），见 `EVIDENCE.md §8` / `HANDOFF.md` 第三、四问
- [x] 未自评为已验收；轮 42 主表行留给验收方
- [x] 全量回归结果已回填 §3：`artifacts/r42-regression.txt` 为最终依据，`g5_format` 的不稳定已如实标注
