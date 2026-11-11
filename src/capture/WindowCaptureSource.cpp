#include "capture/WindowCaptureSource.h"

#include <dwmapi.h>

namespace sr::capture {
namespace {

// Win8.1+ 的 PW_RENDERFULLCONTENT，MinGW 的 winuser.h 里没有。
constexpr UINT kRenderFullContent = 0x00000002;

uint32_t MakeEven(int32_t value) {
    const int32_t clamped = value < 2 ? 2 : value;
    return static_cast<uint32_t>(clamped & ~1);
}

}  // namespace

WindowCaptureSource::WindowCaptureSource(const IClock& clock) : clock_(clock) {}

WindowCaptureSource::~WindowCaptureSource() {
    Stop();
}

bool WindowCaptureSource::RefreshBounds(std::string& error) {
    if (window_ == nullptr || !IsWindow(window_)) {
        error = "window handle is invalid";
        return false;
    }

    RECT rect{};
    // 优先用 DWM 的扩展边框，避免把窗口阴影/圆角外空白录进来。
    if (FAILED(DwmGetWindowAttribute(window_, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect)))) {
        if (!GetWindowRect(window_, &rect)) {
            error = "GetWindowRect failed";
            return false;
        }
    }

    bounds_x_ = rect.left;
    bounds_y_ = rect.top;
    const uint32_t width = MakeEven(rect.right - rect.left);
    const uint32_t height = MakeEven(rect.bottom - rect.top);
    if (width == 0 || height == 0) {
        error = "window has zero size";
        return false;
    }
    size_.width = width;
    size_.height = height;
    error.clear();
    return true;
}

void WindowCaptureSource::ReleaseResources() {
    if (memory_dc_ != nullptr) {
        if (dib_ != nullptr) {
            SelectObject(memory_dc_, nullptr);
        }
        DeleteDC(memory_dc_);
        memory_dc_ = nullptr;
    }
    if (dib_ != nullptr) {
        DeleteObject(dib_);
        dib_ = nullptr;
    }
    bits_ = nullptr;
    texture_.Reset();
    context_.Reset();
    device_.Reset();
}

bool WindowCaptureSource::CreateResources(std::string& error) {
    ReleaseResources();

    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL achieved = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = D3D11CreateDevice(nullptr,
                                   D3D_DRIVER_TYPE_HARDWARE,
                                   nullptr,
                                   D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                   levels,
                                   2,
                                   D3D11_SDK_VERSION,
                                   device_.GetAddressOf(),
                                   &achieved,
                                   context_.GetAddressOf());
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(nullptr,
                               D3D_DRIVER_TYPE_HARDWARE,
                               nullptr,
                               D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                               &levels[1],
                               1,
                               D3D11_SDK_VERSION,
                               device_.GetAddressOf(),
                               &achieved,
                               context_.GetAddressOf());
    }
    if (FAILED(hr)) {
        error = "D3D11CreateDevice failed";
        return false;
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = size_.width;
    desc.Height = size_.height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    hr = device_->CreateTexture2D(&desc, nullptr, texture_.GetAddressOf());
    if (FAILED(hr)) {
        error = "CreateTexture2D failed";
        return false;
    }

    memory_dc_ = CreateCompatibleDC(nullptr);
    if (memory_dc_ == nullptr) {
        error = "CreateCompatibleDC failed";
        return false;
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = static_cast<LONG>(size_.width);
    info.bmiHeader.biHeight = -static_cast<LONG>(size_.height);  // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    dib_ = CreateDIBSection(memory_dc_, &info, DIB_RGB_COLORS, &bits_, nullptr, 0);
    if (dib_ == nullptr || bits_ == nullptr) {
        error = "CreateDIBSection failed";
        return false;
    }
    SelectObject(memory_dc_, dib_);
    error.clear();
    return true;
}

bool WindowCaptureSource::Start(const CaptureRequest& request, std::string& error) {
    Stop();

    window_ = reinterpret_cast<HWND>(static_cast<uintptr_t>(request.window_handle));
    target_fps_ = request.target_fps == 0 ? 15u : (request.target_fps > 30u ? 30u : request.target_fps);

    if (!RefreshBounds(error)) {
        return false;
    }
    if (!CreateResources(error)) {
        ReleaseResources();
        return false;
    }

    needs_rebuild_ = false;
    failed_captures_ = 0;
    last_frame_ = Tick{};
    running_ = true;
    error.clear();
    return true;
}

void WindowCaptureSource::Stop() {
    running_ = false;
    ReleaseResources();
    window_ = nullptr;
}

bool WindowCaptureSource::TryAcquire(VideoFrame& out) {
    if (!running_ || window_ == nullptr || !texture_) {
        return false;
    }

    // 限帧：PrintWindow 是 CPU 路径，不限制会直接把 CPU 打满。
    const Tick now = clock_.Now();
    if (last_frame_.Valid()) {
        const int64_t interval_ms = clock_.ElapsedMs(last_frame_, now);
        const int64_t budget_ms = target_fps_ == 0 ? 66 : (1000 / static_cast<int64_t>(target_fps_));
        if (interval_ms < budget_ms) {
            return false;
        }
    }

    if (!IsWindow(window_)) {
        needs_rebuild_ = true;
        running_ = false;
        return false;
    }

    // 窗口移动/缩放：重建纹理以跟随（M1-08 的基础能力）。
    const Size previous = size_;
    std::string error;
    if (!RefreshBounds(error)) {
        needs_rebuild_ = true;
        return false;
    }
    if (size_.width != previous.width || size_.height != previous.height) {
        if (!CreateResources(error)) {
            needs_rebuild_ = true;
            return false;
        }
    }

    HDC target = memory_dc_;
    BOOL ok = PrintWindow(window_, target, kRenderFullContent);
    if (!ok) {
        ok = PrintWindow(window_, target, 0);
    }
    if (!ok) {
        ++failed_captures_;
        // 单次失败可能只是窗口正在重绘；连续失败才判定源不可用。
        if (failed_captures_ >= 10) {
            needs_rebuild_ = true;
        }
        return false;
    }
    failed_captures_ = 0;

    context_->UpdateSubresource(texture_.Get(), 0, nullptr, bits_, size_.width * 4, 0);
    last_frame_ = now;

    out.native_texture = texture_.Get();
    out.desc.pts = now;
    out.desc.size = size_;
    out.desc.format = PixelFormat::Bgra8Unorm;
    out.desc.display_index = 0;
    out.desc.duplicated = false;
    out.frame_id = ++frame_id_;
    return true;
}

SourceCaps WindowCaptureSource::Caps() const {
    SourceCaps caps;
    caps.max_size = size_;
    caps.max_fps = 30;
    caps.supports_cursor_toggle = false;
    // PrintWindow 拿到的是窗口自身绘制结果，被遮挡也能采（M1-09）。
    caps.supports_occlusion = true;
    caps.backend = "printwindow";
    return caps;
}

}  // namespace sr::capture
