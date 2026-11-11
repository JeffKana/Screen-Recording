# EVIDENCE: S2-002（轮 39 · M1-08 窗口尺寸跟随 + M1-08#2 两策略 + 崩溃修复；M1-09 保持已实现）

## 0 · 先更正轮 38 的错误归因

轮 38 报告写的是「本机 SDK 没有缩放 API、没有 `VideoProcessorSetStreamScaling`」，
并据此把 M1-08 判成「平台不支持 → BLOCKED」。**这个归因是错的**：

| 轮 38 的说法 | 事实 |
| --- | --- |
| 「SDK 缺缩放 API」 | D3D11 里**从来就没有** `VideoProcessorSetStreamScaling` 这个函数（`d3d11.h` / `d3d11_4.h` 里都没有；`d3d11video.h` 不在 SDK 里）。缩放 API 就是 **`VideoProcessorSetStreamSourceRect` → `VideoProcessorSetStreamDestRect`** 这一对「源矩形 → 目标矩形」映射，轮 38 的代码里**本来就在调用**。 |
| 「只能 1:1 搬运」 | 不成立：源矩形与目标矩形尺寸不同即可缩放。 |
| 「机制已关闭且不留死代码」 | 不成立：轮 38 自设的护栏（`Nv12Converter.cpp:309` 源尺寸≠输出尺寸直接 `return false`）把**已经写好的** fill/stretch 缩放分支挡成了**不可达死分支**。 |

→ 轮 38 整轮白跑的根因：**把「没找到函数名」当成了「平台不支持」**（验收方提醒①）。
→ 轮 39 的做法：**先实验（E1/E2）后改码**，并把实验结论写进代码注释与本文件。

## 1 · E1 / E2 实验（轮 39 指令 P0 第 1、2 项）

### E1：删掉自设护栏、**不**重建 VideoProcessor

改动：删掉 `Nv12Converter.cpp:309` 的护栏；`EnsureVideoProcessor()` 临时改成「处理器已存在就直接返回」
（= 永不按新源尺寸重建）。

```text
EXIT=0
converter=gpu-videoprocessor
source_size_changes=1
source_size=1280x720
frames_dropped=0
```

**实测结论（与预期不同，如实记录）**：Blt **没有失败**，也**没有把画面裁成处理器的输入尺寸**
——蓝色边框四边都在，内容完整。也就是说：**在本机（虚拟显示驱动）上，
VideoProcessor 并不要求 CONTENT_DESC.InputWidth/Height 与实际输入一致**。

> 验收方的机制判断（「处理器绑定了 InputWidth/Height，源尺寸变了要重建」）在本机**不成立**。
> 轮 38 观察到的「VideoProcessorBlt failed」的真实成因是**崩溃后的设备状态**（采集源重建崩在
> 纹理释放那一刻 → 设备状态异常 → Blt 失败 → 永久降级 CPU），不是缩放能力缺失。

### E2：删掉护栏 + 按新源尺寸重建 enumerator/processor

改动：`EnsureVideoProcessor()` 恢复为「源尺寸变化时用新尺寸重建 enumerator + processor」，
并在失败信息里带上 **HRESULT 与两端 content desc**（验收方要求的诊断信息）：

```cpp
error = "VideoProcessorBlt failed hr=0x" + ToHex(hr) +
        " source=WxH processor_input=WxH output=WxH dest=l,t,WxH";
```

```text
EXIT=0
converter=gpu-videoprocessor        ← 仍在 GPU 路径（没有降级 CPU）
source_size_changes=1
source_size=1280x720
frames_dropped=0
```

**结论**：E1 与 E2 的画面结果一致（都正确）。重建处理器**不是本机必需**，但它与 D3D11 文档
（CONTENT_DESC 描述输入尺寸）一致、代价是每帧一次尺寸比较，且能防住「严格驱动裁剪源矩形」。
→ **保留重建逻辑**（防御性），但**不把它当成唯一手段**；真正让缩放生效的是
`SetStreamSourceRect → SetStreamDestRect`（这一条 E1/E2 都验证了）。

## 2 · M1-08#1 / #2 实现（轮 39 指令 P0 第 3 项：4 条 SKIP → PASS）

### 2.1 采集侧：按帧的内容尺寸跟随（不重建采集源）

`src/capture/WgcCaptureSource.cpp`：
- 每帧读 `frame.ContentSize()`（SDK 里**有**这个 API：`IDirect3D11CaptureFrame::ContentSize`）；
- 与当前尺寸不同 → `impl_->pool.Recreate(device, format, 2, 新尺寸)`（**帧池必须重建**，
  否则 WGC 送来的帧会被裁到旧尺寸）+ 重建拷贝纹理 + 更新 `impl_->size`；
- **不重建采集源**（不重开 item、不重订阅事件）——验收方提醒②：E2 成功后不去重写采集层 resize。
  这一步只重建 D3D11 资源，避开了轮 38 的崩溃路径。

### 2.2 转换侧：缩放（源矩形 → 目标矩形）

`src/compositor/Nv12Converter.cpp`：`ComputeDestRect()` 按策略算目标矩形 →
`SetStreamSourceRect(源全幅)` + `SetStreamDestRect(目标矩形)` → VideoProcessor 完成缩放。

### 2.3 会话侧：上一帧纹理用 ComPtr 持有（崩溃修复）

轮 38 的真缺陷（验收方指出 `:946-955`）：采集侧重建纹理会 Release 旧纹理，
而会话手里只有裸指针 → 下一帧转换时**访问违规**（0xC0000005，实测 5 次里 2 次）。
修法：`RecordingSession` 用 `win::ComPtr<ID3D11Texture2D>` 持有上一帧纹理
（`latest_texture.Attach(...)`；源重建/恢复时 `Reset()`），旧纹理在会话释放引用前一直有效。

### 2.4 用例（`verify/cases/g5_window.ps1`）：4 条 SKIP → PASS

```powershell
pwsh -NoProfile -File verify/cases/g5_window.ps1 -BuildDir build-msvc
```

```text
  PASS m1-08-record-exit — 录制退出码=0
  PASS m1-08-fit-mode-applied — fit_mode=pad（期望 pad）
  PASS m1-08-source-follows — 源尺寸变化次数=1，最终源尺寸=1280x720（期望 ≥1 次、1280x720）
  PASS m1-08-no-dropped — 丢帧=0 / 采集=353
  PASS m1-08-frame-read — 成片 800x600，抽帧结果：ok
  PASS m1-08-content-complete — 蓝色边框包围盒 x=[0,799] y=[76,525]；内容包围盒 x=[0,799] y=[76,525]（两者应贴合，偏差 ≤8px）
  PASS m1-08-fit-pad-black-bars — pad 黑边 上=76px 下=74px（期望 75±2px；黑边占比=25.2%）
  PASS m1-08-no-black-frame-on-switch — 切换点 177~183 帧里的全黑帧：无（期望 0）
  PASS m1-08-stretch-fit-mode-applied — fit_mode=stretch（期望 stretch）
  PASS m1-08-fit-stretch-no-bars — stretch 黑边占比=0.0%（期望 <2%，即拉伸铺满无黑边）
  PASS m1-08-stretch-content-complete — stretch 蓝色边框包围盒 x=[0,799] y=[0,599]；内容包围盒 x=[0,799] y=[0,599]
  PASS m1-08-cycle-exit — 录制退出码=0（期望 0：反复 resize 不得崩）
  PASS m1-08-cycle-resizes — 尺寸变化次数=24（期望 ≥20 次）
  PASS m1-08-cycle-no-dropped — 丢帧=0 / 采集=771
RESULT: 24 PASS / 0 FAIL / 0 SKIP
```

| 验收方判据 | 实测 |
| --- | --- |
| 800×600→1280×720 画面完整 | 蓝色边框四边贴合内容包围盒（偏差 ≤8px）✓ |
| fill 黑边 ±2px | 上 **76px** / 下 **74px**（理论 75）✓ |
| stretch 无黑边 | 黑边占比 **0.0%** ✓ |
| 切换 5 帧无全黑 | 177~183 帧内全黑帧 **0** ✓ |
| 自动 resize 20 次 0 崩溃 | **24 次**尺寸变化，退出码 **0**，丢帧 **0** ✓ |

**测试方法的一处增强**：目标窗口加了**蓝色边框**（`srprobe windowhost`）。
「内容完整」不再靠「绿色包围盒贴边」判断（加了边框后绿色不再贴边 ✗），
而是判断**蓝色边框的包围盒是否贴合内容包围盒**——被裁剪时右边/下边的边框会消失 → 可判定。

## 3 · 回归

| 用例 | 结果 |
| --- | --- |
| `g5_window`（本轮） | **24 PASS / 0 FAIL / 0 SKIP** |
| `g5_format` | 14 PASS / 0 FAIL |
| `g4_gate` | 12 PASS / 0 FAIL |
| `g3_recovery` | 9 PASS / 0 FAIL |
| `g5_encoder_list` | 17 PASS / 0 FAIL（首跑 15/1 是**残留 sr-ui 进程**导致：上一轮 ui_smoke 失败后没清干净，单实例锁挡住第二个 UI；清理后复跑 17/0 ✓） |
| `g5_tempfile` | 10 PASS / 0 FAIL |
| `ui_smoke` | 103 PASS / 0 FAIL（一次 102/1，失败项 `unfocused-hotkey`「前台窗口探测」=环境敏感项，重跑即过；本轮未改热键相关代码 ✓） |
| 三构建 | MSVC Release / MSVC Debug / MinGW Release 均 0 error 0 warning |
| 覆盖表 | **90 已实现 / 1 有卡未实现 / 25 无卡**，缺理由 0，`--check` 通过，契约 C1~C8 8/8 |
