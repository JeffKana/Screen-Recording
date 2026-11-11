#include "ipc/PipeChannel.h"

#include <vector>

namespace sr::ipc {
namespace {

constexpr DWORD kBufferSize = 64 * 1024;

bool ReadExact(HANDLE handle, uint8_t* buffer, size_t size) {
    size_t offset = 0;
    while (offset < size) {
        DWORD read = 0;
        const DWORD want = static_cast<DWORD>(size - offset);
        if (!ReadFile(handle, buffer + offset, want, &read, nullptr)) {
            if (GetLastError() == ERROR_MORE_DATA) {
                offset += read;
                continue;
            }
            return false;
        }
        if (read == 0) {
            return false;  // 对端关闭
        }
        offset += read;
    }
    return true;
}

bool WriteExact(HANDLE handle, const uint8_t* buffer, size_t size) {
    size_t offset = 0;
    while (offset < size) {
        DWORD written = 0;
        const DWORD want = static_cast<DWORD>(size - offset);
        if (!WriteFile(handle, buffer + offset, want, &written, nullptr)) {
            return false;
        }
        if (written == 0) {
            return false;
        }
        offset += written;
    }
    return true;
}

}  // namespace

std::string LastErrorMessage(const char* context) {
    const DWORD code = GetLastError();
    std::string text = context != nullptr ? context : "win32";
    text += " failed, GetLastError=";
    text += std::to_string(static_cast<unsigned long>(code));
    return text;
}

std::wstring MakePipeName(uint32_t process_id) {
    return L"\\\\.\\pipe\\sr.recorder." + std::to_wstring(process_id);
}

PipeConnection::PipeConnection(HANDLE handle, std::wstring name)
    : handle_(handle), name_(std::move(name)) {}

PipeConnection::~PipeConnection() {
    Close();
}

PipeConnection::PipeConnection(PipeConnection&& other) noexcept
    : handle_(other.handle_), name_(std::move(other.name_)) {
    other.handle_ = INVALID_HANDLE_VALUE;
}

PipeConnection& PipeConnection::operator=(PipeConnection&& other) noexcept {
    if (this != &other) {
        Close();
        handle_ = other.handle_;
        name_ = std::move(other.name_);
        other.handle_ = INVALID_HANDLE_VALUE;
    }
    return *this;
}

bool PipeConnection::Send(const Message& message) {
    if (!IsOpen()) {
        return false;
    }
    if (message.payload.size() > kMaxPayload) {
        return false;
    }
    const std::vector<uint8_t> frame = EncodeFrame(message.type, message.payload);
    return WriteExact(handle_, frame.data(), frame.size());
}

bool PipeConnection::Receive(Message& out) {
    if (!IsOpen()) {
        return false;
    }
    uint8_t header[kHeaderSize] = {};
    if (!ReadExact(handle_, header, kHeaderSize)) {
        return false;
    }
    MessageType type = MessageType::Hello;
    uint32_t payload_length = 0;
    if (!DecodeHeader(header, kHeaderSize, type, payload_length)) {
        // magic/版本/长度非法：按契约直接断开，不做容错。
        Close();
        return false;
    }
    out.type = type;
    out.payload.assign(payload_length, 0);
    if (payload_length > 0 && !ReadExact(handle_, out.payload.data(), payload_length)) {
        Close();
        return false;
    }
    return true;
}

bool PipeConnection::TryReceive(Message& out) {
    if (handle_ == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD available = 0;
    if (!PeekNamedPipe(handle_, nullptr, 0, nullptr, &available, nullptr)) {
        Close();  // 对端已断开
        return false;
    }
    if (available < kHeaderSize) {
        return false;
    }

    // 只探头部，不消费：这样半帧不会把读指针推进到框架中间。
    uint8_t header[kHeaderSize] = {};
    DWORD peeked = 0;
    if (!PeekNamedPipe(handle_, header, kHeaderSize, &peeked, &available, nullptr) ||
        peeked < kHeaderSize) {
        return false;
    }

    MessageType type{};
    uint32_t payload_length = 0;
    if (!DecodeHeader(header, kHeaderSize, type, payload_length)) {
        Close();  // 协议错误：宁可断开，也不在半帧上继续
        return false;
    }
    if (available < kHeaderSize + payload_length) {
        return false;  // 整帧未到齐
    }

    // 整帧已就绪，此时 Receive 不会阻塞。
    return Receive(out);
}

void PipeConnection::Close() {
    if (handle_ != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(handle_);
        DisconnectNamedPipe(handle_);
        CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }
}

PipeServer::~PipeServer() {
    Close();
}

bool PipeServer::Open(const std::wstring& name, std::string& error) {
    Close();
    name_ = name;
    return CreateInstance(error);
}

bool PipeServer::CreateInstance(std::string& error) {
    HANDLE handle = CreateNamedPipeW(name_.c_str(),
                                     PIPE_ACCESS_DUPLEX,
                                     PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                     PIPE_UNLIMITED_INSTANCES,
                                     kBufferSize,
                                     kBufferSize,
                                     0,
                                     nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        error = LastErrorMessage("CreateNamedPipeW");
        return false;
    }
    handle_ = handle;
    error.clear();
    return true;
}

std::unique_ptr<PipeConnection> PipeServer::TryAcceptClient(std::string& error) {
    if (handle_ == INVALID_HANDLE_VALUE) {
        if (!CreateInstance(error)) {
            return nullptr;
        }
    }

    DWORD mode = PIPE_READMODE_BYTE | PIPE_NOWAIT;
    const bool nowait_ok = SetNamedPipeHandleState(handle_, &mode, nullptr, nullptr) != 0;
    const BOOL connected = ConnectNamedPipe(handle_, nullptr);
    const DWORD last_error = GetLastError();
    if (connected == 0 && last_error != ERROR_PIPE_CONNECTED) {
        if (last_error == ERROR_NO_DATA) {
            client_seen_ = true;
            Close();
        }
        return nullptr;
    }

    if (nowait_ok) {
        DWORD blocking_mode = PIPE_READMODE_BYTE;
        SetNamedPipeHandleState(handle_, &blocking_mode, nullptr, nullptr);
    }
    auto connection = std::unique_ptr<PipeConnection>(new PipeConnection(handle_, name_));
    handle_ = INVALID_HANDLE_VALUE;
    error.clear();
    return connection;
}

std::unique_ptr<PipeConnection> PipeServer::WaitForClient(uint32_t timeout_ms, std::string& error) {
    if (handle_ == INVALID_HANDLE_VALUE) {
        error = "pipe is not open";
        return nullptr;
    }

    // 临时切到非阻塞模式，让 ConnectNamedPipe 立刻返回，从而实现「带超时的同步 accept」。
    // 不引线程、不改成 OVERLAPPED（会连读路径一起改，见 CONTRACT §7）。
    DWORD mode = PIPE_READMODE_BYTE | PIPE_NOWAIT;
    const bool nowait_ok = SetNamedPipeHandleState(handle_, &mode, nullptr, nullptr) != 0;

    const ULONGLONG deadline = GetTickCount64() + (timeout_ms == 0 ? 1u : timeout_ms);
    for (;;) {
        const BOOL connected = ConnectNamedPipe(handle_, nullptr);
        const DWORD last_error = GetLastError();
        if (connected != 0 || last_error == ERROR_PIPE_CONNECTED) {
            break;
        }
        if (last_error != ERROR_PIPE_LISTENING && last_error != ERROR_NO_DATA) {
            error = LastErrorMessage("ConnectNamedPipe");
            return nullptr;
        }
        if (last_error == ERROR_NO_DATA) {
            // 上一客户端已断开，实例作废：重建后再等。
            Close();
            if (!CreateInstance(error)) {
                return nullptr;
            }
            if (nowait_ok) {
                SetNamedPipeHandleState(handle_, &mode, nullptr, nullptr);
            }
            continue;
        }
        if (GetTickCount64() >= deadline) {
            error = "no client connected within timeout";
            return nullptr;
        }
        Sleep(5);
    }

    if (nowait_ok) {
        DWORD blocking_mode = PIPE_READMODE_BYTE;
        SetNamedPipeHandleState(handle_, &blocking_mode, nullptr, nullptr);
    }

    auto connection = std::unique_ptr<PipeConnection>(new PipeConnection(handle_, name_));
    handle_ = INVALID_HANDLE_VALUE;
    error.clear();
    return connection;
}
std::unique_ptr<PipeConnection> PipeServer::WaitForClient(std::string& error) {
    if (handle_ == INVALID_HANDLE_VALUE) {
        error = "pipe server is not open";
        return nullptr;
    }
    if (!ConnectNamedPipe(handle_, nullptr)) {
        const DWORD code = GetLastError();
        if (code != ERROR_PIPE_CONNECTED) {
            error = LastErrorMessage("ConnectNamedPipe");
            Close();
            return nullptr;
        }
    }

    HANDLE connected = handle_;
    handle_ = INVALID_HANDLE_VALUE;

    // 立刻准备下一个实例，保证断开后可重连。
    std::string next_error;
    if (!CreateInstance(next_error)) {
        // 下一个实例创建失败不阻断当前会话，只影响重连能力。
        error = next_error;
    } else {
        error.clear();
    }

    return std::unique_ptr<PipeConnection>(new PipeConnection(connected, name_));
}

void PipeServer::Close() {
    if (handle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }
}

std::unique_ptr<PipeConnection> PipeClient::Connect(const std::wstring& name,
                                                    std::string& error,
                                                    uint32_t timeout_ms) {
    const ULONGLONG deadline = GetTickCount64() + timeout_ms;
    for (;;) {
        HANDLE handle = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (handle != INVALID_HANDLE_VALUE) {
            error.clear();
            return std::unique_ptr<PipeConnection>(new PipeConnection(handle, name));
        }
        const DWORD code = GetLastError();
        if (code != ERROR_PIPE_BUSY && code != ERROR_FILE_NOT_FOUND) {
            error = LastErrorMessage("CreateFileW");
            return nullptr;
        }
        if (GetTickCount64() >= deadline) {
            error = "connect timeout";
            return nullptr;
        }
        // WaitNamedPipe 对「管道尚未创建」会立即返回失败。必须退避，
        // 否则 UI 侧 50ms 级的非阻塞连接尝试会变成忙等，占满一个核。
        WaitNamedPipeW(name.c_str(), 20);
        Sleep(20);
    }
}

}  // namespace sr::ipc
