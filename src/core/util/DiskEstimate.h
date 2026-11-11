#pragma once

#include <cstdint>

namespace sr::util {

// 音频轨默认码率（AAC 192k）；与 RecordingSession 的 AudioEncoderConfig 保持一致。
constexpr uint32_t kAudioBitrateKbps = 192;

// 剩余可录秒数 = max(0, free - stop) / (total_kbps * 1000 / 8)。
// total_kbps 必须已含音频（视频码率 + kAudioBitrateKbps）。
// 低于停止线或参数非法时返回 0——「还能录 0.3 秒」没有操作意义。
// M2-09 / M8-04 的唯一实现：StorageGuard 与 UI 都必须调用这里，不得各写一遍。
double EstimateRecordSeconds(uint64_t free_bytes,
                             uint64_t stop_bytes,
                             uint32_t total_bitrate_kbps) noexcept;

}  // namespace sr::util
