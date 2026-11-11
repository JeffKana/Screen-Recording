#pragma once

#include <string>

namespace sr::util {

// 确保目录存在且可写：创建目录（含父级）→ 写一个唯一临时文件 → 删除。
// 真实写入比只读 ACL 检查可靠，能同时覆盖只读介质、配额耗尽与受控文件夹访问。
// 这是 M8-06 的唯一实现；Recorder 的 PrepareOutputPath 与 UI 的路径选择共用。
//
// 注意：UI 的预检只是提示，真正以 Recorder 的 exit 13 为准（评审轮 28 备注②）。
bool EnsureDirectoryWritable(const std::string& directory, std::string& error);
bool EnsureDirectoryWritable(const std::wstring& directory, std::wstring& error);

}  // namespace sr::util
