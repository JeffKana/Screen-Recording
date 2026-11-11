#pragma once

#include <string>

#include "core/interfaces/ICaptureSource.h"
#include "core/types/IClock.h"
#include "platform/ComPtr.h"

#include <d3d11.h>
#include <dxgi1_2.h>

namespace sr::capture {

using win::ComPtr;

// DXGI Desktop Duplication 全屏采集（03 文档 §4 的第二条路径）。
// 选择它的理由：延迟最低、D3D11 纹理直出；代价是只能整屏，且不含鼠标指针。
class DxgiCaptureSource final : public ICaptureSource {
public:
    explicit DxgiCaptureSource(const IClock& clock);
    ~DxgiCaptureSource() override;

    bool Start(const CaptureRequest& request, std::string& error) override;
    void Stop() override;
    bool IsRunning() const override { return running_; }
    bool TryAcquire(VideoFrame& out) override;
    SourceCaps Caps() const override;
    bool NeedsRebuild() const override { return needs_rebuild_; }

    ID3D11Device* Device() const { return device_.Get(); }
    ID3D11DeviceContext* Context() const { return context_.Get(); }

    // 枚举所有 (adapter, output) 组合，按全局序号取指定显示器。
    static uint32_t EnumerateOutputs(std::string& description);

private:
    bool CreateDeviceForOutput(uint32_t display_index, std::string& error);

    const IClock& clock_;
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGIOutputDuplication> duplication_;
    ComPtr<ID3D11Texture2D> frame_texture_;

    uint32_t display_index_ = 0;
    Size size_{};
    uint64_t frame_id_ = 0;
    bool running_ = false;
    bool needs_rebuild_ = false;
};

}  // namespace sr::capture
