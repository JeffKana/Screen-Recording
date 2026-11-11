#pragma once

#include <cstddef>
#include <cstdint>

namespace sr {

enum class RecorderState : uint8_t {
    Idle = 0,
    Preflight,
    Countdown,
    Recording,
    Paused,
    Stopping,
    Recovering,
    Crashed,
};

enum class RecorderCommand : uint8_t {
    Start = 0,
    PreflightOk,
    PreflightFail,
    CountdownElapsed,
    Cancel,
    Pause,
    Resume,
    Stop,
    FinalizeDone,
    Recover,
    RecoverOk,
    RecoverFail,
    Crash,
    Reset,
};

enum class TransitionStatus : uint8_t {
    Accepted = 0,
    Rejected,
};

struct Transition {
    RecorderState from;
    RecorderCommand command;
    RecorderState to;
};

const char* ToString(RecorderState state) noexcept;
const char* ToString(RecorderCommand command) noexcept;
const char* ToString(TransitionStatus status) noexcept;

// 唯一权威转移表，与 CONTRACT.md §5 逐条对应（T01~T15）。
const Transition* TransitionTable(size_t& out_count) noexcept;

// 录制状态机。状态只存在于 Recorder 进程，UI 只看快照（02 文档 §5 规则 1）。
class RecorderStateMachine {
public:
    RecorderState State() const noexcept { return state_; }

    // 命令幂等：重复下发返回 Accepted 且状态不变（CONTRACT.md §5 末段）。
    // 非法组合返回 Rejected 且状态不变。
    TransitionStatus Apply(RecorderCommand command) noexcept;

    static bool IsIdempotentNoOp(RecorderState state, RecorderCommand command) noexcept;

    // M2-16：录制中关键参数锁定。音量类参数不在此列。
    bool IsParameterLocked() const noexcept;

    // 是否处于会产生媒体数据的状态。
    bool IsCapturing() const noexcept;

    static size_t TableSize() noexcept;

private:
    RecorderState state_ = RecorderState::Idle;
};

}  // namespace sr
