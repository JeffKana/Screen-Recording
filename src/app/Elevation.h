#pragma once

// M1-27：UAC 提权状态查询 + 「以管理员身份重启」入口。
//
// 为什么是头文件内联实现：sr-ui 的源文件清单写在 src/app/CMakeLists.txt（O0 文件域），
// 新增 .cpp 必须改构建脚本；轮 40 约束「不得越出文件域」，故用头文件内联，
// 头文件不需要登记到构建脚本（只被 main.cpp include）。
//
// 实测事实（2026-11-08，本机 Windows 11，轮 40 实测，勿当成推断）：
//   * 非提权进程用 WGC 采**高完整性窗口** → 实测拿到真实内容
//     （抽帧绿色占比 0.9915 / 黑色占比 0.0085，backend=wgc，frames_captured=103）。
//     即「UAC 提权窗口一定黑屏」在本机**不成立**（本机 UAC 策略为
//     ConsentPromptBehaviorAdmin=0 / PromptOnSecureDesktop=0，UAC 对话框不上安全桌面）。
//   * 同一窗口走兜底 PrintWindow 路径 → 实测 frames_captured=0、record_exit=4（取不到帧）。
//   * 本机可无交互构造高完整性进程：ShellExecuteW(runas) 直接成功（不提权对话框）。
//
// 因此本模块**只提供可判定的提权状态**，不声称「采集结果一定是黑屏」；
// 提示文案与入口由 UI 层决定，采集侧结论留给各自的用例去测。

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

#include <string>

namespace sr::app {

// 查询某个令牌的提权状态（TokenElevation）。
inline bool QueryTokenElevated(HANDLE token) {
    if (token == nullptr) {
        return false;
    }
    TOKEN_ELEVATION elevation{};
    DWORD returned = 0;
    if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &returned) == FALSE) {
        return false;
    }
    return elevation.TokenIsElevated != 0;
}

// 当前进程是否已以管理员身份运行。
inline bool IsProcessElevated() {
    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) == FALSE) {
        return false;
    }
    const bool elevated = QueryTokenElevated(token);
    CloseHandle(token);
    return elevated;
}

// 窗口所属进程是否已提权。用于判断「目标窗口的权限高于本进程」。
// 取不到时返回 false（按「不高于」处理，避免把取不到当成高权限而误报）。
inline bool IsWindowElevated(HWND window) {
    if (window == nullptr || IsWindow(window) == FALSE) {
        return false;
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid == 0) {
        return false;
    }
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process == nullptr) {
        return false;
    }
    HANDLE token = nullptr;
    bool elevated = false;
    if (OpenProcessToken(process, TOKEN_QUERY, &token) != FALSE) {
        elevated = QueryTokenElevated(token);
        CloseHandle(token);
    }
    CloseHandle(process);
    return elevated;
}

// 取当前命令行里「程序名之后」的部分（保留原始参数，不重新拼装，避免引号走样）。
inline std::wstring CurrentArgumentTail() {
    const wchar_t* raw = GetCommandLineW();
    if (raw == nullptr) {
        return {};
    }
    const std::wstring text(raw);
    size_t start = 0;
    const size_t quote = text.find(L'"');
    if (quote != std::wstring::npos) {
        const size_t close = text.find(L'"', quote + 1);
        start = (close == std::wstring::npos) ? text.size() : close + 1;
    } else {
        const size_t space = text.find(L' ');
        start = (space == std::wstring::npos) ? text.size() : space;
    }
    while (start < text.size() && (text[start] == L' ' || text[start] == L'\t')) {
        ++start;
    }
    return text.substr(start);
}

// 当前可执行文件路径。
inline std::wstring CurrentExecutablePath() {
    wchar_t module[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, module, static_cast<DWORD>(std::size(module)));
    if (length == 0) {
        return {};
    }
    return std::wstring(module, length);
}

// 构造提权重启要用的参数部分（不含 exe 本身）。
//
// 为什么要给提权实例单独一份日志：sr::diag::Init 以 "wb" 打开日志文件（截断），
// 两个实例写同一个日志会互相抹掉对方的行（第一句结论就查不到了）。
// 参数解析里 --log 出现两次时**后者生效**，所以追加在尾部即可覆盖。
inline std::wstring BuildElevatedParameters(const std::wstring& log_path) {
    std::wstring parameters = CurrentArgumentTail();
    if (!log_path.empty()) {
        if (!parameters.empty()) {
            parameters += L" ";
        }
        parameters += L"--log \"" + log_path + L".elevated\"";
    }
    return parameters;
}

// 构造提权重启要用的完整命令行（引号包裹 exe + 参数），供日志与验收断言使用。
inline std::wstring BuildElevatedCommandLine(const std::wstring& log_path) {
    const std::wstring exe = CurrentExecutablePath();
    std::wstring command = L"\"" + (exe.empty() ? std::wstring(L"sr-ui.exe") : exe) + L"\"";
    const std::wstring parameters = BuildElevatedParameters(log_path);
    if (!parameters.empty()) {
        command += L" " + parameters;
    }
    return command;
}

// 提权启动的结果。
enum class ElevationLaunch {
    kStarted,      // 已成功拉起提权实例
    kCancelled,    // 用户在 UAC 对话框上点了取消
    kFailed,       // 其它失败（错误码见 error）
};

// 用 ShellExecuteW(runas) 拉起提权实例。**不自实现提权**，失败不静默吞掉。
inline ElevationLaunch LaunchElevated(const std::wstring& exe,
                                      const std::wstring& parameters,
                                      std::wstring& error) {
    error.clear();
    const std::wstring target = exe.empty() ? CurrentExecutablePath() : exe;
    if (target.empty()) {
        error = L"无法确定可执行文件路径";
        return ElevationLaunch::kFailed;
    }
    SetLastError(ERROR_SUCCESS);
    // 提权实例自己开新控制台窗口更清楚；SW_SHOWNORMAL 保持常规可见。
    const HINSTANCE result = ShellExecuteW(nullptr, L"runas", target.c_str(),
                                            parameters.empty() ? nullptr : parameters.c_str(),
                                            nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) > 32) {
        return ElevationLaunch::kStarted;
    }
    const DWORD code = GetLastError();
    if (code == ERROR_CANCELLED) {
        error = L"用户取消了提权";
        return ElevationLaunch::kCancelled;
    }
    error = L"ShellExecute(runas) 失败，GetLastError=" + std::to_wstring(code);
    return ElevationLaunch::kFailed;
}

}  // namespace sr::app
