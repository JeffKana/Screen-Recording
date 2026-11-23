# SELFTEST · V1-026（轮 54）

## §0 开工四项自查（`docs/06 §4.2`）

| 项 | 结论 | 依据 |
| --- | --- | --- |
| 需求编号可检索 | **通过** | 本轮是缺陷修复（`R1-52`，`docs/26 §3.2`）+ 补丁发布（`docs/24`），不新增/翻绿子句：`M5-15`（同名不覆盖）、`M8-07`（收尾换名）早已完成，本轮修的是两者**没覆盖到的并发窗口**；覆盖表保持 111/1/4 |
| 必读章节可定位 | **通过** | `.codebuddy/plans/轮54指令.md`、`验收报告-轮52-53.md §1/§4`、`docs/26 §3.2`、`docs/05 §7.1`（严重级定义）、`docs/20 §1/§6`、`CONTRACT.md §7/§18.2`（本轮未改协议） |
| 上游交付物可获取 | **通过** | `main @ e5b7f24`（#19/#20/#21 已合并）+ `verify/artifacts/lt01-real-20261121-133707/summary.json`（8h 结果） |
| 文件域无交集 | **通过** | 产品 5 个文件（`OutputPath` / `RecordingSession` / `main` / `MkvWriter` / `Mp4Writer`）+ 1 个用例（`g3_output.ps1`）+ 1 个工具（`build_release.ps1`）+ 文档；未碰 `verify/` 其它用例的判据 |

## §1 越界自查

| 改了 | 性质 |
| --- | --- |
| `src/recorder/OutputPath.{h,cpp}` | `.part` 原子占位（`CREATE_NEW`）+ 顺延 |
| `src/recorder/RecordingSession.{h,cpp}` | 句柄移交、`output reserved=` 日志、`--wait-for-part` 屏障（`SR_TEST_HOOKS`） |
| `src/recorder/main.cpp` | 解析 `--wait-for-part`（`SR_TEST_HOOKS`）+ 调试帮助行 |
| `src/mux/MkvWriter.{h,cpp}`、`src/mux/Mp4Writer.{h,cpp}` | `AdoptOpen`（接管已有句柄） |
| `verify/cases/g3_output.ps1` | **只加** 2 条并发用例 + `-RecorderPath`；A/B/C/D1/D2 的断言一字未改 |
| `verify/tools/build_release.ps1` | `channel` 四态（`0.1.x` → `mvp-patch`） |
| 文档 | `docs/20/22/24/26/07`、`README`、`RELEASE-NOTES-0.1.1.md`（新建） |

**未改**：`docs/01`、`docs/05`、`docs/27`、其它 `verify/cases/*`、`vcpkg.json`、`CONTRACT.md`、默认容器。

## §2 静态检查

```powershell
python verify/tools/contract_check.py          # 汇总: 8 项检查, 8 PASS, 0 FAIL
python verify/tools/coverage_report.py --check # coverage table is up to date（111 / 1 / 4）
python verify/tools/card_consistency_check.py  # OK —— 扫描 67 张卡，0 处冲突
python verify/tools/domain_table_check.py      # MISMATCH_COUNT=0
```

## §3 用例与门槛（本轮实跑）

| 用例 | 结果 | 备注 |
| --- | --- | --- |
| `g3_output`（新构建，屏障） | **17 PASS / 0 FAIL** | 15 + 2；并发 20 轮 ×2 进程、撞车 20/20、40 产物 0 损坏 |
| `g3_output -RecorderPath <0.1.0 exe>` | **15 PASS / 2 FAIL** | 红端：38 产物 7 损坏、2 个 `exit≠0` |
| `g3_output -RecorderPath <0.1.1 发布 exe>` | **17 PASS / 0 FAIL** | 交付二进制自身可用（无屏障，40 产物 0 损坏） |
| `g5_fileops` | **9 PASS / 0 FAIL** | 未改的文件操作路径（删除/回收站/`.part` 保护） |
| `g5_format` | **15 PASS / 0 FAIL / 0 SKIP** | 容器与码率口径未受影响 |
| `g5_mp4` | **23 PASS / 0 FAIL** | faststart 预留回填路径未受影响（box 43/0） |
| `ui_smoke` | **103 PASS / 0 FAIL / 0 SKIP** ×3 | 含 `--list` 实时快照（新的 `FILE_SHARE_READ` 占位仍可读） |
| `build_release -Version 0.1.1` | **17 PASS / 0 FAIL** | 包自检（含解压后重算 SHA） |

## §4 构建（零告警）

| 构建 | 结果 |
| --- | --- |
| `build-msvc`（`SR_TEST_HOOKS=ON`） | 0 warning / 0 error |
| `build-mingw`（`SR_TEST_HOOKS=ON`） | 0 warning / 0 error |
| `build-release`（`SR_TEST_HOOKS=OFF;SR_BUILD_TOOLS=OFF`） | 0 warning / 0 error |

## §5 Definition of Done

- [x] 任务卡每条判据都有证据（`EVIDENCE.md §1~§5`）
- [x] **先红后绿**：同一份用例在 0.1.0 的 exe 上是 `15/2`（7 个产物损坏），在本轮构建上是 `17/0`
- [x] 回归零失败（`g3_output` / `g5_fileops` / `g5_format` / `g5_mp4` / `ui_smoke`）
- [x] 三构建零告警
- [x] 无死代码：`FindFreePath`（`OutputPath.cpp`）仍被 `CommitOutput` 使用；旧 `Open()` 保留（`fopen` + `AdoptOpen`）
- [x] 改动未超出文件域（`§1`）；`CONTRACT.md` 未动（IPC/协议零变更）
- [x] 文档已同步（`docs/20/22/24/26/07` + `README` + `RELEASE-NOTES-0.1.1.md`）
- [x] 发布物未被覆盖（`v0.1.0` 原样；`v0.1.1` 新增，`isPrerelease=false`，`build_sha` == tag 提交）
- [ ] **`R1-52` 的 UI 路径并发未实测**（列为未做，见 `HANDOFF.md §3`）
- [ ] **0.1.1 未重跑 8h 长稳**（0.1.0 线的 3 次结果沿用，见 `HANDOFF.md §3`）
