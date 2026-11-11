#pragma once

#include <cstdint>
#include <vector>

#include "core/interfaces/IAudioSource.h"
#include "core/types/IClock.h"

namespace sr::audio {

// 音频时间轴：把「源产出多少」转换成「时间轴上应该有多少」。
//
// 这是 M1-21 的实现核心。WASAPI Loopback 在系统静音期不产生任何数据包，
// 若按包驱动时间轴，音频会比视频短，长录制必然音画分离。因此这里以 QPC
// 为准计算「到此刻应得多少帧」，缺多少补多少静音，保证输出长度恒等于时间长度。
class AudioTimeline {
public:
    AudioTimeline(const IClock& clock, uint32_t sample_rate, uint16_t channels);

    // 按时间轴拉取一帧（内部会向 src 取数据，不足补 0）。
    // 返回写入的帧数；0 表示当前不需要新数据（调用方应稍后再来）。
    // 输出始终写入 out，长度为返回值 × channels，且已清零过剩余部分。
    uint32_t Pull(IAudioSource& src, float* out, uint32_t capacity_frames);

    void Reset();

    uint64_t ExpectedFrames() const { return expected_frames_; }
    uint64_t ProducedFrames() const { return produced_frames_; }
    uint64_t SilenceFilledFrames() const { return silence_filled_frames_; }
    uint64_t SourceGapCount() const { return source_gap_count_; }

private:
    const IClock& clock_;
    uint32_t sample_rate_;
    uint16_t channels_;
    Tick start_{};
    bool started_ = false;
    uint64_t expected_frames_ = 0;
    uint64_t produced_frames_ = 0;
    uint64_t silence_filled_frames_ = 0;
    uint64_t source_gap_count_ = 0;
    std::vector<float> scratch_;
};

}  // namespace sr::audio
