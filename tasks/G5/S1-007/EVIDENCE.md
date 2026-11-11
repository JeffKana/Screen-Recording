# EVIDENCE: S1-007（轮 33 → 轮 35）

> 口径声明：本卡覆盖 M3-04#2 / M3-03 / M5-07 与维护项 R1/R2/R3/R5 + 轮 35 的 F2。
> **8h 单文件不在本卡**（见 `tasks/G5/S6-002`），本卡不出现「8 小时稳定」的结论。
> 数据来源：2026-10-27/28 / `build-msvc/bin/Release` / 本机（Windows，1920×1080，MF 软编 `mf-h264-sw`）。
> 复现：所有命令均可直接重跑；产物目录见各节。

## M3-04#2 VBR（平均码率 + 峰值上限）

- 验证方式：`--rate-mode vbr --bitrate 6000 --max-bitrate 9000 --quality 80` 录 20s，
  全屏 testsrc2 运动源；逐秒统计视频包码率，跳过首秒。
- 命令：
  ```
  pwsh -NoProfile -File verify/cases/g5_format.ps1
  ```
- 输出：`artifacts/g5_format_summary.json`（轮 35 按裁定改后的 14 条）
  - ① 运动源：`rate_control=vbr`、max=9000、quality=80；逐秒 **4,903 ~ 6,632 kbps**，
    avg **5,959**，偏差 **0.7%**（判据 ≤20%）
  - ② 峰值：运动源 **6,632** / 静态 **1,561**，均 **< 9,000**（判据 ≤102%）
  - ③ 静态桌面：逐秒 1,510~1,561kbps，avg **1,556**，偏差 −74.1%——**只记录，不判定**
  - `cbr_filler_bytes=0`（两种内容都是 VBR，不补齐）
  - 随后复跑 CBR 12s：`rate_control=cbr`、`spread=0.0%`、`filler>0`；
    `cbr / vbr 运动 / vbr 静态 / cbr_after_vbr` 四份文件并存且大小不同
- 结论：**已实现**（轮 35 裁定落地：`docs/01` M3-04 + `docs/05` §2.4 三条，用例断言一一对应，
  本地 **14/14**）。仍待验收方独立复跑确认；G5 关闭不以本卡自测为准。

**裁定依据**：`docs/12-轮34-VBR测法裁定记录.md` §7（验收方选 A，用内容类型定义测法）。
静态桌面的 −74% 是 VBR 正常行为，按 ③ 只记录；不再作为 PARTIAL 理由，也不允许给 VBR 加 filler。

## M3-03#1 60fps / M3-03#2 15fps（产品入口）

- 验证方式：三段链路——
  1. `sr-ui --set-fps <n>` 写 `[video] fps`（与设置面板同一个 `SaveVideoSettings`）；
  2. 重启 UI，日志 `app_start ... fps=<n> rate_mode=<m>` 回读；
  3. UI 经热键录 4s，日志 `recorder_cmd fps=<n>` + 成片 `ffprobe avg_frame_rate`。
- 命令：
  ```
  pwsh -NoProfile -File verify/cases/g5_fps.ps1
  ```
- 输出：`artifacts/g5_fps_summary.json`
  - 六档 15 / 24 / 25 / 30 / 60 / 自定义 45：写 INI 成功、重启回读一致（6/6）
  - 60fps 端到端：`recorder_cmd fps=60`，成片 `avg_frame_rate=60.00`
  - 15fps 端到端：`recorder_cmd fps=15`，成片 `avg_frame_rate=15.00`
  - 引擎级：`encode_fps=60.0 / 15.1`，帧数 900 / 225（15fps 上限 227）
- 结论：通过（#1/#2 达标）

## M5-07#1 重命名后磁盘文件名同步变更

- 命令：`pwsh -NoProfile -File verify/cases/g5_fileops.ps1`
- 输出：`artifacts/g5_fileops_summary.json`；日志关键字 `file_op op=rename ok=1`
- 实测：`alpha.mkv` → `beta.mkv`，磁盘 `alpha` 不存在、`beta` 存在；
  同一日志的后续 `list_entry` 只出现 `name=beta.mkv`（列表已刷新）。
- 结论：通过

## M5-07#2 删除进回收站而非直接抹除

- 实测：`file_op op=delete ok=1 count=1 confirm=1 recycle=1`；
  文件从目录消失；回收站 `$I` 记录解析出
  `C:\Users\98468\AppData\Local\Temp\sr-fileops\gamma-093629.mkv`。
- 实现：`SHFileOperationW` + `FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI`，
  未使用 `std::filesystem::remove`。
- 结论：通过

## M5-07#3 批量操作有二次确认

- 实测：两份文件，`--list-confirm no` → `file_op op=delete ok=0 count=2 confirm=0`，
  两份都还在；`--list-confirm yes` → `ok=1 count=2 confirm=1`，两份都进回收站。
- 产品路径：列表右键「删除到回收站」→ `MessageBox(MB_YESNO)`，取消走同一 `confirm=0` 分支。
- 附加：`live.mkv.part` 删除被拒（`file_op op=delete ok=0` + 日志含「临时文件」），文件保留。
- 结论：通过

## R1 覆盖模型支持子句级交付

- 命令：
  ```
  python verify/tools/coverage_report.py --self-test
  python verify/tools/coverage_report.py --write
  python verify/tools/coverage_report.py --check
  ```
- 输出：`artifacts/coverage_selftest.txt`（`self_test ok: 三态 + 子句级优先判定正确`）
- 实测：
  - `- M3-04#1: 已实现` 与需求级 `- M3-04: 有卡未实现` 并存时，#1 判「已实现」、#2 保持「有卡未实现」；
  - 反向用例：子句级写「有卡未实现」时，需求级「已实现」不能把它拉绿；
  - `docs/09` 重生成后 **85 已实现 / 2 有卡未实现 / 29 无卡 / 缺理由 0**，`--check` up to date；
  - 契约 C1~C8 **8/8**（`artifacts/contract.txt`）。
- 结论：通过

## R2 诊断包导出后源日志 intact

- 命令：`pwsh -NoProfile -File verify/cases/g5_diag_real.ps1`
- 输出：`artifacts/g5_diag_real.txt`
- 实测：源日志 334,673 → 334,673 字节（只允许追加），前 4KB SHA-256 不变；
  导出 `log.txt` 56,153 字节、跨度 600s、末条 `t=273177890` 与源一致。
- 结论：通过（新增 2 条断言，D-54 回归护栏）

## R3 画质上限 70/85 对照

- 命令：`pwsh -NoProfile -File verify/cases/g5_quality.ps1`
- 输出：`artifacts/g5_quality_summary.json`
- 实测（同一张 1920×1080 静态源图，全屏显示，各录 8s，抽第 3s 帧对源图算）：
  - q70：PSNR **33.78dB** / SSIM **0.9935**（回读 70）
  - q85：PSNR **33.78dB** / SSIM **0.9935**（回读 85）
  - filler：5,099,162 / 5,100,603 字节（差 0.03%）
- 结论：**已测，但无可测差异**。该 MFT 在 CBR 下接受并回读画质档位，却不改变成片；
  D-53 的稳定化应归因于 `mean=max + buffer + filler`，不再声称「画质上限 70」是画质保证。

## R5 列表「大小」字段

- 命令：`pwsh -NoProfile -File verify/cases/g5_list.ps1`
- 输出：`artifacts/g5_list.txt`（**27 PASS / 0 FAIL**）
- 实测：a.mkv `1,549,197 = 1,549,197`；b.mkv `2,323,539 = 2,323,539`；
  c.mkv `773,692 = 773,692`（列表 size 与磁盘字节完全一致）。
- 结论：通过

## F2 失焦热键判据（轮 35 维护项）

- 旧判据：`foreground=(?!Screen Recorder)`，桌面前台标题为空时假通过。
- 新判据：`foreground_hwnd != 应用主窗口 HWND`，标题只进日志。
- 红：合成 `hotkey id=1 foreground= foreground_hwnd=<app>` 时旧断言 True（假通过）、
  新断言 False（正确拒绝）。`artifacts/f2-red-green.txt`。
- 绿：真实运行 `foreground_hwnd=2033852 app_hwnd=10882356` → PASS（ui_smoke 103/0）。

## 未覆盖 / 不计为通过

1. **8h 单文件**：不在本卡；第 3 轮 02:47 正常收尾，5/6 干净 PASS（时长/内存/句柄/丢帧/漂移），
   第 6 项因 `-f null` 的 6,667 条 null-muxer DTS 警告待裁定（DEFECT-02）。
   M6-02#1 / M6-09#1 保持「有卡未实现」，详见 `tasks/G5/S6-002/DEFECT-01/02.md`。
2. **UI 画质档位入口**：只做了 `--quality` 与 INI 之外的录制参数；设置面板没有画质选项
   （M3-04 只要求 CBR/VBR 模式，模式入口已做）。R3 已证明该档位无可测效果，故不补 UI。
3. **打开所在目录 / 复制路径**：已实现并从右键菜单可达，但无自动化断言（非本轮验收项）。
