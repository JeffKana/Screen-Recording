#include "recorder/SessionServer.h"

#include <cstdio>
#include <utility>

#include "diag/Logger.h"
#include "ipc/Protocol.h"

namespace sr::recorder {
namespace {

// 骨架服务的 stdout 前缀仍是 `[sr-recorder] `（由 main 里的 logger tag 决定）。
void LogLine(const std::string& text) {
    sr::diag::Info(text);
}

}  // namespace

SessionServer::SessionServer(std::wstring pipe_name, std::string session_token, const IClock& clock)
    : pipe_name_(std::move(pipe_name)), token_(std::move(session_token)), clock_(clock) {}

int SessionServer::Run() {
    ipc::PipeServer server;
    std::string error;
    if (!server.Open(pipe_name_, error)) {
        LogLine("failed to open control pipe: " + error);
        return 2;
    }

    LogLine("listening, waiting for UI handshake");
    for (;;) {
        std::unique_ptr<ipc::PipeConnection> connection = server.WaitForClient(error);
        if (!connection) {
            LogLine("accept failed: " + error);
            return 3;
        }
        LogLine("client connected");
        const bool should_exit = ServeClient(*connection);
        connection->Close();
        if (should_exit) {
            LogLine("shutdown acknowledged");
            return 0;
        }
        LogLine("client disconnected, waiting for next client");
    }
}

bool SessionServer::ServeClient(ipc::PipeConnection& connection) {
    ipc::Message message;
    if (!connection.Receive(message)) {
        LogLine("handshake aborted: no hello received");
        return false;
    }
    if (message.type != ipc::MessageType::Hello) {
        LogLine("handshake rejected: first message must be Hello");
        return false;
    }

    ipc::HelloPayload hello;
    if (!ipc::Decode(message.payload, hello) || hello.token != token_) {
        LogLine("handshake rejected: bad token");
        return false;
    }

    ipc::HelloAckPayload ack;
    ack.protocol_version = ipc::kProtocolVersion;
    ack.process_id = GetCurrentProcessId();
    ack.state = static_cast<uint8_t>(machine_.State());
    if (!connection.Send(ipc::Message{ipc::MessageType::HelloAck, ipc::Encode(ack)})) {
        return false;
    }
    LogLine("handshake ok");
    SendSnapshot(connection);

    for (;;) {
        if (!connection.Receive(message)) {
            return false;
        }
        switch (message.type) {
            case ipc::MessageType::Command: {
                ipc::CommandPayload payload;
                if (!ipc::Decode(message.payload, payload)) {
                    SendError(connection, ErrorCode::UnsupportedSource, Severity::Warn, "malformed command payload");
                    break;
                }
                const RecorderCommand command = static_cast<RecorderCommand>(payload.command);
                const RecorderState before = machine_.State();
                const TransitionStatus status = machine_.Apply(command);

                char line[160] = {};
                std::snprintf(line,
                              sizeof(line),
                              "command %s on %s -> %s (%s)",
                              ToString(command),
                              ToString(before),
                              ToString(machine_.State()),
                              ToString(status));
                LogLine(line);

                if (status == TransitionStatus::Rejected) {
                    SendError(connection,
                              ErrorCode::UnsupportedSource,
                              Severity::Warn,
                              std::string("command rejected in state ") + ToString(before));
                }
                SendSnapshot(connection, ipc::SnapshotKind::CommandReceipt);
                break;
            }
            case ipc::MessageType::Shutdown:
                SendSnapshot(connection, ipc::SnapshotKind::CommandReceipt);
                return true;
            case ipc::MessageType::HelloAck:
            case ipc::MessageType::StateSnapshot:
            case ipc::MessageType::ErrorEvent:
                // 客户端不应发送这些类型；按契约视为协议错误但不立即断开，仅告警。
                SendError(connection, ErrorCode::UnsupportedSource, Severity::Warn, "unexpected message type from client");
                break;
            case ipc::MessageType::Hello:
                SendError(connection, ErrorCode::UnsupportedSource, Severity::Warn, "duplicate hello ignored");
                break;
        }
    }
}

void SessionServer::SendSnapshot(ipc::PipeConnection& connection, ipc::SnapshotKind kind) {
    ipc::StateSnapshotPayload snapshot;
    snapshot.state = static_cast<uint8_t>(machine_.State());
    snapshot.parameter_locked = machine_.IsParameterLocked();
    snapshot.session_elapsed_ms = 0;  // G1 无真实录制，时长恒为 0
    snapshot.kind = static_cast<uint8_t>(kind);
    connection.Send(ipc::Message{ipc::MessageType::StateSnapshot, ipc::Encode(snapshot)});
}

void SessionServer::SendError(ipc::PipeConnection& connection,
                              ErrorCode code,
                              Severity severity,
                              const std::string& message) {
    ipc::ErrorPayload payload;
    payload.code = static_cast<uint16_t>(code);
    payload.severity = static_cast<uint8_t>(severity);
    payload.message = message;
    connection.Send(ipc::Message{ipc::MessageType::ErrorEvent, ipc::Encode(payload)});
}

}  // namespace sr::recorder
