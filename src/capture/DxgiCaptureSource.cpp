#include "capture/DxgiCaptureSource.h"

#include <cstdio>
#include <vector>

namespace sr::capture {
namespace {

struct OutputRef {
    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<IDXGIOutput1> output;
    DXGI_OUTPUT_DESC desc{};
};

bool CollectOutputs(std::vector<OutputRef>& out, std::string& error) {
    ComPtr<IDXGIFactory1> factory;
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1),
                                    reinterpret_cast<void**>(factory.GetAddressOf()));
    if (FAILED(hr)) {
        error = "CreateDXGIFactory1 failed";
        return false;
    }

    for (UINT adapter_index = 0;; ++adapter_index) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(adapter_index, adapter.GetAddressOf()) == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        for (UINT output_index = 0;; ++output_index) {
            ComPtr<IDXGIOutput> raw_output;
            if (adapter->EnumOutputs(output_index, raw_output.GetAddressOf()) == DXGI_ERROR_NOT_FOUND) {
                break;
            }
            ComPtr<IDXGIOutput1> output1;
            if (FAILED(raw_output->QueryInterface(__uuidof(IDXGIOutput1),
                                                  reinterpret_cast<void**>(output1.GetAddressOf())))) {
                continue;
            }
            OutputRef ref;
            ref.adapter = adapter;
            ref.output = output1;
            output1->GetDesc(&ref.desc);
            out.push_back(std::move(ref));
        }
    }

    if (out.empty()) {
        error = "no DXGI output found";
        return false;
    }
    return true;
}

}  // namespace

DxgiCaptureSource::DxgiCaptureSource(const IClock& clock) : clock_(clock) {}

DxgiCaptureSource::~DxgiCaptureSource() {
    Stop();
}

uint32_t DxgiCaptureSource::EnumerateOutputs(std::string& description) {
    std::vector<OutputRef> outputs;
    std::string error;
    if (!CollectOutputs(outputs, error)) {
        description = error;
        return 0;
    }
    description.clear();
    char line[256] = {};
    for (size_t i = 0; i < outputs.size(); ++i) {
        const DXGI_OUTPUT_DESC& desc = outputs[i].desc;
        std::snprintf(line,
                      sizeof(line),
                      "[%zu] %ls %ldx%ld at (%ld,%ld)\n",
                      i,
                      desc.DeviceName,
                      desc.DesktopCoordinates.right - desc.DesktopCoordinates.left,
                      desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top,
                      desc.DesktopCoordinates.left,
                      desc.DesktopCoordinates.top);
        description += line;
    }
    return static_cast<uint32_t>(outputs.size());
}

bool DxgiCaptureSource::CreateDeviceForOutput(uint32_t display_index, std::string& error) {
    std::vector<OutputRef> outputs;
    if (!CollectOutputs(outputs, error)) {
        return false;
    }
    if (display_index >= outputs.size()) {
        error = "display index out of range";
        return false;
    }

    IDXGIAdapter1* adapter = outputs[display_index].adapter.Get();
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL achieved = D3D_FEATURE_LEVEL_11_0;

    HRESULT hr = D3D11CreateDevice(adapter,
                                   D3D_DRIVER_TYPE_UNKNOWN,
                                   nullptr,
                                   flags,
                                   levels,
                                   2,
                                   D3D11_SDK_VERSION,
                                   device_.GetAddressOf(),
                                   &achieved,
                                   context_.GetAddressOf());
    if (FAILED(hr)) {
        // 11_1 在部分系统上返回 E_INVALIDARG，退回 11_0 重试。
        hr = D3D11CreateDevice(adapter,
                               D3D_DRIVER_TYPE_UNKNOWN,
                               nullptr,
                               flags,
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

    hr = outputs[display_index].output->DuplicateOutput(device_.Get(), duplication_.GetAddressOf());
    if (FAILED(hr)) {
        if (hr == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE) {
            error = "duplication unavailable (another capture session holds the output)";
        } else if (hr == E_ACCESSDENIED) {
            error = "duplication denied (secure desktop or protected content)";
        } else {
            error = "DuplicateOutput failed";
        }
        return false;
    }

    DXGI_OUTDUPL_DESC dup_desc{};
    duplication_->GetDesc(&dup_desc);
    size_.width = static_cast<uint32_t>(dup_desc.ModeDesc.Width);
    size_.height = static_cast<uint32_t>(dup_desc.ModeDesc.Height);

    D3D11_TEXTURE2D_DESC texture_desc{};
    texture_desc.Width = size_.width;
    texture_desc.Height = size_.height;
    texture_desc.MipLevels = 1;
    texture_desc.ArraySize = 1;
    texture_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.Usage = D3D11_USAGE_DEFAULT;
    texture_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    hr = device_->CreateTexture2D(&texture_desc, nullptr, frame_texture_.GetAddressOf());
    if (FAILED(hr)) {
        error = "CreateTexture2D for frame copy failed";
        return false;
    }
    return true;
}

bool DxgiCaptureSource::Start(const CaptureRequest& request, std::string& error) {
    Stop();
    display_index_ = request.display_index;
    if (!CreateDeviceForOutput(display_index_, error)) {
        return false;
    }
    needs_rebuild_ = false;
    running_ = true;
    error.clear();
    return true;
}

void DxgiCaptureSource::Stop() {
    running_ = false;
    duplication_.Reset();
    frame_texture_.Reset();
    context_.Reset();
    device_.Reset();
}

bool DxgiCaptureSource::TryAcquire(VideoFrame& out) {
    if (!running_ || !duplication_) {
        return false;
    }

    DXGI_OUTDUPL_FRAME_INFO info{};
    ComPtr<IDXGIResource> resource;
    HRESULT hr = duplication_->AcquireNextFrame(0, &info, resource.GetAddressOf());

    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        return false;  // 桌面无变化，不是错误
    }
    if (hr == DXGI_ERROR_ACCESS_LOST) {
        needs_rebuild_ = true;
        running_ = false;
        return false;
    }
    if (FAILED(hr)) {
        needs_rebuild_ = true;
        return false;
    }

    ComPtr<ID3D11Texture2D> acquired;
    hr = resource->QueryInterface(__uuidof(ID3D11Texture2D),
                                  reinterpret_cast<void**>(acquired.GetAddressOf()));
    if (FAILED(hr)) {
        duplication_->ReleaseFrame();
        return false;
    }

    context_->CopyResource(frame_texture_.Get(), acquired.Get());
    duplication_->ReleaseFrame();

    out.native_texture = frame_texture_.Get();
    out.desc.pts = clock_.Now();
    out.desc.size = size_;
    out.desc.format = PixelFormat::Bgra8Unorm;
    out.desc.display_index = display_index_;
    out.desc.duplicated = false;
    out.frame_id = ++frame_id_;
    return true;
}

SourceCaps DxgiCaptureSource::Caps() const {
    SourceCaps caps;
    caps.max_size = size_;
    caps.max_fps = 0;  // 由显示器刷新率决定，运行时才知道
    // DXGI DD 不含指针；光标需由合成层自绘（02 文档 §4.2）。
    caps.supports_cursor_toggle = false;
    // Desktop Duplication 拿到的是桌面合成结果，被遮挡窗口无法单独采集。
    caps.supports_occlusion = false;
    caps.backend = "dxgi-dd";
    return caps;
}

}  // namespace sr::capture
