#pragma once

#include "core/types/IClock.h"

namespace sr::win {

// QueryPerformanceCounter 实现。全链路唯一时间源（02 文档 §6.1）。
// 之所以把它放在 platform 层而不是 core：core 不允许依赖任何平台头（CONTRACT.md §3）。
class QpcClock final : public IClock {
public:
    QpcClock();

    Tick Now() const override;

    int64_t TicksPerSecond() const override;

private:
    int64_t frequency_ = 0;
    int64_t origin_ = 0;
};

}  // namespace sr::win
