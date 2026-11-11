#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sr::ipc {

// 与 CONTRACT.md §7 严格一致。修改本文件必须走契约变更单。
constexpr uint32_t kMagic = 0x53455243;  // "SERC"
constexpr uint16_t kProtocolVersion = 1;
constexpr size_t kHeaderSize = 12;               // magic(4)+version(2)+type(2)+len(4)
constexpr uint32_t kMaxPayload = 1024u * 1024u;  // 1 MiB，超限直接断开

enum class MessageType : uint16_t {
    Hello = 1,
    HelloAck = 2,
    Command = 3,
    StateSnapshot = 4,
    ErrorEvent = 5,
    Shutdown = 6,
};

struct Message {
    MessageType type = MessageType::Hello;
    std::vector<uint8_t> payload;
};

// ---- 载荷结构 ----

struct HelloPayload {
    std::string token;
};

struct HelloAckPayload {
    uint16_t protocol_version = kProtocolVersion;
    uint32_t process_id = 0;
    uint8_t state = 0;
};

struct CommandPayload {
    uint8_t command = 0;
};

// 快照来源：录制中每秒推送的周期快照 vs 命令回执。
// 命令客户端（srctl）必须等到 CommandReceipt，不能把排队中的周期快照当成命令结果。
enum class SnapshotKind : uint8_t {
    Periodic = 0,
    CommandReceipt = 1,
};

// 快照扩展块（轮 30 / M6-03）：三次追加字段的教训（kind、metrics），
// 这次改成**自描述长度**：base 之后是 `u16 ext_bytes + ext 载荷`，
// 旧客户端用 Reader::Remaining() 跳过整块，未来加字段只需升 ext_version，
// 不必再改 base 布局。协议版本仍为 1（见 CONTRACT §7）。
constexpr uint16_t kSnapshotExtVersion = 1;
constexpr uint16_t kSnapshotExtFields = 4 + 4 + 8 + 8;  // cap/enc x100 + dropped + captured
constexpr uint16_t kSnapshotExtBytes = 2 + kSnapshotExtFields;

struct SnapshotMetrics {
    uint32_t capture_fps_x100 = 0;  // 采集侧到达帧率，每秒滑窗
    uint32_t encode_fps_x100 = 0;   // 编码侧提交帧率，每秒滑窗
    uint64_t frames_dropped = 0;    // 编码提交失败 / 注入丢帧
    uint64_t frames_captured = 0;   // 采集侧累计到达帧数
};

struct StateSnapshotPayload {
    uint8_t state = 0;
    bool parameter_locked = false;
    uint64_t session_elapsed_ms = 0;
    uint8_t kind = static_cast<uint8_t>(SnapshotKind::Periodic);
    // 轮 30（M6-03）：尾部扩展块，旧的 11 字节载荷解析后 has_metrics=false。
    SnapshotMetrics metrics;
    bool has_metrics = false;
};

struct ErrorPayload {
    uint16_t code = 0;
    uint8_t severity = 0;
    std::string message;
};

// ---- 小端序列化 ----

class Writer {
public:
    explicit Writer(std::vector<uint8_t>& buffer) : buffer_(buffer) {}

    void U8(uint8_t value);
    void U16(uint16_t value);
    void U32(uint32_t value);
    void U64(uint64_t value);
    void Str(const std::string& value);

private:
    std::vector<uint8_t>& buffer_;
};

class Reader {
public:
    Reader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    bool U8(uint8_t& value);
    bool U16(uint16_t& value);
    bool U32(uint32_t& value);
    bool U64(uint64_t& value);
    bool Str(std::string& value, size_t max_length = 256);

    bool Ok() const { return ok_; }
    size_t Remaining() const { return size_ - pos_; }

private:
    bool Ensure(size_t n);

    const uint8_t* data_ = nullptr;
    size_t size_ = 0;
    size_t pos_ = 0;
    bool ok_ = true;
};

// ---- 分帧 ----

// 返回 头 + 载荷 的完整字节串。
std::vector<uint8_t> EncodeFrame(MessageType type, const std::vector<uint8_t>& payload);

// 校验头部。失败（magic/版本/长度非法）返回 false，调用方必须断开连接。
bool DecodeHeader(const uint8_t* header, size_t header_size, MessageType& out_type, uint32_t& out_payload_length);

// ---- 载荷编解码 ----

std::vector<uint8_t> Encode(const HelloPayload& value);
std::vector<uint8_t> Encode(const HelloAckPayload& value);
std::vector<uint8_t> Encode(const CommandPayload& value);
std::vector<uint8_t> Encode(const StateSnapshotPayload& value);
std::vector<uint8_t> Encode(const ErrorPayload& value);

bool Decode(const std::vector<uint8_t>& buffer, HelloPayload& out);
bool Decode(const std::vector<uint8_t>& buffer, HelloAckPayload& out);
bool Decode(const std::vector<uint8_t>& buffer, CommandPayload& out);
bool Decode(const std::vector<uint8_t>& buffer, StateSnapshotPayload& out);
bool Decode(const std::vector<uint8_t>& buffer, ErrorPayload& out);

}  // namespace sr::ipc
