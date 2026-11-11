#include "platform/win/QpcClock.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace sr::win {

QpcClock::QpcClock() {
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    frequency_ = frequency.QuadPart;
    if (frequency_ <= 0) {
        frequency_ = 1;
    }
    LARGE_INTEGER origin{};
    QueryPerformanceCounter(&origin);
    origin_ = origin.QuadPart;
}

Tick QpcClock::Now() const {
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    Tick tick;
    // 以构造时刻为原点，保证 Tick::Valid() 在会话开始后成立，且仍保持单调。
    tick.value = counter.QuadPart - origin_ + 1;
    return tick;
}

int64_t QpcClock::TicksPerSecond() const {
    return frequency_;
}

}  // namespace sr::win
