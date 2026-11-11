#include "mux/MkvWriter.h"

#include <cstring>

namespace sr::mux {
namespace {

// 常用 EBML ID（按原始字节大端写入）
constexpr uint32_t kEbmlHeader = 0x1A45DFA3;
constexpr uint32_t kDocType = 0x4282;
constexpr uint32_t kDocTypeVersion = 0x4287;
constexpr uint32_t kDocTypeReadVersion = 0x4285;
constexpr uint32_t kEbmlVersion = 0x4286;
constexpr uint32_t kEbmlReadVersion = 0x42F7;
constexpr uint32_t kEbmlMaxIdLength = 0x42F2;
constexpr uint32_t kEbmlMaxSizeLength = 0x42F3;

constexpr uint32_t kSegment = 0x18538067;
constexpr uint32_t kInfo = 0x1549A966;
constexpr uint32_t kTimecodeScale = 0x2AD7B1;
constexpr uint32_t kMuxingApp = 0x4D80;
constexpr uint32_t kWritingApp = 0x5741;
constexpr uint32_t kDuration = 0x4489;

constexpr uint32_t kTracks = 0x1654AE6B;
constexpr uint32_t kTrackEntry = 0xAE;
constexpr uint32_t kTrackNumber = 0xD7;
constexpr uint32_t kTrackUid = 0x73C5;
constexpr uint32_t kTrackType = 0x83;
constexpr uint32_t kCodecId = 0x86;
constexpr uint32_t kCodecPrivate = 0x63A2;
constexpr uint32_t kDefaultDuration = 0x23E383;
constexpr uint32_t kVideoElem = 0xE0;
constexpr uint32_t kPixelWidth = 0xB0;
constexpr uint32_t kPixelHeight = 0xBA;
constexpr uint32_t kAudioElem = 0xE1;
constexpr uint32_t kSamplingFrequency = 0xB5;
constexpr uint32_t kChannels = 0x9F;

constexpr uint32_t kCluster = 0x1F43B675;
constexpr uint32_t kTimecode = 0xE7;
constexpr uint32_t kSimpleBlock = 0xA3;

void PutId(std::vector<uint8_t>& out, uint32_t id) {
    if (id >= 0x1000000) {
        out.push_back(static_cast<uint8_t>((id >> 24) & 0xFF));
    }
    if (id >= 0x10000) {
        out.push_back(static_cast<uint8_t>((id >> 16) & 0xFF));
    }
    if (id >= 0x100) {
        out.push_back(static_cast<uint8_t>((id >> 8) & 0xFF));
    }
    out.push_back(static_cast<uint8_t>(id & 0xFF));
}

void PutSize(std::vector<uint8_t>& out, uint64_t size) {
    // EBML vint：标记位长度 N 表示整个字段占 N 字节，值位占 7N 位。
    // 这里统一用 8 字节形式（标记 0x01 + 7 字节值），实现最简单。
    out.push_back(0x01);
    for (int shift = 48; shift >= 0; shift -= 8) {
        out.push_back(static_cast<uint8_t>((size >> shift) & 0xFF));
    }
}

// 未知长度：标记 0x01 + 7 个 0xFF（值位全 1）。
void PutUnknownSize(std::vector<uint8_t>& out) {
    out.push_back(0x01);
    for (int i = 0; i < 7; ++i) {
        out.push_back(0xFF);
    }
}

void PutUInt(std::vector<uint8_t>& out, uint64_t value) {
    uint8_t bytes[8] = {};
    for (int i = 0; i < 8; ++i) {
        bytes[i] = static_cast<uint8_t>((value >> (8 * (7 - i))) & 0xFF);
    }
    // 去掉前导零，至少保留 1 字节
    int start = 0;
    while (start < 7 && bytes[start] == 0) {
        ++start;
    }
    out.insert(out.end(), bytes + start, bytes + 8);
}

void PutElement(std::vector<uint8_t>& out, uint32_t id, const std::vector<uint8_t>& payload) {
    PutId(out, id);
    PutSize(out, payload.size());
    out.insert(out.end(), payload.begin(), payload.end());
}

std::vector<uint8_t> UIntElement(uint32_t id, uint64_t value) {
    std::vector<uint8_t> payload;
    PutUInt(payload, value);
    std::vector<uint8_t> out;
    PutElement(out, id, payload);
    return out;
}

std::vector<uint8_t> StringElement(uint32_t id, const char* text) {
    std::vector<uint8_t> payload(text, text + std::strlen(text));
    std::vector<uint8_t> out;
    PutElement(out, id, payload);
    return out;
}

std::vector<uint8_t> BinaryElement(uint32_t id, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> out;
    PutElement(out, id, data);
    return out;
}

void Append(std::vector<uint8_t>& target, const std::vector<uint8_t>& source) {
    target.insert(target.end(), source.begin(), source.end());
}

void PutTrackNumberVint(std::vector<uint8_t>& out, uint64_t track) {
    // 轨道号 <= 127 时单字节 vint
    out.push_back(static_cast<uint8_t>(0x80 | (track & 0x7F)));
}

}  // namespace

MkvWriter::~MkvWriter() {
    if (file_ != nullptr) {
        std::fclose(file_);
        file_ = nullptr;
    }
}

void MkvWriter::SetFailWriteAfter(uint64_t writes) {
    fail_after_ = writes;
}

bool MkvWriter::WriteRaw(const void* data, size_t size) {
    if (file_ == nullptr || size == 0) {
        return file_ != nullptr;
    }
    ++writes_;
    if (fail_after_ != 0 && writes_ > fail_after_) {
        // 注入点放在真实写盘函数里：上层看到的必须是「写失败」，
        // 而不是在调用方之前提前短路（那属于自证，见台账 F2）。
        return false;
    }
    const size_t written = std::fwrite(data, 1, size, file_);
    if (written != size) {
        return false;
    }
    bytes_written_ += size;
    return true;
}

bool MkvWriter::Open(const std::string& path, std::string& error) {
    path_ = path;
    file_ = std::fopen(path.c_str(), "wb+");
    if (file_ == nullptr) {
        error = "cannot open output file: " + path;
        return false;
    }

    std::vector<uint8_t> header;
    PutElement(header, kEbmlHeader, [&]() {
        std::vector<uint8_t> body;
        Append(body, UIntElement(kEbmlVersion, 1));
        Append(body, UIntElement(kEbmlReadVersion, 1));
        Append(body, UIntElement(kEbmlMaxIdLength, 4));
        Append(body, UIntElement(kEbmlMaxSizeLength, 8));
        Append(body, StringElement(kDocType, "matroska"));
        Append(body, UIntElement(kDocTypeVersion, 4));
        Append(body, UIntElement(kDocTypeReadVersion, 2));
        return body;
    }());

    if (!WriteRaw(header.data(), header.size())) {
        error = "write header failed";
        return false;
    }

    // Segment：未知长度，边录边写不回头改。
    // 单独成块写，写完立刻回读自校验——这段头一旦错，整个文件都不合法。
    const uint64_t segment_offset = bytes_written_;
    std::vector<uint8_t> segment_header;
    PutId(segment_header, kSegment);
    PutUnknownSize(segment_header);
    if (segment_header.size() != 12) {
        error = "unexpected segment header length";
        return false;
    }
    if (!WriteRaw(segment_header.data(), segment_header.size())) {
        error = "write segment header failed";
        return false;
    }
    {
        uint8_t verify[12] = {};
        const long origin = static_cast<long>(segment_offset);
        const bool seek_ok = std::fseek(file_, origin, SEEK_SET) == 0;
        const size_t read = seek_ok ? std::fread(verify, 1, sizeof(verify), file_) : 0;
        std::fseek(file_, 0, SEEK_END);
        if (!seek_ok || read != sizeof(verify) || std::memcmp(verify, segment_header.data(), sizeof(verify)) != 0) {
            error = "segment header read-back mismatch";
            return false;
        }
    }

    // Info：Duration 预留 8 字节占位，Close 时回填。
    std::vector<uint8_t> info;
    Append(info, UIntElement(kTimecodeScale, 1000000));
    Append(info, StringElement(kMuxingApp, "sr-recorder"));
    Append(info, StringElement(kWritingApp, "sr-recorder"));

    std::vector<uint8_t> duration_payload(8, 0);
    std::vector<uint8_t> duration_element;
    PutElement(duration_element, kDuration, duration_payload);
    // Duration 是 Info 的最后一个子元素，其 payload 位于 info 缓冲末尾 8 字节。
    Append(info, duration_element);

    std::vector<uint8_t> info_element;
    PutElement(info_element, kInfo, info);

    // 注意：duration_offset_ 必须在 info_element 写完之后再取。
    // 之前把 ftell 放在写入之前，导致回填的 Duration 覆盖了 Segment 的长度字段
    // （台账 D-12：2999.0 的 double 小端字节 00 00 00 00 00 6E A7 40 出现在偏移 100）。
    const uint64_t info_start = bytes_written_;
    (void)info_start;
    if (!WriteRaw(info_element.data(), info_element.size())) {
        error = "write info failed";
        return false;
    }

    // Info 已落盘，此刻文件末尾 8 字节正是 Duration 的占位 payload。
    duration_offset_ = static_cast<uint64_t>(std::ftell(file_)) - 8;

    error.clear();
    return true;
}

bool MkvWriter::SetVideoTrack(const VideoTrackInfo& info, std::string& error) {
    if (file_ == nullptr) {
        error = "file is not open";
        return false;
    }
    video_info_ = info;
    has_video_ = true;
    error.clear();
    return true;
}

bool MkvWriter::SetAudioTrack(const AudioTrackInfo& info, std::string& error) {
    if (file_ == nullptr) {
        error = "file is not open";
        return false;
    }
    audio_info_ = info;
    has_audio_ = true;
    error.clear();
    return true;
}

bool MkvWriter::WriteTracks() {
    std::vector<uint8_t> tracks;

    if (has_video_) {
        std::vector<uint8_t> entry;
        Append(entry, UIntElement(kTrackNumber, 1));
        Append(entry, UIntElement(kTrackUid, 1));
        Append(entry, UIntElement(kTrackType, 1));
        Append(entry, StringElement(kCodecId, "V_MPEG4/ISO/AVC"));
        if (!video_info_.codec_private.empty()) {
            Append(entry, BinaryElement(kCodecPrivate, video_info_.codec_private));
        }
        if (video_info_.fps > 0) {
            Append(entry, UIntElement(kDefaultDuration, 1000000000ull / video_info_.fps));
        }
        std::vector<uint8_t> video;
        Append(video, UIntElement(kPixelWidth, video_info_.width));
        Append(video, UIntElement(kPixelHeight, video_info_.height));
        std::vector<uint8_t> video_element;
        PutElement(video_element, kVideoElem, video);
        Append(entry, video_element);

        std::vector<uint8_t> track_entry;
        PutElement(track_entry, kTrackEntry, entry);
        Append(tracks, track_entry);
    }

    if (has_audio_) {
        std::vector<uint8_t> entry;
        Append(entry, UIntElement(kTrackNumber, 2));
        Append(entry, UIntElement(kTrackUid, 2));
        Append(entry, UIntElement(kTrackType, 2));
        Append(entry, StringElement(kCodecId, "A_AAC"));
        if (!audio_info_.codec_private.empty()) {
            Append(entry, BinaryElement(kCodecPrivate, audio_info_.codec_private));
        }
        std::vector<uint8_t> audio;
        const double sample_rate = static_cast<double>(audio_info_.sample_rate);
        // SamplingFrequency 是 float64
        std::vector<uint8_t> frequency_payload(sizeof(double));
        std::memcpy(frequency_payload.data(), &sample_rate, sizeof(double));
        std::vector<uint8_t> frequency_element;
        PutElement(frequency_element, kSamplingFrequency, frequency_payload);
        Append(audio, frequency_element);
        Append(audio, UIntElement(kChannels, audio_info_.channels));
        std::vector<uint8_t> audio_element;
        PutElement(audio_element, kAudioElem, audio);
        Append(entry, audio_element);

        std::vector<uint8_t> track_entry;
        PutElement(track_entry, kTrackEntry, entry);
        Append(tracks, track_entry);
    }

    std::vector<uint8_t> element;
    PutElement(element, kTracks, tracks);
    if (!WriteRaw(element.data(), element.size())) {
        return false;
    }
    tracks_written_ = true;
    return true;
}

bool MkvWriter::StartCluster(int64_t timestamp_ms) {
    if (!FlushCluster()) {
        return false;
    }
    cluster_.clear();
    cluster_base_ms_ = timestamp_ms;
    cluster_open_ = true;
    ++cluster_count_;
    return true;
}

bool MkvWriter::FlushCluster() {
    if (!cluster_open_) {
        return true;
    }
    std::vector<uint8_t> payload;
    Append(payload, UIntElement(kTimecode, static_cast<uint64_t>(cluster_base_ms_)));
    Append(payload, cluster_);

    std::vector<uint8_t> element;
    PutElement(element, kCluster, payload);
    if (!WriteRaw(element.data(), element.size())) {
        return false;
    }
    cluster_.clear();
    cluster_open_ = false;
    return true;
}

bool MkvWriter::AppendBlock(uint64_t track_number,
                            const uint8_t* data,
                            size_t size,
                            int64_t pts_ms,
                            bool keyframe) {
    if (!cluster_open_) {
        if (!StartCluster(pts_ms)) {
            return false;
        }
    }

    const int64_t relative = pts_ms - cluster_base_ms_;
    if (relative > 30000 || relative < -30000) {
        // int16 相对时间码放不下，必须开新 Cluster。
        if (!StartCluster(pts_ms)) {
            return false;
        }
    }

    std::vector<uint8_t> block;
    PutTrackNumberVint(block, track_number);
    const int16_t relative_i16 = static_cast<int16_t>(pts_ms - cluster_base_ms_);
    block.push_back(static_cast<uint8_t>((relative_i16 >> 8) & 0xFF));
    block.push_back(static_cast<uint8_t>(relative_i16 & 0xFF));
    block.push_back(keyframe ? 0x80 : 0x00);
    block.insert(block.end(), data, data + size);

    PutElement(cluster_, kSimpleBlock, block);
    return true;
}

bool MkvWriter::WriteVideo(const uint8_t* data, size_t size, int64_t pts_100ns, bool keyframe) {
    if (!has_video_ || data == nullptr || size == 0) {
        return false;
    }
    if (!tracks_written_) {
        if (!WriteTracks()) {
            return false;
        }
    }

    const int64_t pts_ms = pts_100ns / 10000;

    // 每 ~1500ms 或遇到关键帧且已超过 1000ms 时切 Cluster：
    // 既限制相对时间码范围，也让崩溃后已完成的 Cluster 尽量多。
    if (cluster_open_ && (pts_ms - cluster_base_ms_ > 1500 || (keyframe && pts_ms - cluster_base_ms_ > 1000))) {
        if (!FlushCluster()) {
            return false;
        }
    }

    if (!AppendBlock(1, data, size, pts_ms, keyframe)) {
        return false;
    }
    last_video_ms_ = pts_ms;
    ++video_blocks_;
    return true;
}

bool MkvWriter::WriteAudio(const uint8_t* data, size_t size, int64_t pts_100ns) {
    if (!has_audio_ || data == nullptr || size == 0) {
        return false;
    }
    const int64_t pts_ms = pts_100ns / 10000;
    if (!AppendBlock(2, data, size, pts_ms, true)) {
        return false;
    }
    ++audio_blocks_;
    return true;
}

bool MkvWriter::FlushPending() {
    return FlushCluster();
}

bool MkvWriter::Close(std::string& error) {
    if (file_ == nullptr) {
        error = "file is not open";
        return false;
    }

    if (!FlushCluster()) {
        error = "flush last cluster failed";
        // 失败也必须关闭句柄：否则上层无法删除/改名 .part，兜底流程会留下半成品（D-33）。
        std::fflush(file_);
        std::fclose(file_);
        file_ = nullptr;
        return false;
    }

    // 回填 Duration（单位：TimecodeScale = 1ms）。
    const double duration_ms = static_cast<double>(last_video_ms_);
    // EBML 的 float 一律大端存储；直接 memcpy 会写出主机小端，读出来是次正规数。
    unsigned char raw[8] = {};
    std::memcpy(raw, &duration_ms, sizeof(double));
    unsigned char bytes[8] = {};
    for (int i = 0; i < 8; ++i) {
        bytes[i] = raw[7 - i];
    }
    if (std::fseek(file_, static_cast<long>(duration_offset_), SEEK_SET) == 0) {
        std::fwrite(bytes, 1, sizeof(bytes), file_);
        std::fseek(file_, 0, SEEK_END);
    }

    std::fflush(file_);
    std::fclose(file_);
    file_ = nullptr;
    error.clear();
    return true;
}

}  // namespace sr::mux
