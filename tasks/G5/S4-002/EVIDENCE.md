# EVIDENCE: S4-002（M3-05 编码器列表：实际可用 + 不可用原因）

> 环境：本机 / Windows 11 24H2 / MSVC 17.14 Release / 虚拟显示
> 探测环境：本机有 MF H.264 MFT（软编 + 一个硬编 MFT），**没有** NVIDIA / AMD 显卡驱动，
> 有 Intel oneVPL 运行时（`libmfxhw64.dll` 存在）→ 三条厂商项全部不可用，原因各不相同。

---

## M3-05#1 —— 界面显示实际可用列表

**命令**：

```powershell
pwsh -NoProfile -File verify/cases/g5_encoder_list.ps1 -BuildDir build-msvc
```

**输出**：

```text
--- 1 sr-recorder --list-encoders ---
  PASS m3-05-list-exit — --list-encoders exit=0
  PASS m3-05-list-count — encoder_count=5 逐项行数=5（期望 ≥1 且相等）
  PASS m3-05-has-mf-h264-sw — 列表 id：mf-h264-sw,mf-h264-hw,nvenc-h264,qsv-h264,amf-h264
  PASS m3-05-unavailable-reason — 不可用项=3；原因样例：未检测到 nvEncodeAPI64.dll/nvEncodeAPI.dll（NVIDIA 驱动/SDK 未安装）
  PASS m3-05-vendor-never-available — 厂商项被标成可用的个数=0（期望 0：本产品未集成厂商 SDK）
  PASS m3-05-canonical-line — 规范单行长度=304
```

**界面侧**（设置页「可用编码器」只读列表）：

```text
--- 4 UI 设置页的编码器列表 ---
  PASS m3-05-ui-settings-window — 设置窗口 hwnd=4336190
  PASS m3-05-ui-listbox-exists — 列表控件 hwnd=8197734
  PASS m3-05-ui-listbox-count — UI 列表条目数=5，recorder 列表项数=5（期望相等）
  PASS m3-05-ui-log-same-line — UI 日志行与 recorder 规范单行逐字一致：True
  PASS m3-05-ui-log-rows — UI 日志 encoder_rows=5，recorder 列表项数=5

RESULT: 17 PASS / 0 FAIL
```

**recorder 的原始输出**（`sr-recorder --list-encoders`）：

```text
encoder_count=5
encoders=[id=mf-h264-sw|state=available;id=mf-h264-hw|state=available;id=nvenc-h264|state=unavailable|reason=未检测到 nvEncodeAPI64.dll/nvEncodeAPI.dll（NVIDIA 驱动/SDK 未安装）;id=qsv-h264|state=unavailable|reason=检测到 libmfxhw64.dll，但本产品未集成厂商 SDK（不引三方依赖）;id=amf-h264|state=unavailable|reason=未检测到 amf.dll（AMD 驱动/AMF SDK 未安装）]
encoder=id=mf-h264-sw	name=MF 软件 H.264 编码器	hardware=0	available=1	reason=
encoder=id=mf-h264-hw	name=MF 硬件 H.264 编码器	hardware=1	available=1	reason=
encoder=id=nvenc-h264	name=NVIDIA NVENC H.264	hardware=1	available=0	reason=未检测到 nvEncodeAPI64.dll/nvEncodeAPI.dll（NVIDIA 驱动/SDK 未安装）
encoder=id=qsv-h264	name=Intel oneVPL / QSV H.264	hardware=1	available=0	reason=检测到 libmfxhw64.dll，但本产品未集成厂商 SDK（不引三方依赖）
encoder=id=amf-h264	name=AMD AMF H.264	hardware=1	available=0	reason=未检测到 amf.dll（AMD 驱动/AMF SDK 未安装）
```

| 断言 | 落点（产品代码） | 说明 |
| --- | --- | --- |
| `m3-05-list-exit` / `-count` | `src/recorder/main.cpp`（`RunListEncoders`） | 命令退出码 0；`encoder_count` 与逐项行数一致 |
| `m3-05-has-mf-h264-sw` | `src/encode/EncoderList.cpp`（`EnumerateFirstMft`） | 列表含 `mf-h264-sw`（MF 软编，**系统自带，一定在**） |
| `m3-05-ui-listbox-count` / `-ui-log-same-line` | `src/app/main.cpp`（设置页只读列表 + `RunRecorderEncoderList`） | UI 列表条目数 == recorder 列表项数；UI 日志的 `encoders=[...]` 与 recorder 的规范单行**逐字一致** |
| `m3-05-vendor-never-available` | `src/encode/EncoderList.cpp`（`AppendVendor`） | 厂商项一律不可用（本产品不引三方依赖、没集成厂商 SDK），**不会**出现「列表说能用、产品打不开」 |

## M3-05#2 —— 不可用项不出现或标注不可用原因

**输出**：

```text
  PASS m3-05-unavailable-reason — 不可用项=3；原因样例：未检测到 nvEncodeAPI64.dll/nvEncodeAPI.dll（NVIDIA 驱动/SDK 未安装）
```

| 断言 | 说明 |
| --- | --- |
| `m3-05-unavailable-reason` | 每条不可用项的 `reason` 非空，**且**提到具体探测对象（`.dll` / SDK / 驱动 / MFT）——不是写死一句话 |

**原因是真实探测的结果**（三类来源都能在输出里看到差别）：

| 项 | 探测方式 | 本机结果 |
| --- | --- | --- |
| `nvenc-h264` | `nvEncodeAPI64.dll` / `nvEncodeAPI.dll` 是否存在（当前目录 + System32 + PATH 每一段） | 都不存在 → 「未检测到 nvEncodeAPI64.dll/nvEncodeAPI.dll（NVIDIA 驱动/SDK 未安装）」 |
| `qsv-h264` | `libmfxhw64.dll` / `mfx_dispatch.dll` / `libmfx.dll` | **存在** `libmfxhw64.dll` → 「检测到 libmfxhw64.dll，但本产品未集成厂商 SDK（不引三方依赖）」 |
| `amf-h264` | `amf.dll` | 不存在 → 「未检测到 amf.dll（AMD 驱动/AMF SDK 未安装）」 |
| `mf-h264-sw` / `mf-h264-hw` | `MFTEnumEx` 的枚举结果 + `ActivateObject` 是否成功 | 成功 → 可用（原因空） |

## 附加验收点（轮 37 指令 §2）

**② 强行指定不可用编码器 → 启动失败并给出原因（复用既有 exit 6 路径）**：

```text
  PASS m3-05-force-unavailable-exit6 — --encoder nvenc-h264 的退出码=6（期望 6：复用既有「encoder open failed」路径）
  PASS m3-05-force-unavailable-reason — 日志含编码器 id 与 unavailable 原因：[record] encoder open failed: encoder 'nvenc-h264' unavailable: 未检测到 nvEncodeAPI64.dll/nvEncodeAPI.dll（NVIDIA 驱动/SDK 未安装）
  PASS m3-05-force-unavailable-no-product — 失败后是否留下成品：False（期望 False）
```

**显式指定时不做「硬编 → 软编」回退**（`src/encode/MfVideoEncoder.cpp`：
`Open()` 里 `requested_id_` 非空时只创建这一个）——否则「点名了硬编却悄悄用软编」等于假绿。

**指定可用编码器 → 录制成功且日志一致**：

```text
  PASS m3-05-force-available-exit0 — --encoder mf-h264-sw 退出码=0
  PASS m3-05-force-available-log — 日志 encoder= 行：[record] encoder=mf-h264-sw rate_control=cbr quality=70 max_kbps=4000
  PASS m3-05-force-available-product — 成片存在：True
```

**③ 日志 `encoders=[...]` 与 UI 列表逐项一致**：

- 规范单行由 **recorder 产出**（`FormatEncoderList`），UI **原样复制**到自己的日志
  （`src/app/main.cpp`：`Log(L"%ls", canonical.c_str())`）——两边各写一遍格式就可能漂移，
  现在是构造上的一致；
- 用例断言：UI 日志行 == recorder 规范单行（逐字）、UI 列表条目数 == recorder 列表项数 ✓。

## 已知限制（诚实标注）

1. **`available` 的口径 = 「MFT 能被激活」**，不是「一定能用于这次录制」——
   能否真正用于录制还取决于设备与协商（协商失败时产品按 M3-06 回退到软编）。
2. **MF 的友好名取不到**（`MFT_FRIENDLY_NAME` 在本机返回空），显示名退回固定文案
   「MF 软件/硬件 H.264 编码器」；`id` 是稳定标识，判定与选择都用 `id` ✓。
3. **厂商项永远不可用**：本产品不引三方依赖、没集成 NVENC/oneVPL/AMF 的 SDK，
   因此厂商项只是「告诉你为什么没有硬编」，不是可选项。
4. **没有 x264**：本产品不引三方依赖，列表里没有 x264（需求原文是「如 …」，非穷举）。
5. **探测不进录制启动路径**：只在设置页首次打开与 `--list-encoders` 时做（轮 37 指令 §4.3）。
