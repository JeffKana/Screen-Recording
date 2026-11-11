#pragma once

#include <memory>
#include <string>

#include "capture/DxgiCaptureSource.h"
#include "core/interfaces/ICaptureSource.h"

namespace sr::capture {

using win::ComPtr;

// 区域采集：内部仍是整屏采集，再按矩形裁到自己的纹理。
// 这样做的理由：DXGI DD 本身只支持整屏；对上层暴露的是一个尺寸等于选区的源，
// 合成与编码层完全不需要知道「区域」这件事（M6-13 的同一思路）。
class RegionCaptureSource final : public ICaptureSource {
public:
    explicit RegionCaptureSource(const IClock& clock);
    ~RegionCaptureSource() override;

    bool Start(const CaptureRequest& request, std::string& error) override;
    void Stop() override;
    bool IsRunning() const override;
    bool TryAcquire(VideoFrame& out) override;
    SourceCaps Caps() const override;
    bool NeedsRebuild() const override;

    // 选区被偶数对齐修正时返回 true，供 UI 提示「尺寸已调整」（M3-02 / M5-02 同类语义）。
    bool WasAdjusted() const { return adjusted_; }

    Rect EffectiveRegion() const { return region_; }

    // 供 CaptureDeviceAccess 递归取内层 DXGI 设备：区域源自身只是裁剪纹理，
    // 真正的采集设备在 inner_ 上。
    DxgiCaptureSource* Inner() const { return inner_.get(); }

private:
    std::unique_ptr<DxgiCaptureSource> inner_;
    ComPtr<ID3D11Texture2D> region_texture_;
    Rect region_{};
    Size size_{};
    bool adjusted_ = false;
    bool running_ = false;
};

}  // namespace sr::capture
