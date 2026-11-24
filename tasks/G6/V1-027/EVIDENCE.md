# EVIDENCE · V1-027（轮 55 · 0.2-1 拆分 + 主题 + 控件基座 + 工作台骨架）

> 判据来源：`.codebuddy/plans/轮55指令.md`（P0-1~P0-4 + 通过判据 1~7）。
> 所有颜色/尺寸证据都是**截图取像素**；日志只用来定位采样坐标，不单独作为视觉证据。
> 原始输出在同目录 `artifacts/`（不提交仓库，按 `docs/20` SOP 只留路径）。

---

## 0 · 先红后绿（新断言必须先红）

新用例 `verify/cases/ui_theme.ps1` 在**旧实现**（`dist/ScreenRecorder-0.1.1-win-x64.zip` 内的
`sr-ui.exe`，即轮 54 之前的系统控件 UI）上跑：

```powershell
python -c "import zipfile,os,shutil;t=os.path.join(os.environ['TEMP'],'sr-old-ui');shutil.rmtree(t,ignore_errors=True);zipfile.ZipFile('dist/ScreenRecorder-0.1.1-win-x64.zip').extractall(t)"
pwsh -File verify/cases/ui_theme.ps1 -BuildDir "$env:TEMP\sr-old-ui\ScreenRecorder-0.1.1-win-x64" -Config bin
```

```
PASS ui-window-created — hwnd=...
FAIL layout-logged — 启动后输出 ui_layout 锚点
FAIL probe-logged — 输出 ui_probe 采样坐标
RESULT: 1 PASS / 2 FAIL / 0 SKIP
```

点级像素对照（同一份旧 exe，`artifacts/old-ui-0.1.1-baseline.png`，客户区 504×191）：

| 采样点 | 旧实现实测 | 新实现实测 | 判据 |
| --- | --- | --- | --- |
| 窗口底（4,10） | `#FFFFFF` | `#0F1115` | docs/29 §4 底色 |
| 按钮区（30,80） | `#F0F0F0`（系统灰） | 主按钮 `#3B82F7` | 主按钮 `#3B82F6` |
| Header 段高 | 0（无 Header） | 48 px | 48±1 |
| 状态栏段高 | 0（无状态栏） | 31~32 px | 32±1 |
| 描边 | 无（系统控件） | `#2A313B` ×3 处 | `#2A313B` |
| `--theme light` | 参数不存在 | `#F7F8FA` | `#F7F8FA` |
| `--dpi-scale 1.25` | 参数不存在（`test_hooks=0`） | header 60 / status 39 | ×1.25±2 |

**红 → 绿成立**：同一份用例在旧 exe 上 FAIL，在本轮构建上 17/0。

---

## 1 · P0-1 拆分：`src/app/ui/` 四模块落地并被主窗口真实使用

| 需求 | 证据（可重跑） | 结果 |
| --- | --- | --- |
| `ui/theme.{h,cpp}`：颜色/字号/间距 token + 明暗主题 + `Scale(v)` | `src/app/ui/theme.h`、`src/app/ui/theme.cpp`；运行日志 `ui_theme_init theme=dark dpi=96 scale=1.00 header_h=48 status_h=32 panel_w=320`；像素断言 `base-color`/`card-color`/`text-color` 全部取自 token | 落地且被使用 |
| `ui/draw.{h,cpp}`：圆角矩形/描边/文本/状态药丸/图标，GDI+，抗锯齿 | `draw.cpp` 用 `Gdiplus::Graphics` + `SmoothingModeAntiAlias`；`ui_theme` 的 `radius-card` 断言（左上角 `#0F1115`、内缩 12px `#161A20`）证明圆角生效；`DrawBuffered` 内存 DC 双缓冲 | 落地且被使用 |
| `ui/layout.{h,cpp}`：纵向栈 + 网格 + 最小尺寸约束 | `layout.cpp` 的 `VStack`/`HStack`/`Grid`/`ClampRange`/`Overlaps`/`Within` 被 `workbench.cpp`、`controls.cpp` 调用；resize 判据见 §2 | 落地且被使用 |
| `ui/controls.{h,cpp}`：Button(primary/secondary/ghost)/Toggle/Card/StatusPill/Field，自绘 + hover/press/focus | `controls.cpp` 的 `kControlClass` 自绘子窗口；`primary-hover` 断言 `#3B82F7 → #5B9CF9`（增量和 60）；`focus-ring-visible` 断言 Tab 后按钮内侧近白像素 364 个 | 落地且被使用 |

行数判据（`python -c` 计数，见 §7）：

```
src/app/main.cpp                  3091   （要求 ≤3500；本轮前 4032）
src/app/ui/controls.cpp            525   （要求 ≤600）
src/app/ui/workbench.cpp           475
src/app/ui/main_window.cpp         457
src/app/ui/list_window.cpp         451
src/app/ui/draw.cpp                322
其余 ui/ 文件均 < 200
```

拆出的文件清单：`ids.h`（常量）、`app_state.h`（AppState 与内部 API 声明，字段一字未改）、
`theme/draw/layout/controls`（基座）、`workbench`（工作台外壳）、`main_window`（MainProc/UpdateUi）、
`list_window`（成片列表窗口，纯平移——它是 0.2 工作台的一部分，见 §6）。

---

## 2 · P0-2 主窗口换成工作台骨架

| 判据 | 证据 | 结果 |
| --- | --- | --- |
| 980×680，最小 860×560 | `wWinMain` 用 `Scale(980)×Scale(680)` 创建；`WM_GETMINMAXINFO` 返回 `Scale(860)×Scale(560)`；默认启动客户区实测 `964×641` | 符合 |
| Header 48px / 内容区 / 状态栏 32px | `ui_theme` 的 `header-height-96dpi`（像素边界 48）、`statusbar-height`（31，容差 ±1） | 符合 |
| 左列表占位（空态文案） | `artifacts/dark.png`：左侧卡片「最近录制 / 还没有录制文件 / 空态：录制完成后成片会列在这里…」 | 符合 |
| 右侧设置面板占位，默认展开 320，可折叠 | `ui_layout ... panel_w=320 panel_open=1`；`panel-open-visible` 断言字段值框底色；点「设置面板」→ 日志 `ui_panel open=0`；`--theme light` 截图可见同一结构 | 符合 |
| 全部新控件，无系统灰底按钮 | 旧实现按钮区 `#F0F0F0`；新实现 `no-system-gray` 断言三处描边均为 `#2A313B`，主按钮 `#3B82F7`；`artifacts/dark.png` 无任何系统灰控件 | 符合 |
| 默认深色 / `--theme light` 可切 | `base-color` `#0F1115`；`theme-light-base` `#F7F8FA`（卡片 `#FFFFFF`） | 符合 |
| resize 无白闪、无重叠 | 主窗口类 `hbrBackground = nullptr` + `WS_EX_COMPOSITED` + `WM_ERASEBKGND` 返回 1 + 内存 DC 双缓冲；`artifacts/min-window.png`（860×560）可见 Header/操作行/列表卡片/状态栏互不压盖；`ui_layout` 每次 resize 重算并重新布局子控件 | 符合（白闪为免闪烁构造，见 HANDOFF 限制说明） |

Tab 焦点截图：`artifacts/focus.png`（焦点矩形画在控件客户区内侧 2px，主按钮为白色环）。

---

## 3 · P0-3 行为等价（本轮最关键）

区域覆盖层、区域编辑器、悬浮条、设置对话框**代码一行未改**（`main.cpp` 中这三块的函数体未纳入本次 diff；
`git diff` 的删除行全部属于「主窗口静态控件 + UpdateUi + 列表窗口」）。

| 用例 | 命令 | 结果 |
| --- | --- | --- |
| `ui_smoke` | `pwsh -File verify/cases/ui_smoke.ps1 -BuildDir build-msvc -Config Release` | **103 PASS / 0 FAIL / 0 SKIP**（与轮 54 同数，未减未跳） |
| `g4_gate` | 同上 `-File verify/cases/g4_gate.ps1` | **12 PASS / 0 FAIL** |
| `g4_control` | 同上 `-File verify/cases/g4_control.ps1` | **32 PASS / 0 FAIL** |
| `g5_mp4` | 同上 `-File verify/cases/g5_mp4.ps1` | **23 PASS / 0 FAIL** |
| `g3_playback` | 同上 `-File verify/cases/g3_playback.ps1` | **11 PASS / 0 FAIL** |

附加（不在卡内门槛，但被本次改动直接触及，故补跑）：

| 用例 | 为什么补跑 | 结果 |
| --- | --- | --- |
| `g5_uac` | 它按 ID 读主窗口 `210`（提示标签）/`209`（提权按钮）的可见性、文本与 `BM_CLICK`；自绘控件必须兼容这些既有判断 | **17 PASS / 0 FAIL / 1 SKIP**（SKIP 为本机 UAC 策略导致的黑屏用例，与轮 54 同因） |
| `g5_fileops` | 成片列表窗口被平移到 `ui/list_window.cpp`，它的 `--show-list/--list-op` 路径必须仍可用 | **9 PASS / 0 FAIL** |

调用路径不变的实现要点：

- 控件仍是**真实子窗口**，ID 与窗口文本不变（`GetDlgItem(main,200/201/203/209/210)` 全部照旧）；
  状态栏文本载体保留为不可见 `STATIC`（id=200），`Get-StatusText` 读到的是同一条文本；
- 自绘按钮实现 `BM_CLICK`，`g5_uac` 用 `BM_CLICK` 触发「开始录制」仍然有效；
- 所有命令仍走 `WM_COMMAND`（`kStartButton`…`kTrayElevate`）到原来那套 `switch`，没有新增旁路。

---

## 4 · P0-4 新用例 `verify/cases/ui_theme.ps1`（17 条，含卡的 10 条）

| # | 断言 | 实测 |
| --- | --- | --- |
| 1 | `base-color` | `#0F1115` @(622,577) |
| 2 | `card-color` | `#161A20` @(32,577) |
| 3 | `primary-fill` | `#3B82F7`（≈`#3B82F6`，GDI+ 取整偏 1） |
| 4 | `primary-hover` | `#3B82F7 → #5B9CF9`，亮度增量和 60（≥3 级） |
| 5 | `text-color` | 标题笔画最亮像素 `#E6EAF0` |
| 6 | `header-height-96dpi` | 48 px（像素边界） |
| 7 | `statusbar-height` | 31 px（容差 ±1） |
| 8 | `dpi-scale-125` | header 60 / status 39（`--dpi-scale 1.25`） |
| 9 | `panel-auto-collapse` | 860×560 → `panel_open=0 panel_w=0`，面板位置变回卡片色 |
| 10 | `theme-light-base` | `#F7F8FA`（卡片 `#FFFFFF`） |
| 11 | `no-system-gray` | 三处描边均 `#2A313B`，无系统默认灰 |
| 12 | `radius-card` | 角像素 `#0F1115`、内缩 12px `#161A20` |
| 13 | `panel-open-visible` | 面板字段值框 `#0F1115`（与收起态区分） |
| 14 | `focus-ring-visible` | Tab 到主按钮后内侧近白像素 364 个 |
| 15-17 | `ui-window-created` / `layout-logged` / `probe-logged` | 窗口与锚点日志 |

命令：`pwsh -File verify/cases/ui_theme.ps1` → **RESULT: 17 PASS / 0 FAIL / 0 SKIP**
（原始输出 `artifacts/ui_theme.out`，截图 `artifacts/{dark,hover,min-window,dpi125,light,focus}.png`）

---

## 5 · 通过判据 5~7

| 判据 | 命令 | 结果 |
| --- | --- | --- |
| `main.cpp` ≤3500、`ui/` 单文件 ≤600 | `python -c "..."`（§7） | 3091 / 最大 525 |
| 包体 ≤500KB | `pwsh -File verify/tools/build_release.ps1` | **17 PASS / 0 FAIL**，`ScreenRecorder-0.1.0-win-x64.zip` = **400,494 B** |
| 启动 ≤300ms | 3 次 `spawn→FindWindow` 计时 | avg **62 ms** / max **99 ms** |
| 零新增依赖 | `git diff --stat vcpkg.json`（无改动）；`gdiplus` 为 Windows 系统组件 | 无新增 |
| 三构建零告警 | `cmake --build build-msvc --config Release/Debug`、`cmake --build build-mingw` | 均 0 warning / 0 error |
| 契约 8/8 | `python verify/tools/contract_check.py` | 8 PASS / 0 FAIL |
| 覆盖表 | `python verify/tools/coverage_report.py --check` | up to date |
| 卡片一致性 / 文件域 | `card_consistency_check.py` / `domain_table_check.py` | 68 张卡 0 冲突 / MISMATCH_COUNT=0 |

---

## 6 · 需要验收方知道的「搬运」决定

为满足 `main.cpp ≤3500` 的硬指标，除主窗口本体（`MainProc`/`UpdateUi`/提权提示）外，
把**成片列表窗口**（`ListProc` 等 451 行）整体平移到 `src/app/ui/list_window.cpp`：

- 逐行未改（只加了 `#include` 与匿名命名空间包裹），`RefreshRecordingList` / `UpdateRecordingListLive` /
  `ShowRecordingList` / `ListProc` 的函数体与原文件一致；
- 依据：0.2 的成片列表本来就要内联进工作台（`docs/29 §3`、§5 目标结构里没有独立的列表窗口文件），
  56 轮直接在 `workbench` 里复用它；
- 行为证据：`g5_fileops` 9/0（`--show-list` + `--list-op rename/delete` 全路径）、`ui_smoke` 103/0。

---

## 7 · 行数口径

```powershell
python -c "import io,glob;[print('%-32s %5d'%(p,len(io.open(p,encoding='utf-8').read().split(chr(10))))) for p in ['src/app/main.cpp']+sorted(glob.glob('src/app/ui/*.cpp'))+sorted(glob.glob('src/app/ui/*.h'))]"
```
