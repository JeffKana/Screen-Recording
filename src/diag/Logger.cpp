#include "diag/Logger.h"

#include <cstdio>
#include <cstring>
#include <deque>
#include <share.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace sr::diag {
namespace {

struct Entry {
    uint64_t tick_ms = 0;
    LogLevel level = LogLevel::Info;
    std::string text;
};

// 4096 行 × 单行上限 1KB ≈ 4MB 上限；debug 级别下 10 分钟的帧级日志也装得下。
constexpr size_t kRingCapacity = 4096;
constexpr size_t kMaxLineBytes = 1024;

struct State {
    FILE* file = nullptr;
    std::string tag;
    LogLevel level = LogLevel::Info;
    bool echo_stdout = false;
    uint64_t written = 0;
    std::deque<Entry> ring;
};

State& Get() {
    static State state;
    return state;
}

const char* LevelPrefix(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:
            return "[debug] ";
        case LogLevel::Warn:
            return "[warn] ";
        default:
            return "";
    }
}

std::string Clamp(const std::string& text) {
    if (text.size() <= kMaxLineBytes) {
        return text;
    }
    return text.substr(0, kMaxLineBytes) + "…(truncated)";
}

}  // namespace

bool ParseLogLevel(const std::string& text, LogLevel& out) {
    if (_stricmp(text.c_str(), "debug") == 0) {
        out = LogLevel::Debug;
        return true;
    }
    if (_stricmp(text.c_str(), "info") == 0) {
        out = LogLevel::Info;
        return true;
    }
    if (_stricmp(text.c_str(), "warn") == 0 || _stricmp(text.c_str(), "warning") == 0) {
        out = LogLevel::Warn;
        return true;
    }
    return false;
}

const char* LogLevelName(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:
            return "debug";
        case LogLevel::Warn:
            return "warn";
        default:
            return "info";
    }
}

void Init(const char* tag, const std::string& file_path, LogLevel level, bool echo_stdout,
          bool append) {
    std::wstring wide_path;
    if (!file_path.empty()) {
        const int chars = MultiByteToWideChar(CP_UTF8, 0, file_path.c_str(), -1, nullptr, 0);
        if (chars > 1) {
            wide_path.resize(static_cast<size_t>(chars - 1));
            MultiByteToWideChar(CP_UTF8, 0, file_path.c_str(), -1, wide_path.data(), chars);
        }
    }
    Init(tag, wide_path, level, echo_stdout, append);
}

void Init(const char* tag, const std::wstring& file_path, LogLevel level, bool echo_stdout,
          bool append) {
    State& state = Get();
    Shutdown();
    state.tag = tag != nullptr ? tag : "";
    state.level = level;
    state.echo_stdout = echo_stdout;
    state.written = 0;
    state.ring.clear();
    if (!file_path.empty()) {
        // 与 UI 日志一致：共享读，验收脚本可以边跑边 tail。
        // append：导出诊断包时同一个路径既是输入又要继续写，不能截断。
        state.file = _wfsopen(file_path.c_str(), append ? L"ab" : L"wb", _SH_DENYNO);
    }
}


void SetLogLevel(LogLevel level) {
    Get().level = level;
}

LogLevel GetLogLevel() {
    return Get().level;
}

void Shutdown() {
    State& state = Get();
    if (state.file != nullptr) {
        std::fflush(state.file);
        std::fclose(state.file);
        state.file = nullptr;
    }
}

void Log(LogLevel level, const std::string& text) {
    State& state = Get();
    if (static_cast<int>(level) < static_cast<int>(state.level)) {
        return;
    }
    const uint64_t now = GetTickCount64();
    const std::string body = Clamp(text);

    if (state.file != nullptr) {
        std::fprintf(state.file, "t=%llu %s%s\n",
                     static_cast<unsigned long long>(now), LevelPrefix(level), body.c_str());
        std::fflush(state.file);
    }
    if (state.echo_stdout) {
        if (!state.tag.empty()) {
            std::printf("[%s]%s%s\n", state.tag.c_str(),
                        level == LogLevel::Info ? " " : LevelPrefix(level), body.c_str());
        } else {
            std::printf("%s%s\n", LevelPrefix(level), body.c_str());
        }
        std::fflush(stdout);
    }

    Entry entry;
    entry.tick_ms = now;
    entry.level = level;
    entry.text = body;
    state.ring.push_back(std::move(entry));
    while (state.ring.size() > kRingCapacity) {
        state.ring.pop_front();
    }
    ++state.written;
}

std::string RecentText(uint64_t max_age_ms) {
    const State& state = Get();
    const uint64_t now = GetTickCount64();
    const uint64_t cutoff = now > max_age_ms ? now - max_age_ms : 0;
    std::string out;
    for (const Entry& entry : state.ring) {
        if (entry.tick_ms < cutoff) {
            continue;
        }
        out += "t=";
        out += std::to_string(static_cast<unsigned long long>(entry.tick_ms));
        out += " ";
        out += LevelPrefix(entry.level);
        out += entry.text;
        out += "\n";
    }
    return out;
}

uint64_t WrittenLines() {
    return Get().written;
}

}  // namespace sr::diag
