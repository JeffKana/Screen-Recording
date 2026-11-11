#pragma once

#include <cstdint>

#include "core/types/Frame.h"

namespace sr {

enum class LayerKind : uint8_t {
    Camera = 0,  // 摄像头画中画（M1-12）
    Watermark,   // 文字/图片水印（M7-06、M7-07）
    PrivacyMask, // 隐私遮挡（M7-09）
    Cursor,      // 自绘光标（M7-04、M7-05）
    Annotation,  // 录制中涂鸦（M7-11）
};

struct CompositorLayer {
    LayerKind kind = LayerKind::Camera;
    // 源纹理。cursor 与文字水印可能为 nullptr（由实现按 layer 内联数据绘制）。
    const void* texture = nullptr;
    Rect destination{};     // 画布坐标系
    Rect source{};          // 源纹理裁剪区；宽高为 0 时表示整张
    uint32_t z_order = 0;   // 越大越靠上
    float opacity = 1.0f;
    bool mirrored = false;  // 摄像头镜像（M1-12）
    // 形状：0=矩形 1=圆角 2=圆形（M1-12）
    uint8_t shape = 0;
    uint8_t corner_radius = 0;
};

// 合成抽象。所有叠加在编码之前完成，因此录制即固化（02 文档 §8 ADR-07）。
// 实现必须全程在 GPU 完成，禁止回读到 CPU（M6-01）。
class ICompositor {
public:
    virtual ~ICompositor() = default;

    // base 为主画面（采集帧），canvas 为最终输出尺寸，必须偶数对齐（M3-02）。
    virtual bool BeginFrame(const VideoFrame& base, Size canvas) = 0;

    virtual bool DrawLayer(const CompositorLayer& layer) = 0;

    // 输出 NV12 纹理，交给 IVideoEncoder::Submit。
    virtual bool EndFrame(VideoFrame& out) = 0;

    // 设置光标增强参数；enabled=false 时应把光标完全交给采集层原生绘制（02 文档 §4.2）。
    virtual void ConfigureCursor(bool enabled, float scale, bool spotlight) = 0;
};

}  // namespace sr
