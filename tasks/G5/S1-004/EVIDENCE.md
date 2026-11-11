# EVIDENCE: 挂账启动 ①（M3-03 / M3-04 / M5-06）

> 轮 31 实测，`verify/cases/g5_format.ps1 -Seconds 10`，产物 `verify/artifacts/format-<ts>/`。

## M3-03 帧率（引擎级）

| 子句 | 命令 | 实测 | 判定 |
| --- | --- | --- | --- |
| #1 60fps | `sr-recorder --record --seconds 10 --fps 60 --bitrate 12000` | `encode_fps=59.1`、`frames_encoded=600`、`wall=10.04` | PASS（引擎） |
| #2 15fps 上限 | `--fps 15 --bitrate 4000` | `encode_fps=15`、`frames_encoded=150`（上限 152） | PASS（引擎） |

产品缺口：设置面板无帧率项、INI 不读 `fps`，用户无法选择 → 整条需求仍「有卡未实现」。

## M3-04 码率

| 子句 | 实测 | 判定 |
| --- | --- | --- |
| #1 CBR 6000kbps 波动 ≤10% | 逐秒 1,864 ~ 5,889 kbps，avg **2,510**，spread **160.4%**（标称偏差 58%） | **FAIL** |
| #2 VBR 偏差 ≤20% | 无 VBR 模式（只有 `MF_MT_AVG_BITRATE`） | 未实现 |

原因：`src/encode/MfVideoEncoder.cpp:155/203` 只设 `MF_MT_AVG_BITRATE`，注释也写明
「精确码控留到接厂商 SDK 时补」。MF 软件编码器把 avg bitrate 当目标，静态桌面下实际
码率远低于标称，逐秒波动极大。

现场：`verify/artifacts/fail-*-m3-04-cbr-spread/`（含 `cbr.mkv` 与逐秒数据来源）。

## M5-06 录制列表

未开工：无列表 UI、无条目索引，`M5-04` 的数据源也尚未落地。本卡只登记范围与依赖，
轮 32 与 `M5-07`（重命名/删除）一起做。

## 覆盖表影响

三条需求共 7 个子句由「无卡」转为「有卡未实现」（card 已建、未交付），
`docs/09` 的挂账理由逐条写明，C8 仍 8/8。
