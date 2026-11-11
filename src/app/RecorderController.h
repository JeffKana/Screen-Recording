#pragma once

#include <cstdio>
#include <functional>
#include <memory>
#include <string>

#include "app/AppConfig.h"
#include "core/state/RecorderState.h"
#include "ipc/PipeChannel.h"
#include "ipc/Protocol.h"

namespace sr::app {

// UI 与 Recorder 子进程之间的唯一通道。
// 约束（轮 27 提醒②）：本类不得直接持有采集/编码/封装对象；
// 录制由 sr-recorder.exe 完成，这里只做进程生命周期 + 命名管道命令/快照。
class RecorderController {
public:
    using LogFn = std::function<void(const std::wstring&)>;
    using SnapshotFn = std::function<void(const sr::ipc::StateSnapshotPayload&)>;

    RecorderController() = default;
    ~RecorderController();

    RecorderController(const RecorderController&) = delete;
    RecorderController& operator=(const RecorderController&) = delete;

    bool Start(const std::wstring& recorder_path,
               const std::wstring& output_path,
               const CaptureRegion& region,
               uint32_t bitrate_kbps,
               uint32_t disk_stop_mb,
               uint32_t fps,
               const std::wstring& rate_mode,
               LogFn log,
               SnapshotFn on_snapshot,
               std::wstring& error);

    // 由 UI 定时器驱动；非阻塞（连接尝试最长约 50ms）。
    void Poll();

    // 轮 30（日志分级）：把 UI 的 --log-level / INI [log] level 透传给子进程。
    void SetLogLevel(const std::wstring& level) { log_level_ = level; }

    bool SendCommand(sr::RecorderCommand command, std::wstring& error);

    // 退出路径：发 Stop 并等待子进程收尾；超时才强杀（会写日志）。
    void Shutdown();

    bool Active() const { return process_ != nullptr; }
    bool Connected() const { return connection_ != nullptr; }
    bool Exited() const { return exited_; }
    bool HasSnapshot() const { return has_snapshot_; }
    const sr::ipc::StateSnapshotPayload& Snapshot() const { return snapshot_; }
    DWORD ExitCode() const { return exit_code_; }
    uint32_t Pid() const { return pid_; }
    const std::wstring& PipeName() const { return pipe_name_; }
    const std::wstring& OutputPath() const { return output_path_; }

private:
    bool TryConnect();
    bool SendHello();
    void Drain();
    void LogText(const std::wstring& text);

    HANDLE process_ = nullptr;
    DWORD pid_ = 0;
    DWORD exit_code_ = 0;
    bool exited_ = false;
    bool has_snapshot_ = false;
    sr::ipc::StateSnapshotPayload snapshot_{};
    std::unique_ptr<sr::ipc::PipeConnection> connection_;
    std::wstring pipe_name_;
    std::wstring output_path_;
    std::wstring token_;
    std::wstring log_level_ = L"info";
    ULONGLONG next_connect_ms_ = 0;
    LogFn log_;
    SnapshotFn on_snapshot_;
};

}  // namespace sr::app
