# HANDOFF: G5/S1-005（M3-04 CBR）

## 我改了什么接口？

- **没有改 IPC / 公共接口签名。** `VideoEncoderConfig::rate_control` 是既有字段（`RateControlMode::Cbr`），
  本轮只是第一次真的用它。
- `MfVideoEncoder` 新增只读访问器：`RateControlName()` / `RateControlError()` / `FillerBytes()` / `StreamChanges()`。
- `RecordingSession::Stats` 新增三个统计字段：`rate_control`（cbr / avg_bitrate / vbr）、
  `filler_bytes`、`stream_changes`；recorder stdout 新增同名行，供验收脚本取值。
- `RecordingSession::Options` 新增 `rate_mode`（`cbr` 默认 / `vbr` 旧行为），recorder 新增
  `--rate-mode <cbr|vbr>`。

## 下游需要接什么？

- **S5（UI 设置面板）**：现在没有「码率模式」入口，`[video] bitrate_kbps` 是唯一旋钮。
  M3-04#2（VBR）落地时需要 UI 选项与质量档位，否则 M3-04 只能停在「有卡未实现」。
- **G6 打包**：CBR 默认会让文件明显变大（6Mbps/60min ≈ 2.7GB），发布说明要写清这一点；
  磁盘水位（M8-03/M8-04）阈值不会因此触发，但用户的预期需要提前告知。

## 已知限制是什么？

1. 只有 `mf-h264-sw` 被真实验证。硬编支持但拒绝 ICodecAPI 时，代码会先试软编，软编也拿不到码控
   则重建硬编、退回「只有 `MF_MT_AVG_BITRATE`」，`rate_control` 字段会显示 `avg_bitrate`（可观测）。
2. filler 是**视频轨**补齐；音频轨按 AAC 自身码率，文件总码率 = 标称 + 音频。
3. `BufferSize` 取 0.1s 额度（`bitrate*100`）是实测折中：更小会更平滑，但动态画面的画质下降更明显。
4. 逐秒统计跳过首尾秒（编码器预热/收尾），与 05 的口径一致。

## 哪些需求我**没**做完？

- **M3-04#2（VBR）未实现** —— 没有质量档位参数，也没有 UI 入口；M3-04 整条仍是「有卡未实现」。
- **M3-03 产品入口未做** —— 帧率仍只有 `--fps` 命令行，设置面板没有帧率项（不属本卡范围）。
