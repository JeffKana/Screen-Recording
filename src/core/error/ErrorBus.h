#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "core/types/Frame.h"

namespace sr {

enum class Severity : uint8_t {
    Warn = 0,       // 仅记录 + 面板标记（丢帧、背压）
    Recoverable,    // 尝试自动恢复并 toast
    Fatal,          // 走 Stopping 收尾并退出录制态
};

enum class ErrorCode : uint16_t {
    None = 0,
    EncoderInitFailed,
    EncoderRuntimeFailed,
    CaptureStartFailed,
    CaptureLost,
    CaptureTimeout,
    AudioDeviceLost,
    DiskSpaceLow,
    DiskFull,
    PathNotWritable,
    FileLocked,
    GpuDeviceLost,
    UnsupportedSource,
    RecoveryFailed,
};

// 模块标识。与 02 文档 §7 的模块划分一一对应，便于日志过滤。
enum class RecorderModule : uint8_t {
    Core = 0,
    Ipc,
    Capture,
    Audio,
    Compositor,
    Encode,
    Mux,
    Recovery,
    App,
};

const char* ToString(ErrorCode code) noexcept;
const char* ToString(Severity severity) noexcept;
const char* ToString(RecorderModule module) noexcept;

struct ErrorEvent {
    ErrorCode code = ErrorCode::None;
    Severity severity = Severity::Warn;
    RecorderModule module = RecorderModule::Core;
    std::string user_message;
    Tick at{};
};

class IErrorSink {
public:
    virtual ~IErrorSink() = default;
    virtual void OnError(const ErrorEvent& event) = 0;
};

// 全局错误总线（02 文档 §7.1）。任何模块都不允许直接弹窗或吞掉异常，
// 必须经此总线，否则 M8 全模块不可验收。
class ErrorBus {
public:
    void Subscribe(IErrorSink* sink);
    void Unsubscribe(IErrorSink* sink);
    void Publish(const ErrorEvent& event);
    size_t SubscriberCount() const;

private:
    mutable std::mutex mutex_;
    std::vector<IErrorSink*> sinks_;
};

}  // namespace sr
