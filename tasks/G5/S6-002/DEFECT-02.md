# DEFECT-02：8h 全片解码的 6,667 条 null-muxer DTS 警告

- 严重度：**待裁定（口径问题）** / 未发现解码错误
- 发现：2026-10-28 12:0x（轮 35 收尾）
- 影响需求：M6-09#1（文件完整可播）；不影响 M6-02#1
- 结论：8h 第 3 轮 **5/6 干净 PASS**；第 6 项按 `-f null` 的 stderr 判为 FAIL，
  但独立 `framecrc` 证明 864,000 帧、DTS 严格递增，**没有解码错误或文件缺口**。
  裁定前 M6-02#1 / M6-09#1 均不转「已实现」，G5 不关闭。

## 1. 六项实测

| # | 项 | 实测 | 判定 |
| --- | --- | --- | --- |
| 1 | 时长 | 28,799.971s（≥28,790）；`record_exit=0`；`frames_encoded` 864,000 = 28,800×30 | PASS |
| 2 | 内存 | PeakWS 9~11min 基线 549.3MB → 全程峰值 550.6MB，**Δ1.3MB**；小时均值 533.5→401.4→364.x，非单调 | PASS |
| 3 | 句柄 | 基线 488 → 峰值 498，**+2.0%** | PASS |
| 4 | 丢帧 | `dropped=0 / captured=176,910` = **0.000%** | PASS |
| 5 | 漂移 | 16 点（第 16 点补测）：最差 **99.0ms @ 28,800s**，其余 0~26ms | PASS（≤100） |
| 6 | 全片可播 | `ffmpeg -f null -`：exit **0**，6,667 条 stderr，**全部**是同一条 null-muxer DTS 警告；无 h264/解码错误 | **待裁定** |

## 2. 第 6 项的原始证据

命令：

```
ffmpeg -hide_banner -v error -i lt01-8h.mkv -f null -
```

- exit code **0**；
- stderr 6,667 行，去重后只有：
  `[null @ ...] Application provided invalid, non monotonically increasing dts to muxer in stream 0: 480002 >= 480002`
- 数值范围 **480002 → 500000，步长 3**；null muxer 时基 1/30，对应媒体
  **16,000.07s ~ 16,666.73s（4h26m40s ~ 4h37m46s）**，即 11 分钟内约 10 条/秒；
- 窗口外的解码 stderr 为空。

## 3. 独立复核（同一文件，换 muxer）

1. **framecrc 全片（只视频）**：
   `ffmpeg -v error -i lt01-8h.mkv -map 0:v:0 -f framecrc -` → 逐行统计：
   `n=864000 bad=0 maxstep=1`，DTS 从 0 到 863,999 严格递增，无重复、无缺口。
2. **容器时间戳**：`ffprobe` 在 16,010 / 16,300 / 16,600s 三个 2s 窗口：
   各 60 个视频包（30fps），PTS 无重复，DTS 33.3ms 等步长。
3. **只解视频**：`-map 0:v:0 -f null -` 仍是同样的 6,667 条，排除音频混流因素。

结论：码流与容器时间戳正常；警告来自 **ffmpeg null muxer 的 DTS 重算**（把 30fps 在
1/30 时基下的连续 DTS 压成重复值），不是文件缺陷，也不是解码错误。

## 4. 为什么不能自己改判

验收指令写的是「`ffmpeg -f null -` 全片解码零错误」。字面上 stderr 非空 → FAIL；
实质上解码器零错误、864,000 帧完整。两种读法结论相反，属于**验收口径**，执行方不自行选择。

## 5. 建议（供裁定）

- 方案 A：把「全片可播」判据改为「`-f framecrc` 帧数 = 时长×帧率 且 DTS 严格递增」；
- 方案 B：保留 `-f null`，但只统计非 null-muxer 的 stderr（过滤
  `Application provided invalid, non monotonically increasing dts`）；
- 方案 C：维持字面判据，则本轮 M6-09 FAIL，需定位 ffmpeg 版本/时基并重跑 8h。

三个方案都不改动产品代码；区别只在验收命令。**在裁定前不做新功能。**

## 6. 附带发现（D-61）

用例原先把 `at >= duration` 作为 break 条件，`duration=28,799.971` 把第 16 点（28,800s）
跳过了，summary 只有 15 点。已改为 `at > duration + 0.5` 才 break；
本轮第 16 点补测为 **99.0ms**（文件末尾音轨比视频短 99ms），仍 ≤100ms。
补测数据：`verify/artifacts/FROZEN-20261028-0247/drift-16points.json`。
