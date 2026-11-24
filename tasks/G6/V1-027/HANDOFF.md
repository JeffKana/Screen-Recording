# HANDOFF · V1-027（轮 55 · 0.2-1 拆分 + 主题 + 控件基座 + 工作台骨架）

## 1. 我做了什么

| 类 | 内容 |
| --- | --- |
| 拆分（P0-1） | 新增 `src/app/ui/`：`theme`（颜色/字号/间距/圆角 token + 明暗主题 + `Scale()`）、`draw`（GDI+ 圆角矩形/描边/文本/图标 + 内存 DC 双缓冲）、`layout`（VStack/HStack/Grid/最小尺寸约束）、`controls`（Button primary/secondary/ghost、Toggle、Field、Label、Pill，自绘 + hover/press/focus）、`workbench`（工作台外壳）、`main_window`（`MainProc`/`UpdateUi`/提权提示）、`ids.h`（常量表）、`app_state.h`（`AppState` 与内部 API 声明，字段一字未改） |
| 工作台骨架（P0-2） | 980×680 / 最小 860×560；Header 48 + 内容区（左「最近录制」卡片空态 + 右设置面板 320 可折叠）+ 状态栏 32；默认深色；`--theme light` 可切；主窗口类**不注册背景刷子** + `WS_EX_COMPOSITED` + `WM_ERASEBKGND`→1，从构造上排除白闪 |
| 行为等价（P0-3） | 控件仍是**真实子窗口**且 ID/窗口文本不变（`GetDlgItem(main,200/201/203/209/210)`、`BM_CLICK`、状态栏文本容器全部照旧）；命令仍走原 `WM_COMMAND` 分派；覆盖层/编辑器/悬浮条/设置对话框**一行未改** |
| 新用例（P0-4） | `verify/cases/ui_theme.ps1`：17 条，全部以**截图取像素**判定（颜色 ±4/255、段高 ±1/+2px） |
| DPI | `WM_DPICHANGED`（取新 dpi → 重设窗口 → 重排布局 → 重绘）+ `WM_GETMINMAXINFO` 最小尺寸按 scale 换算；`--dpi-scale` 注入 125% 供用例重跑 |
| 行数 | `main.cpp` 4032 → **3091**；`ui/` 最大单文件 525（`controls.cpp`） |

## 2. 我是怎么验证的

- 一条判据一条证据：`EVIDENCE.md §1`（四模块与被使用点）、`§2`（骨架尺寸/视觉判据）、`§3`（行为等价 + 6 组回归）、`§4`（17 条像素断言逐条实测值）、`§5`（体积/启动/契约）。
- 红端不是推断：**同一份 `ui_theme.ps1`** 跑 0.1.1 包内旧 `sr-ui.exe` → `1 PASS / 2 FAIL`（无 `ui_layout/ui_probe` 锚点）；
  点级旧值 `#FFFFFF` 底 / `#F0F0F0` 系统灰按钮 / 无 Header，全部记在 `EVIDENCE.md §0`。
- 视觉证据是截图：`artifacts/{dark,hover,min-window,dpi125,light,focus}.png`（每张都是客户区原尺寸截屏）。

## 3. 哪些需求我**没**做完（逐条，不得计为通过）

| 项 | 状态 | 说明 |
| --- | --- | --- |
| **DPI 150% 真机** | **未做（BLOCKED）** | 本机只有 100% 屏。125% 用 `--dpi-scale 1.25` 注入，像素实测 header 60 / status 39；150% 需真机，按 `docs/29 §7` 标 BLOCKED，**不得标 PASS** |
| **「resize 无白闪」逐帧录像** | **未做** | 只有构造性证据（无背景刷子 + `WS_EX_COMPOSITED` + 内存 DC 双缓冲）与「截图无灰底/无未绘制区块」。若验收方要求录屏逐帧核验，需要新增用例 |
| **动效（120~180ms ease-out）** | **未做** | 卡 P0-2 没有要求本轮做动效（`docs/29 §8` 把动效放在 58 轮）；本轮只有 hover/press/focus 的即时态，无过渡动画 |
| **设置项切换主题** | **未做（选了另一条）** | 卡允许「`--theme light` **或** 设置项」二选一，本轮实现的是 `--theme`；设置对话框里没有主题下拉 |
| **UIA 无障碍** | **未做** | 卡明确本轮不要求完整 UIA；已做的是「键盘 Tab 可达 + 焦点矩形可见」（`focus-ring-visible` 断言） |
| **工作台内容（控制条/列表/状态栏数据/设置即时保存）** | **未做（属于 56 轮）** | 本轮只做骨架与占位：列表是空态文案，设置面板是只读字段，不写盘 |
| **托盘图标状态机 / 悬浮条重绘** | **未做（属于 57 轮）** | 托盘与悬浮条本轮保持原样（含 `MakeRedDotIcon`/`UpdateToolbar` 未改） |

## 4. 需要下游（56 轮）注意的接口

### 4.1 工作台是「外壳 + 自绘子控件」，不是一块画布

- `ui::Workbench`（`workbench.h`）：`Create/Layout/Paint/SetModel/ToggleSettings/HandleKeyDown`；
  子控件（按钮/字段/药丸）是 `kControlClass` 真窗口，ID 在 `ui/ids.h`；
- 视图数据用 `WorkbenchModel`（纯 POD：状态文本、药丸文案、字段值…），**不持有任何录制对象**；
  56 轮加控制条/列表时，往 `WorkbenchModel` 加字段 + 在 `LayoutXxx()` 里排位置即可，不需要碰 `MainProc`。

### 4.2 状态栏文本有两条通路，改一条必须改两条

- 可见部分：`Workbench::PaintShell` 用 `model.status_text` 画（`\n` 分两行）；
- 自动化/无障碍部分：不可见 `STATIC`（id=200）承载同一字符串，`ui_smoke`/`g5_closeout`/`g5_storage` 靠它取状态。
  两处都由 `Workbench::SetModel` 统一写入，**不要只更新一处**。

### 4.3 面板折叠有两条规则

- 手动：`kPanelToggleButton`（211）→ `Workbench::ToggleSettings()`；
- 自动：客户区宽 < `Scale(900)` 时强制收起（`AutoCollapsed()`），860×560 窗口即触发。
  56 轮若把最小窗口改小，要同步这条阈值，否则「最小窗口下右侧面板自动收起」会失效。

### 4.4 成片列表被平移了

`ListProc`/`RefreshRecordingList`/`ShowRecordingList` 现在在 `src/app/ui/list_window.cpp`（逐行未改）。
56 轮把它内联进工作台时，直接复用 `FillListRow`/`RunRecorderList`，不要另写一套取数逻辑。

## 5. 验收方请重跑这些

```powershell
# 1) 视觉判据（约 20 秒，含 DPI 125% 与浅色主题两次 spawn）
pwsh -File verify/cases/ui_theme.ps1                     # 期望 17 PASS / 0 FAIL / 0 SKIP
# 2) 红端（用 0.1.1 包内旧 exe 跑同一份用例）
python -c "import zipfile,os,shutil;t=os.path.join(os.environ['TEMP'],'sr-old-ui');shutil.rmtree(t,ignore_errors=True);zipfile.ZipFile('dist/ScreenRecorder-0.1.1-win-x64.zip').extractall(t)"
pwsh -File verify/cases/ui_theme.ps1 -BuildDir "$env:TEMP\sr-old-ui\ScreenRecorder-0.1.1-win-x64" -Config bin   # 期望 1 PASS / 2 FAIL
# 3) 行为等价（4 组）
pwsh -File verify/cases/ui_smoke.ps1    -BuildDir build-msvc -Config Release   # 期望 103/0/0
pwsh -File verify/cases/g4_gate.ps1     -BuildDir build-msvc -Config Release   # 期望 12/0
pwsh -File verify/cases/g4_control.ps1  -BuildDir build-msvc -Config Release   # 期望 32/0
pwsh -File verify/cases/g5_mp4.ps1      -BuildDir build-msvc -Config Release   # 期望 23/0
pwsh -File verify/cases/g3_playback.ps1 -BuildDir build-msvc -Config Release   # 期望 11/0
# 4) 体积与启动
pwsh -File verify/tools/build_release.ps1                # 期望 17/0，包 ≈400KB（≤500KB）
# 5) 行数
python -c "import io,glob;[print('%-32s %5d'%(p,len(io.open(p,encoding='utf-8').read().split(chr(10))))) for p in ['src/app/main.cpp']+sorted(glob.glob('src/app/ui/*.cpp'))]"
```
