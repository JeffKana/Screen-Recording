#include "app/RecorderController.h"

#include <filesystem>
#include <vector>

namespace sr::app {
namespace {

std::wstring Quote(const std::wstring& text) {
    return L"\"" + text + L"\"";
}

std::wstring ParentDirectory(const std::wstring& path) {
    const std::filesystem::path parent = std::filesystem::path(path).parent_path();
    return parent.empty() ? L"." : parent.wstring();
}

std::wstring Widen(const char* text) {
    std::wstring out;
    if (text == nullptr) {
        return out;
    }
    for (; *text != '\0'; ++text) {
        out.push_back(static_cast<wchar_t>(*text));
    }
    return out;
}

std::string NarrowAscii(const std::wstring& text) {
    std::string out;
    out.reserve(text.size());
    for (wchar_t c : text) {
        out.push_back(static_cast<char>(c));
    }
    return out;
}

}  // namespace

RecorderController::~RecorderController() {
    if (connection_) {
        connection_->Close();
        connection_.reset();
    }
    if (process_ != nullptr) {
        TerminateProcess(process_, 1);
        WaitForSingleObject(process_, 3000);
        CloseHandle(process_);
        process_ = nullptr;
    }
}

bool RecorderController::Start(const std::wstring& recorder_path,
                               const std::wstring& output_path,
                               const CaptureRegion& region,
                               uint32_t bitrate_kbps,
                               uint32_t disk_stop_mb,
                               uint32_t fps,
                               const std::wstring& rate_mode,
                               LogFn log,
                               SnapshotFn on_snapshot,
                               std::wstring& error) {
    if (process_ != nullptr) {
        error = L"recorder already running";
        return false;
    }
    if (connection_) {
        connection_->Close();
        connection_.reset();
    }

    log_ = std::move(log);
    on_snapshot_ = std::move(on_snapshot);
    output_path_ = output_path;
    exited_ = false;
    has_snapshot_ = false;
    snapshot_ = sr::ipc::StateSnapshotPayload{};
    exit_code_ = 0;

    token_ = L"ui-" + std::to_wstring(GetTickCount64()) + L"-" +
             std::to_wstring(GetCurrentProcessId());

    std::wstring region_args;
    if (region.enabled()) {
        region_args = L" --region " + std::to_wstring(region.x) + L"," +
                      std::to_wstring(region.y) + L"," + std::to_wstring(region.width) + L"," +
                      std::to_wstring(region.height);
    }
    // M3-03 / M3-04：帧率与码率模式由 INI 决定，UI 只负责透传，不在录制层写死。
    const std::wstring command_line =
        Quote(recorder_path) + L" --record --seconds 3600 --fps " + std::to_wstring(fps) +
        L" --bitrate " + std::to_wstring(bitrate_kbps) +
        L" --rate-mode " + (rate_mode.empty() ? L"cbr" : rate_mode) +
        L" --disk-stop-mb " + std::to_wstring(disk_stop_mb) +
        L" --out " + Quote(output_path) + region_args + L" --control-pipe --control-token " +
        token_ + L" --control-wait 300" + L" --log-level " + log_level_;

    // M3-03 / M3-04：把实际下发的参数写进日志，验收可据此断言「INI 真的传到了录制进程」。
    if (log_) {
        log_(L"recorder_cmd fps=" + std::to_wstring(fps) + L" rate_mode=" +
             (rate_mode.empty() ? L"cbr" : rate_mode) + L" bitrate=" +
             std::to_wstring(bitrate_kbps));
    // M3-10：档位联动后实际下发的三项值写进日志，验收据此断言「联动真的到了录制进程」。
    if (log_) {
        log_(L"preset_dispatched fps=" + std::to_wstring(fps) + L" bitrate_kbps=" +
             std::to_wstring(bitrate_kbps));
    }
    }

    std::vector<wchar_t> mutable_command(command_line.begin(), command_line.end());
    mutable_command.push_back(L'\0');

    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;

    const std::wstring child_log = output_path + L".recorder.log";
    HANDLE log_handle = CreateFileW(child_log.c_str(), GENERIC_WRITE,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE null_input = CreateFileW(L"NUL", GENERIC_READ,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                                    OPEN_EXISTING, 0, nullptr);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    if (log_handle != INVALID_HANDLE_VALUE) {
        startup.dwFlags |= STARTF_USESTDHANDLES;
        startup.hStdOutput = log_handle;
        startup.hStdError = log_handle;
        startup.hStdInput = null_input != INVALID_HANDLE_VALUE ? null_input : nullptr;
    }

    PROCESS_INFORMATION process_info{};
    const std::wstring working_dir = ParentDirectory(recorder_path);
    const BOOL created = CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW, nullptr, working_dir.c_str(),
                                        &startup, &process_info);

    if (log_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(log_handle);
    }
    if (null_input != INVALID_HANDLE_VALUE) {
        CloseHandle(null_input);
    }

    if (!created) {
        error = L"CreateProcess failed, GetLastError=" + std::to_wstring(GetLastError());
        return false;
    }

    CloseHandle(process_info.hThread);
    process_ = process_info.hProcess;
    pid_ = process_info.dwProcessId;
    pipe_name_ = L"\\\\.\\pipe\\sr.recorder." + std::to_wstring(pid_);
    next_connect_ms_ = GetTickCount64();

    LogText(L"spawn pid=" + std::to_wstring(pid_) + L" out=" + output_path +
            L" pipe=" + pipe_name_ + L" recorder_log=" + child_log);
    error.clear();
    return true;
}

void RecorderController::Poll() {
    const ULONGLONG now = GetTickCount64();

    bool just_exited = false;
    if (process_ != nullptr) {
        if (WaitForSingleObject(process_, 0) == WAIT_OBJECT_0) {
            DWORD code = 0;
            GetExitCodeProcess(process_, &code);
            CloseHandle(process_);
            process_ = nullptr;
            exit_code_ = code;
            exited_ = true;
            just_exited = true;
            LogText(L"recorder_exit code=" + std::to_wstring(code));
        }
    }

    if (connection_) {
        Drain();
    }

    if (just_exited && connection_) {
        connection_->Close();
        connection_.reset();
    }

    if (!connection_ && process_ != nullptr && now >= next_connect_ms_) {
        if (!TryConnect()) {
            next_connect_ms_ = now + 100;
        }
    }
}

bool RecorderController::TryConnect() {
    std::string error;
    auto connection = sr::ipc::PipeClient::Connect(pipe_name_, error, 50);
    if (!connection) {
        return false;
    }
    connection_ = std::move(connection);
    if (!SendHello()) {
        connection_->Close();
        connection_.reset();
        return false;
    }
    LogText(L"control connected pipe=" + pipe_name_);
    return true;
}

bool RecorderController::SendHello() {
    if (!connection_) {
        return false;
    }
    sr::ipc::HelloPayload hello;
    hello.token = NarrowAscii(token_);
    if (!connection_->Send(sr::ipc::Message{sr::ipc::MessageType::Hello,
                                            sr::ipc::Encode(hello)})) {
        LogText(L"hello send failed");
        return false;
    }
    return true;
}

void RecorderController::Drain() {
    for (int guard = 0; guard < 16; ++guard) {
        sr::ipc::Message message;
        if (!connection_->TryReceive(message)) {
            if (!connection_->IsOpen()) {
                LogText(L"control lost");
                connection_.reset();
                if (process_ != nullptr) {
                    next_connect_ms_ = GetTickCount64() + 200;
                }
            }
            return;
        }
        switch (message.type) {
            case sr::ipc::MessageType::HelloAck: {
                sr::ipc::HelloAckPayload ack;
                if (sr::ipc::Decode(message.payload, ack)) {
                    LogText(L"hello_ack pid=" + std::to_wstring(ack.process_id) +
                            L" state=" + Widen(sr::ToString(static_cast<sr::RecorderState>(ack.state))));
                }
                break;
            }
            case sr::ipc::MessageType::StateSnapshot: {
                sr::ipc::StateSnapshotPayload snapshot;
                if (!sr::ipc::Decode(message.payload, snapshot)) {
                    LogText(L"malformed snapshot");
                    break;
                }
                snapshot_ = snapshot;
                has_snapshot_ = true;
                if (on_snapshot_) {
                    on_snapshot_(snapshot);
                }
                break;
            }
            case sr::ipc::MessageType::ErrorEvent: {
                sr::ipc::ErrorPayload payload;
                if (sr::ipc::Decode(message.payload, payload)) {
                    LogText(L"error_event code=" + std::to_wstring(payload.code) +
                            L" severity=" + std::to_wstring(payload.severity) +
                            L" message=" + Widen(payload.message.c_str()));
                }
                break;
            }
            default:
                break;
        }
    }
}

bool RecorderController::SendCommand(sr::RecorderCommand command, std::wstring& error) {
    if (!connection_) {
        error = L"控制通道未连接";
        return false;
    }
    sr::ipc::CommandPayload payload;
    payload.command = static_cast<uint8_t>(command);
    if (!connection_->Send(sr::ipc::Message{sr::ipc::MessageType::Command,
                                            sr::ipc::Encode(payload)})) {
        error = L"命令发送失败";
        return false;
    }
    LogText(L"command=" + Widen(sr::ToString(command)) + L" sent");
    error.clear();
    return true;
}

void RecorderController::Shutdown() {
    if (process_ != nullptr) {
        if (connection_) {
            std::wstring error;
            SendCommand(sr::RecorderCommand::Stop, error);
        }
        for (int i = 0; i < 200 && process_ != nullptr; ++i) {
            Poll();
            Sleep(50);
        }
        if (process_ != nullptr) {
            LogText(L"force_kill pid=" + std::to_wstring(pid_));
            TerminateProcess(process_, 1);
            WaitForSingleObject(process_, 5000);
            CloseHandle(process_);
            process_ = nullptr;
            exited_ = true;
            exit_code_ = 1;
        }
    }
    if (connection_) {
        connection_->Close();
        connection_.reset();
    }
}

void RecorderController::LogText(const std::wstring& text) {
    if (log_) {
        log_(text);
    }
}

}  // namespace sr::app
