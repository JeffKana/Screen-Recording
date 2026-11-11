#include "audio/FloatMixer.h"

#include <cmath>
#include <cstring>

namespace sr::audio {

void FloatMixer::SetInputCount(uint32_t input_count) {
    input_count_ = (input_count == 0) ? 1 : (input_count > kMaxInputs ? kMaxInputs : input_count);
}

void FloatMixer::SetGain(uint32_t input_index, float gain) {
    if (input_index >= kMaxInputs) {
        return;
    }
    if (gain < 0.0f) {
        gain = 0.0f;
    }
    if (gain > 2.0f) {
        gain = 2.0f;
    }
    gains_[input_index] = gain;
}

void FloatMixer::SetMuted(uint32_t input_index, bool muted) {
    if (input_index < kMaxInputs) {
        muted_[input_index] = muted;
    }
}

float FloatMixer::Gain(uint32_t input_index) const {
    return input_index < kMaxInputs ? gains_[input_index] : 0.0f;
}

bool FloatMixer::IsMuted(uint32_t input_index) const {
    return input_index < kMaxInputs ? muted_[input_index] : true;
}

uint32_t FloatMixer::Mix(const AudioBlock* const* inputs,
                         uint32_t input_count,
                         float* out,
                         uint32_t out_capacity_samples) {
    clipped_ = false;
    if (out == nullptr || inputs == nullptr) {
        return 0;
    }
    if (input_count > kMaxInputs) {
        input_count = kMaxInputs;
    }

    // 通道数取第一个有效输入；样本数取所有有效输入的最小值，避免越界读。
    uint16_t channels = 0;
    uint32_t frames = 0;
    for (uint32_t i = 0; i < input_count; ++i) {
        const AudioBlock* block = inputs[i];
        if (block == nullptr || block->samples == nullptr || block->desc.channels == 0) {
            continue;
        }
        if (channels == 0) {
            channels = block->desc.channels;
            frames = block->desc.frame_count;
        } else if (block->desc.channels == channels && block->desc.frame_count < frames) {
            frames = block->desc.frame_count;
        }
    }
    if (channels == 0 || frames == 0) {
        return 0;
    }

    const uint32_t samples = frames * static_cast<uint32_t>(channels);
    if (samples > out_capacity_samples) {
        frames = out_capacity_samples / channels;
        if (frames == 0) {
            return 0;
        }
    }

    std::memset(out, 0, frames * channels * sizeof(float));

    for (uint32_t i = 0; i < input_count; ++i) {
        const AudioBlock* block = inputs[i];
        if (block == nullptr || block->samples == nullptr || block->desc.channels != channels) {
            continue;
        }
        if (muted_[i]) {
            continue;
        }
        const float gain = gains_[i];
        if (gain <= 0.0f) {
            continue;
        }
        for (uint32_t s = 0; s < frames * channels; ++s) {
            out[s] += block->samples[s] * gain;
        }
    }

    // 限幅：混音后越界会让播放端直接爆音，这里硬钳并在统计里可观测。
    const uint32_t total = frames * channels;
    for (uint32_t s = 0; s < total; ++s) {
        if (out[s] > 1.0f) {
            out[s] = 1.0f;
            clipped_ = true;
        } else if (out[s] < -1.0f) {
            out[s] = -1.0f;
            clipped_ = true;
        }
    }

    return total;
}

}  // namespace sr::audio
