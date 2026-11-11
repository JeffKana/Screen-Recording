#include "capture/WgcCaptureSource.h"

#include <atomic>
#include <cstdio>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dwmapi.h>

#ifdef SR_HAS_WGC

#include <d3d11.h>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

namespace sr::capture {
namespace {

namespace wgc = winrt::Windows::Graphics::Capture;

// 轮 39（M1-08）：窗口尺寸跟随**已实现**——按每帧的 ContentSize() 重建帧池
// （pool.Recreate()），**不重建采集源**；缩放由 SetStreamDestRect 完成
// （见 src/compositor/Nv12Converter.cpp 的 ComputeDestRect）。
// 轮 38 那条「按边界变化重建采集源会触发间歇性访问冲突」的顾虑已被实测排除：
// verify/cases/g5_window.ps1 24 PASS / 0 FAIL（源尺寸跟随 1 次、最终 1280×720、
// 连续 resize 24 次 exit 0、丢帧 0）。
namespace wgdx = winrt::Windows::Graphics::DirectX;
namespace wgdx11 = winrt::Windows::Graphics::DirectX::Direct3D11;
using winrt::Windows::Foundation::Metadata::ApiInformation;

BOOL CALLBACK CollectMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM context) {
    auto* list = reinterpret_cast<std::vector<HMONITOR>*>(context);
    list->push_back(monitor);
    return TRUE;
}

}  // namespace

struct WgcCaptureSource::Impl {
    explicit Impl(const IClock& c) : clock(c) {}

    const IClock& clock;
    winrt::com_ptr<ID3D11Device> device;
    winrt::com_ptr<ID3D11DeviceContext> context;
    winrt::com_ptr<ID3D11Texture2D> texture;

    wgc::Direct3D11CaptureFramePool pool{nullptr};
    wgc::GraphicsCaptureSession session{nullptr};
    wgc::GraphicsCaptureItem item{nullptr};
    winrt::event_token closed_token{};

    Size size{};
    uint64_t frame_id = 0;
    // 轮 39（M1-08）：帧池按内容尺寸重建时要用同一个 WinRT 设备。
    wgdx11::IDirect3DDevice winrt_device{nullptr};
    bool running = false;
    // 轮 39（M1-08）：窗口尺寸变化次数（观测量）。
    uint64_t size_changes = 0;
    std::atomic<bool> closed{false};
    std::atomic<bool> needs_rebuild{false};
};

bool WgcCaptureSource::IsSupported() {
    try {
        return wgc::GraphicsCaptureSession::IsSupported();
    } catch (...) {
        return false;
    }
}

WgcCaptureSource::WgcCaptureSource(const IClock& clock) : impl_(std::make_unique<Impl>(clock)) {}

WgcCaptureSource::~WgcCaptureSource() {
    Stop();
}

bool WgcCaptureSource::Start(const CaptureRequest& request, std::string& error) {
    Stop();

    if (!IsSupported()) {
        error = "GraphicsCaptureSession::IsSupported() is false";
        return false;
    }

    try {
        // 1) 创建设备（需要 BGRA 支持，WGC 输出 BGRA8 纹理）
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
        D3D_FEATURE_LEVEL achieved = D3D_FEATURE_LEVEL_11_0;
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, 2,
                                       D3D11_SDK_VERSION, impl_->device.put(), &achieved,
                                       impl_->context.put());
        if (FAILED(hr)) {
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, &levels[1], 1,
                                   D3D11_SDK_VERSION, impl_->device.put(), &achieved,
                                   impl_->context.put());
        }
        if (FAILED(hr)) {
            error = "D3D11CreateDevice failed for WGC";
            return false;
        }

        // 2) 创建采集项：窗口 or 显示器
        auto interop = winrt::get_activation_factory<wgc::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        if (request.kind == CaptureKind::Window) {
            const HWND hwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(request.window_handle));
            hr = interop->CreateForWindow(hwnd, winrt::guid_of<wgc::GraphicsCaptureItem>(),
                                          winrt::put_abi(impl_->item));
        } else {
            std::vector<HMONITOR> monitors;
            EnumDisplayMonitors(nullptr, nullptr, CollectMonitor, reinterpret_cast<LPARAM>(&monitors));
            if (request.display_index >= monitors.size()) {
                error = "display index out of range for WGC";
                return false;
            }
            hr = interop->CreateForMonitor(monitors[request.display_index],
                                           winrt::guid_of<wgc::GraphicsCaptureItem>(),
                                           winrt::put_abi(impl_->item));
        }
        if (FAILED(hr) || impl_->item == nullptr) {
            error = "CreateForMonitor/CreateForWindow failed";
            return false;
        }

        // 3) 用 DXGI 设备包装出 WinRT 设备
        winrt::com_ptr<IDXGIDevice> dxgi_device;
        winrt::check_hresult(impl_->device->QueryInterface(IID_PPV_ARGS(dxgi_device.put())));
        winrt::com_ptr<::IInspectable> inspectable;
        winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi_device.get(), inspectable.put()));
        const auto winrt_device = inspectable.as<wgdx11::IDirect3DDevice>();
        // 轮 39（M1-08）：帧池尺寸变化时要用同一个 WinRT 设备调 Recreate，故存一份。
        impl_->winrt_device = winrt_device;

        const auto item_size = impl_->item.Size();
        impl_->size.width = static_cast<uint32_t>(item_size.Width);
        impl_->size.height = static_cast<uint32_t>(item_size.Height);
        if (impl_->size.width == 0 || impl_->size.height == 0) {
            error = "capture item reports zero size";
            return false;
        }

        // 4) 帧池 + 会话
        impl_->pool = wgc::Direct3D11CaptureFramePool::CreateFreeThreaded(
            winrt_device, wgdx::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, item_size);
        impl_->session = impl_->pool.CreateCaptureSession(impl_->item);

        impl_->session.IsCursorCaptureEnabled(request.include_cursor);
        if (ApiInformation::IsPropertyPresent(L"Windows.Graphics.Capture.GraphicsCaptureSession",
                                              L"IsBorderRequired")) {
            // Win11 才有；关掉黄色采集边框，避免被录进画面。
            impl_->session.IsBorderRequired(false);
        }

        // Closed 事件挂在 GraphicsCaptureItem 上（不是 Session）。
        // 源消失、权限收回时置位，由上层重建或降级（M8-10）。
        impl_->closed = false;
        impl_->closed_token = impl_->item.Closed([impl = impl_.get()](auto&&, auto&&) {
            impl->closed = true;
            impl->needs_rebuild = true;
        });

        // 5) 兜底纹理：WGC 的帧纹理不能跨帧持有，复制到自己的纹理
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = impl_->size.width;
        desc.Height = impl_->size.height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        hr = impl_->device->CreateTexture2D(&desc, nullptr, impl_->texture.put());
        if (FAILED(hr)) {
            error = "CreateTexture2D failed for WGC";
            return false;
        }

        impl_->session.StartCapture();
        impl_->running = true;
        impl_->needs_rebuild = false;
        error.clear();
        return true;
    } catch (const winrt::hresult_error& ex) {
        error = "WGC start failed: " + winrt::to_string(ex.message());
        Stop();
        return false;
    } catch (...) {
        error = "WGC start failed: unknown error";
        Stop();
        return false;
    }
}

void WgcCaptureSource::Stop() {
    if (!impl_) {
        return;
    }
    impl_->running = false;
    try {
        if (impl_->item != nullptr) {
            impl_->item.Closed(impl_->closed_token);
        }
        if (impl_->session != nullptr) {
            impl_->session.Close();
        }
        if (impl_->pool != nullptr) {
            impl_->pool.Close();
        }
    } catch (...) {
        // 关闭路径不允许抛异常
    }
    impl_->session = nullptr;
    impl_->pool = nullptr;
    impl_->item = nullptr;
    impl_->texture = nullptr;
    impl_->context = nullptr;
    impl_->device = nullptr;
}

bool WgcCaptureSource::IsRunning() const {
    return impl_ && impl_->running;
}

ID3D11Device* WgcCaptureSource::Device() const {
    return impl_ ? impl_->device.get() : nullptr;
}

ID3D11DeviceContext* WgcCaptureSource::Context() const {
    return impl_ ? impl_->context.get() : nullptr;
}

bool WgcCaptureSource::TryAcquire(VideoFrame& out) {
    if (!impl_ || !impl_->running || impl_->pool == nullptr) {
        return false;
    }
    if (impl_->closed) {
        impl_->needs_rebuild = true;
        return false;
    }

    try {
        auto frame = impl_->pool.TryGetNextFrame();
        if (frame == nullptr) {
            return false;  // 桌面无变化
        }

        const auto surface = frame.Surface();
        // IDirect3DDxgiInterfaceAccess 是 ABI 层的裸 COM 接口，不走 winrt::as<T>() 投影，
        // 必须显式 QueryInterface。
        winrt::com_ptr<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess> access;
        winrt::check_hresult(surface.as<::IUnknown>()->QueryInterface(
            __uuidof(::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess),
            access.put_void()));
        winrt::com_ptr<ID3D11Texture2D> frame_texture;
        winrt::check_hresult(access->GetInterface(IID_PPV_ARGS(frame_texture.put())));

        // 轮 39（M1-08）：窗口尺寸变了 → 帧池按新尺寸重建 + 拷贝纹理按新尺寸重建。
        // 帧池不重建的话，WGC 送来的帧会被裁到旧尺寸（内容不完整）。
        // 这一步只重建 D3D11 资源，**不重建采集源**（不重开 item / 不重订阅事件）。
        const auto content_size = frame.ContentSize();
        const auto content_width = static_cast<uint32_t>(content_size.Width);
        const auto content_height = static_cast<uint32_t>(content_size.Height);
        if (content_width > 0 && content_height > 0 &&
            (content_width != impl_->size.width || content_height != impl_->size.height)) {
            frame.Close();
            impl_->pool.Recreate(impl_->winrt_device,
                                 wgdx::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2,
                                 {static_cast<int32_t>(content_width),
                                  static_cast<int32_t>(content_height)});
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = content_width;
            desc.Height = content_height;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
            impl_->texture = nullptr;
            if (FAILED(impl_->device->CreateTexture2D(&desc, nullptr, impl_->texture.put()))) {
                return false;
            }
            impl_->size.width = content_width;
            impl_->size.height = content_height;
            ++impl_->size_changes;
            return false;  // 这一帧是旧尺寸的，跳过；下一帧就是新尺寸。
        }

        impl_->context->CopyResource(impl_->texture.get(), frame_texture.get());
        frame.Close();

        out.native_texture = impl_->texture.get();
        out.desc.pts = impl_->clock.Now();
        out.desc.size = impl_->size;
        out.desc.format = PixelFormat::Bgra8Unorm;
        out.desc.display_index = 0;
        out.desc.duplicated = false;
        out.frame_id = ++impl_->frame_id;
        return true;
    } catch (...) {
        impl_->needs_rebuild = true;
        return false;
    }
}

SourceCaps WgcCaptureSource::Caps() const {
    SourceCaps caps;
    if (impl_) {
        caps.max_size = impl_->size;
    }
    caps.max_fps = 0;
    caps.supports_cursor_toggle = true;  // IsCursorCaptureEnabled
    caps.supports_occlusion = true;      // 窗口被遮挡仍可采集（M1-09）
    caps.backend = "wgc";
    return caps;
}

bool WgcCaptureSource::NeedsRebuild() const {
    return impl_ && impl_->needs_rebuild.load();
}

}  // namespace sr::capture

#else  // SR_HAS_WGC 未定义（MinGW 构建）

namespace sr::capture {

struct WgcCaptureSource::Impl {
    explicit Impl(const IClock& c) : clock(c) {}
    const IClock& clock;
};

bool WgcCaptureSource::IsSupported() {
    return false;
}

WgcCaptureSource::WgcCaptureSource(const IClock& clock) : impl_(std::make_unique<Impl>(clock)) {}

WgcCaptureSource::~WgcCaptureSource() = default;

bool WgcCaptureSource::Start(const CaptureRequest&, std::string& error) {
    error = "WGC backend is not compiled in (requires MSVC + C++/WinRT)";
    return false;
}

void WgcCaptureSource::Stop() {}

bool WgcCaptureSource::IsRunning() const {
    return false;
}

ID3D11Device* WgcCaptureSource::Device() const {
    return nullptr;
}

ID3D11DeviceContext* WgcCaptureSource::Context() const {
    return nullptr;
}

bool WgcCaptureSource::TryAcquire(VideoFrame&) {
    return false;
}

SourceCaps WgcCaptureSource::Caps() const {
    SourceCaps caps;
    caps.backend = "wgc-unavailable";
    return caps;
}

bool WgcCaptureSource::NeedsRebuild() const {
    return false;
}

}  // namespace sr::capture

#endif  // SR_HAS_WGC
