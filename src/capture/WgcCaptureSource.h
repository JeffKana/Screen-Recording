#pragma once

#include <memory>
#include <string>

#include "core/interfaces/ICaptureSource.h"
#include "core/types/IClock.h"

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace sr::capture {

// Windows.Graphics.Capture 采集源（03 文档 §4 的主路径）。
//
// 为什么用 PIMPL：WGC 依赖 C++/WinRT，只有 MSVC 能编译。头文件保持无 winrt 依赖，
// MinGW 构建时 .cpp 走空实现（SR_HAS_WGC 未定义），工厂自动改用 DXGI/PrintWindow。
// 这样两条工具链都能构建，且接口层完全不用感知编译器差异。
class WgcCaptureSource final : public ICaptureSource {
public:
    explicit WgcCaptureSource(const IClock& clock);
    ~WgcCaptureSource() override;

    bool Start(const CaptureRequest& request, std::string& error) override;
    void Stop() override;
    bool IsRunning() const override;
    bool TryAcquire(VideoFrame& out) override;
    SourceCaps Caps() const override;
    bool NeedsRebuild() const override;

    // 运行时探测：老系统或受限会话下 WGC 可能不可用（M8-08 的 UnsupportedSource）。
    static bool IsSupported();

    // 供合成/转换层取到同一条 D3D11 设备（纹理跨设备不可用）。
    ID3D11Device* Device() const;
    ID3D11DeviceContext* Context() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace sr::capture
