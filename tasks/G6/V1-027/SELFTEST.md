# SELFTEST · V1-027（轮 55）

## §0 开工四项自查（`docs/06 §4.2`）

| 项 | 结论 | 依据 |
| --- | --- | --- |
| 需求编号可检索 | **通过（口径说明）** | 本轮是 0.2 线的 UI 重构首卡（`docs/29`），不新增/翻绿功能子句：`docs/01` 的 116 条功能子句本轮**一条不动**（覆盖表保持 111/1/4）。判据来自任务卡 P0-1~P0-4 + `docs/29 §4/§7`。|
| 必读章节可定位 | **通过** | `docs/29`（§1 技术路线、§3 交互、§4 视觉 token、§5 代码结构、§7 验收门槛、§8 轮次规划）、`docs/28`（0.1 冻结 / 0.2 解冻）、`docs/06 §2/§5.4`（角色与文件域）、`docs/20`（SOP：分支/PR/CI/台账） |
| 上游交付物可获取 | **通过** | `main @ 5d5f468`（#24 已合并，含 `docs/29` + 轮 55 卡）；`round/55` 从该提交拉出；上一版 UI 二进制来自 `dist/ScreenRecorder-0.1.1-win-x64.zip`（先红对照用） |
| 文件域无交集 | **通过** | 本轮只碰 `src/app/`（含新增 `src/app/ui/`）+ `verify/cases/ui_theme.ps1` + `docs/07` + `tasks/G6/V1-027/`；无并行卡 |

## §1 越界自查

| 改了 | 性质 |
| --- | --- |
| `src/app/main.cpp` | **只删不增行为**：删掉主窗口静态控件创建、`UpdateUi`、`ApplyElevationUi`、`ShowMainWindow`、`MainProc`、成片列表窗口（共 -1004 行）；新增 `--theme` / `--dpi-scale` 解析、窗口尺寸与样式、`WM_PAINT/WM_SIZE/WM_DPICHANGED/WM_GETMINMAXINFO/WM_ERASEBKGND` 转发（+74 行） |
| `src/app/CMakeLists.txt` | 加入 7 个 ui 源文件；`target_link_libraries` 增加系统库 `gdiplus` |
| `src/app/ui/*`（新增 14 个文件） | theme / draw / layout / controls / workbench / main_window / list_window / ids.h / app_state.h |
| `verify/cases/ui_theme.ps1` | **新增用例**（卡 P0-4 授权）。`verify/` 下既有用例**一字未改** |
| `docs/07-执行台账.md` | 只加轮 55 一行，结论栏留白（按卡要求） |

**未改**：`vcpkg.json`、`CONTRACT.md`、`docs/01`、`docs/05`、`docs/27`、`src/{capture,encode,mux,recorder,ipc,core,diag,resilience}/`、
其它 `verify/cases/*`、区域覆盖层/区域编辑器/悬浮条/设置对话框的任何函数体。

## §2 静态检查

```powershell
python verify/tools/contract_check.py          # 汇总: 8 项检查, 8 PASS, 0 FAIL
python verify/tools/coverage_report.py --check # coverage table is up to date
python verify/tools/card_consistency_check.py  # card_consistency: OK —— 扫描 68 张卡，0 处冲突
python verify/tools/domain_table_check.py      # MISMATCH_COUNT=0
```

## §3 用例与门槛（本轮实跑，Release/`SR_TEST_HOOKS=ON`）

| 用例 | 结果 | 备注 |
| --- | --- | --- |
| `ui_smoke` | **103 PASS / 0 FAIL / 0 SKIP** | 与轮 54 同数，未减未跳 |
| `ui_theme`（新） | **17 PASS / 0 FAIL / 0 SKIP** | 卡的 10 条 + 圆角/面板展开/焦点/锚点 |
| `g4_gate` | **12 PASS / 0 FAIL** | |
| `g4_control` | **32 PASS / 0 FAIL** | |
| `g5_mp4` | **23 PASS / 0 FAIL** | mp4 box 43/0 |
| `g3_playback` | **11 PASS / 0 FAIL** | |
| `g5_uac`（附加） | **17 PASS / 0 FAIL / 1 SKIP** | `uac-target-black` 仍 SKIP（本机 UAC 策略，与轮 54 同因） |
| `g5_fileops`（附加） | **9 PASS / 0 FAIL** | 覆盖被平移的成片列表窗口 |
| `build_release` | **17 PASS / 0 FAIL** | 包 400,494 B |

## §4 构建（零告警）

| 构建 | 结果 |
| --- | --- |
| `build-msvc` Release（`SR_TEST_HOOKS=ON`） | 0 warning / 0 error |
| `build-msvc` Debug（`SR_TEST_HOOKS=ON`） | 0 warning / 0 error |
| `build-mingw`（Ninja，`-Wall -Wextra`） | 0 warning / 0 error |
| `build-release`（`SR_TEST_HOOKS=OFF;SR_BUILD_TOOLS=OFF`） | 0 warning / 0 error |

## §5 性能与体积

| 指标 | 实测 | 门槛 |
| --- | --- | --- |
| spawn → 窗口可见 | avg 62 ms / max 99 ms（3 次） | ≤300 ms |
| 发布包 | 400,494 B | ≤500 KB |
| `main.cpp` | 3,091 行 | ≤3,500 |
| `ui/` 最大单文件 | 525 行（`controls.cpp`） | ≤600 |

## §6 Definition of Done

- [x] 任务卡每条需求都有证据（`EVIDENCE.md §1~§5`）
- [x] **先红后绿**：同一份 `ui_theme.ps1` 在 0.1.1 旧 exe 上 `1 PASS / 2 FAIL`，在本轮构建上 `17 PASS / 0 FAIL`；点级旧值已记录（`#FFFFFF` / `#F0F0F0` / 无 Header）
- [x] 回归零失败（`ui_smoke` / `g4_gate` / `g4_control` / `g5_mp4` / `g3_playback`，另补 `g5_uac` / `g5_fileops`）
- [x] Release + Debug + MinGW + 发布构建零告警
- [x] 无死代码、无调试残留、无注掉的块
- [x] 改动未超出任务卡声明的文件域（`src/app/` + 新用例 + 文档 + 交付包）
- [x] 接口无变更（IPC 契约未动），不需要契约变更单
- [x] 文档已同步（`docs/07` 台账行；`docs/29 §8` 已把 55 轮列为产出，本轮按此交付）

## §7 已知限制（不隐瞒）

1. **DPI 150% 未验**：本机只有 100%；125% 用 `--dpi-scale 1.25` 注入验证（像素实测 header 60 / status 39）。
   150% 需真机，按 `docs/29 §7` 标 **BLOCKED，不标 PASS**。
2. **「resize 无白闪」是构造性证据**：主窗口类不注册背景刷子 + `WS_EX_COMPOSITED` + 内存 DC 双缓冲 +
   `WM_ERASEBKGND` 返回 1；用例不做逐帧录像，只以「截图无灰底、无未绘制区块」间接佐证。
3. **`--dpi-scale` 是测试钩子**（`SR_TEST_HOOKS` 构建才有），发布包不含。
4. **`dist/` 里的 0.1.0 包被 `build_release.ps1` 重写**（脚本默认行为，只影响本地构建产物，不提交仓库；
   已发布 tag 与资产未动）。
