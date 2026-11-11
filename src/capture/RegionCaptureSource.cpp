#include "capture/RegionCaptureSource.h"

namespace sr::capture {
namespace {

uint32_t MakeEven(uint32_t value) {
    return (value == 0) ? 0 : (value & ~1u);
}

}  // namespace

RegionCaptureSource::RegionCaptureSource(const IClock& clock)
    : inner_(std::make_unique<DxgiCaptureSource>(clock)) {}

RegionCaptureSource::~RegionCaptureSource() {
    Stop();
}

bool RegionCaptureSource::Start(const CaptureRequest& request, std::string& error) {
    Stop();

    CaptureRequest display_request = request;
    display_request.kind = CaptureKind::Display;
    if (!inner_->Start(display_request, error)) {
        return false;
    }

    const Size screen = inner_->Caps().max_size;

    // 裁剪到屏幕内，再按 YUV420 要求做偶数对齐（M3-02）。
    int32_t x = request.region.x;
    int32_t y = request.region.y;
    int32_t w = request.region.width;
    int32_t h = request.region.height;
    if (w <= 0 || h <= 0) {
        x = 0;
        y = 0;
        w = static_cast<int32_t>(screen.width);
        h = static_cast<int32_t>(screen.height);
    }
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + w > static_cast<int32_t>(screen.width)) {
        w = static_cast<int32_t>(screen.width) - x;
    }
    if (y + h > static_cast<int32_t>(screen.height)) {
        h = static_cast<int32_t>(screen.height) - y;
    }

    const uint32_t even_w = MakeEven(static_cast<uint32_t>(w));
    const uint32_t even_h = MakeEven(static_cast<uint32_t>(h));
    adjusted_ = (even_w != static_cast<uint32_t>(w)) || (even_h != static_cast<uint32_t>(h));

    region_.x = x;
    region_.y = y;
    region_.width = static_cast<int32_t>(even_w);
    region_.height = static_cast<int32_t>(even_h);
    size_.width = even_w;
    size_.height = even_h;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = size_.width;
    desc.Height = size_.height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    const HRESULT hr = inner_->Device()->CreateTexture2D(&desc, nullptr, region_texture_.GetAddressOf());
    if (FAILED(hr)) {
        error = "CreateTexture2D for region failed";
        Stop();
        return false;
    }

    running_ = true;
    error.clear();
    return true;
}

void RegionCaptureSource::Stop() {
    running_ = false;
    region_texture_.Reset();
    if (inner_) {
        inner_->Stop();
    }
}

bool RegionCaptureSource::IsRunning() const {
    return running_ && inner_ && inner_->IsRunning();
}

bool RegionCaptureSource::TryAcquire(VideoFrame& out) {
    if (!running_ || !inner_) {
        return false;
    }

    VideoFrame full;
    if (!inner_->TryAcquire(full)) {
        return false;
    }

    D3D11_BOX box{};
    box.left = static_cast<UINT>(region_.x);
    box.top = static_cast<UINT>(region_.y);
    box.front = 0;
    box.right = static_cast<UINT>(region_.x + region_.width);
    box.bottom = static_cast<UINT>(region_.y + region_.height);
    box.back = 1;

    ID3D11Texture2D* source = static_cast<ID3D11Texture2D*>(full.native_texture);
    inner_->Context()->CopySubresourceRegion(region_texture_.Get(), 0, 0, 0, 0, source, 0, &box);

    // 时间戳沿用底层帧，保证区域与全屏在同一时间轴上，不做二次取时。
    out = full;
    out.native_texture = region_texture_.Get();
    out.desc.size = size_;
    return true;
}

SourceCaps RegionCaptureSource::Caps() const {
    SourceCaps caps = inner_ ? inner_->Caps() : SourceCaps{};
    caps.max_size = size_;
    caps.backend = "dxgi-dd-region";
    return caps;
}

bool RegionCaptureSource::NeedsRebuild() const {
    return inner_ && inner_->NeedsRebuild();
}

}  // namespace sr::capture
