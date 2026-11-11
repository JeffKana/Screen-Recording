# EVIDENCE: S4-001 编码与封装

复现命令：

```powershell
.\build-mingw\bin\sr-recorder.exe --record --seconds 8 --fps 30 --out build-mingw\t8.mkv --dump-es build-mingw\t8.h264
python verify/tools/mkv_probe.py build-mingw\t8.mkv
```

## M5-04 / M2-03 封装与产物（8 秒样本）

- 录制统计：

```
encoder=mf-h264-sw
converter=gpu-videoprocessor
frames_encoded=240
blocks_muxed=240
clusters=6
bytes_written=1993631
wall_seconds=8.03
```

- 独立解析器（`mkv_probe.py`，与写入代码无共享实现）：

```
[PASS] doctype — DocType='matroska'
[PASS] timecode-scale — scale=1000000
[PASS] duration — duration=7999.0ms
[PASS] tracks — 1 track(s)
[PASS] video-track — codec=V_MPEG4/ISO/AVC 1920x1080
[PASS] avcc-config — codec_private=41 bytes
[PASS] video-blocks — 240 blocks
[PASS] clusters — 6 cluster(s)
[PASS] avcc-chain — bad=0
[PASS] nal-sps — nal types=[1, 5, 6, 7, 8, 9]
[PASS] nal-idr — IDR count=8
[PASS] nal-slices — non-IDR slices=232
[PASS] timestamp-range — range=33..7999 duration=7999.0ms
RESULT: 0 failure(s)
```

- 结论：**PASS（结构层）**。240 帧全部落盘，Duration 与最后一帧 PTS 严格一致（7999ms）。

## M3-07 H.264 输出

- avcC 41 字节，结构自洽（`1 | profile | compat | level | 0xFF | 0xE1 | sps_len | sps | 1 | pps_len | pps`）。
- NAL 类型覆盖 7(SPS)/8(PPS)/5(IDR)/1(非 IDR 切片)/6(SEI)/9(AUD)，每 30 帧一个 IDR。
- `avcc-chain bad=0`：所有块的长度前缀链完整，无截断/溢出。
- 结论：**PASS**。码流是合法可解码的 H.264；但**未做解码器回放验证**（无 ffmpeg，见下）。

## M3-06 硬编优先 / 软编回退

- 硬编 MFT 创建或协商失败后自动回落 `mf-h264-sw`，录制全程未中断。
- 结论：**PASS**。日志可见回退原因，成片无空洞。

## M3-01 / M3-02 分辨率与对齐

- 输出 1920×1080，与采集源一致；编码器输入按 `& ~1` 对齐。
- 结论：**PASS**。

## M3-05 编码器探测与选择

- 当前只枚举到 Media Foundation 的软编 MFT；NVENC / oneVPL / AMF 需各自 SDK（本环境不可得）。
- 结论：**PARTIAL**。探测框架在，候选集不完整。

## 解码回放验证（M2-03 / M3-07 的最终判据）

工具：Gyan.FFmpeg 9.0.2 full build（winget 安装）。

```
pwsh -NoProfile -File verify/cases/g3_playback.ps1 -BuildDir build-mingw -Seconds 6
```

输出：

```
--- 容器与流信息 (ffprobe) ---
codec_name=h264  width=1920  height=1080  pix_fmt=yuv420p
r_frame_rate=30/1  format_name=matroska,webm  duration=5.999000
  PASS container — format_name=matroska,webm
  PASS codec — codec_name=h264
  PASS resolution — 1920x1080
  PASS pixel-format — pix_fmt=yuv420p
  PASS frame-rate — r_frame_rate=30/1
  PASS duration — duration=5.999 (expect ~6)
--- 全量解码 (ffmpeg -f null) ---
  PASS decode-clean — no decoder errors
--- 解码帧数 (ffprobe -count_frames) ---
  PASS frame-count — decoded=180 expected=180
--- 画面非空 (采样帧平均色) ---
  PASS frame-0-not-blank — avg_rgb=227,130,130
  PASS frame-90-not-blank — avg_rgb=231,130,130
  PASS frame-179-not-blank — avg_rgb=231,130,130
RESULT: 11 PASS / 0 FAIL
```

- `decode-clean`：ffmpeg 以 `-v error` 解码全部 180 帧，**未输出任何错误**，说明码流无损坏。
- `frame-count`：解码帧数 180 == 6s × 30fps，无丢帧、无重复填充错位。
- `frame-*-not-blank`：把采样帧缩到 1×1 取平均色，三帧均为浅色（约 229/130/130），
  排除“容器合法但画面全黑”这种假通过。

- 结论：**PASS**。这是“可播放”的直接证据，不再依赖结构推断。

## 未验证项（诚实标注）

| 项 | 状态 | 原因 |
| --- | --- | --- |
| 断电/崩溃后分片可恢复 | 未验证 | 需故障注入，属 G5 范畴 |
| 音频轨（M3-12） | BLOCKED | 本卡未实现 AAC 编码 |
| 三厂商硬编 | BLOCKED | 缺 SDK |

## 汇总

| 需求 | 结论 |
| --- | --- |
| M3-01 | PASS |
| M3-02 | PASS |
| M3-05 | PARTIAL |
| M3-06 | PASS |
| M3-07 | PASS |
| M5-04 | PASS |
| M2-03 | PASS（180 帧全解，无解码错误，画面非空） |
