#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "core/types/Frame.h"

namespace sr {

enum class VideoCodec : uint8_t {
    H264 = 0,
    H265,
    Av1,
};

enum class RateControlMode : uint8_t {
    Cbr = 0,  // M3-04
    Vbr,
    Crf,  // M3-13
};

struct VideoEncoderConfig {
    VideoCodec codec = VideoCodec::H264;
    RateControlMode rate_control = RateControlMode::Vbr;
    uint32_t width = 1920;
    uint32_t height = 1080;
    uint32_t fps = 30;
    uint32_t bitrate_kbps = 8000;
    uint32_t max_bitrate_kbps = 12000;   // VBR 峰值上限
    uint32_t crf = 23;
    // 画质档位 0~100；0 表示按模式取默认（CBR 70 / VBR 80）。D-53 的「画质上限」。
    uint32_t quality = 0;
    uint32_t keyframe_interval_sec = 2;  // M3-11
    PixelFormat input_format = PixelFormat::Nv12;
    // 空字符串表示「由实现自动探测」，对应 M3-05。
    std::string preferred_encoder;
    // 注入：强制所有 MFT 创建失败，用于验证 M8-08 的「全部不可用」真实分支。
    bool fail_all_encoders = false;
};

struct AudioEncoderConfig {
    uint32_t sample_rate = 48000;  // M3-12
    uint16_t channels = 2;
    uint32_t bitrate_kbps = 192;
};

// 编码器能力查询结果（M3-05：参数面板必须以此为准，避免「界面可选但开录失败」）。
struct EncoderCaps {
    const char* name = "none";
    uint32_t max_width = 0;
    uint32_t max_height = 0;
    uint32_t max_fps = 0;
    uint32_t max_bitrate_kbps = 0;
    bool hardware = false;
    bool supports_hevc = false;
    bool supports_av1 = false;
    bool supports_10bit = false;  // M3-16
    bool accepts_gpu_texture = true;
};

// 编码后的包。data 生命周期止于下一次 TryReceive 或 Close。
struct EncodedPacket {
    const uint8_t* data = nullptr;
    size_t size = 0;
    Tick pts{};
    Tick dts{};
    bool keyframe = false;
    bool is_audio = false;
};

}  // namespace sr
