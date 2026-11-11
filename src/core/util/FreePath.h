#pragma once

#include <cstdio>
#include <filesystem>
#include <string>

namespace sr::util {

// 「不覆盖已有文件」的唯一实现。
//
// 这段逻辑此前被写过三次（OutputPath::FindFreePath、MkvRecovery::Probe、
// MkvRecovery::Repair），只有第一份是「全占即失败」，后两份是「找不到就沿用原路径」，
// 于是连续产生 D-24 / D-32 / D-34 三次覆盖事故。收敛到此，禁止再就地重写。
//
// 语义：
//   - 目标不存在 → 直接返回目标
//   - 否则依次尝试 stem_001.ext … stem_999.ext
//   - 999 个都被占用 → 返回空串（表示放弃，绝不复用已存在路径）
inline std::string FindFreePath(const std::string& desired) {
    if (desired.empty()) {
        return std::string();
    }

    const std::filesystem::path base(desired);
    std::error_code ec;
    if (!std::filesystem::exists(base, ec)) {
        return desired;
    }

    const std::string stem = base.stem().string();
    const std::string extension = base.extension().string();
    for (int index = 1; index <= 999; ++index) {
        char suffix[16] = {};
        std::snprintf(suffix, sizeof(suffix), "_%03d", index);
        const std::filesystem::path candidate = base.parent_path() / (stem + suffix + extension);
        ec.clear();
        if (!std::filesystem::exists(candidate, ec)) {
            return candidate.string();
        }
    }
    return std::string();
}

}  // namespace sr::util
