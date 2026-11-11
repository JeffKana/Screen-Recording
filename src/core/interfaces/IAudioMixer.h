#pragma once

#include <cstdint>

#include "core/types/Frame.h"

namespace sr {

// 多路混音抽象。单轨混音（M1-18）与双轨保留（M1-19）是同一实现的两个输出模式。
// 契约：调用方必须保证各输入样本数一致；缺包由调用方先用静音补齐，
//       混音器本身不做时间轴对齐（它不知道时钟）。
class IAudioMixer {
public:
    virtual ~IAudioMixer() = default;

    // 1 = 混合为单条音轨；2 = 两路各自独立输出。
    virtual void SetInputCount(uint32_t input_count) = 0;

    // gain 取值 0.0 ~ 2.0（对应 M1-20 的 0~200%）。
    virtual void SetGain(uint32_t input_index, float gain) = 0;

    virtual void SetMuted(uint32_t input_index, bool muted) = 0;

    virtual float Gain(uint32_t input_index) const = 0;

    virtual bool IsMuted(uint32_t input_index) const = 0;

    // inputs 中允许出现 nullptr（该路本块无数据，按静音处理）。
    // out 为交错 float32 缓冲，容量按 out_capacity_samples（总样本数）给出。
    // 返回实际写入的样本总数；0 表示容量不足或无可混音输入。
    virtual uint32_t Mix(const AudioBlock* const* inputs,
                         uint32_t input_count,
                         float* out,
                         uint32_t out_capacity_samples) = 0;

    // 混音后是否发生了限幅（削波保护）。用于诊断与 M7 的音量提示。
    virtual bool LastBlockClipped() const = 0;
};

}  // namespace sr
