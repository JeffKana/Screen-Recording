#pragma once

#include <string>
#include <vector>

namespace sr::app {

// M5-07：录制文件操作。全部走 Shell，删除必须进回收站（FOF_ALLOWUNDO），
// 不允许用 DeleteFile / std::filesystem::remove 直接抹除。

// 重命名：new_name 只允许文件名（不含路径分隔符）；未带扩展名时沿用原扩展名。
// 目标已存在时失败，不覆盖（与 M5-15 同一口径）。
bool RenameRecording(const std::wstring& directory,
                     const std::wstring& old_name,
                     const std::wstring& new_name,
                     std::wstring& new_path,
                     std::wstring& error);

// 删除到回收站。任一路径是正在录制的 .part 时整体拒绝。
bool DeleteToRecycleBin(const std::vector<std::wstring>& paths, std::wstring& error);

// 在资源管理器中定位文件。
bool RevealInExplorer(const std::wstring& path, std::wstring& error);

// 复制完整路径到剪贴板。
bool CopyPathToClipboard(const std::wstring& path, std::wstring& error);

}  // namespace sr::app
