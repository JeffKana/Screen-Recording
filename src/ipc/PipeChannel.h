#pragma once

#include <memory>
#include <string>

#include "ipc/Protocol.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace sr::ipc {

// 管道名必须含 pid，避免多会话冲突（CONTRACT.md §7）。
std::wstring MakePipeName(uint32_t process_id);

class PipeConnection {
public:
    ~PipeConnection();
    PipeConnection(PipeConnection&& other) noexcept;
    PipeConnection& operator=(PipeConnection&& other) noexcept;
    PipeConnection(const PipeConnection&) = delete;
    PipeConnection& operator=(const PipeConnection&) = delete;

    bool Send(const Message& message);

    // 阻塞读取一整帧。返回 false 表示对端断开或协议错误。
    bool Receive(Message& out);

    // 非阻塞接收：只有「整帧已到」才消费，半帧一律不读。
    // 返回 false 时调用方应检查 IsOpen() 区分「暂无数据」与「对端断开」。
    bool TryReceive(Message& out);

    void Close();
    bool IsOpen() const { return handle_ != INVALID_HANDLE_VALUE; }

private:
    explicit PipeConnection(HANDLE handle, std::wstring name);

    HANDLE handle_ = INVALID_HANDLE_VALUE;
    std::wstring name_;
    bool client_seen_ = false;
    friend class PipeServer;
    friend class PipeClient;
};

// 单客户端模型：一次只服务一个 UI 进程，连接断开后重新等待（G1 够用）。
class PipeServer {
public:
    ~PipeServer();
    PipeServer() = default;
    PipeServer(const PipeServer&) = delete;
    PipeServer& operator=(const PipeServer&) = delete;

    bool Open(const std::wstring& name, std::string& error);

    // 阻塞至有客户端连接。
    std::unique_ptr<PipeConnection> WaitForClient(std::string& error);

    // 带超时的等待：靠 PIPE_NOWAIT 轮询 ConnectNamedPipe 实现，
    // 不引线程、不改成异步 I/O（CONTRACT §7 线格式与句柄模型保持不变）。
    std::unique_ptr<PipeConnection> WaitForClient(uint32_t timeout_ms, std::string& error);

    void Close();

    // 非阻塞地尝试接受一个客户端（未连上返回 nullptr，不阻塞）。
    std::unique_ptr<PipeConnection> TryAcceptClient(std::string& error);

    // 曾有客户端连上（哪怕立刻断开）：用于区分「从未连接」与「连过又断」。
    bool ClientSeen() const { return client_seen_; }

private:
    bool CreateInstance(std::string& error);

    HANDLE handle_ = INVALID_HANDLE_VALUE;
    std::wstring name_;
    bool client_seen_ = false;
};

class PipeClient {
public:
    static std::unique_ptr<PipeConnection> Connect(const std::wstring& name,
                                                   std::string& error,
                                                   uint32_t timeout_ms = 3000);
};

// 把 GetLastError() 转成可读文本，供日志与诊断包使用（M8-14）。
std::string LastErrorMessage(const char* context);

}  // namespace sr::ipc
