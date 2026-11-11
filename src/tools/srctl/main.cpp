// srctl —— Recorder 控制通道的诊断探针。
// 用途：在无 UI 的环境下验证握手、状态机与命令拒绝行为（tasks/G1/S1-002 的验收工具）。
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "core/state/RecorderState.h"
#include "ipc/PipeChannel.h"
#include "ipc/Protocol.h"

namespace {

void PrintUsage() {
    std::printf(
        "srctl --pipe <pipe-name> [--token <token>] [--hold-ms <n>] <command>\n"
        "  --hold-ms 命令回执后保持连接 N 毫秒再断开（用于验证暂停超时等长连接场景）\n"
        "commands:\n"
        "  hello state\n"
        "  start preflight-ok preflight-fail countdown cancel\n"
        "  pause resume stop finalize-done\n"
        "  recover recover-ok recover-fail crash reset\n"
        "  shutdown\n");
}

bool MapCommand(const char* name, sr::RecorderCommand& out) {
    struct Entry {
        const char* name;
        sr::RecorderCommand command;
    };
    // 顺序与 CONTRACT.md §5 的 14 个命令一致，便于逐条遍历转移表。
    static constexpr Entry kEntries[] = {
        {"start", sr::RecorderCommand::Start},
        {"preflight-ok", sr::RecorderCommand::PreflightOk},
        {"preflight-fail", sr::RecorderCommand::PreflightFail},
        {"countdown", sr::RecorderCommand::CountdownElapsed},
        {"cancel", sr::RecorderCommand::Cancel},
        {"pause", sr::RecorderCommand::Pause},
        {"resume", sr::RecorderCommand::Resume},
        {"stop", sr::RecorderCommand::Stop},
        {"finalize-done", sr::RecorderCommand::FinalizeDone},
        {"recover", sr::RecorderCommand::Recover},
        {"recover-ok", sr::RecorderCommand::RecoverOk},
        {"recover-fail", sr::RecorderCommand::RecoverFail},
        {"crash", sr::RecorderCommand::Crash},
        {"reset", sr::RecorderCommand::Reset},
    };
    for (const Entry& entry : kEntries) {
        if (std::strcmp(name, entry.name) == 0) {
            out = entry.command;
            return true;
        }
    }
    return false;
}

void PrintSnapshot(const sr::ipc::StateSnapshotPayload& snapshot) {
    const auto state = static_cast<sr::RecorderState>(snapshot.state);
    std::printf("state=%s parameter_locked=%s elapsed_ms=%llu\n",
                sr::ToString(state),
                snapshot.parameter_locked ? "true" : "false",
                static_cast<unsigned long long>(snapshot.session_elapsed_ms));
    if (snapshot.has_metrics) {
        // 轮 30（M6-03）：三项指标直接打印 recorder 下发的原值（x100 定点数还原），
        // 这里不做任何时间/帧数推算，避免「客户端自证」。
        std::printf("metrics capture_fps=%.2f encode_fps=%.2f dropped=%llu captured=%llu\n",
                    static_cast<double>(snapshot.metrics.capture_fps_x100) / 100.0,
                    static_cast<double>(snapshot.metrics.encode_fps_x100) / 100.0,
                    static_cast<unsigned long long>(snapshot.metrics.frames_dropped),
                    static_cast<unsigned long long>(snapshot.metrics.frames_captured));
    } else {
        std::printf("metrics absent\n");
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::string pipe_name;
    std::string token = "dev";
    std::string command;
    int hold_ms = 0;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--pipe") == 0 && i + 1 < argc) {
            pipe_name = argv[++i];
        } else if (std::strcmp(argv[i], "--token") == 0 && i + 1 < argc) {
            token = argv[++i];
        } else if (std::strcmp(argv[i], "--hold-ms") == 0 && i + 1 < argc) {
            hold_ms = std::atoi(argv[++i]);
        } else if (argv[i][0] != '-') {
            command = argv[i];
        } else {
            PrintUsage();
            return 1;
        }
    }
    if (pipe_name.empty() || command.empty()) {
        PrintUsage();
        return 1;
    }

    const std::wstring wide_name(pipe_name.begin(), pipe_name.end());
    std::string error;
    auto connection = sr::ipc::PipeClient::Connect(wide_name, error);
    if (!connection) {
        std::printf("connect failed: %s\n", error.c_str());
        return 2;
    }

    sr::ipc::HelloPayload hello;
    hello.token = token;
    if (!connection->Send(sr::ipc::Message{sr::ipc::MessageType::Hello, sr::ipc::Encode(hello)})) {
        std::printf("send hello failed\n");
        return 3;
    }

    sr::ipc::Message reply;
    if (!connection->Receive(reply)) {
        std::printf("no hello ack (token rejected?)\n");
        return 4;
    }
    if (reply.type != sr::ipc::MessageType::HelloAck) {
        std::printf("unexpected reply type %u\n", static_cast<unsigned>(reply.type));
        return 5;
    }
    sr::ipc::HelloAckPayload ack;
    if (!sr::ipc::Decode(reply.payload, ack)) {
        std::printf("malformed hello ack\n");
        return 6;
    }
    std::printf("handshake ok: pid=%u protocol=%u\n", ack.process_id, ack.protocol_version);

    if (connection->Receive(reply) && reply.type == sr::ipc::MessageType::StateSnapshot) {
        sr::ipc::StateSnapshotPayload snapshot;
        if (sr::ipc::Decode(reply.payload, snapshot)) {
            PrintSnapshot(snapshot);
        }
    }

    if (command == "hello" || command == "state") {
        connection->Close();
        return 0;
    }

    if (command == "shutdown") {
        connection->Send(sr::ipc::Message{sr::ipc::MessageType::Shutdown, {}});
        if (connection->Receive(reply) && reply.type == sr::ipc::MessageType::StateSnapshot) {
            sr::ipc::StateSnapshotPayload snapshot;
            if (sr::ipc::Decode(reply.payload, snapshot)) {
                PrintSnapshot(snapshot);
            }
        }
        std::printf("shutdown sent\n");
        connection->Close();
        return 0;
    }

    sr::RecorderCommand mapped{};
    if (!MapCommand(command.c_str(), mapped)) {
        std::printf("unknown command: %s\n", command.c_str());
        return 7;
    }

    sr::ipc::CommandPayload payload;
    payload.command = static_cast<uint8_t>(mapped);
    if (!connection->Send(sr::ipc::Message{sr::ipc::MessageType::Command, sr::ipc::Encode(payload)})) {
        std::printf("send command failed\n");
        return 8;
    }

    // 服务端可能先回 ErrorEvent 再回 StateSnapshot；录制中还有每秒的周期快照。
    // 新服务端用 kind=1 标记命令回执，周期快照必须跳过，否则排队中的 Recording
    // 会被误读成 Pause 的回执。
    // 旧服务端（只有 10 字节快照，kind 解为 0）不会回 kind=1：等到 300ms 仍没有回执时，
    // 退化为旧行为，打印最后一条快照并记录一行 legacy_snapshot_fallback，绝不静默返回 0。
    sr::ipc::StateSnapshotPayload last_snapshot{};
    bool have_last = false;
    bool got_receipt = false;
    size_t skipped = 0;
    ULONGLONG first_periodic = 0;
    const ULONGLONG deadline = GetTickCount64() + 1500;
    while (GetTickCount64() < deadline) {
        sr::ipc::Message message;
        if (!connection->TryReceive(message)) {
            if (!connection->IsOpen()) {
                break;
            }
            if (have_last && first_periodic != 0 && GetTickCount64() - first_periodic >= 300) {
                break;
            }
            Sleep(10);
            continue;
        }
        if (message.type == sr::ipc::MessageType::ErrorEvent) {
            sr::ipc::ErrorPayload err;
            if (sr::ipc::Decode(message.payload, err)) {
                std::printf("rejected: code=%u severity=%u message=%s\n",
                            err.code, err.severity, err.message.c_str());
            }
            continue;
        }
        if (message.type != sr::ipc::MessageType::StateSnapshot) {
            continue;
        }
        sr::ipc::StateSnapshotPayload snapshot;
        if (!sr::ipc::Decode(message.payload, snapshot)) {
            continue;
        }
        if (snapshot.kind != static_cast<uint8_t>(sr::ipc::SnapshotKind::CommandReceipt)) {
            last_snapshot = snapshot;
            have_last = true;
            ++skipped;
            if (first_periodic == 0) {
                first_periodic = GetTickCount64();
            }
            continue;
        }
        PrintSnapshot(snapshot);
        got_receipt = true;
        break;
    }
    if (!got_receipt && have_last) {
        // 旧 recorder：没有 kind 字段。按旧行为把最后一条快照当作命令结果。
        std::printf("legacy_snapshot_fallback skipped=%zu\n", skipped);
        PrintSnapshot(last_snapshot);
    }

    // 保持连接：用于验证「手动暂停 + 客户端长时间在线」时的 --pause-timeout。
    // 客户端不断开，服务端就不会走自动恢复宽限，只能由暂停超时收尾。
    if (hold_ms > 0) {
        Sleep(static_cast<DWORD>(hold_ms));
    }
    connection->Close();
    return 0;
}
