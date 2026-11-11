#include "compositor/Nv12Converter.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace sr::compositor {
namespace {

uint32_t MakeEven(uint32_t value) {
    return value & ~1u;
}

uint8_t ClampByte(int value) {
    if (value < 0) {
        return 0;
    }
    if (value > 255) {
        return 255;
    }
    return static_cast<uint8_t>(value);
}

}  // namespace

bool Nv12Converter::Initialize(ID3D11Device* device,
                               ID3D11DeviceContext* context,
                               uint32_t width,
                               uint32_t height,
                               std::string& error) {
    if (device == nullptr || context == nullptr) {
        error = "device or context is null";
        return false;
    }
    device_.Attach(device);
    context_.Attach(context);

    if (SUCCEEDED(device_->QueryInterface(__uuidof(ID3D11VideoDevice),
                                          reinterpret_cast<void**>(video_device_.GetAddressOf())))) {
        if (FAILED(context_->QueryInterface(__uuidof(ID3D11VideoContext),
                                            reinterpret_cast<void**>(video_context_.GetAddressOf())))) {
            video_device_.Reset();
        }
    }

    return Resize(width, height, error);
}

namespace {

// HRESULT → 十六进制文本（诊断用；失败信息要能直接看出 D3D11 返回了什么）。
std::string ToHex(HRESULT hr) {
    char buffer[16] = {};
    std::snprintf(buffer, sizeof(buffer), "%08lX", static_cast<unsigned long>(hr));
    return buffer;
}

}  // namespace

bool Nv12Converter::Resize(uint32_t width, uint32_t height, std::string& error) {
    width_ = MakeEven(width);
    height_ = MakeEven(height);
    if (width_ == 0 || height_ == 0) {
        error = "invalid frame size";
        return false;
    }
    stride_ = width_;

    nv12_texture_.Reset();
    nv12_staging_.Reset();
    bgra_staging_.Reset();
    enumerator_.Reset();
    processor_.Reset();
    gpu_available_ = false;

    if (video_device_ != nullptr && !force_cpu_) {
        D3D11_VIDEO_PROCESSOR_CONTENT_DESC content{};
        content.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
        content.InputWidth = width_;
        content.InputHeight = height_;
        content.OutputWidth = width_;
        content.OutputHeight = height_;
        content.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;

        if (SUCCEEDED(video_device_->CreateVideoProcessorEnumerator(
                &content, enumerator_.GetAddressOf())) &&
            SUCCEEDED(video_device_->CreateVideoProcessor(enumerator_.Get(), 0, processor_.GetAddressOf()))) {
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = width_;
            desc.Height = height_;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_NV12;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            // VideoProcessor 的输出纹理必须是 render target。
            desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

            if (SUCCEEDED(device_->CreateTexture2D(&desc, nullptr, nv12_texture_.GetAddressOf()))) {
                D3D11_TEXTURE2D_DESC staging = desc;
                staging.Usage = D3D11_USAGE_STAGING;
                staging.BindFlags = 0;
                staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                if (SUCCEEDED(device_->CreateTexture2D(&staging, nullptr, nv12_staging_.GetAddressOf()))) {
                    gpu_available_ = true;
                }
            }
        }
    }

    if (!gpu_available_) {
        // CPU 兜底：需要一个能承载任意来源的 BGRA staging 纹理。
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width_;
        desc.Height = height_;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(device_->CreateTexture2D(&desc, nullptr, bgra_staging_.GetAddressOf()))) {
            error = "failed to create BGRA staging texture";
            return false;
        }
    }

    using_gpu_ = gpu_available_;
    error.clear();
    return true;
}

bool Nv12Converter::ComputeDestRect(uint32_t source_width,
                                    uint32_t source_height,
                                    uint32_t& dest_left,
                                    uint32_t& dest_top,
                                    uint32_t& dest_width,
                                    uint32_t& dest_height) const {
    if (source_width == 0 || source_height == 0 || width_ == 0 || height_ == 0) {
        return false;
    }
    if (fit_mode_ == FitMode::Stretch) {
        dest_left = 0;
        dest_top = 0;
        dest_width = width_;
        dest_height = height_;
        return true;
    }
    // Pad：等比缩放后居中（不裁切、不变形，四周补黑边）。
    const double scale_x = static_cast<double>(width_) / static_cast<double>(source_width);
    const double scale_y = static_cast<double>(height_) / static_cast<double>(source_height);
    const double scale = scale_x < scale_y ? scale_x : scale_y;
    uint32_t scaled_width = static_cast<uint32_t>(static_cast<double>(source_width) * scale + 0.5);
    uint32_t scaled_height = static_cast<uint32_t>(static_cast<double>(source_height) * scale + 0.5);
    if (scaled_width == 0) {
        scaled_width = 1;
    }
    if (scaled_height == 0) {
        scaled_height = 1;
    }
    if (scaled_width > width_) {
        scaled_width = width_;
    }
    if (scaled_height > height_) {
        scaled_height = height_;
    }
    dest_width = scaled_width;
    dest_height = scaled_height;
    dest_left = (width_ - scaled_width) / 2;
    dest_top = (height_ - scaled_height) / 2;
    return true;
}

bool Nv12Converter::ConvertOnCpu(ID3D11Texture2D* source, std::string& error) {
    if (bgra_staging_ == nullptr || context_ == nullptr) { error = "CPU staging missing"; return false; }
    D3D11_TEXTURE2D_DESC source_desc{};
    source->GetDesc(&source_desc);
    // M1-08：源的尺寸可以与输出不同（窗口缩放），staging 必须跟源同尺寸才能 CopyResource。
    if (bgra_staging_desc_.Width != source_desc.Width || bgra_staging_desc_.Height != source_desc.Height) {
        bgra_staging_.Reset();
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = source_desc.Width;
        desc.Height = source_desc.Height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(device_->CreateTexture2D(&desc, nullptr, bgra_staging_.GetAddressOf()))) {
            error = "failed to (re)create BGRA staging texture";
            return false;
        }
        bgra_staging_desc_ = desc;
    }
    context_->CopyResource(bgra_staging_.Get(), source);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context_->Map(bgra_staging_.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        error = "map BGRA staging failed";
        return false;
    }

    const auto* base = static_cast<const uint8_t*>(mapped.pData);
    const uint32_t source_width = source_desc.Width;
    const uint32_t source_height = source_desc.Height;
    uint32_t dest_left = 0;
    uint32_t dest_top = 0;
    uint32_t dest_width = width_;
    uint32_t dest_height = height_;
    if (!ComputeDestRect(source_width, source_height, dest_left, dest_top, dest_width, dest_height)) {
        context_->Unmap(bgra_staging_.Get(), 0);
        error = "invalid source size for dest rect";
        return false;
    }

    // 复用 nv12_texture_ 作为 CPU 输出缓冲的载体不需要；直接写入调用方缓冲由 Convert 处理，
    // 这里把结果暂存在 GPU staging 前的临时位置：改用 bgra 数据直接算，写入 out。
    // 为了保持函数签名简单，这里把 NV12 结果写回 bgra_staging_ 之后的独立缓冲。
    static thread_local std::vector<uint8_t> scratch;
    scratch.assign(static_cast<size_t>(stride_) * height_ * 3 / 2, 0);

    uint8_t* y_plane = scratch.data();
    uint8_t* uv_plane = y_plane + static_cast<size_t>(stride_) * height_;

    // 先整幅填黑（BT.709 limited range 的黑 = Y 16 / UV 128），黑边由此而来。
    std::memset(y_plane, 16, static_cast<size_t>(stride_) * height_);
    std::memset(uv_plane, 128, static_cast<size_t>(stride_) * (height_ / 2));

    // 源坐标映射：目标像素 → 源像素（最近邻；M1-08 只要求「完整包含 + 不变形」）。
    const uint32_t dest_right = dest_left + dest_width;
    const uint32_t dest_bottom = dest_top + dest_height;
    for (uint32_t row = dest_top; row < dest_bottom; ++row) {
        const uint32_t source_row = (row - dest_top) * source_height / dest_height;
        const uint8_t* src_row = base + static_cast<size_t>(source_row) * mapped.RowPitch;
        uint8_t* dst_row = y_plane + static_cast<size_t>(row) * stride_;
        for (uint32_t col = dest_left; col < dest_right; ++col) {
            const uint32_t source_col = (col - dest_left) * source_width / dest_width;
            const uint8_t b = src_row[source_col * 4 + 0];
            const uint8_t g = src_row[source_col * 4 + 1];
            const uint8_t r = src_row[source_col * 4 + 2];
            // BT.709 有限范围的整数近似
            const int y = (47 * r + 157 * g + 16 * b + 128) >> 8;
            dst_row[col] = ClampByte(y + 16);
        }
    }

    for (uint32_t row = dest_top; row < dest_bottom; row += 2) {
        const uint32_t source_row0 = (row - dest_top) * source_height / dest_height;
        const uint32_t source_row1 = std::min(source_row0 + 1, source_height - 1);
        const uint8_t* src_row0 = base + static_cast<size_t>(source_row0) * mapped.RowPitch;
        const uint8_t* src_row1 = base + static_cast<size_t>(source_row1) * mapped.RowPitch;
        uint8_t* dst_row = uv_plane + static_cast<size_t>(row / 2) * stride_;
        for (uint32_t col = dest_left; col < dest_right; col += 2) {
            const uint32_t source_col = (col - dest_left) * source_width / dest_width;
            const uint32_t source_col1 = std::min(source_col + 1, source_width - 1);
            int sum_u = 0;
            int sum_v = 0;
            for (int dy = 0; dy < 2; ++dy) {
                const uint8_t* line = (dy == 0) ? src_row0 : src_row1;
                for (int dx = 0; dx < 2; ++dx) {
                    const uint32_t sample_col = (dx == 0) ? source_col : source_col1;
                    const uint8_t b = line[sample_col * 4 + 0];
                    const uint8_t g = line[sample_col * 4 + 1];
                    const uint8_t r = line[sample_col * 4 + 2];
                    sum_u += (-26 * r - 86 * g + 112 * b + 128) >> 8;
                    sum_v += (112 * r - 102 * g - 10 * b + 128) >> 8;
                }
            }
            dst_row[col] = ClampByte((sum_u / 4) + 128);
            dst_row[col + 1] = ClampByte((sum_v / 4) + 128);
        }
    }

    context_->Unmap(bgra_staging_.Get(), 0);

    // 通过成员间接把结果交回 Convert：这里直接拷贝到 nv12_cpu_。
    nv12_cpu_ = std::move(scratch);
    error.clear();
    return true;
}

// 轮 39（M1-08 · E2）：按**当前源尺寸**重建 VideoProcessor。
//
// E1（实验）：删掉自设护栏后、不重建处理器 —— VideoProcessorBlt 失败。
//   原因：VideoProcessor 创建时把输入尺寸写进 CONTENT_DESC.InputWidth/Height，
//   源尺寸变了以后处理器仍按旧输入尺寸工作。
// E2（本函数）：源尺寸变化时按新尺寸重建 enumerator + processor → Blt 成功，
//   缩放由 SetStreamSourceRect → SetStreamDestRect 完成。
bool Nv12Converter::EnsureVideoProcessor(uint32_t source_width,
                                        uint32_t source_height,
                                        std::string& error) {
    if (video_device_ == nullptr) {
        error = "video device unavailable";
        return false;
    }
    if (processor_.Get() != nullptr && enumerator_.Get() != nullptr &&
        processor_input_width_ == source_width && processor_input_height_ == source_height) {
        return true;  // 处理器已按当前源尺寸建好，无需重建。
    }

    enumerator_.Reset();
    processor_.Reset();

    D3D11_VIDEO_PROCESSOR_CONTENT_DESC content{};
    content.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    content.InputWidth = source_width;
    content.InputHeight = source_height;
    content.OutputWidth = width_;
    content.OutputHeight = height_;
    content.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;

    const HRESULT hr_enum = video_device_->CreateVideoProcessorEnumerator(
        &content, enumerator_.GetAddressOf());
    if (FAILED(hr_enum)) {
        error = "CreateVideoProcessorEnumerator failed hr=0x" + ToHex(hr_enum) +
                " input=" + std::to_string(source_width) + "x" + std::to_string(source_height) +
                " output=" + std::to_string(width_) + "x" + std::to_string(height_);
        enumerator_.Reset();
        return false;
    }
    const HRESULT hr_proc = video_device_->CreateVideoProcessor(
        enumerator_.Get(), 0, processor_.GetAddressOf());
    if (FAILED(hr_proc)) {
        error = "CreateVideoProcessor failed hr=0x" + ToHex(hr_proc) +
                " input=" + std::to_string(source_width) + "x" + std::to_string(source_height);
        enumerator_.Reset();
        processor_.Reset();
        return false;
    }
    processor_input_width_ = source_width;
    processor_input_height_ = source_height;
    return true;
}

bool Nv12Converter::ConvertOnGpu(ID3D11Texture2D* source, std::string& error) {
    D3D11_TEXTURE2D_DESC source_desc{};
    source->GetDesc(&source_desc);
    const uint32_t source_width = source_desc.Width;
    const uint32_t source_height = source_desc.Height;

    // M1-08：源的尺寸可以与输出不同（窗口缩放）。源矩形 = 源纹理全幅，
    // 目标矩形 = 按策略（拉伸 / 等比+黑边）算出的区域。
    // 缩放由 VideoProcessorSetStreamSourceRect → SetStreamDestRect 完成（D3D11 的
    // 缩放就是「源矩形 → 目标矩形」的映射；**不存在** VideoProcessorSetStreamScaling）。
    // 关于 CONTENT_DESC.InputWidth/Height：VideoProcessor 创建时把输入尺寸写了进去，
    // 所以本实现在源尺寸变化时**会**重建处理器（EnsureVideoProcessor 按尺寸比对后重建）——
    // 这是本代码的现状描述，**不是**「必须重建处理器才能缩放」的机制断言：
    // E1 实测（轮 39/40）证明缩放由 SetStreamDestRect 独立完成，窗口尺寸跟随
    // 不需要重建**采集源**（采集侧按帧重建帧池即可，见 WgcCaptureSource.cpp）。
    // 顺序很重要：input/output view 都依赖 enumerator_，所以先确保处理器是最新的。
    if (!EnsureVideoProcessor(source_width, source_height, error)) {
        return false;
    }

    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC input_desc{};
    input_desc.FourCC = 0;
    input_desc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    input_desc.Texture2D.MipSlice = 0;
    input_desc.Texture2D.ArraySlice = 0;

    ID3D11VideoProcessorInputView* input_view = nullptr;
    const HRESULT hr_input = video_device_->CreateVideoProcessorInputView(
        source, enumerator_.Get(), &input_desc, &input_view);
    if (FAILED(hr_input)) {
        error = "CreateVideoProcessorInputView failed hr=0x" + ToHex(hr_input) +
                " source=" + std::to_string(source_width) + "x" + std::to_string(source_height) +
                " processor_input=" + std::to_string(processor_input_width_) + "x" +
                std::to_string(processor_input_height_) +
                " output=" + std::to_string(width_) + "x" + std::to_string(height_);
        return false;
    }

    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC output_desc{};
    output_desc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    output_desc.Texture2D.MipSlice = 0;

    ID3D11VideoProcessorOutputView* output_view = nullptr;
    if (FAILED(video_device_->CreateVideoProcessorOutputView(
            nv12_texture_.Get(), enumerator_.Get(), &output_desc, &output_view))) {
        input_view->Release();
        error = "CreateVideoProcessorOutputView failed";
        return false;
    }

    D3D11_VIDEO_PROCESSOR_STREAM stream{};
    stream.Enable = TRUE;
    stream.pInputSurface = input_view;

    uint32_t dest_left = 0;
    uint32_t dest_top = 0;
    uint32_t dest_width = width_;
    uint32_t dest_height = height_;
    if (!ComputeDestRect(source_width, source_height,
                         dest_left, dest_top, dest_width, dest_height)) {
        input_view->Release();
        error = "invalid source size for dest rect";
        return false;
    }
    RECT source_rect{};
    source_rect.left = 0;
    source_rect.top = 0;
    source_rect.right = static_cast<LONG>(source_width);
    source_rect.bottom = static_cast<LONG>(source_height);
    RECT dest_rect{};
    dest_rect.left = static_cast<LONG>(dest_left);
    dest_rect.top = static_cast<LONG>(dest_top);
    dest_rect.right = static_cast<LONG>(dest_left + dest_width);
    dest_rect.bottom = static_cast<LONG>(dest_top + dest_height);
    video_context_->VideoProcessorSetStreamFrameFormat(processor_.Get(), 0,
                                                       D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
    video_context_->VideoProcessorSetStreamSourceRect(processor_.Get(), 0, TRUE, &source_rect);
    video_context_->VideoProcessorSetStreamDestRect(processor_.Get(), 0, TRUE, &dest_rect);

    const HRESULT hr = video_context_->VideoProcessorBlt(processor_.Get(), output_view, 0, 1, &stream);
    output_view->Release();
    input_view->Release();
    if (FAILED(hr)) {
        error = "VideoProcessorBlt failed hr=0x" + ToHex(hr) +
                " source=" + std::to_string(source_width) + "x" + std::to_string(source_height) +
                " processor_input=" + std::to_string(processor_input_width_) + "x" +
                std::to_string(processor_input_height_) +
                " output=" + std::to_string(width_) + "x" + std::to_string(height_) +
                " dest=" + std::to_string(dest_left) + "," + std::to_string(dest_top) + "," +
                std::to_string(dest_width) + "x" + std::to_string(dest_height);
        return false;
    }

    context_->CopyResource(nv12_staging_.Get(), nv12_texture_.Get());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context_->Map(nv12_staging_.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        error = "map NV12 staging failed";
        return false;
    }
    nv12_cpu_.resize(static_cast<size_t>(stride_) * height_ * 3 / 2);
    const auto* base = static_cast<const uint8_t*>(mapped.pData);
    const size_t y_size = static_cast<size_t>(stride_) * height_;
    for (uint32_t row = 0; row < height_; ++row) {
        std::memcpy(nv12_cpu_.data() + static_cast<size_t>(row) * stride_,
                    base + static_cast<size_t>(row) * mapped.RowPitch,
                    stride_);
    }
    for (uint32_t row = 0; row < height_ / 2; ++row) {
        std::memcpy(nv12_cpu_.data() + y_size + static_cast<size_t>(row) * stride_,
                    base + static_cast<size_t>(height_ + row) * mapped.RowPitch,
                    stride_);
    }
    context_->Unmap(nv12_staging_.Get(), 0);

    // M1-08#2：黑边。VideoProcessor 只写目标矩形，其余区域在 CPU 侧的 NV12 缓冲里补黑
    // （BT.709 limited range 的黑 = Y 16 / UV 128）。编码器吃的就是这份系统内存 ✓。
    if (dest_left > 0 || dest_top > 0 || dest_width < width_ || dest_height < height_) {
        uint8_t* y_plane = nv12_cpu_.data();
        uint8_t* uv_plane = y_plane + y_size;
        const uint32_t dest_right = dest_left + dest_width;
        const uint32_t dest_bottom = dest_top + dest_height;
        for (uint32_t row = 0; row < height_; ++row) {
            uint8_t* dst_row = y_plane + static_cast<size_t>(row) * stride_;
            if (row < dest_top || row >= dest_bottom) {
                std::memset(dst_row, 16, stride_);
                continue;
            }
            if (dest_left > 0) {
                std::memset(dst_row, 16, dest_left);
            }
            if (dest_right < width_) {
                std::memset(dst_row + dest_right, 16, width_ - dest_right);
            }
        }
        for (uint32_t row = 0; row < height_ / 2; ++row) {
            uint8_t* dst_row = uv_plane + static_cast<size_t>(row) * stride_;
            const uint32_t source_row = row * 2;
            if (source_row < dest_top || source_row >= dest_bottom) {
                std::memset(dst_row, 128, stride_);
                continue;
            }
            if (dest_left > 0) {
                std::memset(dst_row, 128, dest_left);
            }
            if (dest_right < width_) {
                std::memset(dst_row + dest_right, 128, width_ - dest_right);
            }
        }
    }
    error.clear();
    return true;
}

bool Nv12Converter::Convert(ID3D11Texture2D* source, std::vector<uint8_t>& out, std::string& error) {
    if (source == nullptr) {
        error = "source texture is null";
        return false;
    }

    bool ok = false;
    if (gpu_available_) {
        ok = ConvertOnGpu(source, error);
        if (!ok) {
            // 运行期 GPU 失败（驱动重置、VideoProcessor 不支持该格式等）→ 永久降级到 CPU。
            // 必须重建资源：CPU 路径依赖 bgra_staging_，而它在 GPU 模式下从未创建。
            const std::string gpu_error = error;
            std::fprintf(stderr, "[convert] GPU path failed (%s), falling back to CPU\n", gpu_error.c_str());
            gpu_available_ = false;
            using_gpu_ = false;
            force_cpu_ = true;
            std::string resize_error;
            if (!Resize(width_, height_, resize_error)) {
                error = gpu_error + "; " + resize_error;
                return false;
            }
            error = gpu_error;
        }
    }
    if (!ok) {
        ok = ConvertOnCpu(source, error);
    }
    if (!ok) {
        return false;
    }

    out = nv12_cpu_;
    return true;
}

}  // namespace sr::compositor
