#pragma once

#include <cstdint>
#include <string>

// 轮 30（T-17 / M6-03 / M8-14）：进程内日志与环形缓冲。
//
// 设计约束（来自轮 30 指令注意事项 ④）：
//   「最近 10 分钟日志」用环形缓冲，不做整文件拷贝——8h 录制的日志可能有几十 MB。
// 兼容约束：
//   默认（info）行格式保持 `t=<ms> <message>`，验收脚本的 `^t=(\d+) (.*)$` 解析不变；
//   debug/warn 行在 message 前加 `[debug] ` / `[warn] `，既能按级别过滤，也不破坏前缀匹配。
namespace sr::diag {

enum class LogLevel : int {
    Debug = 0,
    Info = 1,
    Warn = 2,
};

// "debug" / "info" / "warn"（大小写不敏感）。失败时返回 false 并保持 out 不变。
bool ParseLogLevel(const std::string& text, LogLevel& out);
const char* LogLevelName(LogLevel level);

// tag 用于 stdout 前缀（如 "record" → `[record] message`）；空则不加前缀。
// file_path 为空时只写 stdout。level 为最低输出级别。
// 宽字符路径版本用于 UI：日志路径可能包含非 ASCII 字符。
// append=true 时以追加方式打开（导出诊断包场景：同一个文件既是输入又要继续写日志，
// 默认的 "wb" 会把它清空——轮 32 D-54）。
void Init(const char* tag, const std::string& file_path, LogLevel level, bool echo_stdout,
          bool append = false);
void Init(const char* tag, const std::wstring& file_path, LogLevel level, bool echo_stdout,
          bool append = false);
void SetLogLevel(LogLevel level);
LogLevel GetLogLevel();
void Shutdown();

void Log(LogLevel level, const std::string& text);
inline void Debug(const std::string& text) { Log(LogLevel::Debug, text); }
inline void Info(const std::string& text) { Log(LogLevel::Info, text); }
inline void Warn(const std::string& text) { Log(LogLevel::Warn, text); }

// 环形缓冲里 max_age_ms 内的行（默认 10 分钟）。用于诊断包，不读整个日志文件。
std::string RecentText(uint64_t max_age_ms = 600000);

// 已写入的行数（过滤后），用于自检与证据。
uint64_t WrittenLines();

}  // namespace sr::diag
