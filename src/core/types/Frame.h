#pragma once

#include <cstdint>

namespace sr {

// 像素格式。core 层只做语义声明，具体解释权在实现层。
enum class PixelFormat : uint8_t {
    Unknown = 0,
    Bgra8Unorm,
    Nv12,
    P010,
};

struct Size {
    uint32_t width = 0;
    uint32_t height = 0;
};

struct Rect {
    int32_t x = 0;
    int32_t y = 0;
    int32_t width = 0;
    int32_t height = 0;
};

// QPC 单调时钟刻度。value == 0 表示「无有效时间戳」。
struct Tick {
    int64_t value = 0;

    constexpr bool Valid() const noexcept { return value > 0; }
    friend constexpr bool operator<(Tick a, Tick b) noexcept { return a.value < b.value; }
    friend constexpr bool operator==(Tick a, Tick b) noexcept { return a.value == b.value; }
};

struct VideoFrameDesc {
    Tick pts{};
    Size size{};
    PixelFormat format = PixelFormat::Unknown;
    uint32_t display_index = 0;
    // true 表示这是为补齐时间轴而复制的上一帧（见 02 文档 §6.3 负漂移分支）。
    bool duplicated = false;
};

// 采集层产出的帧。native_texture 由采集实现解释，core 层不得解引用。
struct VideoFrame {
    void* native_texture = nullptr;
    VideoFrameDesc desc{};
    uint64_t frame_id = 0;
};

struct AudioBlockDesc {
    // 会话内累计样本序号，用于推导 audio_clock（02 文档 §6.2）。
    int64_t first_sample = 0;
    uint32_t sample_rate = 48000;
    uint16_t channels = 2;
    uint32_t frame_count = 0;  // 每声道样本数
};

// 音频块。samples 指向交错 float32，生命周期由提供方持有。
struct AudioBlock {
    const float* samples = nullptr;
    AudioBlockDesc desc{};
};

inline uint32_t BytesPerSample(PixelFormat) noexcept {
    // 仅用于容量估算，不参与像素解释。
    return 4;
}

}  // namespace sr
