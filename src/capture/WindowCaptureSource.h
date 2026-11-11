#pragma once

#include <string>

#include "core/interfaces/ICaptureSource.h"
#include "core/types/IClock.h"
#include "platform/ComPtr.h"

#include <d3d11.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace sr::capture {

using win::ComPtr;

// 窗口采集的兜底路径（03 文档 §4 的第三条）：PrintWindow + PW_RENDERFULLCONTENT。
// 它能拿到被遮挡窗口的内容（M1-09），代价是 CPU 位图 + 限帧，因此只在 WGC/DXGI
// 都不适用时启用。WGC 主路径需要 C++/WinRT，本环境（MinGW）不可用，见台账 D-05。
class WindowCaptureSource final : public ICaptureSource {
public:
    explicit WindowCaptureSource(const IClock& clock);
    ~WindowCaptureSource() override;

    bool Start(const CaptureRequest& request, std::string& error) override;
    void Stop() override;
    bool IsRunning() const override { return running_; }
    bool TryAcquire(VideoFrame& out) override;
    SourceCaps Caps() const override;
    bool NeedsRebuild() const override { return needs_rebuild_; }

private:
    bool RefreshBounds(std::string& error);
    bool CreateResources(std::string& error);
    void ReleaseResources();

    const IClock& clock_;
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<ID3D11Texture2D> texture_;

    HWND window_ = nullptr;
    HDC memory_dc_ = nullptr;
    HBITMAP dib_ = nullptr;
    void* bits_ = nullptr;

    int32_t bounds_x_ = 0;
    int32_t bounds_y_ = 0;
    Size size_{};
    uint32_t target_fps_ = 15;
    uint64_t frame_id_ = 0;
    Tick last_frame_{};
    bool running_ = false;
    bool needs_rebuild_ = false;
    uint32_t failed_captures_ = 0;
};

}  // namespace sr::capture
