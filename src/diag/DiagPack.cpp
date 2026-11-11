#include "diag/DiagPack.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <sstream>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dxgi.h>
#include <dxgi1_2.h>

#include "diag/Logger.h"

namespace sr::diag {
namespace {

constexpr size_t kMaxLogBytes = 400u * 1024u;   // log.txt 上限，保证整包 < 5MB
constexpr size_t kMaxConfigBytes = 64u * 1024u;
constexpr size_t kRecorderTailBytes = 128u * 1024u;
constexpr uint64_t kRecentWindowMs = 10ull * 60ull * 1000ull;

std::string Utf8(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (bytes <= 1) {
        return {};
    }
    std::string out(static_cast<size_t>(bytes - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, out.data(), bytes, nullptr, nullptr);
    return out;
}

// INI 由 WritePrivateProfileStringW 写成 ANSI（系统代码页），直接拷贝会得到
// 混合编码的文本。这里统一转 UTF-8；带 BOM 的 UTF-8 / UTF-16LE 也一并处理。
std::string NormalizeTextEncoding(const std::string& raw) {
    if (raw.empty()) {
        return raw;
    }
    const auto* bytes = reinterpret_cast<const unsigned char*>(raw.data());
    if (raw.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) {
        return raw.substr(3);
    }
    if (raw.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
        const size_t units = (raw.size() - 2) / sizeof(wchar_t);
        const std::wstring wide(reinterpret_cast<const wchar_t*>(raw.data() + 2), units);
        return Utf8(wide);
    }
    const int chars = MultiByteToWideChar(CP_ACP, 0, raw.c_str(), static_cast<int>(raw.size()),
                                          nullptr, 0);
    if (chars <= 0) {
        return raw;
    }
    std::wstring wide(static_cast<size_t>(chars), L'\0');
    MultiByteToWideChar(CP_ACP, 0, raw.c_str(), static_cast<int>(raw.size()), wide.data(), chars);
    return Utf8(wide);
}

bool MatchNoCase(const char* text, const char* needle) {
    for (; *needle != '\0'; ++text, ++needle) {
        if (*text == '\0') {
            return false;
        }
        if (std::tolower(static_cast<unsigned char>(*text)) !=
            std::tolower(static_cast<unsigned char>(*needle))) {
            return false;
        }
    }
    return true;
}

std::string TruncateTail(const std::string& text, size_t limit) {
    if (text.size() <= limit) {
        return text;
    }
    size_t start = text.size() - limit;
    // 从行边界开始，避免半行。
    const size_t newline = text.find('\n', start);
    if (newline != std::string::npos) {
        start = newline + 1;
    }
    return "…(older lines dropped)\n" + text.substr(start);
}

bool WriteText(const std::filesystem::path& path, const std::string& text, std::string& error) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        error = "cannot write " + path.filename().string();
        return false;
    }
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    stream.close();
    if (!stream) {
        error = "short write " + path.filename().string();
        return false;
    }
    return true;
}

std::string ReadFilePrefix(const std::filesystem::path& path, size_t limit) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return {};
    }
    std::string data(limit, '\0');
    stream.read(data.data(), static_cast<std::streamsize>(data.size()));
    data.resize(static_cast<size_t>(stream.gcount()));
    return data;
}

std::string ReadFileTail(const std::filesystem::path& path, size_t limit) {
    std::error_code ec;
    const uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec) {
        return {};
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return {};
    }
    const uintmax_t offset = size > limit ? size - limit : 0;
    stream.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    std::string data(limit, '\0');
    stream.read(data.data(), static_cast<std::streamsize>(data.size()));
    data.resize(static_cast<size_t>(stream.gcount()));
    if (offset > 0) {
        const size_t newline = data.find('\n');
        if (newline != std::string::npos) {
            data = data.substr(newline + 1);
        }
        data = "…(tail only)\n" + data;
    }
    return data;
}

std::string RegistryString(HKEY root, const wchar_t* subkey, const wchar_t* name) {
    wchar_t buffer[512] = {};
    DWORD bytes = sizeof(buffer);
    DWORD type = 0;
    if (RegGetValueW(root, subkey, name, RRF_RT_REG_SZ, &type, buffer, &bytes) != ERROR_SUCCESS) {
        return {};
    }
    return Utf8(buffer);
}

std::string OsDescription() {
    const wchar_t* kCurrentVersion = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    const std::string product = RegistryString(HKEY_LOCAL_MACHINE, kCurrentVersion, L"ProductName");
    const std::string build = RegistryString(HKEY_LOCAL_MACHINE, kCurrentVersion, L"CurrentBuild");
    const std::string display = RegistryString(HKEY_LOCAL_MACHINE, kCurrentVersion, L"DisplayVersion");
    std::ostringstream out;
    out << (product.empty() ? "Windows" : product);
    if (!display.empty()) {
        out << " " << display;
    }
    if (!build.empty()) {
        out << " (build " << build << ")";
    }
    SYSTEM_INFO info{};
    GetNativeSystemInfo(&info);
    out << " " << (info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ? "x64" : "x86");
    return out.str();
}

std::string GpuReport() {
    std::ostringstream out;
    out << "[adapters]\n";
    IDXGIFactory1* factory = nullptr;
    const HRESULT created = CreateDXGIFactory1(__uuidof(IDXGIFactory1),
                                               reinterpret_cast<void**>(&factory));
    if (FAILED(created) || factory == nullptr) {
        out << "error=DXGI factory unavailable (hr=" << static_cast<long>(created) << ")\n";
        return out.str();
    }
    for (UINT index = 0;; ++index) {
        IDXGIAdapter1* adapter = nullptr;
        if (factory->EnumAdapters1(index, &adapter) == DXGI_ERROR_NOT_FOUND || adapter == nullptr) {
            break;
        }
        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(adapter->GetDesc1(&desc))) {
            out << "adapter." << index << ".name=" << Utf8(desc.Description) << "\n";
            out << "adapter." << index << ".vendor=0x" << std::hex << desc.VendorId << std::dec << "\n";
            out << "adapter." << index << ".device=0x" << std::hex << desc.DeviceId << std::dec << "\n";
            out << "adapter." << index << ".vram_mb="
                << static_cast<unsigned long long>(desc.DedicatedVideoMemory / (1024ull * 1024ull)) << "\n";
            out << "adapter." << index << ".software=" << ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) ? 1 : 0)
                << "\n";
        }
        for (UINT output_index = 0;; ++output_index) {
            IDXGIOutput* output = nullptr;
            if (adapter->EnumOutputs(output_index, &output) == DXGI_ERROR_NOT_FOUND || output == nullptr) {
                break;
            }
            DXGI_OUTPUT_DESC output_desc{};
            if (SUCCEEDED(output->GetDesc(&output_desc))) {
                RECT rect = output_desc.DesktopCoordinates;
                out << "adapter." << index << ".output." << output_index << ".device="
                    << Utf8(output_desc.DeviceName) << "\n";
                out << "adapter." << index << ".output." << output_index << ".rect=" << rect.left << ","
                    << rect.top << "," << (rect.right - rect.left) << "x"
                    << (rect.bottom - rect.top) << "\n";
                out << "adapter." << index << ".output." << output_index << ".attached="
                    << (output_desc.AttachedToDesktop ? 1 : 0) << "\n";
            }
            output->Release();
        }
        adapter->Release();
    }
    factory->Release();

    out << "\n[monitors]\n";
    out << "monitors.count=" << GetSystemMetrics(SM_CMONITORS) << "\n";
    out << "virtual_screen=" << GetSystemMetrics(SM_CXVIRTUALSCREEN) << "x"
        << GetSystemMetrics(SM_CYVIRTUALSCREEN) << "\n";
    HDC screen_dc = GetDC(nullptr);
    if (screen_dc != nullptr) {
        out << "system_dpi=" << GetDeviceCaps(screen_dc, LOGPIXELSX) << "\n";
        ReleaseDC(nullptr, screen_dc);
    }
    struct MonitorContext {
        std::ostringstream* out = nullptr;
        int index = 0;
    } context{&out, 0};
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
            auto* ctx = reinterpret_cast<MonitorContext*>(data);
            MONITORINFOEXW info{};
            info.cbSize = sizeof(info);
            if (GetMonitorInfoW(monitor, &info)) {
                RECT rect = info.rcMonitor;
                *ctx->out << "monitor." << ctx->index << ".device=" << Utf8(info.szDevice) << "\n";
                *ctx->out << "monitor." << ctx->index << ".rect=" << rect.left << "," << rect.top << ","
                          << (rect.right - rect.left) << "x" << (rect.bottom - rect.top) << "\n";
                *ctx->out << "monitor." << ctx->index << ".primary="
                          << ((info.dwFlags & MONITORINFOF_PRIMARY) ? 1 : 0) << "\n";
                ++ctx->index;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&context));

    out << "\n[hardware]\n";
    out << "cpu=" << RegistryString(HKEY_LOCAL_MACHINE,
                                    L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                                    L"ProcessorNameString")
        << "\n";
    SYSTEM_INFO info{};
    GetNativeSystemInfo(&info);
    out << "cpu.logical=" << info.dwNumberOfProcessors << "\n";
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory)) {
        out << "memory.total_mb=" << (memory.ullTotalPhys / (1024ull * 1024ull)) << "\n";
        out << "memory.available_mb=" << (memory.ullAvailPhys / (1024ull * 1024ull)) << "\n";
    }
    return out.str();
}

std::string TimestampForDirectory() {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer), "%04u%02u%02u-%02u%02u%02u",
                  now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    return buffer;
}

std::string TimestampForHeader() {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    char buffer[40] = {};
    std::snprintf(buffer, sizeof(buffer), "%04u-%02u-%02uT%02u:%02u:%02u",
                  now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    return buffer;
}

}  // namespace

std::string TruncateByAge(const std::string& text, uint64_t window_ms) {
    // 日志行格式固定为 `t=<tick_ms> …`（见 Logger.cpp），据此做时间窗裁剪：
    // 先找最新一条的时间戳，再丢掉比它早 window_ms 以上的行。
    auto timestamp_at = [&text](size_t pos, size_t end, uint64_t& value) {
        if (end <= pos + 2 || text[pos] != 't' || text[pos + 1] != '=') {
            return false;
        }
        uint64_t parsed = 0;
        size_t index = pos + 2;
        for (; index < end && text[index] >= '0' && text[index] <= '9'; ++index) {
            parsed = parsed * 10 + static_cast<uint64_t>(text[index] - '0');
        }
        if (index == pos + 2) {
            return false;
        }
        value = parsed;
        return true;
    };

    uint64_t newest = 0;
    bool any = false;
    for (size_t pos = 0; pos < text.size();) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) {
            end = text.size();
        }
        uint64_t value = 0;
        if (timestamp_at(pos, end, value)) {
            any = true;
            newest = std::max(newest, value);
        }
        if (end == text.size()) {
            break;
        }
        pos = end + 1;
    }
    if (!any) {
        return text;
    }

    const uint64_t cutoff = newest > window_ms ? newest - window_ms : 0;
    std::string out;
    out.reserve(text.size());
    for (size_t pos = 0; pos < text.size();) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) {
            end = text.size();
        }
        uint64_t value = 0;
        // 没有时间戳的行（文件头 / 说明）一律保留，避免把上下文裁掉。
        const bool keep = !timestamp_at(pos, end, value) || value >= cutoff;
        if (keep) {
            out.append(text, pos, end - pos);
            out.push_back('\n');
        }
        if (end == text.size()) {
            break;
        }
        pos = end + 1;
    }
    return out;
}

std::string SanitizeText(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    size_t index = 0;
    while (index < text.size()) {
        const char current = text[index];
        const bool backslash = current == '\\' && index + 7 <= text.size() &&
                               MatchNoCase(text.c_str() + index, "\\Users\\");
        const bool slash = current == '/' && index + 7 <= text.size() &&
                           MatchNoCase(text.c_str() + index, "/Users/");
        if (backslash || slash) {
            const size_t name_start = index + 7;
            size_t name_end = text.find_first_of("\\/\"'\r\n\t ", name_start);
            if (name_end == std::string::npos) {
                name_end = text.size();
            }
            if (name_end > name_start) {
                out += backslash ? "\\Users\\<user>" : "/Users/<user>";
                index = name_end;
                continue;
            }
        }
        out.push_back(current);
        ++index;
    }
    return out;
}

std::wstring DefaultDiagRoot() {
    wchar_t buffer[MAX_PATH] = {};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    std::wstring root = length > 0 ? std::wstring(buffer, length) : std::wstring(L".");
    root += L"\\sr-ui\\diag";
    return root;
}

DiagResult Export(const DiagRequest& request) {
    const ULONGLONG started = GetTickCount64();
    DiagResult result;

    std::filesystem::path root = request.output_root;
    if (root.empty()) {
        root = DefaultDiagRoot();
    }
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    std::filesystem::path directory = root / ("sr-diag-" + TimestampForDirectory());
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        result.error = "cannot create " + Utf8(directory.wstring());
        result.elapsed_ms = GetTickCount64() - started;
        return result;
    }

    std::string error;
    std::ostringstream version;
    version << "product=" << request.product << "\n";
    version << "version=" << request.version << "\n";
    version << "protocol=" << request.protocol << "\n";
    version << "build=" << request.build << "\n";
    version << "test_hooks=" << (request.test_hooks ? 1 : 0) << "\n";
    version << "os=" << OsDescription() << "\n";
    version << "collected_at=" << TimestampForHeader() << "\n";
    if (!WriteText(directory / "version.txt", version.str(), error)) {
        result.error = error;
        result.elapsed_ms = GetTickCount64() - started;
        return result;
    }

    if (!WriteText(directory / "gpu.txt", GpuReport(), error)) {
        result.error = error;
        result.elapsed_ms = GetTickCount64() - started;
        return result;
    }

    std::ostringstream log;
    log << "=== app log (last 10 minutes, ring buffer) ===\n";
    const std::string recent = RecentText(kRecentWindowMs);
    log << recent;
    if (recent.empty()) {
        log << "(no recent log lines in this process)\n";
    }
    // 环形缓冲为空（典型：独立进程执行 --export-diag）时，退化为读取本进程日志尾部。
    // 仍然只读尾部 + 截断，不做整文件拷贝。
    if (!request.ui_log_path.empty()) {
        const std::string tail = ReadFileTail(request.ui_log_path, kRecorderTailBytes);
        if (!tail.empty()) {
            log << "\n=== ui log (bounded tail) ===\n" << tail;
            if (tail.back() != '\n') {
                log << "\n";
            }
        }
    }
    if (!request.recorder_log_path.empty()) {
        log << "\n=== recorder log (tail) ===\n";
        const std::string tail = ReadFileTail(request.recorder_log_path, kRecorderTailBytes);
        log << (tail.empty() ? "(recorder log unavailable)\n" : tail);
        if (!tail.empty() && tail.back() != '\n') {
            log << "\n";
        }
    }
    // 先按时间窗裁到最近 10 分钟（环形缓冲 + recorder 日志尾部一起过一遍），
    // 再按 400KB 截尾，保证「只含最近 10 分钟」不依赖日志总量。
    const std::string log_text =
        TruncateTail(TruncateByAge(SanitizeText(log.str()), kRecentWindowMs), kMaxLogBytes);
    if (!WriteText(directory / "log.txt", log_text, error)) {
        result.error = error;
        result.elapsed_ms = GetTickCount64() - started;
        return result;
    }

    std::ostringstream config;
    config << "; 配置快照（仅文本，不含用户画面/音频）\n";
    if (!request.config_path.empty()) {
        const std::string raw = SanitizeText(
            NormalizeTextEncoding(ReadFilePrefix(request.config_path, kMaxConfigBytes)));
        if (raw.empty()) {
            config << "; 配置文件不存在或为空: " << Utf8(request.config_path) << "\n";
        } else {
            config << raw;
            if (raw.back() != '\n') {
                config << "\n";
            }
        }
    } else {
        config << "; 未提供配置文件路径\n";
    }
    config << "\n[session]\n" << request.session;
    if (!WriteText(directory / "config.ini", SanitizeText(config.str()), error)) {
        result.error = error;
        result.elapsed_ms = GetTickCount64() - started;
        return result;
    }

    size_t total = 0;
    for (const char* name : {"version.txt", "gpu.txt", "log.txt", "config.ini"}) {
        const std::filesystem::path path = directory / name;
        const uintmax_t size = std::filesystem::file_size(path, ec);
        total += ec ? 0 : static_cast<size_t>(size);
        result.files.emplace_back(name);
    }
    result.ok = true;
    result.directory = directory.wstring();
    result.bytes = total;
    result.elapsed_ms = GetTickCount64() - started;
    return result;
}

}  // namespace sr::diag
