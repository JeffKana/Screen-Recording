#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sr {

enum class ContainerFormat : uint8_t {
    Mkv = 0,  // 录制中的分片容器（容错好，见 ADR-02）
    Mp4,      // 成品容器（faststart）
};

struct MuxerConfig {
    std::string temp_dir;    // 分片写入目录（M5-04）
    std::string final_path;  // 成品目标路径
    ContainerFormat container = ContainerFormat::Mkv;
    // 0 表示不按该维度分片。两者都为 0 则单文件写到结束。
    uint32_t segment_seconds = 0;   // M2-11
    uint64_t segment_bytes = 0;     // M5-05（FAT32 4GB 限制）
    bool faststart = true;
};

// 单个待修复分片的探测结果（M8-02）。
struct RecoveryItem {
    std::string segment_path;
    std::string suggested_output;
    uint64_t size_bytes = 0;
    bool repairable = false;
    // 最后一个完整 Cluster 的末尾时间戳（ms）：判断修复后是否丢得过多的依据。
    int64_t last_pts_ms = 0;
    std::string probe_reason;
    // M5-06：列表展示用（成品取 Info/Duration；.part 取最后完整 Cluster 的 PTS）。
    int64_t duration_ms = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

struct RecoveryReport {
    std::vector<RecoveryItem> items;
    uint32_t scanned = 0;
    uint32_t repaired = 0;
    uint32_t failed = 0;
};

}  // namespace sr
