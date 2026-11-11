#include "app/FileOps.h"

#include <cstring>
#include <filesystem>
#include <system_error>

#include <windows.h>
#include <shellapi.h>

namespace sr::app {
namespace {

constexpr wchar_t kInvalidNameChars[] = L"<>:\"/\\|?*";

bool EndsWithNoCase(const std::wstring& text, const std::wstring& suffix) {
    if (text.size() < suffix.size()) {
        return false;
    }
    return _wcsicmp(text.c_str() + (text.size() - suffix.size()), suffix.c_str()) == 0;
}

bool HasInvalidNameChar(const std::wstring& name) {
    for (wchar_t c : name) {
        if (c < 32) {
            return true;
        }
        for (const wchar_t* bad = kInvalidNameChars; *bad != L'\0'; ++bad) {
            if (c == *bad) {
                return true;
            }
        }
    }
    return false;
}

std::wstring FileNameOf(const std::wstring& path) {
    return std::filesystem::path(path).filename().wstring();
}

}  // namespace

bool RenameRecording(const std::wstring& directory,
                     const std::wstring& old_name,
                     const std::wstring& new_name,
                     std::wstring& new_path,
                     std::wstring& error) {
    new_path.clear();
    if (old_name.empty() || new_name.empty()) {
        error = L"文件名不能为空";
        return false;
    }
    if (HasInvalidNameChar(new_name) || new_name == L"." || new_name == L"..") {
        error = L"新文件名含非法字符: " + new_name;
        return false;
    }

    const std::filesystem::path source = std::filesystem::path(directory) / old_name;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(source, ec)) {
        error = L"源文件不存在: " + old_name;
        return false;
    }
    if (EndsWithNoCase(old_name, L".part")) {
        error = L"正在录制的临时文件不能重命名";
        return false;
    }

    std::wstring final_name = new_name;
    const std::wstring old_extension = std::filesystem::path(old_name).extension().wstring();
    if (std::filesystem::path(final_name).extension().empty() && !old_extension.empty()) {
        final_name += old_extension;
    }
    const std::filesystem::path target = std::filesystem::path(directory) / final_name;
    if (std::filesystem::exists(target, ec)) {
        error = L"目标文件已存在: " + final_name;
        return false;
    }

    // MoveFileEx 不带 MOVEFILE_REPLACE_EXISTING：即使并发创建同名文件也不会覆盖。
    if (!MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_COPY_ALLOWED)) {
        error = L"重命名失败, GetLastError=" + std::to_wstring(GetLastError());
        return false;
    }
    new_path = target.wstring();
    error.clear();
    return true;
}

bool DeleteToRecycleBin(const std::vector<std::wstring>& paths, std::wstring& error) {
    if (paths.empty()) {
        error = L"没有选中文件";
        return false;
    }
    std::wstring from;
    for (const std::wstring& path : paths) {
        if (EndsWithNoCase(path, L".part")) {
            error = L"正在录制的临时文件不能删除: " + FileNameOf(path);
            return false;
        }
        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec)) {
            error = L"文件不存在: " + FileNameOf(path);
            return false;
        }
        // 多字符串：每个路径以 \0 结尾，整体再以 \0 收尾。
        from += path;
        from.push_back(L'\0');
    }
    from.push_back(L'\0');

    SHFILEOPSTRUCTW operation{};
    operation.wFunc = FO_DELETE;
    operation.pFrom = from.c_str();
    // FOF_ALLOWUNDO = 进回收站；FOF_NOCONFIRMATION 的确认由 UI 的二次确认框负责。
    operation.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    const int result = SHFileOperationW(&operation);
    if (result != 0 || operation.fAnyOperationsAborted) {
        error = L"删除到回收站失败, code=" + std::to_wstring(result);
        return false;
    }
    error.clear();
    return true;
}

bool RevealInExplorer(const std::wstring& path, std::wstring& error) {
    const std::wstring arguments = L"/select,\"" + path + L"\"";
    const HINSTANCE instance = ShellExecuteW(nullptr, L"open", L"explorer.exe",
                                            arguments.c_str(), nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(instance) <= 32) {
        error = L"无法打开资源管理器";
        return false;
    }
    error.clear();
    return true;
}

bool CopyPathToClipboard(const std::wstring& path, std::wstring& error) {
    if (!OpenClipboard(nullptr)) {
        error = L"无法打开剪贴板";
        return false;
    }
    EmptyClipboard();
    const size_t bytes = (path.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory == nullptr) {
        CloseClipboard();
        error = L"剪贴板内存分配失败";
        return false;
    }
    void* target = GlobalLock(memory);
    if (target == nullptr) {
        GlobalFree(memory);
        CloseClipboard();
        error = L"剪贴板内存锁定失败";
        return false;
    }
    std::memcpy(target, path.c_str(), bytes);
    GlobalUnlock(memory);
    if (SetClipboardData(CF_UNICODETEXT, memory) == nullptr) {
        GlobalFree(memory);
        CloseClipboard();
        error = L"写入剪贴板失败";
        return false;
    }
    CloseClipboard();
    error.clear();
    return true;
}

}  // namespace sr::app
