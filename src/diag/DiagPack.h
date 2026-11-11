#pragma once

#include <cstdint>
#include <string>
#include <vector>

// M8-14：一键导出诊断包（05 ER-14）。
//
// 产物固定四类文件：version.txt / gpu.txt / log.txt / config.ini。
// 硬约束：
//   - 只写文本，不含任何画面/音频/帧数据（grep 断言 png/jpg/mkv/frame）；
//   - 导出前把路径里的用户名脱敏（C:\Users\<name> → C:\Users\<user>）；
//   - 总大小 < 5MB、导出 ≤ 2s（log.txt 用环形缓冲 + 截断，不整文件拷贝）。
namespace sr::diag {

struct DiagRequest {
    std::wstring output_root;        // 父目录；实际写到 <root>\sr-diag-<时间戳>
    std::wstring config_path;        // INI 快照来源，可为空
    std::wstring recorder_log_path;  // 本次会话的 recorder 日志（可选，只读尾部）
    std::wstring ui_log_path;        // 本进程日志文件（可选，环形缓冲为空时兜底取尾部）
    std::string product = "sr-ui";
    std::string version = "0.1.0";
    std::string protocol = "1";
    std::string build = "unknown";
    std::string session;  // 当前会话参数（键=值，换行分隔）
    bool test_hooks = false;
};

struct DiagResult {
    bool ok = false;
    std::wstring directory;
    std::vector<std::string> files;
    uint64_t bytes = 0;
    uint64_t elapsed_ms = 0;
    std::string error;
};

DiagResult Export(const DiagRequest& request);

// %LOCALAPPDATA%\sr-ui\diag
std::wstring DefaultDiagRoot();

// 把文本里的 C:\Users\<用户名> / 反斜杠用户目录替换为 <user>。
std::string SanitizeText(const std::string& text);

// 只保留最近 window_ms 的 `t=<ms>` 行（T-17 诊断包：最近 10 分钟）。
// 没有时间戳的行（文件头/说明）保留在首部；整段都没有时间戳时原样返回。
// 独立成函数是为了能用合成日志做确定性验证（sr-recorder --diag-window-test）。
std::string TruncateByAge(const std::string& text, uint64_t window_ms);

}  // namespace sr::diag
