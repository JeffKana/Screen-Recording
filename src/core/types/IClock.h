#pragma once

#include "core/types/Frame.h"

namespace sr {

// 时间基准抽象。core 层只认这个接口，实现放 src/platform/win（对应的就是 QPC）。
// 这是 M6-13「新增平台仅需实现接口」的具体落点：换平台只需换实现。
class IClock {
public:
    virtual ~IClock() = default;

    virtual Tick Now() const = 0;

    // 每秒刻度数，用于 Tick <-> 毫秒 换算。
    virtual int64_t TicksPerSecond() const = 0;

    int64_t ElapsedMs(Tick from, Tick to) const {
        const int64_t hz = TicksPerSecond();
        if (hz <= 0) {
            return 0;
        }
        return (to.value - from.value) * 1000 / hz;
    }
};

}  // namespace sr
