#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/types/Frame.h"
#include "platform/ComPtr.h"

#include <d3d11.h>
#include <d3d11_1.h>

namespace sr::compositor {

using win::ComPtr;

// 源尺寸与输出尺寸不一致时的适配策略（M1-08#2）。
//   Stretch：把源拉伸铺满输出（会变形，不留黑边）。
//   Pad：等比缩放后居中，四周补黑边（不裁切、不变形）。
enum class FitMode : uint8_t {
    Stretch = 0,
    Pad = 1,
};

// BGRA → NV12 转换（02 文档 §4.1「合成 → 转 NV12」）。
//
// 主路径用 D3D11 VideoProcessor 在 GPU 完成缩放与色彩转换；若该设备的 VideoProcessor
// 不可用（老显卡/驱动裁剪），退回 CPU 转换。两条路径都输出 CPU 可读的 NV12 缓冲，
// 因为 G3 阶段的编码器（Media Foundation 软编 MFT）只吃系统内存。
// 硬编零拷贝（纹理直入 NVENC/oneVPL）需要厂商 SDK，本环境不可用，见台账 D-11。
class Nv12Converter {
public:
    bool Initialize(ID3D11Device* device,
                    ID3D11DeviceContext* context,
                    uint32_t width,
                    uint32_t height,
                    std::string& error);

    // 尺寸变化时重建资源（M8-10）。
    bool Resize(uint32_t width, uint32_t height, std::string& error);

    bool Convert(ID3D11Texture2D* source, std::vector<uint8_t>& out, std::string& error);

    // M1-08#2：适配策略。默认 Stretch（与历史行为一致：源与输出同尺寸时两者等价）。
    void SetFitMode(FitMode mode) { fit_mode_ = mode; }
    FitMode FitModeValue() const { return fit_mode_; }

    // M6-08#2：令**既有** CPU 中转路径可达（混合显卡跨 GPU 拷贝不可避让时的兜底）。
    // 只设置运行期降级用的同一个标志，不新建转换算法；必须在 Initialize/Resize 之前调用。
    void ForceCpu() { force_cpu_ = true; }

    // M1-08：源尺寸变化时不需要重建输出资源——输出尺寸固定，内容按策略缩放。
    // 返回 false 表示源尺寸无效。
    bool ComputeDestRect(uint32_t source_width,
                         uint32_t source_height,
                         uint32_t& dest_left,
                         uint32_t& dest_top,
                         uint32_t& dest_width,
                         uint32_t& dest_height) const;

    uint32_t Width() const { return width_; }
    uint32_t Height() const { return height_; }
    uint32_t Stride() const { return stride_; }
    bool UsingGpu() const { return using_gpu_; }

private:
    bool CreateGpuResources(std::string& error);
    bool ConvertOnGpu(ID3D11Texture2D* source, std::string& error);
    bool ConvertOnCpu(ID3D11Texture2D* source, std::string& error);
    // 轮 39（M1-08 · E2）：VideoProcessor 在创建时把输入尺寸写进
    // D3D11_VIDEO_PROCESSOR_CONTENT_DESC.InputWidth/Height。源尺寸变了（窗口缩放）后，
    // 必须按**新的源尺寸**重建 enumerator + processor，否则 VideoProcessorBlt 失败。
    // 尺寸一致时直接复用，不做无谓重建。
    bool EnsureVideoProcessor(uint32_t source_width, uint32_t source_height, std::string& error);

    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<ID3D11VideoDevice> video_device_;
    ComPtr<ID3D11VideoContext> video_context_;
    ComPtr<ID3D11VideoProcessorEnumerator> enumerator_;
    ComPtr<ID3D11VideoProcessor> processor_;
    ComPtr<ID3D11Texture2D> nv12_texture_;
    ComPtr<ID3D11Texture2D> nv12_staging_;
    ComPtr<ID3D11Texture2D> bgra_staging_;
    D3D11_TEXTURE2D_DESC bgra_staging_desc_{};

    std::vector<uint8_t> nv12_cpu_;

    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t stride_ = 0;
    // 当前 VideoProcessor 绑定的输入尺寸（= 创建时的源尺寸）。
    uint32_t processor_input_width_ = 0;
    uint32_t processor_input_height_ = 0;
    FitMode fit_mode_ = FitMode::Stretch;
    bool force_cpu_ = false;
    bool using_gpu_ = false;
    bool gpu_available_ = false;
};

}  // namespace sr::compositor
