#include "audio/AudioTimeline.h"

#include <algorithm>
#include <cstring>

namespace sr::audio {

AudioTimeline::AudioTimeline(const IClock& clock, uint32_t sample_rate, uint16_t channels)
    : clock_(clock), sample_rate_(sample_rate == 0 ? 48000 : sample_rate), channels_(channels == 0 ? 2 : channels) {}

void AudioTimeline::Reset() {
    start_ = Tick{};
    started_ = false;
    expected_frames_ = 0;
    produced_frames_ = 0;
    silence_filled_frames_ = 0;
    source_gap_count_ = 0;
    scratch_.clear();
}

uint32_t AudioTimeline::Pull(IAudioSource& src, float* out, uint32_t capacity_frames) {
    if (out == nullptr || capacity_frames == 0) {
        return 0;
    }

    // 第一次调用只建立时间原点，不产出数据，避免把「从 0 到现在的全部时间」一次性补齐。
    if (!started_) {
        start_ = clock_.Now();
        started_ = true;
        return 0;
    }

    const int64_t elapsed_ms = clock_.ElapsedMs(start_, clock_.Now());
    if (elapsed_ms <= 0) {
        return 0;
    }

    expected_frames_ = static_cast<uint64_t>(elapsed_ms) * sample_rate_ / 1000;
    if (expected_frames_ <= produced_frames_) {
        return 0;
    }

    uint64_t need = expected_frames_ - produced_frames_;
    if (need > capacity_frames) {
        need = capacity_frames;
    }
    const uint32_t frames_needed = static_cast<uint32_t>(need);
    const uint32_t samples_needed = frames_needed * channels_;

    std::memset(out, 0, samples_needed * sizeof(float));

    // 只取一次源数据。源在静音期可能什么都不给，此时整块都是静音。
    AudioBlock block;
    uint32_t filled_frames = 0;
    if (src.TryRead(block) && block.samples != nullptr && block.desc.channels > 0) {
        const uint32_t source_frames = block.desc.frame_count;
        const uint16_t source_channels = block.desc.channels;
        const uint32_t copy_frames = std::min(source_frames, frames_needed);

        if (source_channels == channels_) {
            std::memcpy(out, block.samples, static_cast<size_t>(copy_frames) * channels_ * sizeof(float));
        } else {
            // 声道数不一致（如源是单声道）时就近映射，不做重采样。
            const uint32_t min_channels = std::min<uint32_t>(source_channels, channels_);
            for (uint32_t f = 0; f < copy_frames; ++f) {
                for (uint32_t c = 0; c < channels_; ++c) {
                    const uint32_t src_c = std::min<uint32_t>(c, min_channels - 1);
                    out[f * channels_ + c] = block.samples[f * source_channels + src_c];
                }
            }
        }
        filled_frames = copy_frames;
        if (copy_frames < frames_needed) {
            ++source_gap_count_;
        }
    } else {
        ++source_gap_count_;
    }

    silence_filled_frames_ += (frames_needed - filled_frames);
    produced_frames_ += frames_needed;
    return frames_needed;
}

}  // namespace sr::audio
