#include "core/state/RecorderState.h"

namespace sr {
namespace {

// CONTRACT.md §5 的 T01~T15，顺序与文档一致，不得重排。
constexpr Transition kTransitions[] = {
    {RecorderState::Idle, RecorderCommand::Start, RecorderState::Preflight},              // T01
    {RecorderState::Preflight, RecorderCommand::PreflightOk, RecorderState::Countdown},   // T02
    {RecorderState::Preflight, RecorderCommand::PreflightFail, RecorderState::Idle},      // T03
    {RecorderState::Countdown, RecorderCommand::CountdownElapsed, RecorderState::Recording},  // T04
    {RecorderState::Countdown, RecorderCommand::Cancel, RecorderState::Idle},             // T05
    {RecorderState::Recording, RecorderCommand::Pause, RecorderState::Paused},            // T06
    {RecorderState::Paused, RecorderCommand::Resume, RecorderState::Recording},           // T07
    {RecorderState::Recording, RecorderCommand::Stop, RecorderState::Stopping},           // T08
    {RecorderState::Paused, RecorderCommand::Stop, RecorderState::Stopping},              // T09
    {RecorderState::Stopping, RecorderCommand::FinalizeDone, RecorderState::Idle},        // T10
    {RecorderState::Recording, RecorderCommand::Recover, RecorderState::Recovering},      // T11
    {RecorderState::Recovering, RecorderCommand::RecoverOk, RecorderState::Recording},    // T12
    {RecorderState::Recovering, RecorderCommand::RecoverFail, RecorderState::Stopping},   // T13
    {RecorderState::Recording, RecorderCommand::Crash, RecorderState::Crashed},           // T14
    {RecorderState::Crashed, RecorderCommand::Reset, RecorderState::Idle},                // T15
};

constexpr size_t kTransitionCount = sizeof(kTransitions) / sizeof(kTransitions[0]);

// 重复下发不改变状态且不报错的组合。M2-10 要求「录制中重复点开始」无副作用。
struct NoOpPair {
    RecorderState state;
    RecorderCommand command;
};

constexpr NoOpPair kIdempotentNoOps[] = {
    {RecorderState::Recording, RecorderCommand::Start},
    {RecorderState::Recording, RecorderCommand::Resume},
    {RecorderState::Paused, RecorderCommand::Pause},
    {RecorderState::Idle, RecorderCommand::Cancel},
    {RecorderState::Idle, RecorderCommand::Stop},
    {RecorderState::Stopping, RecorderCommand::Stop},
};

constexpr size_t kNoOpCount = sizeof(kIdempotentNoOps) / sizeof(kIdempotentNoOps[0]);

}  // namespace

const char* ToString(RecorderState state) noexcept {
    switch (state) {
        case RecorderState::Idle: return "Idle";
        case RecorderState::Preflight: return "Preflight";
        case RecorderState::Countdown: return "Countdown";
        case RecorderState::Recording: return "Recording";
        case RecorderState::Paused: return "Paused";
        case RecorderState::Stopping: return "Stopping";
        case RecorderState::Recovering: return "Recovering";
        case RecorderState::Crashed: return "Crashed";
    }
    return "Unknown";
}

const char* ToString(RecorderCommand command) noexcept {
    switch (command) {
        case RecorderCommand::Start: return "Start";
        case RecorderCommand::PreflightOk: return "PreflightOk";
        case RecorderCommand::PreflightFail: return "PreflightFail";
        case RecorderCommand::CountdownElapsed: return "CountdownElapsed";
        case RecorderCommand::Cancel: return "Cancel";
        case RecorderCommand::Pause: return "Pause";
        case RecorderCommand::Resume: return "Resume";
        case RecorderCommand::Stop: return "Stop";
        case RecorderCommand::FinalizeDone: return "FinalizeDone";
        case RecorderCommand::Recover: return "Recover";
        case RecorderCommand::RecoverOk: return "RecoverOk";
        case RecorderCommand::RecoverFail: return "RecoverFail";
        case RecorderCommand::Crash: return "Crash";
        case RecorderCommand::Reset: return "Reset";
    }
    return "Unknown";
}

const char* ToString(TransitionStatus status) noexcept {
    return status == TransitionStatus::Accepted ? "Accepted" : "Rejected";
}

const Transition* TransitionTable(size_t& out_count) noexcept {
    out_count = kTransitionCount;
    return kTransitions;
}

size_t RecorderStateMachine::TableSize() noexcept {
    return kTransitionCount;
}

bool RecorderStateMachine::IsIdempotentNoOp(RecorderState state, RecorderCommand command) noexcept {
    for (size_t i = 0; i < kNoOpCount; ++i) {
        if (kIdempotentNoOps[i].state == state && kIdempotentNoOps[i].command == command) {
            return true;
        }
    }
    return false;
}

TransitionStatus RecorderStateMachine::Apply(RecorderCommand command) noexcept {
    for (size_t i = 0; i < kTransitionCount; ++i) {
        if (kTransitions[i].from == state_ && kTransitions[i].command == command) {
            state_ = kTransitions[i].to;
            return TransitionStatus::Accepted;
        }
    }
    if (IsIdempotentNoOp(state_, command)) {
        return TransitionStatus::Accepted;
    }
    return TransitionStatus::Rejected;
}

bool RecorderStateMachine::IsParameterLocked() const noexcept {
    return state_ == RecorderState::Recording || state_ == RecorderState::Paused ||
           state_ == RecorderState::Recovering;
}

bool RecorderStateMachine::IsCapturing() const noexcept {
    return state_ == RecorderState::Recording || state_ == RecorderState::Paused;
}

}  // namespace sr
