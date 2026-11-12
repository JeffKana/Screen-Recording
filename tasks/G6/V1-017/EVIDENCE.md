# EVIDENCE: V1-017（轮 45 · 用例侧三项）

> 口径声明：本卡**只改用例与激励源**，判据数字（5s 滑窗 ≤10%、单秒 ≤2×、UI 条目数相等）一个未动。
> 数据来源：2026-11-11 20:38~20:56 / `build-msvc` Release / Win11 26200 / 1920x1080 / DPI 100%。

## 1. M3-04#1 用例：静态与活动两组固化进同一套脚本

- 验证方式：`g5_cbr_probe.ps1` 新增 `active` 场景与 `-Mode` 逗号列表；判定改为按轮 44 口径
  （`Get-Window5Spread` + 单秒 2× 硬挡板），逐次输出 `VERDICT=PASS/FAIL`。
- 命令：
  `pwsh -NoProfile -File verify/cases/g5_cbr_probe.ps1 -Seconds 40 -Runs 3 -Mode static,active -OutDir tasks/G5/S4-005/artifacts/cbr-6runs`
- 输出：`tasks/G5/S4-005/artifacts/cbr-6runs/probe-summary.txt`（与 `S4-005` 共用同一份 6 次数据）
- 结果原样（`probe-summary.txt` 的 MODE 行）：

```
MODE static runs=3 verdict_pass=3 over_10pct=0 win5=0.0%,0.0%,0.0% peak=6000,6000,6000 capture_fps=1.0,2.0,1.0
MODE active runs=3 verdict_pass=3 over_10pct=0 win5=0.5%,0.6%,0.6% peak=6098,6085,6086 capture_fps=51.0,49.0,48.0
```

- 结论：**脚本层面达标**（一条命令同时覆盖两类场景、逐次给判定、可无人值守）。
  **数据强度不足**（active 的编码器真实输出 ~180KB/s，弱于真实桌面的 1.18MB 过冲秒），
  已在 `tasks/G5/S4-005/EVIDENCE.md` E3 写明，**不作为 M3-04#1 翻绿依据**。

### 1.1 激励源 `verify/lib/ActiveDesktop.ps1` 为什么这么做

- 大面积纯色块动画实测只占 143KB/s，编不动编码器 → 不能作为「有活动」的证据；
  真实桌面（终端/IDE）是**密集抗锯齿小字 + 大块浅色区域 + 光标移动**，故本脚本按此实现：
  满屏 12pt 等宽小字、每 250ms 整屏重排、光标划圆、浅底深字。
- 明确标注限制：它仍**弱于**真实桌面（见上），`capture_fps` 48~51 只能证明「桌面对采集侧确实在变」，
  不能等价于验收方的 IDE 负载。

## 2. M3-05#1/#2 `g5_encoder_list` 竞态（5 连绿）

- 验证方式：把「窗口出现后立刻 `LB_GETCOUNT`」改为 **≤2s 轮询到条目数就绪**再断言；
  `encoders=[...]` / `encoder_rows=` 的日志读取同样改为 ≤2s 轮询。**判据仍是「相等」**。
- 命令：`pwsh -NoProfile -File verify/cases/g5_encoder_list.ps1`（连跑 5 次）
- 输出：`artifacts/encoder-list-5runs.txt`

```
run 1 : RESULT: 17 PASS / 0 FAIL
run 2 : RESULT: 17 PASS / 0 FAIL
run 3 : RESULT: 17 PASS / 0 FAIL
run 4 : RESULT: 17 PASS / 0 FAIL
run 5 : RESULT: 17 PASS / 0 FAIL
```

- 结论：**通过**。轮 44 的 15/1 由此可解释为「控件先创建、条目后填充」的取数竞态
  （验收方轮 44 复跑 17/0 亦未复现，与本判断一致）。

## 3. 矩阵补 1 格：Win11 24H2 × 双屏 × 100% × **dxgi** 后端

- 为什么是这一格：单屏 100% 已在前两轮分别用 `wgc`（轮 42/43）与 `dxgi`（轮 44）测过；
  `printwindow` 是**窗口采集**后端（`CaptureFactory.cpp:55-57` 只接 `WindowCaptureSource`），
  不能用于 `--display` 显示采集，因此单屏格没有第三个可用后端。
  本轮补的是**双屏 100% × dxgi**（后端维度的另一组合，此前双屏只测过 wgc）。
- 命令：`pwsh -NoProfile -File tasks/G6/V1-017/artifacts/run-r45-matrix.ps1`
- 输出：`artifacts/r45-matrix.txt` + `artifacts/dual-screen-100-dxgi.out`

```
R45_MATRIX_START 2026-11-11 20:53:15
ENV OS=Microsoft Windows 11 家庭版 中文版 build=26200 DPI=100% main=1920x1080 screens=2
CELL dual-screen-100-dxgi backend=dxgi exit=0 wall=60.02 frames=1800 dropped=0 duration=59.999 av_delta_ms=1 nonblack=3/3 decode_err=0 PASS=True
R45_MATRIX_DONE 2026-11-11 20:54:19
```

- 三件套逐项：① 录制 60.02s / 1800 帧 / dropped=0；② 抽 n=30/900/1770 三帧非全黑 3/3；
  ③ 音画尾偏差 1ms；另 duration=59.999s、双轨、`ffmpeg -f null` 零错误。
- 结论：**通过**（`docs/14` 的具体行文由 `V1-018` 更新，避免两卡同改一份文档）。
