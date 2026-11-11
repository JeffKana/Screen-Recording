# HANDOFF: S2-002（轮 39 · M1-08#1/#2 已实现；M1-09 已实现）

## 1. 我改了什么接口？

**核心契约未改**（`CONTRACT.md` §4 未动）。新增/改动都是**产品入口 + 只读观测量 + 一个内部持有方式**：

| 位置 | 变化 |
| --- | --- |
| `src/capture/WgcCaptureSource.cpp` | 每帧读 `IDirect3D11CaptureFrame::ContentSize()`；尺寸变了 → `pool.Recreate()` + 重建拷贝纹理（**不重建采集源**） |
| `src/compositor/Nv12Converter.{h,cpp}` | `EnsureVideoProcessor()`：源尺寸变化时按新尺寸重建 enumerator+processor；失败信息带 **HRESULT + 两端 content desc**（`ToHex()`） |
| `src/recorder/RecordingSession.cpp` | 上一帧纹理改用 `win::ComPtr<ID3D11Texture2D>` 持有（**崩溃修复**：裸指针会在采集侧重建纹理后悬空） |
| `src/tools/srprobe/window_host.cpp` | 目标窗口加**蓝色边框**；新增 `--resize-cycle N`（每 N 秒来回切换尺寸，压「反复 resize」路径） |
| `verify/cases/g5_window.ps1` | M1-08 的 4 条 SKIP → 真实断言；新增「反复 resize 0 崩溃」3 条；抽帧中间文件按视频名命名（修掉 pad/stretch 互相复用旧帧的缺陷） |

## 2. 下游需要接什么？

| 下游 | 要做什么 |
| --- | --- |
| 验收方 | 复跑 `pwsh -NoProfile -File verify/cases/g5_window.ps1 -BuildDir build-msvc`（期望 24 PASS / 0 FAIL / 0 SKIP）；覆盖表终态 **90/1/25** |
| 验收方 | **归因更正**：轮 38 报告的「SDK 缺缩放 API / 平台不支持」是错的（D3D11 没有 `VideoProcessorSetStreamScaling` 这个函数；缩放 = `SetStreamSourceRect`→`SetStreamDestRect`），已在 `EVIDENCE.md` §0 与台账轮 39 行更正 |
| 后续轮次（S2） | 若要支持**显示器/区域**源在运行期改尺寸（热插拔 M8-10），同样的「按内容尺寸重建」逻辑要覆盖 DXGI/PrintWindow 两条路径（当前只做了 WGC） |
| G6 | `--window/--fit` 是产品参数，进发布包 ✓；`srprobe windowhost` 是验收支撑工具，不进包 ✓ |

## 3. 已知限制是什么？

1. **缩放在 GPU 上是「源矩形 → 目标矩形」映射**，等比缩放（pad）时**画面本身不变形**，
   但 VideoProcessor 的缩放滤波质量取决于驱动（本机是虚拟显示驱动，未评估画质）。
2. **`EnsureVideoProcessor` 在本机并非必需**（E1 实测：不重建也能正确缩放），
   保留它是为了与 D3D11 文档一致、防止严格驱动按 CONTENT_DESC 裁剪源矩形。
   代价：每次源尺寸变化会重建一次 enumerator+processor（实测 24 次 resize 无异常 ✓）。
3. **CPU 兜底路径仍是最近邻**（缩放质量一般）；GPU 路径不可用时才会走它。
4. **只覆盖 WGC 窗口源**：显示器/区域源的运行期尺寸变化（热插拔）未做。
5. **窗口源没有 UI 入口**：只有 CLI（`--window` / `--fit`）。UI 侧要有「选窗口 + 选适配策略」才谈得上产品化。
6. **测试窗口的蓝框是验收专用**：`srprobe windowhost` 画的蓝框会录进成片（用例按颜色断言用），
   不是产品行为。

## 4. 哪些需求我**没**做完？

| 需求 | 状态 | 说明 |
| --- | --- | --- |
| M1-08#1 | **已实现** | 4 条 SKIP 全转 PASS ✓；反复 resize 24 次 0 崩溃 ✓ |
| M1-08#2 | **已实现** | pad 黑边 76/74px（理论 75±2）✓；stretch 黑边 0.0% ✓ |
| M1-09#1 | **已实现**（轮 38 起） | 遮挡时绿色 95.9% / 遮挡物 0.0% / 边框贴合 ✓ |
| M1-27#1（提权提示） | 有卡未实现 | **轮 39 未做**（P1）：UI 的提权检测 + 自检项 + 「以管理员身份重启」入口 + 用例；见 `tasks/G5/S5-003` |
| 3 条无卡收口 | 无卡 | **轮 39 未做**（P1）：覆盖表仍有 25 条无卡 |
| 归因更正 | 已完成 | 轮 38 的错误归因已在 `EVIDENCE.md` §0 与台账轮 39 行更正 ✓ |
