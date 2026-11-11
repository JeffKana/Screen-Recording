#pragma once

#include "core/interfaces/IAudioMixer.h"

namespace sr::audio {

// float32 混音器。内部只处理一种格式（03 文档 §5.1 第 3 条），
// 时间轴对齐不在这里做 —— 它不知道时钟，静音补齐由 AudioTimeline 负责。
class FloatMixer final : public IAudioMixer {
public:
    static constexpr uint32_t kMaxInputs = 4;

    void SetInputCount(uint32_t input_count) override;
    void SetGain(uint32_t input_index, float gain) override;
    void SetMuted(uint32_t input_index, bool muted) override;
    float Gain(uint32_t input_index) const override;
    bool IsMuted(uint32_t input_index) const override;

    uint32_t Mix(const AudioBlock* const* inputs,
                 uint32_t input_count,
                 float* out,
                 uint32_t out_capacity_samples) override;

    bool LastBlockClipped() const override { return clipped_; }

private:
    uint32_t input_count_ = 1;
    float gains_[kMaxInputs] = {1.0f, 1.0f, 1.0f, 1.0f};
    bool muted_[kMaxInputs] = {false, false, false, false};
    bool clipped_ = false;
};

}  // namespace sr::audio
