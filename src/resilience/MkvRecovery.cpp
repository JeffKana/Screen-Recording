#include "resilience/MkvRecovery.h"

#include "core/util/FreePath.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <vector>

namespace sr::resilience {
namespace {

constexpr uint32_t kIdSegment = 0x18538067;
constexpr uint32_t kIdInfo = 0x1549A966;
constexpr uint32_t kIdTracks = 0x1654AE6B;
constexpr uint32_t kIdCluster = 0x1F43B675;
constexpr uint32_t kIdTimecode = 0xE7;
constexpr uint32_t kIdDuration = 0x4489;
constexpr uint32_t kIdSimpleBlock = 0xA3;
// M5-06 列表：分辨率藏在 Tracks > TrackEntry > Video 里。
constexpr uint32_t kIdTrackEntry = 0xAE;
constexpr uint32_t kIdVideo = 0xE0;
constexpr uint32_t kIdPixelWidth = 0xB0;
constexpr uint32_t kIdPixelHeight = 0xBA;

struct Cursor {
    const std::vector<uint8_t>& data;
    size_t pos = 0;

    bool AtEnd() const { return pos >= data.size(); }

    bool ReadId(uint32_t& id) {
        if (pos >= data.size()) {
            return false;
        }
        const uint8_t first = data[pos];
        if (first == 0) {
            return false;
        }
        int length = 1;
        uint8_t mask = 0x80;
        while ((first & mask) == 0) {
            mask >>= 1;
            ++length;
        }
        if (pos + static_cast<size_t>(length) > data.size()) {
            return false;
        }
        id = 0;
        for (int i = 0; i < length; ++i) {
            id = (id << 8) | data[pos + i];
        }
        pos += static_cast<size_t>(length);
        return true;
    }

    bool ReadSize(uint64_t& size, bool& unknown) {
        if (pos >= data.size()) {
            return false;
        }
        const uint8_t first = data[pos];
        if (first == 0) {
            return false;
        }
        int length = 1;
        uint8_t mask = 0x80;
        while ((first & mask) == 0) {
            mask >>= 1;
            ++length;
        }
        if (pos + static_cast<size_t>(length) > data.size()) {
            return false;
        }
        uint64_t value = first & (mask - 1);
        for (int i = 1; i < length; ++i) {
            value = (value << 8) | data[pos + i];
        }
        unknown = value == ((1ULL << (7 * length)) - 1ULL);
        pos += static_cast<size_t>(length);
        size = value;
        return true;
    }
};

struct ScanResult {
    size_t valid_end = 0;        // 最后一个完整 Cluster 的结束偏移
    uint64_t video_blocks = 0;
    int64_t last_pts_ms = 0;
    bool has_video_track = false;
    uint64_t duration_offset = 0;  // Duration payload 的绝对偏移（0 表示不存在）
    // M5-06：列表要显示时长与分辨率，这里顺带解析（只读头，不额外扫盘）。
    double duration_ms = 0.0;
    bool duration_valid = false;
    uint32_t width = 0;
    uint32_t height = 0;
};

uint64_t ReadBigEndianUnsigned(const std::vector<uint8_t>& data, size_t pos, size_t length) {
    uint64_t value = 0;
    for (size_t i = 0; i < length && pos + i < data.size(); ++i) {
        value = (value << 8) | data[pos + i];
    }
    return value;
}

// 解析单个 Cluster 的子元素，取出 Timecode 与最大块相对时间。
void ParseCluster(const std::vector<uint8_t>& data, size_t start, size_t end, int64_t& max_pts_ms) {
    Cursor cursor{data, start};
    int64_t cluster_base = 0;
    while (cursor.pos < end) {
        uint32_t id = 0;
        uint64_t size = 0;
        bool unknown = false;
        if (!cursor.ReadId(id) || !cursor.ReadSize(size, unknown)) {
            return;
        }
        if (unknown) {
            return;
        }
        const size_t payload = cursor.pos;
        const size_t payload_end = std::min(payload + static_cast<size_t>(size), end);
        if (id == kIdTimecode && size <= 8) {
            cluster_base = static_cast<int64_t>(ReadBigEndianUnsigned(data, payload, size));
        } else if (id == kIdSimpleBlock && payload_end >= payload + 4) {
            // 轨道号 vint + int16 相对时间 + flags
            const int16_t relative = static_cast<int16_t>((data[payload + 1] << 8) | data[payload + 2]);
            const int64_t pts = cluster_base + relative;
            if (pts > max_pts_ms) {
                max_pts_ms = pts;
            }
        }
        cursor.pos = payload_end;
    }
}

ScanResult ScanFile(const std::vector<uint8_t>& data) {
    ScanResult result;
    Cursor cursor{data, 0};

    while (!cursor.AtEnd()) {
        uint32_t id = 0;
        uint64_t size = 0;
        bool unknown = false;
        if (!cursor.ReadId(id) || !cursor.ReadSize(size, unknown)) {
            break;
        }
        const size_t payload_start = cursor.pos;

        if (id == kIdCluster) {
            if (unknown) {
                break;  // 我们从不写未知长度的 Cluster
            }
            const size_t cluster_end = payload_start + static_cast<size_t>(size);
            if (cluster_end > data.size()) {
                break;  // 崩溃时正在写的 Cluster：丢弃
            }
            int64_t cluster_max = -1;
            ParseCluster(data, payload_start, cluster_end, cluster_max);
            if (cluster_max >= 0) {
                result.last_pts_ms = std::max(result.last_pts_ms, cluster_max);
                ++result.video_blocks;
            }
            result.valid_end = cluster_end;
            cursor.pos = cluster_end;
            continue;
        }

        if (unknown) {
            // Segment：未知长度，继续往后读
            continue;
        }

        const size_t payload_end = payload_start + static_cast<size_t>(size);
        if (payload_end > data.size()) {
            break;
        }

        if (id == kIdTracks) {
            // 粗略判断是否存在视频轨（含 V_MPEG4/ISO/AVC 字符串即可）
            static const char kAvcId[] = "V_MPEG4/ISO/AVC";
            const std::string_view block(reinterpret_cast<const char*>(data.data() + payload_start), size);
            result.has_video_track = block.find(kAvcId) != std::string_view::npos;
            // M5-06：TrackEntry > Video > PixelWidth/PixelHeight（只读头，代价可忽略）。
            Cursor track_cursor{data, payload_start};
            while (track_cursor.pos < payload_end) {
                uint32_t entry_id = 0;
                uint64_t entry_size = 0;
                bool entry_unknown = false;
                if (!track_cursor.ReadId(entry_id) || !track_cursor.ReadSize(entry_size, entry_unknown) ||
                    entry_unknown) {
                    break;
                }
                const size_t entry_end = std::min(track_cursor.pos + static_cast<size_t>(entry_size), payload_end);
                if (entry_id == kIdTrackEntry) {
                    Cursor entry_cursor{data, track_cursor.pos};
                    while (entry_cursor.pos < entry_end) {
                        uint32_t field_id = 0;
                        uint64_t field_size = 0;
                        bool field_unknown = false;
                        if (!entry_cursor.ReadId(field_id) || !entry_cursor.ReadSize(field_size, field_unknown) ||
                            field_unknown) {
                            break;
                        }
                        const size_t field_end =
                            std::min(entry_cursor.pos + static_cast<size_t>(field_size), entry_end);
                        if (field_id == kIdVideo) {
                            Cursor video_cursor{data, entry_cursor.pos};
                            while (video_cursor.pos < field_end) {
                                uint32_t video_id = 0;
                                uint64_t video_size = 0;
                                bool video_unknown = false;
                                if (!video_cursor.ReadId(video_id) ||
                                    !video_cursor.ReadSize(video_size, video_unknown) || video_unknown) {
                                    break;
                                }
                                if (video_id == kIdPixelWidth && video_size <= 4) {
                                    result.width = static_cast<uint32_t>(
                                        ReadBigEndianUnsigned(data, video_cursor.pos, video_size));
                                } else if (video_id == kIdPixelHeight && video_size <= 4) {
                                    result.height = static_cast<uint32_t>(
                                        ReadBigEndianUnsigned(data, video_cursor.pos, video_size));
                                }
                                video_cursor.pos = std::min(
                                    video_cursor.pos + static_cast<size_t>(video_size), field_end);
                            }
                        }
                        entry_cursor.pos = field_end;
                    }
                }
                track_cursor.pos = entry_end;
            }
            if (result.valid_end == 0) {
                result.valid_end = payload_end;
            }
        } else if (id == kIdInfo) {
            // 在 Info 内找 Duration 的 payload 偏移
            Cursor inner{data, payload_start};
            while (inner.pos < payload_end) {
                uint32_t child_id = 0;
                uint64_t child_size = 0;
                bool child_unknown = false;
                if (!inner.ReadId(child_id) || !inner.ReadSize(child_size, child_unknown) || child_unknown) {
                    break;
                }
                if (child_id == kIdDuration && child_size == 8) {
                    result.duration_offset = inner.pos;
                    const uint64_t raw = ReadBigEndianUnsigned(data, inner.pos, 8);
                    double value = 0.0;
                    std::memcpy(&value, &raw, sizeof(value));
                    if (value > 0.0 && value < 1e12) {
                        result.duration_ms = value;
                        result.duration_valid = true;
                    }
                }
                inner.pos = std::min(inner.pos + static_cast<size_t>(child_size), payload_end);
            }
            if (result.valid_end == 0) {
                result.valid_end = payload_end;
            }
        } else if (result.valid_end == 0) {
            result.valid_end = payload_end;
        }

        cursor.pos = payload_end;
    }

    return result;
}

}  // namespace

RecoveryItem MkvRecovery::Probe(const std::string& segment_path) {
    RecoveryItem item;
    item.segment_path = segment_path;

    std::ifstream in(segment_path, std::ios::binary | std::ios::ate);
    if (!in) {
        item.probe_reason = "cannot open file";
        return item;
    }
    const std::streamsize size = in.tellg();
    in.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(size > 0 ? size : 0));
    if (size > 0 && !in.read(reinterpret_cast<char*>(data.data()), size)) {
        item.probe_reason = "read failed";
        return item;
    }
    item.size_bytes = static_cast<uint64_t>(data.size());

    const ScanResult scan = ScanFile(data);
    item.last_pts_ms = scan.last_pts_ms;
    item.duration_ms = scan.duration_valid ? static_cast<int64_t>(scan.duration_ms) : scan.last_pts_ms;
    item.width = scan.width;
    item.height = scan.height;
    item.repairable = scan.has_video_track && scan.video_blocks > 0;
    if (item.repairable) {
        const size_t lost = data.size() - scan.valid_end;
        char reason[256] = {};
        std::snprintf(reason, sizeof(reason),
                      "video_blocks=%llu last_pts=%lldms valid_end=%zu lost_bytes=%zu",
                      static_cast<unsigned long long>(scan.video_blocks),
                      static_cast<long long>(scan.last_pts_ms),
                      scan.valid_end,
                      lost);
        item.probe_reason = reason;
        item.suggested_output = segment_path;
        const size_t dot = item.suggested_output.find_last_of('.');
        if (dot != std::string::npos) {
            item.suggested_output = item.suggested_output.substr(0, dot);
        }
        if (item.suggested_output.size() > 5 &&
            item.suggested_output.compare(item.suggested_output.size() - 5, 5, ".part") == 0) {
            item.suggested_output = item.suggested_output.substr(0, item.suggested_output.size() - 5);
        }
        item.suggested_output += ".recovered.mkv";
        // D-32：建议产物名必须避让已存在文件，否则二次修复会静默覆盖上一次的结果。
        {
            std::error_code name_ec;
            if (std::filesystem::exists(item.suggested_output, name_ec)) {
                const std::filesystem::path base(item.suggested_output);
                const std::string stem = base.stem().string();
                const std::string ext = base.extension().string();
                for (int index = 1; index <= 999; ++index) {
                    char suffix[16] = {};
                    std::snprintf(suffix, sizeof(suffix), "_%03d", index);
                    const std::filesystem::path candidate = base.parent_path() / (stem + suffix + ext);
                    name_ec.clear();
                    if (!std::filesystem::exists(candidate, name_ec)) {
                        item.suggested_output = candidate.string();
                        break;
                    }
                }
            }
        }
    } else {
        item.probe_reason = "no complete video cluster found";
    }
    return item;
}

// M5-06：成品文件的时长/分辨率只在头部（Segment > Info/Tracks）。只读 1MB，
// 避免为了显示列表把一个 2GB 文件全读进内存。
RecoveryItem MkvRecovery::InspectHeader(const std::string& file_path) {
    RecoveryItem item;
    item.segment_path = file_path;
    std::ifstream in(file_path, std::ios::binary);
    if (!in) {
        item.probe_reason = "cannot open file";
        return item;
    }
    std::error_code ec;
    const uintmax_t size = std::filesystem::file_size(file_path, ec);
    item.size_bytes = ec ? 0 : static_cast<uint64_t>(size);
    constexpr size_t kHeaderBytes = 1u << 20;
    std::vector<uint8_t> data(kHeaderBytes, 0);
    in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    data.resize(static_cast<size_t>(in.gcount()));

    const ScanResult scan = ScanFile(data);
    item.width = scan.width;
    item.height = scan.height;
    item.duration_ms = scan.duration_valid ? static_cast<int64_t>(scan.duration_ms) : 0;
    item.last_pts_ms = scan.last_pts_ms;
    item.repairable = false;  // 成品是否可播不在这里判定（那是 M8-09 的范围）
    if (!scan.has_video_track) {
        item.probe_reason = "no video track in header";
    }
    return item;
}

RecoveryReport MkvRecovery::Scan(const std::string& directory) {
    RecoveryReport report;
    std::error_code ec;
    if (!std::filesystem::is_directory(directory, ec)) {
        return report;
    }
    for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
        if (ec || !entry.is_regular_file()) {
            continue;
        }
        const std::string path = entry.path().string();
        if (path.size() < 5 || path.compare(path.size() - 5, 5, ".part") != 0) {
            continue;
        }
        ++report.scanned;
        report.items.push_back(Probe(path));
    }
    return report;
}

bool MkvRecovery::Repair(const RecoveryItem& item, const std::string& out_path, std::string& error) {
    std::ifstream in(item.segment_path, std::ios::binary | std::ios::ate);
    if (!in) {
        error = "cannot open source";
        return false;
    }
    const std::streamsize size = in.tellg();
    in.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(size > 0 ? size : 0));
    if (size > 0 && !in.read(reinterpret_cast<char*>(data.data()), size)) {
        error = "read failed";
        return false;
    }
    in.close();

    const ScanResult scan = ScanFile(data);
    if (!scan.has_video_track || scan.video_blocks == 0 || scan.valid_end == 0) {
        error = "nothing to recover";
        return false;
    }

    // 截断到最后一个完整 Cluster；Segment 是未知长度，因此截断后仍是合法文件。
    data.resize(scan.valid_end);

    // 回填 Duration（单位 ms，float64 大端）
    if (scan.duration_offset != 0 && scan.duration_offset + 8 <= data.size()) {
        const double duration_ms = static_cast<double>(scan.last_pts_ms);
        unsigned char raw[8] = {};
        std::memcpy(raw, &duration_ms, sizeof(double));
        for (int i = 0; i < 8; ++i) {
            data[scan.duration_offset + static_cast<size_t>(i)] = raw[7 - i];
        }
    }

    // D-32/D-34：避让统一走 FreePath，绝不复用已存在路径。
    std::string target_path = sr::util::FindFreePath(out_path);
    if (target_path.empty()) {
        error = "no free output name for " + out_path;
        return false;
    }
    {
        std::error_code exist_ec;
        if (std::filesystem::exists(target_path, exist_ec)) {
            const std::filesystem::path base(target_path);
            const std::string stem = base.stem().string();
            const std::string ext = base.extension().string();
            for (int index = 1; index <= 999; ++index) {
                char suffix[16] = {};
                std::snprintf(suffix, sizeof(suffix), "_%03d", index);
                const std::filesystem::path candidate = base.parent_path() / (stem + suffix + ext);
                exist_ec.clear();
                if (!std::filesystem::exists(candidate, exist_ec)) {
                    target_path = candidate.string();
                    break;
                }
            }
        }
    }
    std::ofstream out(target_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "cannot create output: " + target_path;
        return false;
    }
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!out) {
        error = "write failed";
        return false;
    }
    out.close();

    // 修复成功后把源 .part 改名，避免 --scan 反复报同一个文件、反复提示用户。
    const std::string source = item.segment_path;
    if (source.size() >= 5 && source.compare(source.size() - 5, 5, ".part") == 0) {
        std::error_code rename_ec;
        // D-34：改名目标同样要避让，并检查错误——否则会静默覆盖已有的 .part.repaired。
        const std::string repaired_path = sr::util::FindFreePath(source + ".repaired");
        if (!repaired_path.empty()) {
            std::filesystem::rename(source, repaired_path, rename_ec);
        }
    }

    error.clear();
    return true;
}

}  // namespace sr::resilience
