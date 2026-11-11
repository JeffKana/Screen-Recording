#pragma once

#include <string>

#include "core/error/ErrorBus.h"
#include "core/state/RecorderState.h"
#include "core/types/IClock.h"
#include "ipc/PipeChannel.h"

namespace sr::recorder {

// Recorder 进程的控制服务。负责握手、命令校验与状态快照推送（02 文档 §2）。
// G1 阶段不包含任何采集/编码逻辑，只验证「双进程可握手 + 状态机可驱动」。
class SessionServer {
public:
    SessionServer(std::wstring pipe_name, std::string session_token, const IClock& clock);

    // 阻塞运行；返回进程退出码。0 = 正常关闭。
    int Run();

private:
    // 返回 true 表示收到 Shutdown，应结束进程。
    bool ServeClient(ipc::PipeConnection& connection);

    void SendSnapshot(ipc::PipeConnection& connection,
                      ipc::SnapshotKind kind = ipc::SnapshotKind::Periodic);

    void SendError(ipc::PipeConnection& connection, ErrorCode code, Severity severity, const std::string& message);

    std::wstring pipe_name_;
    std::string token_;
    const IClock& clock_;
    RecorderStateMachine machine_;
};

}  // namespace sr::recorder
