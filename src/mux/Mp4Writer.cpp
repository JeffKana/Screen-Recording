#include "mux/Mp4Writer.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <numeric>

namespace sr::mux {
namespace {

constexpr uint32_t kMovieTimescale = 1000;
constexpr uint32_t kVideoTimescale = 90000;
constexpr uint32_t kIsoLanguageUnd = 0x55C4;  // 'und'（ISO-639-2/T）

void Put8(std::vector<uint8_t>& out, uint8_t value) {
    out.push_back(value);
}

void Put16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value));
}

void Put32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>(value >> 24));
    out.push_back(static_cast<uint8_t>(value >> 16));
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value));
}

void Put64(std::vector<uint8_t>& out, uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        out.push_back(static_cast<uint8_t>(value >> shift));
    }
}

void PutType(std::vector<uint8_t>& out, const char* fourcc) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<uint8_t>(fourcc[i]));
    }
}

void PutZeros(std::vector<uint8_t>& out, size_t count) {
    out.insert(out.end(), count, 0);
}

void Append(std::vector<uint8_t>& target, const std::vector<uint8_t>& source) {
    target.insert(target.end(), source.begin(), source.end());
}

std::vector<uint8_t> Box(const char* type, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> out;
    Put32(out, static_cast<uint32_t>(8 + payload.size()));
    PutType(out, type);
    Append(out, payload);
    return out;
}

std::vector<uint8_t> FullBox(const char* type, uint32_t flags,
                             const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> body;
    Put8(body, 0);  // version
    Put8(body, static_cast<uint8_t>((flags >> 16) & 0xFF));
    Put8(body, static_cast<uint8_t>((flags >> 8) & 0xFF));
    Put8(body, static_cast<uint8_t>(flags & 0xFF));
    Append(body, payload);
    return Box(type, body);
}

// ISO/IEC 14496-1 描述符长度：每字节 7 位有效，最高位表示后面还有字节。
void PutDescriptorSize(std::vector<uint8_t>& out, uint32_t size) {
    uint8_t bytes[4] = {};
    int count = 1;
    bytes[3] = static_cast<uint8_t>(size & 0x7F);
    uint32_t rest = size >> 7;
    while (rest > 0 && count < 4) {
        bytes[3 - count] = static_cast<uint8_t>(0x80 | (rest & 0x7F));
        rest >>= 7;
        ++count;
    }
    for (int i = 4 - count; i < 4; ++i) {
        // 除最后一字节外都要带 continuation 位。
        out.push_back(i == 3 ? bytes[i] : static_cast<uint8_t>(bytes[i] | 0x80));
    }
}

std::vector<uint8_t> Descriptor(uint8_t tag, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> out;
    Put8(out, tag);
    PutDescriptorSize(out, static_cast<uint32_t>(payload.size()));
    Append(out, payload);
    return out;
}

void PutMatrix(std::vector<uint8_t>& out) {
    Put32(out, 0x00010000); Put32(out, 0); Put32(out, 0);
    Put32(out, 0); Put32(out, 0x00010000); Put32(out, 0);
    Put32(out, 0); Put32(out, 0); Put32(out, 0x40000000);
}

uint32_t DeltaFromPts(int64_t diff_100ns, uint32_t timescale) {
    if (diff_100ns <= 0) {
        return 0;
    }
    // 四舍五入，别用 1/fps 累加：长录制上会漂（轮 48 指令 §注意事项 #2）。
    const int64_t ticks = (diff_100ns * static_cast<int64_t>(timescale) + 5000000LL) / 10000000LL;
    if (ticks < 1) {
        return 1;
    }
    if (ticks > static_cast<int64_t>(std::numeric_limits<uint32_t>::max())) {
        return std::numeric_limits<uint32_t>::max();
    }
    return static_cast<uint32_t>(ticks);
}

int32_t SignedPtsToTicks(int64_t diff_100ns, uint32_t timescale) {
    const int64_t ticks = diff_100ns * static_cast<int64_t>(timescale) / 10000000LL;
    if (ticks > static_cast<int64_t>(std::numeric_limits<int32_t>::max())) {
        return std::numeric_limits<int32_t>::max();
    }
    if (ticks < static_cast<int64_t>(std::numeric_limits<int32_t>::min())) {
        return std::numeric_limits<int32_t>::min();
    }
    return static_cast<int32_t>(ticks);
}

// 样本是 AVCC 形式（4 字节大端长度 + NAL）。stss 只标 IDR（nal_unit_type == 5）。
bool HasIdr(const uint8_t* data, size_t size) {
    size_t offset = 0;
    while (offset + 4 <= size) {
        const uint32_t length = (static_cast<uint32_t>(data[offset]) << 24) |
                                (static_cast<uint32_t>(data[offset + 1]) << 16) |
                                (static_cast<uint32_t>(data[offset + 2]) << 8) |
                                static_cast<uint32_t>(data[offset + 3]);
        offset += 4;
        if (length == 0 || offset + length > size) {
            break;
        }
        if ((data[offset] & 0x1F) == 5) {
            return true;
        }
        offset += length;
    }
    return false;
}

std::vector<uint8_t> FtypBox() {
    std::vector<uint8_t> payload;
    PutType(payload, "isom");
    Put32(payload, 0x00000200);  // minor version
    PutType(payload, "isom");
    PutType(payload, "iso2");
    PutType(payload, "avc1");
    PutType(payload, "mp41");
    return Box("ftyp", payload);
}

std::vector<uint8_t> MvhdBox(uint32_t duration_ms, uint32_t next_track_id) {
    std::vector<uint8_t> p;
    Put32(p, 0); Put32(p, 0);                 // creation / modification time
    Put32(p, kMovieTimescale);
    Put32(p, duration_ms);
    Put32(p, 0x00010000);                     // rate 1.0
    Put16(p, 0x0100);                         // volume 1.0
    Put16(p, 0);
    PutZeros(p, 8);                           // reserved
    PutMatrix(p);
    PutZeros(p, 24);                          // pre_defined[6]
    Put32(p, next_track_id);
    return FullBox("mvhd", 0, p);
}

std::vector<uint8_t> TkhdBox(uint32_t track_id, uint32_t duration_ms,
                             uint32_t width, uint32_t height, bool audio) {
    std::vector<uint8_t> p;
    Put32(p, 0); Put32(p, 0);                 // creation / modification time
    Put32(p, track_id);
    Put32(p, 0);                              // reserved
    Put32(p, duration_ms);
    PutZeros(p, 8);                           // reserved[2]
    Put16(p, 0);                              // layer
    Put16(p, 0);                              // alternate_group
    Put16(p, audio ? 0x0100 : 0);             // volume
    Put16(p, 0);                              // reserved
    PutMatrix(p);
    Put32(p, width << 16);                    // 16.16
    Put32(p, height << 16);
    return FullBox("tkhd", 0x000003, p);      // track_enabled | track_in_movie
}

std::vector<uint8_t> MdhdBox(uint32_t timescale, uint32_t duration) {
    std::vector<uint8_t> p;
    Put32(p, 0); Put32(p, 0);
    Put32(p, timescale);
    Put32(p, duration);
    Put16(p, static_cast<uint16_t>(kIsoLanguageUnd));
    Put16(p, 0);
    return FullBox("mdhd", 0, p);
}

std::vector<uint8_t> HdlrBox(const char* handler, const char* name) {
    std::vector<uint8_t> p;
    Put32(p, 0);                              // pre_defined
    PutType(p, handler);
    PutZeros(p, 12);                          // reserved[3]
    const size_t length = std::strlen(name) + 1;
    p.insert(p.end(), name, name + length);
    return FullBox("hdlr", 0, p);
}

std::vector<uint8_t> DinfBox() {
    const std::vector<uint8_t> self_contained = FullBox("url ", 0x000001, {});
    std::vector<uint8_t> dref;
    Put32(dref, 1);
    Append(dref, self_contained);
    return Box("dinf", FullBox("dref", 0, dref));
}

std::vector<uint8_t> SttsBox(const std::vector<uint32_t>& deltas) {
    std::vector<uint8_t> p;
    std::vector<std::pair<uint32_t, uint32_t>> runs;
    for (uint32_t delta : deltas) {
        if (!runs.empty() && runs.back().second == delta) {
            ++runs.back().first;
        } else {
            runs.emplace_back(1, delta);
        }
    }
    Put32(p, static_cast<uint32_t>(runs.size()));
    for (const auto& run : runs) {
        Put32(p, run.first);
        Put32(p, run.second);
    }
    return FullBox("stts", 0, p);
}

// B 帧重排：解码顺序（stts/DTS）与显示顺序（PTS）不同，差值放 ctts。
// 有负偏移时用 version=1（带符号），否则 version=0（无符号）。
std::vector<uint8_t> CttsBox(const std::vector<int32_t>& offsets) {
    if (offsets.empty()) {
        return {};
    }
    bool all_zero = true;
    bool negative = false;
    for (int32_t value : offsets) {
        if (value != 0) { all_zero = false; }
        if (value < 0) { negative = true; }
    }
    if (all_zero) {
        return {};  // PTS == DTS，不需要 ctts
    }

    std::vector<std::pair<uint32_t, int32_t>> runs;
    for (int32_t value : offsets) {
        if (!runs.empty() && runs.back().second == value) {
            ++runs.back().first;
        } else {
            runs.emplace_back(1, value);
        }
    }
    std::vector<uint8_t> p;
    Put32(p, static_cast<uint32_t>(runs.size()));
    for (const auto& run : runs) {
        Put32(p, run.first);
        Put32(p, static_cast<uint32_t>(run.second));
    }
    // FullBox 的 version 放在第 1 字节；这里需要改版本，单独构造。
    std::vector<uint8_t> body;
    Put8(body, negative ? 1 : 0);
    Put8(body, 0); Put8(body, 0); Put8(body, 0);
    Append(body, p);
    return Box("ctts", body);
}

std::vector<uint8_t> StssBox(const std::vector<uint32_t>& sync) {
    std::vector<uint8_t> p;
    Put32(p, static_cast<uint32_t>(sync.size()));
    for (uint32_t sample : sync) {
        Put32(p, sample);
    }
    return FullBox("stss", 0, p);
}

std::vector<uint8_t> StscBox(size_t sample_count) {
    std::vector<uint8_t> p;
    Put32(p, sample_count == 0 ? 0u : 1u);
    if (sample_count > 0) {
        Put32(p, 1);  // first_chunk
        Put32(p, 1);  // samples_per_chunk：A/V 交错，每个样本自成一 chunk
        Put32(p, 1);  // sample_description_index
    }
    return FullBox("stsc", 0, p);
}

std::vector<uint8_t> StszBox(const std::vector<uint32_t>& sizes) {
    std::vector<uint8_t> p;
    Put32(p, 0);  // sample_size=0 → 后接逐样本大小表
    Put32(p, static_cast<uint32_t>(sizes.size()));
    for (uint32_t size : sizes) {
        Put32(p, size);
    }
    return FullBox("stsz", 0, p);
}

// 偏移全在 uint32 范围用 stco；超过则用 co64（避免长录制静默写出错误偏移）。
std::vector<uint8_t> ChunkOffsetBox(const std::vector<uint64_t>& offsets) {
    bool need_64 = false;
    for (uint64_t offset : offsets) {
        if (offset > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) {
            need_64 = true;
            break;
        }
    }
    std::vector<uint8_t> p;
    Put32(p, static_cast<uint32_t>(offsets.size()));
    for (uint64_t offset : offsets) {
        if (need_64) {
            Put64(p, offset);
        } else {
            Put32(p, static_cast<uint32_t>(offset));
        }
    }
    return FullBox(need_64 ? "co64" : "stco", 0, p);
}

std::vector<uint8_t> Avc1Box(const VideoTrackInfo& info) {
    std::vector<uint8_t> p;
    PutZeros(p, 6);                           // reserved
    Put16(p, 1);                              // data_reference_index
    Put16(p, 0); Put16(p, 0);                 // pre_defined / reserved
    PutZeros(p, 12);                          // pre_defined[3]
    Put16(p, static_cast<uint16_t>(info.width));
    Put16(p, static_cast<uint16_t>(info.height));
    Put32(p, 0x00480000);                     // 72 dpi
    Put32(p, 0x00480000);
    Put32(p, 0);                              // reserved
    Put16(p, 1);                              // frame_count
    PutZeros(p, 32);                          // compressorname（空）
    Put16(p, 0x0018);                         // depth 24
    Put16(p, 0xFFFF);                         // pre_defined = -1
    Append(p, Box("avcC", info.codec_private));
    return Box("avc1", p);
}

std::vector<uint8_t> EsdsBox(const AudioTrackInfo& info) {
    const std::vector<uint8_t> decoder_specific =
        Descriptor(0x05, info.codec_private);

    std::vector<uint8_t> decoder_config;
    Put8(decoder_config, 0x40);               // objectTypeIndication: MPEG-4 Audio
    Put8(decoder_config, 0x15);               // streamType=5(audio), upStream=0, reserved=1
    PutZeros(decoder_config, 3);              // bufferSizeDB（未知填 0）
    Put32(decoder_config, 192000);            // maxBitrate
    Put32(decoder_config, 192000);            // avgBitrate
    Append(decoder_config, decoder_specific);

    std::vector<uint8_t> es;
    Put16(es, 0);                             // ES_ID
    Put8(es, 0);                              // flags
    Append(es, Descriptor(0x04, decoder_config));
    std::vector<uint8_t> sl;
    Put8(sl, 2);                              // predefined
    Append(es, Descriptor(0x06, sl));
    return FullBox("esds", 0, Descriptor(0x03, es));
}

std::vector<uint8_t> Mp4aBox(const AudioTrackInfo& info) {
    std::vector<uint8_t> p;
    PutZeros(p, 6);                           // reserved
    Put16(p, 1);                              // data_reference_index
    PutZeros(p, 8);                           // version/revision/vendor
    Put16(p, info.channels);
    Put16(p, 16);                             // samplesize
    Put16(p, 0); Put16(p, 0);                 // pre_defined / reserved
    Put32(p, info.sample_rate << 16);         // 16.16 定点
    Append(p, EsdsBox(info));
    return Box("mp4a", p);
}

std::vector<uint8_t> StsdBox(const std::vector<uint8_t>& sample_entry) {
    std::vector<uint8_t> p;
    Put32(p, 1);
    Append(p, sample_entry);
    return FullBox("stsd", 0, p);
}

// Windows CRT 的 std::fseek 只吃 32 位 long；>2GB 的 mdat 回填会落错位。
// MSVC 与 MinGW-w64 都提供 _fseeki64（msvcrt 兼容层），这里统一走 64 位偏移。
bool SeekAbs(std::FILE* file, uint64_t offset) {
#if defined(_MSC_VER) || defined(__MINGW32__)
    return _fseeki64(file, static_cast<int64_t>(offset), SEEK_SET) == 0;
#else
    return std::fseek(file, static_cast<long>(offset), SEEK_SET) == 0;
#endif
}

}  // namespace

Mp4Writer::~Mp4Writer() {
    if (file_ != nullptr) {
        std::fclose(file_);
        file_ = nullptr;
    }
}

void Mp4Writer::SetFailWriteAfter(uint64_t writes) {
    fail_after_ = writes;
}

bool Mp4Writer::WriteRaw(const void* data, size_t size) {
    if (file_ == nullptr || size == 0) {
        return file_ != nullptr;
    }
    ++writes_;
    if (fail_after_ != 0 && writes_ > fail_after_) {
        return false;
    }
    if (std::fwrite(data, 1, size, file_) != size) {
        return false;
    }
    bytes_written_ += size;
    return true;
}

bool Mp4Writer::Open(const std::string& path, std::string& error) {
    path_ = path;
    file_ = std::fopen(path.c_str(), "wb+");
    if (file_ == nullptr) {
        error = "cannot open output file: " + path;
        return false;
    }

    const std::vector<uint8_t> ftyp = FtypBox();
    if (!WriteRaw(ftyp.data(), ftyp.size())) {
        error = "write ftyp failed";
        return false;
    }

    // mdat 用 64 位 largesize（size=1 + type + 8 字节 largesize），Close 时回填。
    // 这样 >4GB 的录制也不会把大小字段写错；头固定 16 字节。
    mdat_offset_ = bytes_written_;
    std::vector<uint8_t> mdat_header;
    Put32(mdat_header, 1);
    PutType(mdat_header, "mdat");
    Put64(mdat_header, 0);
    if (!WriteRaw(mdat_header.data(), mdat_header.size())) {
        error = "write mdat header failed";
        return false;
    }

    video_.timescale = kVideoTimescale;
    error.clear();
    return true;
}

bool Mp4Writer::SetVideoTrack(const VideoTrackInfo& info, std::string& error) {
    if (file_ == nullptr) {
        error = "file is not open";
        return false;
    }
    video_info_ = info;
    has_video_ = true;
    error.clear();
    return true;
}

bool Mp4Writer::SetAudioTrack(const AudioTrackInfo& info, std::string& error) {
    if (file_ == nullptr) {
        error = "file is not open";
        return false;
    }
    audio_info_ = info;
    audio_.timescale = info.sample_rate == 0 ? 48000u : info.sample_rate;
    has_audio_ = true;
    error.clear();
    return true;
}

bool Mp4Writer::AppendSample(Track& track, const uint8_t* data, size_t size,
                             int64_t pts_100ns, bool sync) {
    if (file_ == nullptr || data == nullptr || size == 0 ||
        size > static_cast<size_t>(std::numeric_limits<uint32_t>::max())) {
        return false;
    }
    track.pts_100ns.push_back(pts_100ns);

    const uint64_t offset = bytes_written_;
    if (!WriteRaw(data, size)) {
        return false;
    }
    track.offsets.push_back(offset);
    track.sizes.push_back(static_cast<uint32_t>(size));
    if (sync) {
        track.sync.push_back(static_cast<uint32_t>(track.sizes.size()));  // 1-based
    }
    return true;
}

// 轮 48：实录发现 MF 软编带 B 帧，输出 PTS 在解码顺序里是
// 1,3,2,5,4,…（非单调）。直接拿相邻 PTS 差当 stts 会写出 0/9000 交替的错表
// （实测 format duration 7.37s vs 录制 5s，ffmpeg 报 DTS 非单调）。
// 正解：把 PTS 集合排序得到真实 DTS 序列 → stts 取相邻 DTS 差；
// 每个样本的 PTS-DTS 差放 ctts，显示顺序由容器还原。
void Mp4Writer::FinalizeTrack(Track& track, uint32_t nominal_delta) {
    const size_t n = track.sizes.size();
    track.deltas.clear();
    track.ctts.clear();
    if (n == 0) {
        return;
    }

    std::vector<int64_t> dts = track.pts_100ns;
    std::sort(dts.begin(), dts.end());
    for (size_t i = 1; i < dts.size(); ++i) {
        if (dts[i] <= dts[i - 1]) {
            dts[i] = dts[i - 1] + 1;  // 防御性修正：重复 PTS 不得让 DTS 倒退
        }
    }

    track.deltas.reserve(n);
    for (size_t i = 0; i + 1 < n; ++i) {
        track.deltas.push_back(DeltaFromPts(dts[i + 1] - dts[i], track.timescale));
    }
    // 最后一个样本没有「下一个 DTS」：沿用上一个真实差值（不是 1/fps 推算）。
    track.deltas.push_back(n == 1 ? nominal_delta : track.deltas.back());

    track.ctts.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        track.ctts.push_back(SignedPtsToTicks(track.pts_100ns[i] - dts[i], track.timescale));
    }
}

uint32_t Mp4Writer::TrackDuration(const Track& track) {
    const uint64_t total = std::accumulate(track.deltas.begin(), track.deltas.end(), 0ULL);
    return total > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())
               ? std::numeric_limits<uint32_t>::max()
               : static_cast<uint32_t>(total);
}

bool Mp4Writer::WriteVideo(const uint8_t* data, size_t size, int64_t pts_100ns, bool keyframe) {
    if (!has_video_) {
        return false;
    }
    // 轮 48：stss 只认真实 IDR。调用侧的 keyframe 是按 GOP 假设算的（见
    // RecordingSession.cpp），不能拿来当 IDR 用，否则 seek 表会错位。
    (void)keyframe;
    return AppendSample(video_, data, size, pts_100ns, HasIdr(data, size));
}

bool Mp4Writer::WriteAudio(const uint8_t* data, size_t size, int64_t pts_100ns) {
    if (!has_audio_) {
        return false;
    }
    // AAC 每帧都是同步样本，stss 省略（等于「全部同步」）。
    return AppendSample(audio_, data, size, pts_100ns, false);
}

bool Mp4Writer::FlushPending() {
    if (file_ == nullptr) {
        return true;
    }
    return std::fflush(file_) == 0;
}

bool Mp4Writer::PatchMdatSize(std::string& error) {
    const uint64_t mdat_size = bytes_written_ - mdat_offset_;
    uint8_t raw[8] = {};
    for (int i = 0; i < 8; ++i) {
        raw[i] = static_cast<uint8_t>(mdat_size >> (8 * (7 - i)));
    }
    if (!SeekAbs(file_, mdat_offset_ + 8)) {
        error = "seek to mdat largesize failed";
        return false;
    }
    if (std::fwrite(raw, 1, sizeof(raw), file_) != sizeof(raw)) {
        error = "patch mdat largesize failed";
        return false;
    }
    if (std::fseek(file_, 0, SEEK_END) != 0) {
        error = "seek back to end failed";
        return false;
    }
    return true;
}

std::vector<uint8_t> Mp4Writer::BuildVideoTrak(uint32_t movie_duration_ms) const {
    std::vector<uint8_t> minf;
    std::vector<uint8_t> vmhd;
    Put16(vmhd, 0);       // graphicsmode
    Put16(vmhd, 0); Put16(vmhd, 0); Put16(vmhd, 0);  // opcolor
    Append(minf, FullBox("vmhd", 0x000001, vmhd));
    Append(minf, DinfBox());

    std::vector<uint8_t> stbl;
    Append(stbl, StsdBox(Avc1Box(video_info_)));
    Append(stbl, SttsBox(video_.deltas));
    Append(stbl, CttsBox(video_.ctts));
    // 没有 stss 表示「全部同步」；写 0 条反而表示「无同步样本」，必须区分。
    if (!video_.sync.empty() && video_.sync.size() < video_.sizes.size()) {
        Append(stbl, StssBox(video_.sync));
    }
    Append(stbl, StscBox(video_.sizes.size()));
    Append(stbl, StszBox(video_.sizes));
    Append(stbl, ChunkOffsetBox(video_.offsets));
    Append(minf, Box("stbl", stbl));

    std::vector<uint8_t> mdia;
    Append(mdia, MdhdBox(video_.timescale, TrackDuration(video_)));
    Append(mdia, HdlrBox("vide", "VideoHandler"));
    Append(mdia, Box("minf", minf));

    std::vector<uint8_t> trak;
    Append(trak, TkhdBox(1, movie_duration_ms, video_info_.width, video_info_.height, false));
    Append(trak, Box("mdia", mdia));
    return Box("trak", trak);
}

std::vector<uint8_t> Mp4Writer::BuildAudioTrak(uint32_t movie_duration_ms) const {
    std::vector<uint8_t> minf;
    std::vector<uint8_t> smhd;
    Put16(smhd, 0); Put16(smhd, 0);  // balance / reserved
    Append(minf, FullBox("smhd", 0, smhd));
    Append(minf, DinfBox());

    std::vector<uint8_t> stbl;
    Append(stbl, StsdBox(Mp4aBox(audio_info_)));
    Append(stbl, SttsBox(audio_.deltas));
    Append(stbl, CttsBox(audio_.ctts));
    Append(stbl, StscBox(audio_.sizes.size()));
    Append(stbl, StszBox(audio_.sizes));
    Append(stbl, ChunkOffsetBox(audio_.offsets));
    Append(minf, Box("stbl", stbl));

    std::vector<uint8_t> mdia;
    Append(mdia, MdhdBox(audio_.timescale, TrackDuration(audio_)));
    Append(mdia, HdlrBox("soun", "SoundHandler"));
    Append(mdia, Box("minf", minf));

    std::vector<uint8_t> trak;
    Append(trak, TkhdBox(2, movie_duration_ms, 0, 0, true));
    Append(trak, Box("mdia", mdia));
    return Box("trak", trak);
}

std::vector<uint8_t> Mp4Writer::BuildMoov() const {
    const uint64_t audio_ticks = TrackDuration(audio_);
    int64_t max_ctts = 0;
    for (int32_t value : video_.ctts) {
        max_ctts = std::max(max_ctts, static_cast<int64_t>(value));
    }
    // 显示时间轴可能比解码时间轴长一个最大 ctts 偏移（B 帧），mvhd 要覆盖它。
    const uint64_t video_ticks = static_cast<uint64_t>(TrackDuration(video_)) +
                                 (max_ctts > 0 ? static_cast<uint64_t>(max_ctts) : 0);
    const uint64_t video_ms = video_ticks * kMovieTimescale / video_.timescale;
    const uint64_t audio_ms = audio_ticks * kMovieTimescale / audio_.timescale;
    const uint64_t movie_ms = std::max(video_ms, audio_ms);
    const uint32_t duration_ms = static_cast<uint32_t>(
        std::min<uint64_t>(movie_ms, std::numeric_limits<uint32_t>::max()));

    std::vector<uint8_t> moov;
    Append(moov, MvhdBox(duration_ms, has_audio_ ? 3u : 2u));
    if (has_video_) {
        Append(moov, BuildVideoTrak(duration_ms));
    }
    if (has_audio_) {
        Append(moov, BuildAudioTrak(duration_ms));
    }
    return Box("moov", moov);
}

bool Mp4Writer::Close(std::string& error) {
    if (file_ == nullptr) {
        error = "file is not open";
        return false;
    }

    // 收尾生成 sample table：stts 来自排序后的真实 DTS 差，ctts 还原 B 帧显示顺序。
    // nominal 只在「只有一个样本」时兜底，不是逐帧累加 1/fps。
    FinalizeTrack(video_, video_.timescale / (video_info_.fps == 0 ? 30u : video_info_.fps));
    FinalizeTrack(audio_, 1024);

    if (!PatchMdatSize(error)) {
        std::fflush(file_);
        std::fclose(file_);
        file_ = nullptr;
        return false;
    }

    const std::vector<uint8_t> moov = BuildMoov();
    if (!WriteRaw(moov.data(), moov.size())) {
        error = "write moov failed";
        std::fflush(file_);
        std::fclose(file_);
        file_ = nullptr;
        return false;
    }

    std::fflush(file_);
    std::fclose(file_);
    file_ = nullptr;
    error.clear();
    return true;
}

}  // namespace sr::mux
