# EVIDENCE: S4-008（轮 47 产品实现卡 · M3-05 列表一致性）

> 口径声明：本卡只证明「列表与开录结果一致」，**不证明本机有可用硬件编码器**
> （本机硬件 MFT 协商失败，硬编能力仍需真机）；不写验收结论。
> 数据来源：2026-11-13 / `build-msvc/bin/Release`（另含 Debug 与 MinGW 三构建日志）/
> Win11 build 26200 / i5-13500 / UHD 770。
> 用例与红/绿原始输出由主卡 `tasks/G6/V1-020` 统一保管：`artifacts/r47-red.txt`、`r47-green.txt`
> 等；本卡引用其路径与本卡直接命令的输出。`artifacts/` 不入库，命令可重跑。

## P0-1 #1 · M3-05#1「界面显示实际可用列表」——列表与开录一致（修法 A）

- 验证方式：`--list-encoders` 的 MF 项走与录制**同一条** `MfVideoEncoder::Open`
  （创建 MFT → 协商 NV12/H.264 → 码控 → 试提交 NV12 帧并取回 H.264 包）；
  只在该链路全部成功时标 `available=1`。
- 命令：`.\build-msvc\bin\Release\sr-recorder.exe --list-encoders`
- 输出（原始行）：
  `encoder=id=mf-h264-sw hardware=0 available=1 reason=`
  `encoder=id=mf-h264-hw hardware=1 available=0 reason=MFT 真激活失败（Open: encoder 'mf-h264-hw' unavailable: MFT 协商失败（encoder rejected NV12 input type））`
  `encoder_count=5`；单次耗时 **278ms**（UI 设置页 2s 轮询内）。
- 命令：`pwsh -NoProfile -File verify/cases/g5_encoder_consistency.ps1`
- 输出：`RESULT: 12 PASS / 0 FAIL`，其中
  `PASS r47-forward-available-items — 可用项=1；mf-h264-sw: exit=0 encoder=<[record] encoder=mf-h264-sw ...> 成片=True`；
  `PASS r47-no-silent-fallback — 是否出现软编：False`（`tasks/G6/V1-020/artifacts/r47-green.txt`）。
- 旧值（红端）：`RESULT: 4 PASS / 8 FAIL`，`mf-h264-hw` 在列表里 `available=1`、
  强制指定却 `exit=0, encoder=mf-h264-sw`（`r47-red.txt`）。
- 结论：通过（列表的 `available=1` 项，`--encoder <id>` 真的用上它）
- 复现环境：本机 / Win11 build 26200 / MSVC Release

## P0-1 #2 · M3-05#2 / M3-06#2「不可用项带原因 / 强制不可用开录前报错」（修法 B）

- 验证方式：`requested_id_` 非空时禁止「协商失败」「码控失败」两处硬编→软编回退，
  统一走 `RecordingSession` 既有 `exit 6`；自动选择（未点名）保持 M3-06 回退。
- 命令：`pwsh -NoProfile -File verify/cases/g5_encoder_consistency.ps1`
- 输出：`PASS r47-backward-unavailable-items — 不可用项=4；mf-h264-hw/nvenc-h264/qsv-h264/amf-h264: exit=6 原因=True 无成片=True`。
- 直接命令：`.\build-msvc\bin\Release\sr-recorder.exe --record --seconds 2 --fps 30 --bitrate 2000 --out $env:TEMP\sr-r47-green-hw.mkv --encoder mf-h264-hw`
  → `[record] encoder open failed: encoder 'mf-h264-hw' unavailable: MFT 协商失败（encoder rejected NV12 input type）`、
  `record_exit=6`、无成片。
- 自动路径未被误伤：`pwsh -NoProfile -File verify/cases/g5_encoder_list.ps1` → `17 PASS / 0 FAIL`；
  `--encoder mf-h264-sw` → `exit=0`、`encoder=mf-h264-sw`、成片存在；
  `g5_inject.ps1`（`--fail-encoder` 无参）→ `28 PASS / 0 FAIL`；`g5_r43_closeout.ps1` → `12 PASS / 0 FAIL`。
- 结论：通过（`available=0` 的每一项 `exit 6` 且原因非空；`--fail-encoder` 无参语义保持）

## P0-1 #3 · 反向护栏：人为让 `mf-h264-hw` 激活失败

- 命令：`.\build-msvc\bin\Release\sr-recorder.exe --list-encoders --fail-encoder hardware`
- 输出：`encoder=id=mf-h264-hw hardware=1 available=0 reason=MFT 真激活失败（Open: ... injected: hardware encoder unavailable）`；
  `mf-h264-sw available=1`（注入只打硬件，不牵连软编）。
- 命令：`... --record --seconds 2 --out <tmp> --encoder mf-h264-hw --fail-encoder hardware`
  → `exit=6`、原因含 `unavailable`、无成片（`r47-injected-forced-exit6`）。
- 命令：撤掉注入再跑 `--list-encoders` → 原因不再含 `injected`、规范行与首轮逐字一致
  （`r47-probe-retry-not-cached`）——**失败状态没有被缓存，探测失败可重试**。
- 命令：`... --list-encoders --probe-repeat 5` → 5 轮状态 `1/0` 一致；
  `... --probe-repeat 5 --fail-encoder hardware` → 句柄 `204,204,204,204,204`（增长 0）。
- 红端：`--list-encoders --fail-encoder hardware` 在轮 46 二进制上 `exit=1`（旧 CLI 不接受参数），
  四条注入相关断言全部 FAIL。
- 结论：通过

## A / B 改动落点与「不泄漏 device/processor」

- 改法 A：`src/encode/EncoderList.cpp::MakeMf` → `MfVideoEncoder::ProbeAvailable`；
  `MfVideoEncoder::TryEncodeOneFrame` 提交一帧 NV12 并取回 H.264 包（不超过 4 帧 + DRAIN）。
- 改法 B：`src/encode/MfVideoEncoder.cpp::Open` 用 `LookupEncoder` 静态映射取代录制路径上的整表探测，
  并在 `Negotiate` / `ApplyRateControl` 两处加 `forced` 判断；`CreateTransform` 支持定向注入。
- 录制路径的 MF/COM 启停：`mf_started_` / `com_owned_` 归属标记 + `Close()` 在**失败分支也**
  `MFShutdown()` / `CoUninitialize()`；所有 COM 指针用 `win::ComPtr`。
- 命令：`.\build-msvc\bin\Release\sr-recorder.exe --list-encoders --probe-repeat 5 --fail-encoder hardware`
  → 5 轮句柄 204 恒定（跳过硬件 MFT 激活时，本卡自身资源零增长）。
- 代码事实：探测器不创建 `ID3D11Device` / `ID3D11VideoProcessor` / `ID3D11VideoProcessorEnumerator`，
  也不持有采集设备；D-25 的「转换器绑在已释放设备上」在本卡无泄漏面。
  第三方硬件 MFT 自身 `ActivateObject` 的驱动侧句柄（真实激活 5 轮 268→280，+3/次）
  在 `HANDOFF.md §3` 如实记录，产品路径用一次性子进程隔离。
- 命令：`cmake --build build-msvc --config Debug` / `--config Release` / `cmake --build build-mingw`
  → 三个 `exit=0 warnings=0`（`tasks/G6/V1-020/artifacts/build-*.log`）。
- 结论：通过

## 覆盖状态迁移（先回退、后翻绿）

| 步骤 | 命令 | 输出 |
| --- | --- | --- |
| ① 回退 | `python verify/tools/coverage_report.py --write`（本卡声明「有卡未实现」） | `已实现 107, 有卡未实现 2, 无卡 7`；`--check` PASS；契约 8/8 |
| ② 修 + 用例 | 红 `4 PASS / 8 FAIL` → 绿 `12 PASS / 0 FAIL` | `tasks/G6/V1-020/artifacts/r47-red.txt` / `r47-green.txt` |
| ③ 翻绿 | `python verify/tools/coverage_report.py --write` | `已实现 108, 有卡未实现 1, 无卡 7`；`--check` PASS |

- 结论：通过（**没有**为凑 108 直接标已实现）

## 已知限制（不得计为通过）

1. 本机 `mf-h264-hw` 真激活失败 → 列表标不可用、强制 `exit 6`；**没有把硬编做出来**，
   硬编抽测格三件套不可执行，见 `tasks/G6/V1-020/EVIDENCE.md` 与 `docs/14`。
2. 探测参数固定 1280×720@30 / 6000kbps CBR，未逐项探测所有分辨率/帧率/码率组合。
3. 本卡不写验收结论；验收由验收方独立重跑后给。
