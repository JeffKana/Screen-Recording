#include "core/util/PathProbe.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace sr::util {
namespace {

std::atomic<uint32_t> g_probe_sequence{0};

std::filesystem::path ProbePath(const std::filesystem::path& directory) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    char name[96] = {};
    std::snprintf(name,
                  sizeof(name),
                  ".sr_probe_%llu_%u.tmp",
                  static_cast<unsigned long long>(stamp),
                  static_cast<unsigned>(g_probe_sequence.fetch_add(1)));
    return directory / name;
}

bool WriteProbe(const std::filesystem::path& directory, std::wstring& error) {
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        error = L"cannot create output directory: " + directory.wstring();
        return false;
    }
    if (!std::filesystem::is_directory(directory, ec)) {
        error = L"output path is not a directory: " + directory.wstring();
        return false;
    }

    // 唯一探针名：两个录制进程并发开录时，固定名会互相删除对方的探针
    // （tasks/G5/S1-001/HANDOFF.md 的已知缺口）。
    const std::filesystem::path probe = ProbePath(directory);
    {
        std::ofstream file(probe, std::ios::binary | std::ios::trunc);
        if (!file) {
            error = L"output directory is not writable: " + directory.wstring();
            return false;
        }
        file.put('x');
        if (!file.good()) {
            file.close();
            std::filesystem::remove(probe, ec);
            error = L"output directory rejected write test: " + directory.wstring();
            return false;
        }
    }
    std::filesystem::remove(probe, ec);
    error.clear();
    return true;
}

}  // namespace

bool EnsureDirectoryWritable(const std::string& directory, std::string& error) {
    if (directory.empty()) {
        error = "output directory is empty";
        return false;
    }
    std::wstring wide_error;
    const bool ok = WriteProbe(std::filesystem::path(directory), wide_error);
    if (!ok) {
        // Recorder 的日志是窄字符；路径一般为 ASCII，非 ASCII 部分退化为 '?'，
        // 但错误类别（not writable / not a directory）仍然可读。
        error.clear();
        for (wchar_t c : wide_error) {
            error.push_back(c < 0x80 ? static_cast<char>(c) : '?');
        }
    } else {
        error.clear();
    }
    return ok;
}

bool EnsureDirectoryWritable(const std::wstring& directory, std::wstring& error) {
    if (directory.empty()) {
        error = L"output directory is empty";
        return false;
    }
    return WriteProbe(std::filesystem::path(directory), error);
}

}  // namespace sr::util
