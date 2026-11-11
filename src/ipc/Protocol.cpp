#include "ipc/Protocol.h"

#include <cstring>

namespace sr::ipc {

void Writer::U8(uint8_t value) {
    buffer_.push_back(value);
}

void Writer::U16(uint16_t value) {
    buffer_.push_back(static_cast<uint8_t>(value & 0xFF));
    buffer_.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}

void Writer::U32(uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        buffer_.push_back(static_cast<uint8_t>((value >> shift) & 0xFF));
    }
}

void Writer::U64(uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
        buffer_.push_back(static_cast<uint8_t>((value >> shift) & 0xFF));
    }
}

void Writer::Str(const std::string& value) {
    const uint16_t length = static_cast<uint16_t>(value.size() > 0xFFFFu ? 0xFFFFu : value.size());
    U16(length);
    buffer_.insert(buffer_.end(), value.begin(), value.begin() + length);
}

bool Reader::Ensure(size_t n) {
    if (!ok_ || pos_ + n > size_) {
        ok_ = false;
        return false;
    }
    return true;
}

bool Reader::U8(uint8_t& value) {
    if (!Ensure(1)) {
        return false;
    }
    value = data_[pos_++];
    return true;
}

bool Reader::U16(uint16_t& value) {
    if (!Ensure(2)) {
        return false;
    }
    value = static_cast<uint16_t>(data_[pos_]) | static_cast<uint16_t>(data_[pos_ + 1]) << 8;
    pos_ += 2;
    return true;
}

bool Reader::U32(uint32_t& value) {
    if (!Ensure(4)) {
        return false;
    }
    value = 0;
    for (int i = 0; i < 4; ++i) {
        value |= static_cast<uint32_t>(data_[pos_ + i]) << (8 * i);
    }
    pos_ += 4;
    return true;
}

bool Reader::U64(uint64_t& value) {
    if (!Ensure(8)) {
        return false;
    }
    value = 0;
    for (int i = 0; i < 8; ++i) {
        value |= static_cast<uint64_t>(data_[pos_ + i]) << (8 * i);
    }
    pos_ += 8;
    return true;
}

bool Reader::Str(std::string& value, size_t max_length) {
    uint16_t length = 0;
    if (!U16(length) || !Ensure(length)) {
        return false;
    }
    if (length > max_length) {
        ok_ = false;
        return false;
    }
    value.assign(reinterpret_cast<const char*>(data_ + pos_), length);
    pos_ += length;
    return true;
}

std::vector<uint8_t> EncodeFrame(MessageType type, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> frame;
    frame.reserve(kHeaderSize + payload.size());
    Writer writer(frame);
    writer.U32(kMagic);
    writer.U16(kProtocolVersion);
    writer.U16(static_cast<uint16_t>(type));
    writer.U32(static_cast<uint32_t>(payload.size()));
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

bool DecodeHeader(const uint8_t* header, size_t header_size, MessageType& out_type, uint32_t& out_payload_length) {
    if (header == nullptr || header_size < kHeaderSize) {
        return false;
    }
    Reader reader(header, header_size);
    uint32_t magic = 0;
    uint16_t version = 0;
    uint16_t type = 0;
    uint32_t length = 0;
    if (!reader.U32(magic) || !reader.U16(version) || !reader.U16(type) || !reader.U32(length)) {
        return false;
    }
    if (magic != kMagic || version != kProtocolVersion || length > kMaxPayload) {
        return false;
    }
    if (type < static_cast<uint16_t>(MessageType::Hello) || type > static_cast<uint16_t>(MessageType::Shutdown)) {
        return false;
    }
    out_type = static_cast<MessageType>(type);
    out_payload_length = length;
    return true;
}

std::vector<uint8_t> Encode(const HelloPayload& value) {
    std::vector<uint8_t> buffer;
    Writer writer(buffer);
    writer.Str(value.token);
    return buffer;
}

std::vector<uint8_t> Encode(const HelloAckPayload& value) {
    std::vector<uint8_t> buffer;
    Writer writer(buffer);
    writer.U16(value.protocol_version);
    writer.U32(value.process_id);
    writer.U8(value.state);
    return buffer;
}

std::vector<uint8_t> Encode(const CommandPayload& value) {
    std::vector<uint8_t> buffer;
    Writer writer(buffer);
    writer.U8(value.command);
    return buffer;
}

std::vector<uint8_t> Encode(const StateSnapshotPayload& value) {
    std::vector<uint8_t> buffer;
    Writer writer(buffer);
    writer.U8(value.state);
    writer.U8(value.parameter_locked ? 1u : 0u);
    writer.U64(value.session_elapsed_ms);
    writer.U8(value.kind);
    // 扩展块：自描述长度，旧客户端按 Remaining() 整块跳过。
    writer.U16(kSnapshotExtBytes);
    writer.U16(kSnapshotExtVersion);
    writer.U32(value.metrics.capture_fps_x100);
    writer.U32(value.metrics.encode_fps_x100);
    writer.U64(value.metrics.frames_dropped);
    writer.U64(value.metrics.frames_captured);
    return buffer;
}

std::vector<uint8_t> Encode(const ErrorPayload& value) {
    std::vector<uint8_t> buffer;
    Writer writer(buffer);
    writer.U16(value.code);
    writer.U8(value.severity);
    writer.Str(value.message);
    return buffer;
}

bool Decode(const std::vector<uint8_t>& buffer, HelloPayload& out) {
    Reader reader(buffer.data(), buffer.size());
    return reader.Str(out.token);
}

bool Decode(const std::vector<uint8_t>& buffer, HelloAckPayload& out) {
    Reader reader(buffer.data(), buffer.size());
    uint8_t state = 0;
    if (!reader.U16(out.protocol_version) || !reader.U32(out.process_id) || !reader.U8(state)) {
        return false;
    }
    out.state = state;
    return true;
}

bool Decode(const std::vector<uint8_t>& buffer, CommandPayload& out) {
    Reader reader(buffer.data(), buffer.size());
    return reader.U8(out.command);
}

bool Decode(const std::vector<uint8_t>& buffer, StateSnapshotPayload& out) {
    Reader reader(buffer.data(), buffer.size());
    uint8_t locked = 0;
    if (!reader.U8(out.state) || !reader.U8(locked) || !reader.U64(out.session_elapsed_ms)) {
        return false;
    }
    out.parameter_locked = locked != 0;
    out.kind = 0;
    if (reader.Remaining() >= 1) {
        reader.U8(out.kind);  // 旧载荷（10 字节）按周期快照处理，保持向后兼容
    }
    out.metrics = SnapshotMetrics{};
    out.has_metrics = false;
    if (reader.Remaining() >= 2) {
        uint16_t ext_bytes = 0;
        if (!reader.U16(ext_bytes)) {
            return true;  // 扩展块坏了不影响 base 字段
        }
        if (reader.Remaining() >= ext_bytes && ext_bytes >= 2) {
            uint16_t ext_version = 0;
            if (reader.U16(ext_version) && ext_version >= 1 &&
                reader.Remaining() >= kSnapshotExtFields) {
                reader.U32(out.metrics.capture_fps_x100);
                reader.U32(out.metrics.encode_fps_x100);
                reader.U64(out.metrics.frames_dropped);
                reader.U64(out.metrics.frames_captured);
                out.has_metrics = true;
            }
        }
    }
    return true;
}

bool Decode(const std::vector<uint8_t>& buffer, ErrorPayload& out) {
    Reader reader(buffer.data(), buffer.size());
    if (!reader.U16(out.code) || !reader.U8(out.severity)) {
        return false;
    }
    return reader.Str(out.message);
}

}  // namespace sr::ipc
