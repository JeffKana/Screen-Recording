# HANDOFF · V1-026（轮 54 · `R1-52` 原子占位 + 0.1.1 + 长稳收口）

## 1. 我做了什么

| 类 | 内容 |
| --- | --- |
| `R1-52` 修复 | `.part` 从「先查存在再 `fopen`」改成 **`CreateFileW(CREATE_NEW)` 原子占位**：选名与占用一步完成，撞车顺延 `.1.part` → `_001` → …；句柄经 `AdoptOpen` **直接交给写入器**（没有第二次 `fopen`，没把 TOCTOU 请回来）。`.part` 残留避让（D-32）与 `_001` 命名规则一字未改 |
| 确定性竞态注入 | `--wait-for-part <file>`（仅 `SR_TEST_HOOKS`）：两个 recorder 等到同一个屏障文件出现才去选名，把「同一毫秒」变成 20/20 必然发生的事件；另加 `output reserved=<part>` 日志，撞车与否可核 |
| 用例 | `g3_output` **+2 条**（20 轮 ×2 进程）：绿端 **17/0**（撞车 20/20、40 产物 0 损坏）；同一份用例跑 0.1.0 的 exe → **15/2**（7 个产物损坏） |
| 0.1.1 | `channel` 扩四态（`0.1.x` → `mvp-patch`）；包 **377,450B / `ADADE5A3…`**，`build_sha=627732f` == tag；`v0.1.1` 非 prerelease |
| LT-01 判定 | 第 3 次 **10 PASS / 0 FAIL**：28,799.971s、丢帧 0、内存 +7.9MB、句柄 +1.64%、全解 0 error、16 点**最差 26ms@25,200s** → **判据 ③ 3/3** |
| P1 | `docs/20` 补「串行依赖必须 rebase、不叠分支」；`docs/26` 归档 `R1-52`（严重 → 闭环） |

## 2. 我是怎么验证的

- 一条判据一条证据：`EVIDENCE.md §1`（机制图 + 红/绿原始输出 + D-32 未破坏）、`§2`（17/17、SHA、VERSION 四态）、`§3`（六项判据逐条 + 最差点）、`§4`（release 属性与资产 digest）。
- 红端不是"推断"：**同一份用例**、`-RecorderPath` 指向 0.1.0 的 exe，跑出 `15 PASS / 2 FAIL`、7 个产物 `mkv_probe`/`ffmpeg` 均报错。
- 门槛：`SELFTEST.md §3`（`g4` 之外的 5 组用例 + 打包自检）、`§4`（三构建零告警）。

## 3. 哪些需求我**没**做完（逐条，不得计为通过）

| 项 | 状态 | 说明 |
| --- | --- | --- |
| **`R1-52` 的 UI 路径并发** | **未做** | 验收方要求「UI 路径能复现 → 升 Blocker 并撤回 0.1.0」。本轮只做了代码层收敛；UI 由单实例启动 recorder、输出名由 UI 生成，无法从 UI 构造两个并发 recorder。**「UI 不可达」是推断，不是实测** —— 若验收方要求实测，需要构造一个「两个 sr-ui 实例同时开录」的路径，属新用例，本轮未做 |
| **0.1.1 的 8h 长稳** | **未做** | 三次 8h 都在 0.1.0 线上跑完；0.1.1 只改输出路径占位（不碰采集/编码/封装主循环），重跑 8h 不会给出新信息。若验收方要求「补丁版也要 8h」，请明确，我按新卡执行 |
| **0.1.1 的 install/uninstall 整机冒烟** | **未做** | 安装脚本、卸载脚本、包内布局与 0.1.0 完全一致（本轮只换了 `bin/*.exe` 与 `VERSION.txt`），沿用轮 53 的冒烟结论；未再动系统安装目录 |
| **`F3-42`（recorder 静默退出）** | **仍未定性** | 本轮未复现（20 轮 ×2 进程共 40 次启动，无一次静默退出）；级别维持「未定性观察」（`docs/26 §3.1`） |

## 4. 三件需要验收方/ O0 定的事

### 4.1 「顺延」的语义

卡里写「另一个 `exit 13` 或自动顺延到 `_001`（二选一，行为要稳定）」。本轮选的是**顺延**：

- 两进程都 `exit 0`，产物分别是 `X` 与 `X_001`（实测 20/20 轮如此）；
- 理由：拒绝会让用户丢一次录制，而顺延是既有 `M5-15` 的行为延伸；且 `CommitOutput` 本来就会在收尾时换名。

如果验收方要的是「拒绝」（`exit 13`），改法是「`CREATE_NEW` 失败即返回 13」——但会与 `M5-15`「同名不覆盖、自动 `_001`」冲突，**建议保持顺延**。

### 4.2 `.part` 的共享模式

占位句柄用 `GENERIC_READ|GENERIC_WRITE` + **`FILE_SHARE_READ`**：录制中其它进程只能读（`--list` 实时快照、`ffprobe` 读 `.part`），
不能开第二份写句柄。`ui_smoke` 3×103 覆盖了实时快照路径；如果将来有工具需要**写**开正在录的 `.part`，会拿到 `ERROR_SHARING_VIOLATION`。

### 4.3 `build_sha` 与 tag 的关系（这次对齐了）

0.1.0 那次 `tag=53e6c61` 而 `build_sha=5609589`（差一个 docs 提交）。本次刻意先提交代码、再出包、再打 tag：
**`v0.1.1 → 627732f`，包内 `build_sha=627732f`**，可直接用 tag 复现构建。

## 5. 验收方请重跑这些

```powershell
# 1) 修复本体（约 70 秒；20 轮 ×2 进程，含屏障注入）
pwsh -File verify/cases/g3_output.ps1                              # 期望 17 PASS / 0 FAIL，撞车轮数 20
# 2) 红端（拿 0.1.0 的 exe 跑同一份用例）
pwsh -File verify/cases/g3_output.ps1 -RecorderPath <解压的 0.1.0>\bin\sr-recorder.exe   # 期望 15 PASS / 2 FAIL
# 3) 0.1.1 包与 release
pwsh -File verify/tools/build_release.ps1 -Version 0.1.1           # 期望 17 PASS / 0 FAIL
Get-FileHash dist\ScreenRecorder-0.1.1-win-x64.zip -Algorithm SHA256 # ADADE5A3…
gh release view v0.1.1 --json tagName,isPrerelease,assets
# 4) 长稳结论（不用重跑，直接核 summary.json）
Get-Content verify/artifacts/lt01-real-20261121-133707/summary.json -Raw
```
