# HANDOFF: G2 阶段交接（S2/S3 → S4 / S5）

## 1. 我改了什么接口

| 位置 | 内容 | 影响下游 |
| --- | --- | --- |
| `src/platform/ComPtr.h` | 新增极简 COM 智能指针 `sr::win::ComPtr` | capture / audio 共用；禁止各自再造一份 |
| `src/capture/CaptureFactory.h` | 按 `CaptureKind` 返回实现，后端降级链在此收敛 | S5 只需调 `Create + Start + TryAcquire` |
| `src/audio/AudioTimeline.h` | 新增时间轴组件（非 core 接口，S3 内部） | S4 封装时依赖它给出的连续样本流 |
| `IAudioMixer` 语义 | `SetInputCount` 明确为「参与混音的输入路数」 | 双轨独立输出（M1-19）推迟到 V1.5，届时需扩展接口并走变更单 |

## 2. 下游需要接什么

- **S4（编码封装）**：`ICaptureSource::TryAcquire` 产出的是 `Bgra8Unorm` 纹理，
  必须在合成/编码前用 D3D11 VideoProcessor 转 NV12（G3 任务）。纹理指针仅在下次
  `TryAcquire` 前有效，不要跨帧持有。
- **S4**：音频输入是 `AudioTimeline::Pull` 产出的**等长 float32 块**，封装层按
  `first_sample` 推进 PTS 即可，不需要自己处理缺包。
- **S5（应用界面）**：采集源的选择与降级全部走 `CaptureFactory`，不要在 UI 里判断后端。

## 3. 已知限制

| 限制 | 影响 | 计划 |
| --- | --- | --- |
| **WGC 主路径未实现** | 窗口采集只能走 PrintWindow（CPU、限帧 30fps）；被遮挡仍可采，但高帧率窗口不适用 | MSVC + C++/WinRT 环境补卡（D-05） |
| DXGI DD 不含鼠标指针 | M1-24 光标需由合成层自绘 | G3 合成层处理（02 §4.2 已预留） |
| PrintWindow 对 GPU 渲染窗口可能返回黑图 | 部分 UWP / 浏览器窗口采集失败 | 连续 10 次失败置 `NeedsRebuild`，UI 提示「该窗口不支持」 |
| 双轨输出未实现 | M1-19 属 V1.5 | 随编辑能力一起做 |
| 静音/空闲会话下 DWM 节流至 1Hz | 采集吞吐无法测量 | 探针输出 `dwm_throttled`，验收自动降级为 SKIP |
| 采集与编码未串联 | 现在拿不到可播放文件 | G3 的核心任务 |

## 4. 未完成的需求

M6-06（缺高 DPI 环境）、M8-11（缺设备切换手段）、M1-01 的吞吐复测，均按 E6 挂账，
不计入 G2 达标。任何「已通过」的表述都以 EVIDENCE.md 中列出的实测输出为准。
