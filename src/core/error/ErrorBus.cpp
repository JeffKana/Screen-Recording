#include "core/error/ErrorBus.h"

#include <algorithm>

namespace sr {

const char* ToString(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::None: return "None";
        case ErrorCode::EncoderInitFailed: return "EncoderInitFailed";
        case ErrorCode::EncoderRuntimeFailed: return "EncoderRuntimeFailed";
        case ErrorCode::CaptureStartFailed: return "CaptureStartFailed";
        case ErrorCode::CaptureLost: return "CaptureLost";
        case ErrorCode::CaptureTimeout: return "CaptureTimeout";
        case ErrorCode::AudioDeviceLost: return "AudioDeviceLost";
        case ErrorCode::DiskSpaceLow: return "DiskSpaceLow";
        case ErrorCode::DiskFull: return "DiskFull";
        case ErrorCode::PathNotWritable: return "PathNotWritable";
        case ErrorCode::FileLocked: return "FileLocked";
        case ErrorCode::GpuDeviceLost: return "GpuDeviceLost";
        case ErrorCode::UnsupportedSource: return "UnsupportedSource";
        case ErrorCode::RecoveryFailed: return "RecoveryFailed";
    }
    return "Unknown";
}

const char* ToString(Severity severity) noexcept {
    switch (severity) {
        case Severity::Warn: return "Warn";
        case Severity::Recoverable: return "Recoverable";
        case Severity::Fatal: return "Fatal";
    }
    return "Unknown";
}

const char* ToString(RecorderModule module) noexcept {
    switch (module) {
        case RecorderModule::Core: return "Core";
        case RecorderModule::Ipc: return "Ipc";
        case RecorderModule::Capture: return "Capture";
        case RecorderModule::Audio: return "Audio";
        case RecorderModule::Compositor: return "Compositor";
        case RecorderModule::Encode: return "Encode";
        case RecorderModule::Mux: return "Mux";
        case RecorderModule::Recovery: return "Recovery";
        case RecorderModule::App: return "App";
    }
    return "Unknown";
}

void ErrorBus::Subscribe(IErrorSink* sink) {
    if (sink == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (std::find(sinks_.begin(), sinks_.end(), sink) == sinks_.end()) {
        sinks_.push_back(sink);
    }
}

void ErrorBus::Unsubscribe(IErrorSink* sink) {
    std::lock_guard<std::mutex> lock(mutex_);
    sinks_.erase(std::remove(sinks_.begin(), sinks_.end(), sink), sinks_.end());
}

void ErrorBus::Publish(const ErrorEvent& event) {
    // 先拷贝订阅者列表再回调：避免 sink 在回调中订阅/退订导致死锁。
    std::vector<IErrorSink*> snapshot;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot = sinks_;
    }
    for (IErrorSink* sink : snapshot) {
        if (sink != nullptr) {
            sink->OnError(event);
        }
    }
}

size_t ErrorBus::SubscriberCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sinks_.size();
}

}  // namespace sr
