#pragma once

#include <cstdint>
#include <vector>

namespace sr::mux {

// 轮 48（M3-09#3）：封装格式只影响封装器选择，不进状态机、不进采集/编码链路。
// 默认 Mkv —— 轮 48 卡 §P0-2 判据 5 要求「不带 --format 时逐字节行为不变」。
enum class ContainerFormat {
    Mkv,
    Mp4,
};

// MKV 与 MP4 共用同一份轨道描述：视频的 codec_private 是
// AVCDecoderConfigurationRecord（avcC），音频的是 AudioSpecificConfig。
// 两种容器正好都直接用这两个 blob，不需要各自转换。
struct VideoTrackInfo {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t fps = 30;
    std::vector<uint8_t> codec_private;
};

struct AudioTrackInfo {
    uint32_t sample_rate = 48000;
    uint16_t channels = 2;
    std::vector<uint8_t> codec_private;
};

}  // namespace sr::mux
