#include "recorder/StorageGuard.h"

#include <filesystem>
#include <utility>

#include "core/util/DiskEstimate.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace sr::recorder {

StorageGuard::StorageGuard(std::string path_on_target_volume, uint64_t warn_bytes, uint64_t stop_bytes)
    : path_(std::move(path_on_target_volume)),
      warn_bytes_(warn_bytes),
      stop_bytes_(stop_bytes) {}

bool StorageGuard::Refresh(std::string& error) {
    if (path_.empty()) {
        error = "empty path";
        return false;
    }

    // GetDiskFreeSpaceEx 只接受**目录**；直接传文件路径会返回 ERROR_DIRECTORY。
    // 因此先取父目录，输出目录可能还没创建时继续向上找。
    std::filesystem::path probe(path_);
    probe = probe.parent_path();
    std::error_code ec;
    while (!probe.empty() && !std::filesystem::is_directory(probe, ec)) {
        probe = probe.parent_path();
        ec.clear();
    }
    if (probe.empty()) {
        error = "no existing parent directory for " + path_;
        return false;
    }

    ULARGE_INTEGER available{};
    ULARGE_INTEGER total{};
    ULARGE_INTEGER total_free{};
    if (!GetDiskFreeSpaceExW(probe.wstring().c_str(), &available, &total, &total_free)) {
        error = "GetDiskFreeSpaceEx failed";
        return false;
    }

    // 用 available（当前进程可用的配额）而不是 total_free：
    // 磁盘配额或受控文件夹访问下两者可能不同，前者才是能写进去的量。
    free_bytes_ = available.QuadPart;
    valid_ = true;
    error.clear();
    return true;
}

StorageGuard::Status StorageGuard::Evaluate(uint32_t total_bitrate_kbps) const {
    Status status;
    status.free_bytes = free_bytes_;
    status.must_stop = free_bytes_ <= stop_bytes_;
    status.warn = free_bytes_ <= warn_bytes_;
    status.critical = warn_bytes_ > 0 && free_bytes_ <= warn_bytes_ / 2;

    if (total_bitrate_kbps == 0) {
        return status;
    }
    // 唯一实现：src/core/util/DiskEstimate.h（M2-09 / M8-04 共用）。
    status.estimated_seconds =
        sr::util::EstimateRecordSeconds(free_bytes_, stop_bytes_, total_bitrate_kbps);
    return status;
}

}  // namespace sr::recorder
