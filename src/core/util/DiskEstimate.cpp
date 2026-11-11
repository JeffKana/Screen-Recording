#include "core/util/DiskEstimate.h"

namespace sr::util {

double EstimateRecordSeconds(uint64_t free_bytes,
                             uint64_t stop_bytes,
                             uint32_t total_bitrate_kbps) noexcept {
    if (free_bytes <= stop_bytes || total_bitrate_kbps == 0) {
        return 0.0;
    }
    const uint64_t usable = free_bytes - stop_bytes;
    const double bytes_per_second = static_cast<double>(total_bitrate_kbps) * 1000.0 / 8.0;
    return static_cast<double>(usable) / bytes_per_second;
}

}  // namespace sr::util
