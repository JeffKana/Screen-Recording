#pragma once

#include <string>

#include "core/types/Frame.h"

namespace sr {

// 音频采集抽象。实现：WASAPI Loopback（系统声）与 WASAPI Capture（麦克风）。
class IAudioSource {
public:
    virtual ~IAudioSource() = default;

    virtual bool Start(std::string& error) = 0;

    virtual void Stop() = 0;

    virtual bool IsRunning() const = 0;

    // 非阻塞读取一块样本。返回 false 表示暂无数据。
    // 实现内部持有缓冲，samples 指针仅在下一次 TryRead 前有效。
    virtual bool TryRead(AudioBlock& out) = 0;

    // 静音期是否可能完全不产出数据包。
    // WASAPI Loopback 必须返回 true —— 这是 M1-21 静音填充逻辑的触发依据。
    virtual bool MayStarveWhenSilent() const = 0;

    // 设备被动变更（默认设备切换、被拔出）时置位，由上层重建流并补齐时间戳（M8-11）。
    virtual bool NeedsRebuild() const = 0;
};

}  // namespace sr
