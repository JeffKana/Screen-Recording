#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <windows.h>
#include <psapi.h>

#include "encode/EncoderList.h"
#include "mux/MuxFormat.h"
#include "ipc/PipeChannel.h"
#include "platform/win/QpcClock.h"
#include "recorder/RecordingSession.h"
#include "core/util/FreePath.h"
#include "resilience/MkvRecovery.h"
#include <filesystem>
#include "diag/DiagPack.h"
#include "diag/Logger.h"
#include "recorder/SessionServer.h"

namespace {

constexpr const char* kVersion = "0.1.0";
constexpr uint32_t kExitAlreadyRunning = 4;
constexpr uint32_t kExitDiagFailed = 17;
// 轮 48（M3-09#3）：--format 非法值/缺值 → 参数错误。其余解析失败仍保持历史码 1。
constexpr uint32_t kExitBadFormatArgument = 2;

// F3：测试注入开关由 SR_TEST_HOOKS 宏隔离；打包构建关掉后这些参数不再被接受。
#if defined(SR_TEST_HOOKS)
constexpr bool kTestHooks = true;
constexpr const char* kBuildTag = "dev+test-hooks";
#else
constexpr bool kTestHooks = false;
constexpr const char* kBuildTag = "release";
#endif

std::wstring Widen(const std::string& text) {
    return std::wstring(text.begin(), text.end());
}

struct Options {
    bool show_version = false;
    bool show_help = false;
    std::string token = "dev";
    // 轮 30：日志分级与诊断包
    std::string log_file;
    std::string log_level = "info";
    std::string export_diag;
    std::string config_path;

    // G3：直接录制一段并落盘（不经 UI，用于验收与冒烟）。
    bool record = false;
    uint32_t seconds = 10;
    uint32_t fps = 30;
    // M1-07/M1-08/M1-09：按窗口采集（0 = 不按窗口）+ 适配策略。
    uint64_t window_handle = 0;
    std::string fit_mode = "stretch";
    uint32_t bitrate_kbps = 6000;
    std::string rate_mode = "cbr";
    // M3-04 / D-53：VBR 的峰值上限与两模式的画质档位（0 = 按模式取默认）。
    uint32_t max_bitrate_kbps = 0;
    uint32_t quality = 0;
    uint32_t display = 0;
    // M1-24：是否把鼠标指针写入成片（WGC 支持开关；DXGI/窗口源不支持，见 caps）。
    bool include_cursor = true;
    std::string output = "out.mkv";
    bool output_explicit = false;
    // 轮 48：默认 MKV；--format mp4 且未给 --out 时默认文件名跟格式走。
    sr::mux::ContainerFormat format = sr::mux::ContainerFormat::Mkv;
    // 解析失败时的退出码：只有 --format 参数错误是 2，其余保持历史行为 1。
    uint32_t parse_exit_code = 1;
    std::string dump_es;
    // 崩溃恢复（M8-02）：扫描目录或修复单个 .part 文件
    uint32_t disk_warn_mb = 2048;
    uint32_t disk_stop_mb = 128;
    uint32_t countdown_seconds = 0;
    uint32_t pause_at_seconds = 0;
    uint32_t pause_duration_seconds = 0;
    std::string backend;
    std::vector<uint32_t> pause_at_list;
    std::vector<uint32_t> pause_duration_list;
    uint32_t cancel_at_seconds = 0;
    bool fail_resume = false;
    bool fail_encoder = false;
    // 轮 47：--fail-encoder 的可选目标（hardware / software / all）；空 = 全部失败（既有语义）。
    std::string fail_encoder_target;
    uint64_t fail_write_after = 0;
    bool fail_resume_backend = false;
    std::string fallback_dir;
    std::string control_pipe;
    std::string control_token = "dev";
    std::string fallback_commit;
    uint32_t control_wait_ms = 5000;
    uint32_t pause_timeout_seconds = 900;
    bool region_enabled = false;
    sr::Rect region{};
    std::string scan_dir;
    std::string repair_file;
    // M5-06：文件列表数据源（UI 与验收共用一条解析路径）。
    std::string list_dir;
    std::string list_skip;
    // M3-05：列出实际可用的编码器（探测在设置页/本命令里做，不进录制启动路径）。
    bool list_encoders = false;
    // 轮 47：同一进程里重复探测 N 次，核验「失败可重试 / 状态不被缓存」；句柄数是观测值。
    uint32_t probe_repeat = 1;
    // M3-05：强制指定编码器 id（不可用则开录失败并给原因）。
    std::string encoder_id;
    // M6-03 验收：每 N 帧注入一次丢帧（0 = 不注入）
    uint32_t inject_drop_every = 0;
    // M6-08#2：令既有 CPU 中转路径可达（仅 SR_TEST_HOOKS 构建暴露）。
    bool force_cpu = false;
    // T-17 诊断包时间窗自检：读入合成日志，输出裁剪后的字节数/时间跨度
    std::string diag_window_test;
};

void PrintUsage() {
    std::printf(
        "sr-recorder %s (%s)\n"
        "usage: sr-recorder [--token <token>] [--version] [--help]\n"
        "  --token      与 UI 约定的会话令牌，握手时必须匹配 (默认 dev)\n"
        "  --log-level  debug|info|warn（默认 info；重启生效）\n"
        "  --log-file   <path> 同时把分级日志写入文件\n"
        "  --export-diag <dir> 一键导出诊断包（version/gpu/log/config）\n"
        "  --config     <ini> 诊断包里的配置快照来源\n"
        "  --list       <dir> 列出录制历史（时间倒序；.part 标注可恢复）\n"
        "  --list-skip  <path> 跳过以此前缀开头的 .part（正在录制的临时文件）\n"
        "  --scan       <dir> 扫描可恢复分片 / --repair <part> 修复单个分片\n"
        "  --list-encoders  列出实际可用的视频编码器（含不可用项与原因，M3-05）\n"
        "  --encoder    <id>  强制指定编码器（见 --list-encoders 的 id）；不可用则开录失败\n"
        "  --format     mkv|mp4 封装格式（默认 mkv）；mp4 建议同时给 --out xxx.mp4\n"
        "  --record     直接录制一段（不启动 UI），配合 --seconds/--fps/--out 使用\n"
        "  --window     <hwnd> 按窗口录制，hwnd 支持十进制或 0x 十六进制；\n"
        "               仅窗口源下有效，指定后忽略 --region/--display（窗口源优先）\n"
        "  --fit        pad|stretch 窗口宽高比变化时的适配策略（配合 --window 使用）\n"
        "  --region     x,y,w,h 按区域录制；与 --window 不可同时生效（--window 优先）\n"
        "  --cursor     on|off 是否把鼠标指针写入成片（M1-24；默认 on，仅 WGC 桌面源支持开关）\n"
        "  --version    打印版本与协议版本\n"
        "  --help       打印本帮助\n",
        kVersion, kBuildTag);
#if defined(SR_TEST_HOOKS)
    std::printf(
        "调试开关（SR_TEST_HOOKS=ON 的构建才有，Release 交付包不含）:\n"
        "  --seconds/--fps/--bitrate/--rate-mode cbr|vbr/--max-bitrate/--quality/--out/--display（配合 --record）\n"
        "  --quality <0~100>        画质档位；MF 软编回读接受但不改变成片（轮 34 R3），仅为兼容保留\n"
        "  --inject-drop-every <n>  每 n 帧注入一次丢帧（M6-03 验收）\n"
        "  --force-cpu             强制走既有 CPU 中转路径（M6-08#2 验收；验证的是既有降级能力）\n"
        "  --diag-window-test <file> 用合成日志验证诊断包「只留最近 10 分钟」的裁剪\n"
        "  --fail-resume / --fail-encoder [hardware|software|all] / --fail-write-after <n> / --fail-resume-backend\n"
        "  --fallback-dir <dir> / --fallback-commit <part>\n"
        "  --probe-repeat <n>    与 --list-encoders 合用：同进程重复探测 n 次（重试/泄漏护栏）\n");
#endif
}

bool ParseArgs(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "--version") == 0) {
            options.show_version = true;
        } else if (std::strcmp(arg, "--help") == 0) {
            options.show_help = true;
        } else if (std::strcmp(arg, "--log-file") == 0 && i + 1 < argc) {
            options.log_file = argv[++i];
        } else if (std::strcmp(arg, "--log-level") == 0 && i + 1 < argc) {
            options.log_level = argv[++i];
        } else if (std::strcmp(arg, "--export-diag") == 0 && i + 1 < argc) {
            options.export_diag = argv[++i];
        } else if (std::strcmp(arg, "--config") == 0 && i + 1 < argc) {
            options.config_path = argv[++i];
        } else if (std::strcmp(arg, "--token") == 0) {
            if (i + 1 >= argc) {
                return false;
            }
            options.token = argv[++i];
        } else if (std::strcmp(arg, "--record") == 0) {
            options.record = true;
        } else if (std::strcmp(arg, "--seconds") == 0 && i + 1 < argc) {
            options.seconds = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (std::strcmp(arg, "--fps") == 0 && i + 1 < argc) {
            options.fps = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (std::strcmp(arg, "--bitrate") == 0 && i + 1 < argc) {
            options.bitrate_kbps = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (std::strcmp(arg, "--rate-mode") == 0 && i + 1 < argc) {
            options.rate_mode = argv[++i];
        } else if (std::strcmp(arg, "--max-bitrate") == 0 && i + 1 < argc) {
            options.max_bitrate_kbps = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (std::strcmp(arg, "--quality") == 0 && i + 1 < argc) {
            options.quality = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (std::strcmp(arg, "--display") == 0 && i + 1 < argc) {
            options.display = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (std::strcmp(arg, "--cursor") == 0 && i + 1 < argc) {
            // M1-24：on / off。非法值不静默回落，直接判解析失败。
            const char* mode = argv[++i];
            if (std::strcmp(mode, "off") == 0) {
                options.include_cursor = false;
            } else if (std::strcmp(mode, "on") == 0) {
                options.include_cursor = true;
            } else {
                return false;
            }
        } else if (std::strcmp(arg, "--out") == 0 && i + 1 < argc) {
            options.output = argv[++i];
            options.output_explicit = true;
        } else if (std::strcmp(arg, "--format") == 0) {
            // M3-09#3：非法值/缺值 → exit 2 + usage，不静默回退到默认格式。
            if (i + 1 >= argc) {
                std::fprintf(stderr, "missing --format value (mkv|mp4)\n");
                options.parse_exit_code = kExitBadFormatArgument;
                return false;
            }
            const char* value = argv[++i];
            if (std::strcmp(value, "mkv") == 0) {
                options.format = sr::mux::ContainerFormat::Mkv;
            } else if (std::strcmp(value, "mp4") == 0) {
                options.format = sr::mux::ContainerFormat::Mp4;
            } else {
                std::fprintf(stderr, "invalid --format value: %s (mkv|mp4)\n", value);
                options.parse_exit_code = kExitBadFormatArgument;
                return false;
            }
        } else if (std::strcmp(arg, "--dump-es") == 0 && i + 1 < argc) {
            options.dump_es = argv[++i];
        } else if (std::strcmp(arg, "--disk-warn-mb") == 0 && i + 1 < argc) {
            options.disk_warn_mb = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (std::strcmp(arg, "--disk-stop-mb") == 0 && i + 1 < argc) {
            options.disk_stop_mb = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (std::strcmp(arg, "--window") == 0 && i + 1 < argc) {
            // M1-07/M1-08/M1-09：按窗口采集。接受十进制与 0x 前缀十六进制。
            const char* value = argv[++i];
            options.window_handle = std::strtoull(value, nullptr, 0);
            if (options.window_handle == 0) {
                std::fprintf(stderr, "invalid --window value: %s\n", value);
                return false;
            }
        } else if (std::strcmp(arg, "--fit") == 0 && i + 1 < argc) {
            // M1-08#2：适配策略（pad = 等比+黑边 / stretch = 拉伸铺满）。
            options.fit_mode = argv[++i];
            if (options.fit_mode != "pad" && options.fit_mode != "stretch") {
                std::fprintf(stderr, "invalid --fit value: %s (pad|stretch)\n",
                             options.fit_mode.c_str());
                return false;
            }
        } else if (std::strcmp(arg, "--backend") == 0 && i + 1 < argc) {
            options.backend = argv[++i];
        } else if (std::strcmp(arg, "--region") == 0 && i + 1 < argc) {
            int x = 0;
            int y = 0;
            int width = 0;
            int height = 0;
            if (std::sscanf(argv[++i], "%d,%d,%d,%d", &x, &y, &width, &height) != 4) {
                return false;
            }
            if (width > 0 && height > 0) {
                options.region_enabled = true;
                options.region = sr::Rect{x, y, width, height};
            }
        } else if (std::strcmp(arg, "--countdown") == 0 && i + 1 < argc) {
            options.countdown_seconds = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (std::strcmp(arg, "--pause-at") == 0 && i + 1 < argc) {
            for (char* token = std::strtok(argv[++i], ","); token != nullptr; token = std::strtok(nullptr, ",")) {
                options.pause_at_list.push_back(static_cast<uint32_t>(std::strtoul(token, nullptr, 10)));
            }
        } else if (std::strcmp(arg, "--pause-duration") == 0 && i + 1 < argc) {
            for (char* token = std::strtok(argv[++i], ","); token != nullptr; token = std::strtok(nullptr, ",")) {
                options.pause_duration_list.push_back(static_cast<uint32_t>(std::strtoul(token, nullptr, 10)));
            }
        } else if (std::strcmp(arg, "--cancel-at") == 0 && i + 1 < argc) {
            options.cancel_at_seconds = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
#if defined(SR_TEST_HOOKS)
        } else if (std::strcmp(arg, "--fail-resume") == 0) {
            options.fail_resume = true;
        } else if (std::strcmp(arg, "--fail-encoder") == 0) {
            options.fail_encoder = true;
            // 可选目标只吃三个字面量；别的参数（含无参形式）一律保持既有语义，
            // 不让 --fail-encoder --out ... 这类组合被误吞掉下一个开关。
            if (i + 1 < argc) {
                const char* next = argv[i + 1];
                if (std::strcmp(next, "hardware") == 0 || std::strcmp(next, "software") == 0 ||
                    std::strcmp(next, "all") == 0) {
                    options.fail_encoder_target = argv[++i];
                }
            }
        } else if (std::strcmp(arg, "--probe-repeat") == 0 && i + 1 < argc) {
            options.probe_repeat = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
            if (options.probe_repeat == 0 || options.probe_repeat > 20) {
                options.probe_repeat = 1;
            }
        } else if (std::strcmp(arg, "--fail-write-after") == 0 && i + 1 < argc) {
            options.fail_write_after = std::strtoull(argv[++i], nullptr, 10);
        } else if (std::strcmp(arg, "--fail-resume-backend") == 0) {
            options.fail_resume_backend = true;
        } else if (std::strcmp(arg, "--inject-drop-every") == 0 && i + 1 < argc) {
            options.inject_drop_every = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (std::strcmp(arg, "--force-cpu") == 0) {
            options.force_cpu = true;
        } else if (std::strcmp(arg, "--diag-window-test") == 0 && i + 1 < argc) {
            options.diag_window_test = argv[++i];
#endif
        } else if (std::strcmp(arg, "--fallback-dir") == 0 && i + 1 < argc) {
            options.fallback_dir = argv[++i];
        } else if (std::strcmp(arg, "--control-pipe") == 0) {
            // 可选值：不给值时用「进程 pid 命名」的默认管道（srctl 可直接算出来）。
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                options.control_pipe = argv[++i];
            } else {
                // 直接按 §7 的命名规则拼，避免宽窄字符转换告警。
                options.control_pipe = "\\\\.\\pipe\\sr.recorder." + std::to_string(GetCurrentProcessId());
            }
        } else if (std::strcmp(arg, "--control-token") == 0 && i + 1 < argc) {
            options.control_token = argv[++i];
        } else if (std::strcmp(arg, "--fallback-commit") == 0 && i + 1 < argc) {
            options.fallback_commit = argv[++i];
        } else if (std::strcmp(arg, "--control-wait") == 0 && i + 1 < argc) {
            options.control_wait_ms = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (std::strcmp(arg, "--pause-timeout") == 0 && i + 1 < argc) {
            options.pause_timeout_seconds = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (std::strcmp(arg, "--scan") == 0 && i + 1 < argc) {
            options.scan_dir = argv[++i];
        } else if (std::strcmp(arg, "--repair") == 0 && i + 1 < argc) {
            options.repair_file = argv[++i];
        } else if (std::strcmp(arg, "--list") == 0 && i + 1 < argc) {
            options.list_dir = argv[++i];
        } else if (std::strcmp(arg, "--list-skip") == 0 && i + 1 < argc) {
            options.list_skip = argv[++i];
        } else if (std::strcmp(arg, "--list-encoders") == 0) {
            options.list_encoders = true;
        } else if (std::strcmp(arg, "--encoder") == 0 && i + 1 < argc) {
            options.encoder_id = argv[++i];
        } else {
            return false;
        }
    }
    return true;
}

std::wstring LockFilePath() {
    wchar_t temp[MAX_PATH] = {};
    const DWORD length = GetTempPathW(MAX_PATH, temp);
    std::wstring path(temp, length);
    path += L"sr-recorder.lock";
    return path;
}

// M2-12：同一时刻只允许一个 Recorder 实例。第二实例读锁文件拿到 pid 后退出。
HANDLE AcquireSingleInstance(uint32_t& existing_pid) {
    existing_pid = 0;
    const std::wstring path = LockFilePath();

    // 不用 FILE_FLAG_DELETE_ON_CLOSE：带 delete-pending 的文件，后续 open 需要
    // FILE_SHARE_DELETE，而首实例又要靠「不允许共享写」来保证互斥，两者冲突。
    // 改为显式清理（见 main 末尾），进程崩溃留下的陈旧文件会被 CREATE_ALWAYS 覆盖。
    HANDLE handle = CreateFileW(path.c_str(),
                                GENERIC_WRITE,
                                FILE_SHARE_READ,  // 允许他人读取 pid，禁止他人写入
                                nullptr,
                                CREATE_ALWAYS,
                                FILE_ATTRIBUTE_TEMPORARY,
                                nullptr);
    if (handle != INVALID_HANDLE_VALUE) {
        char pid_text[32] = {};
        std::snprintf(pid_text, sizeof(pid_text), "%lu", static_cast<unsigned long>(GetCurrentProcessId()));
        DWORD written = 0;
        WriteFile(handle, pid_text, static_cast<DWORD>(std::strlen(pid_text)), &written, nullptr);
        return handle;
    }

    if (GetLastError() != ERROR_SHARING_VIOLATION) {
        return INVALID_HANDLE_VALUE;
    }

    // 已有实例：读出它的 pid 供调用方提示。
    // share 必须同时允许 READ|WRITE，否则与首实例持有的 GENERIC_WRITE 句柄冲突，
    // 会再次返回 ERROR_SHARING_VIOLATION（导致 pid 永远读不到）。
    HANDLE reader = CreateFileW(path.c_str(),
                                GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                nullptr,
                                OPEN_EXISTING,
                                0,
                                nullptr);
    if (reader == INVALID_HANDLE_VALUE) {
        return INVALID_HANDLE_VALUE;
    }
    char buffer[32] = {};
    DWORD read = 0;
    if (ReadFile(reader, buffer, sizeof(buffer) - 1, &read, nullptr) && read > 0) {
        existing_pid = static_cast<uint32_t>(std::strtoul(buffer, nullptr, 10));
    }
    CloseHandle(reader);
    return INVALID_HANDLE_VALUE;
}

bool EndsWithNoCase(const std::string& text, const std::string& suffix) {
    if (text.size() < suffix.size()) {
        return false;
    }
    const size_t offset = text.size() - suffix.size();
    for (size_t i = 0; i < suffix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(text[offset + i])) !=
            std::tolower(static_cast<unsigned char>(suffix[i]))) {
            return false;
        }
    }
    return true;
}

// 文件名统一按 UTF-8 输出：path::string() 给的是系统代码页，中文文件名会乱码。
std::string Utf8FileName(const std::filesystem::path& path) {
    const std::wstring wide = path.filename().wstring();
    if (wide.empty()) {
        return {};
    }
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (bytes <= 1) {
        return {};
    }
    std::string out(static_cast<size_t>(bytes - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, out.data(), bytes, nullptr, nullptr);
    return out;
}

struct ListEntry {
    std::string name;
    bool part = false;
    const char* state = "complete";
    uint64_t size = 0;
    int64_t mtime_ms = 0;
    int64_t duration_ms = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

// M5-06：列表数据源。成品只读头（时长/分辨率）；.part 走 MkvRecovery::Probe，
// 与 `--scan` 完全同一条判定路径（可恢复 = repairable），不允许两套结论。
// 正在写入的 .part 由调用方用实时快照补齐，这里按前缀跳过（避免扫描增长中的大文件）。
int RunList(const std::string& dir, const std::string& skip_prefix) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        std::printf("list_error=not a directory\n");
        std::fflush(stdout);
        return 12;
    }

    sr::resilience::MkvRecovery recovery;
    std::vector<ListEntry> entries;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) {
            break;
        }
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::string path = entry.path().string();
        const bool part = EndsWithNoCase(path, ".part");
        const bool video = EndsWithNoCase(path, ".mkv") || EndsWithNoCase(path, ".mp4");
        if (!part && !video) {
            continue;
        }
        if (part && !skip_prefix.empty() && path.rfind(skip_prefix, 0) == 0) {
            continue;
        }

        ListEntry row;
        row.name = Utf8FileName(entry.path());
        row.part = part;
        WIN32_FILE_ATTRIBUTE_DATA attributes{};
        if (GetFileAttributesExW(entry.path().wstring().c_str(), GetFileExInfoStandard, &attributes)) {
            const uint64_t ticks = (static_cast<uint64_t>(attributes.ftLastWriteTime.dwHighDateTime) << 32) |
                                   attributes.ftLastWriteTime.dwLowDateTime;
            row.mtime_ms = static_cast<int64_t>(ticks / 10000ull) - 11644473600000LL;
            row.size = (static_cast<uint64_t>(attributes.nFileSizeHigh) << 32) |
                       attributes.nFileSizeLow;
        }
        if (part) {
            const sr::RecoveryItem item = recovery.Probe(path);
            row.state = item.repairable ? "recoverable" : "broken";
            row.duration_ms = item.duration_ms;
            row.width = item.width;
            row.height = item.height;
        } else {
            const sr::RecoveryItem item = recovery.InspectHeader(path);
            row.state = "complete";
            row.duration_ms = item.duration_ms;
            row.width = item.width;
            row.height = item.height;
        }
        entries.push_back(row);
    }

    std::sort(entries.begin(), entries.end(), [](const ListEntry& a, const ListEntry& b) {
        if (a.mtime_ms != b.mtime_ms) {
            return a.mtime_ms > b.mtime_ms;  // 时间倒序
        }
        return a.name > b.name;
    });

    std::printf("list_dir=%s\n", dir.c_str());
    std::printf("count=%zu\n", entries.size());
    for (const ListEntry& row : entries) {
        // 字段用 \t 分隔、name 放最后：文件名里出现 ; 或 = 也不会破坏前面的字段。
        std::printf("entry=kind=%s\tstate=%s\tsize=%llu\tmtime_ms=%lld\tduration_ms=%lld\twidth=%u\theight=%u\tname=%s\n",
                    row.part ? "part" : "video", row.state,
                    static_cast<unsigned long long>(row.size), static_cast<long long>(row.mtime_ms),
                    static_cast<long long>(row.duration_ms), row.width, row.height, row.name.c_str());
    }
    std::fflush(stdout);
    return 0;
}

// M3-05：编码器列表。探测只在这里做（不进录制启动路径）。
// 输出的 `encoders=[...]` 是规范单行序列化：UI 直接复制这一行到自己的日志，
// 「日志与 UI 列表逐项一致」因此是构造上成立的，不靠两边各写一遍格式。
int RunListEncoders(const Options& options) {
    sr::encode::EncoderProbeOptions probe;
    if (options.fail_encoder) {
        const std::string& target = options.fail_encoder_target;
        const bool all = target.empty() || target == "all";
        probe.fail_hardware = all || target == "hardware";
        probe.fail_software = all || target == "software";
    }

    // 轮 47：--probe-repeat >1 时在同一进程里重复真激活探测，逐轮打状态与句柄数，
    // 供用例核验「探测失败可重试、状态不被缓存」。默认 1 次，输出格式不变。
    // 探测器自身不创建/不持有 D3D device 或 video processor；句柄数只是可核验的观测值，
    // 产品路径里每次探测都在一次性子进程中完成（UI 设置页 spawn sr-recorder）。
    std::vector<sr::encode::EncoderInfo> encoders;
    const bool report_repeat = options.probe_repeat > 1;
    for (uint32_t iteration = 0; iteration < options.probe_repeat; ++iteration) {
        encoders = sr::encode::ListEncoders(probe);
        if (report_repeat) {
            DWORD handles = 0;
            (void)GetProcessHandleCount(GetCurrentProcess(), &handles);
            const sr::encode::EncoderInfo* sw = sr::encode::FindEncoder(encoders, "mf-h264-sw");
            const sr::encode::EncoderInfo* hw = sr::encode::FindEncoder(encoders, "mf-h264-hw");
            std::printf("probe_iteration=%u handles=%lu available_mf_sw=%d available_mf_hw=%d\n",
                        iteration + 1, static_cast<unsigned long>(handles),
                        (sw != nullptr && sw->available) ? 1 : 0,
                        (hw != nullptr && hw->available) ? 1 : 0);
        }
    }

    const std::string canonical = sr::encode::FormatEncoderList(encoders);
    std::printf("encoder_count=%zu\n", encoders.size());
    std::printf("encoders=[%s]\n", canonical.c_str());
    for (const sr::encode::EncoderInfo& info : encoders) {
        std::printf("encoder=id=%s\tname=%s\thardware=%d\tavailable=%d\treason=%s\n",
                    info.id.c_str(), info.name.c_str(), info.hardware ? 1 : 0,
                    info.available ? 1 : 0, info.reason.c_str());
    }
    std::fflush(stdout);
    // 同一行也进日志：验收要能比对「日志 encoders=[...]」与「UI 列表」。
    sr::diag::Info("encoders=[" + canonical + "]");
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    if (!ParseArgs(argc, argv, options)) {
        PrintUsage();
        return static_cast<int>(options.parse_exit_code);
    }
    if (options.show_help) {
        PrintUsage();
        return 0;
    }
    if (options.show_version) {
        std::printf("sr-recorder %s (ipc protocol v%u, build %s, test_hooks=%d, core state table %zu transitions)\n",
                    kVersion,
                    static_cast<unsigned>(sr::ipc::kProtocolVersion),
                    kBuildTag,
                    kTestHooks ? 1 : 0,
                    sr::RecorderStateMachine::TableSize());
        return 0;
    }

    sr::diag::LogLevel log_level = sr::diag::LogLevel::Info;
    if (!sr::diag::ParseLogLevel(options.log_level, log_level)) {
        std::printf("invalid --log-level: %s (debug|info|warn)\n", options.log_level.c_str());
        return 1;
    }
    // 轮 30：tag 决定 stdout 前缀。录制会话用 `[record]`，骨架服务用 `[sr-recorder]`，
    // 两者都是既有脚本的抓行依据，不能改。
    // 注意：默认以 "wb" 打开日志文件（截断）。导出诊断包时 `--log-file` 同时是**输入**，
    // 必须用追加模式，否则源日志在读取前就被清空（轮 32 实测踩到：159KB 日志变成 0）。
    sr::diag::Init(options.record ? "record" : "sr-recorder", options.log_file, log_level, true,
                   !options.export_diag.empty());

    if (!options.export_diag.empty()) {
        sr::diag::DiagRequest request;
        request.output_root = Widen(options.export_diag);
        request.config_path = Widen(options.config_path);
        request.product = "sr-recorder";
        request.version = kVersion;
        request.protocol = std::to_string(static_cast<unsigned>(sr::ipc::kProtocolVersion));
        request.build = kBuildTag;
        request.test_hooks = kTestHooks;
        request.session = "mode=export-diag\nlog_level=";
        request.session += sr::diag::LogLevelName(log_level);
        request.session += "\nbuild=";
        request.session += kBuildTag;
        request.session += "\n";
        if (!options.log_file.empty()) {
            request.session += "log_file=" + options.log_file + "\n";
            // 轮 32：诊断包必须带上本进程自己的日志。此前只把路径写进 session，
            // log.txt 里没有 recorder 段——「最近日志」名不副实。
            request.recorder_log_path = Widen(options.log_file);
        }
        const sr::diag::DiagResult result = sr::diag::Export(request);
        std::printf("diag_pack=%s\n", result.ok ? "ok" : "fail");
        std::printf("diag_path=%ls\n", result.directory.c_str());
        std::printf("diag_files=%zu\n", result.files.size());
        std::printf("diag_bytes=%llu\n", static_cast<unsigned long long>(result.bytes));
        std::printf("diag_elapsed_ms=%llu\n", static_cast<unsigned long long>(result.elapsed_ms));
        if (!result.ok) {
            std::printf("error=%s\n", result.error.c_str());
        }
        std::fflush(stdout);
        sr::diag::Shutdown();
        return result.ok ? 0 : static_cast<int>(kExitDiagFailed);
    }

#if defined(SR_TEST_HOOKS)
    if (!options.diag_window_test.empty()) {
        // 用合成日志验证「只留最近 10 分钟」的时间窗逻辑（不依赖真实录制 1 小时）。
        std::ifstream input(options.diag_window_test, std::ios::binary);
        if (!input) {
            std::printf("diag_window=fail reason=open\n");
            return 1;
        }
        std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        const std::string trimmed = sr::diag::TruncateByAge(text, 10ull * 60ull * 1000ull);
        auto span = [](const std::string& body, uint64_t& first, uint64_t& last) {
            first = 0;
            last = 0;
            bool any = false;
            size_t pos = 0;
            while (pos < body.size()) {
                size_t end = body.find('\n', pos);
                if (end == std::string::npos) {
                    end = body.size();
                }
                if (end > pos + 2 && body[pos] == 't' && body[pos + 1] == '=') {
                    uint64_t value = 0;
                    size_t index = pos + 2;
                    for (; index < end && body[index] >= '0' && body[index] <= '9'; ++index) {
                        value = value * 10 + static_cast<uint64_t>(body[index] - '0');
                    }
                    if (index > pos + 2) {
                        if (!any) {
                            first = value;
                            any = true;
                        }
                        last = value;
                    }
                }
                if (end == body.size()) {
                    break;
                }
                pos = end + 1;
            }
            return any;
        };
        uint64_t first = 0;
        uint64_t last = 0;
        const bool has_stamp = span(trimmed, first, last);
        const uint64_t window = (has_stamp && last >= first) ? (last - first) : 0;
        std::printf("diag_window=ok\n");
        std::printf("bytes_in=%zu\n", text.size());
        std::printf("bytes_out=%zu\n", trimmed.size());
        std::printf("span_ms=%llu\n", static_cast<unsigned long long>(window));
        std::printf("dropped_lines=%zu\n",
                    static_cast<size_t>(std::count(text.begin(), text.end(), '\n')) -
                        static_cast<size_t>(std::count(trimmed.begin(), trimmed.end(), '\n')));
        std::fflush(stdout);
        return 0;
    }
#endif

    if (!options.fallback_commit.empty()) {
        // 诊断子命令：直接对指定 .part 走「探测 → 截断修复 → 提交到兜底目录」，
        // 用于验证提交路径本身（不依赖录制过程，因此可以精确构造不可修复输入）。
        sr::resilience::MkvRecovery recovery;
        const sr::RecoveryItem item = recovery.Probe(options.fallback_commit);
        std::string target;
        std::string detail;
        bool ok = false;
        if (!item.repairable) {
            detail = item.probe_reason;
        } else if (options.fallback_dir.empty()) {
            detail = "fallback dir is required";
        } else {
            std::error_code ec;
            std::filesystem::create_directories(options.fallback_dir, ec);
            const std::filesystem::path source(options.fallback_commit);
            std::string stem = source.stem().string();
            if (stem.empty()) {
                stem = "recovered";
            }
            const std::string desired =
                (std::filesystem::path(options.fallback_dir) / (stem + ".recovered.mkv")).string();
            target = sr::util::FindFreePath(desired);
            if (target.empty()) {
                detail = "no free name in fallback dir";
            } else {
                ok = recovery.Repair(item, target, detail);
            }
        }
        std::printf("fallback_commit=%s\n", ok ? "ok" : "fail");
        std::printf("fallback_path=%s\n", ok ? target.c_str() : "");
        std::printf("detail=%s\n", detail.c_str());
        std::printf("record_exit=%d\n", ok ? 0 : 15);
        std::fflush(stdout);
        return ok ? 0 : 15;
    }

    if (options.list_encoders) {
        return RunListEncoders(options);
    }

    if (!options.list_dir.empty()) {
        return RunList(options.list_dir, options.list_skip);
    }

    if (!options.scan_dir.empty() || !options.repair_file.empty()) {
        sr::resilience::MkvRecovery recovery;
        int failures = 0;
        if (!options.scan_dir.empty()) {
            const sr::RecoveryReport report = recovery.Scan(options.scan_dir);
            std::printf("scanned=%u\n", report.scanned);
            for (const sr::RecoveryItem& item : report.items) {
                std::printf("file=%s\n", item.segment_path.c_str());
                std::printf("repairable=%d\n", item.repairable ? 1 : 0);
                std::printf("reason=%s\n", item.probe_reason.c_str());
                std::printf("suggested_output=%s\n", item.suggested_output.c_str());
                if (!item.repairable) {
                    ++failures;
                }
            }
        }
        if (!options.repair_file.empty()) {
            const sr::RecoveryItem item = recovery.Probe(options.repair_file);
            std::printf("probe_repairable=%d\n", item.repairable ? 1 : 0);
            std::printf("probe_reason=%s\n", item.probe_reason.c_str());
            std::string repair_error;
            const bool ok = item.repairable &&
                            recovery.Repair(item, item.suggested_output, repair_error);
            std::printf("repair=%s\n", ok ? "ok" : "fail");
            std::printf("output=%s\n", item.suggested_output.c_str());
            if (!ok) {
                std::printf("error=%s\n", repair_error.c_str());
                ++failures;
            }
        }
        std::fflush(stdout);
        return failures == 0 ? 0 : 12;
    }

    if (options.record) {
        sr::recorder::RecordingSession::Options record_options;
        record_options.seconds = options.seconds;
        record_options.fps = options.fps;
        record_options.bitrate_kbps = options.bitrate_kbps;
        record_options.rate_mode = options.rate_mode;
        record_options.max_bitrate_kbps = options.max_bitrate_kbps;
        record_options.quality = options.quality;
        record_options.force_cpu = options.force_cpu;
        record_options.include_cursor = options.include_cursor;
        record_options.display_index = options.display;
        record_options.window_handle = options.window_handle;
        record_options.fit_mode = options.fit_mode;
        record_options.output_path = options.output;
        // --format mp4 且未显式给 --out：默认名跟格式走，避免把 MP4 内容写进 out.mkv。
        if (options.format == sr::mux::ContainerFormat::Mp4 &&
            !options.output_explicit && options.output == "out.mkv") {
            record_options.output_path = "out.mp4";
        }
        record_options.format = options.format;
        record_options.dump_elementary_stream = options.dump_es;
        record_options.disk_warn_mb = options.disk_warn_mb;
        record_options.disk_stop_mb = options.disk_stop_mb;
        record_options.countdown_seconds = options.countdown_seconds;
        record_options.pause_at_seconds = options.pause_at_seconds;
        record_options.pause_duration_seconds = options.pause_duration_seconds;
        record_options.backend = options.backend;
        record_options.pause_at_list = options.pause_at_list;
        record_options.pause_duration_list = options.pause_duration_list;
        record_options.cancel_at_seconds = options.cancel_at_seconds;
        record_options.fail_resume = options.fail_resume;
        // 无参 / all = 既有「全部失败」；定向值只让对应一类的 MFT 创建失败。
        record_options.fail_encoder =
            options.fail_encoder && (options.fail_encoder_target.empty() ||
                                     options.fail_encoder_target == "all");
        record_options.fail_hardware_encoder =
            options.fail_encoder && options.fail_encoder_target == "hardware";
        record_options.fail_software_encoder =
            options.fail_encoder && options.fail_encoder_target == "software";
    // M3-05：强制指定编码器（不可用 → 开录失败并给原因，走 exit 6）。
    record_options.encoder_id = options.encoder_id;
        record_options.fail_write_after = options.fail_write_after;
        record_options.fail_resume_backend = options.fail_resume_backend;
        record_options.fallback_dir = options.fallback_dir;
        record_options.control_pipe = options.control_pipe;
        record_options.control_token = options.control_token;
        record_options.control_wait_ms = options.control_wait_ms;
        record_options.pause_timeout_seconds = options.pause_timeout_seconds;
        record_options.region_enabled = options.region_enabled;
        record_options.region = options.region;
        record_options.inject_drop_every = options.inject_drop_every;

        sr::recorder::RecordingSession session;
        sr::recorder::RecordingSession::Stats stats;
        std::string error;
        const int code = session.Run(record_options, stats, error);

        std::printf("record_exit=%d\n", code);
        std::printf("include_cursor=%d\n", options.include_cursor ? 1 : 0);
        std::printf("encoder=%s\n", stats.encoder);
        std::printf("rate_control=%s\n", stats.rate_control);
        std::printf("rate_control_quality=%u\n", stats.rate_control_quality);
        std::printf("rate_control_max_kbps=%u\n", stats.rate_control_max_kbps);
        std::printf("cbr_filler_bytes=%llu\n", static_cast<unsigned long long>(stats.filler_bytes));
        std::printf("stream_changes=%u\n", stats.stream_changes);
        std::printf("converter=%s\n", stats.converter);
        std::printf("frames_encoded=%llu\n", static_cast<unsigned long long>(stats.frames_encoded));
        std::printf("frames_duplicated=%llu\n", static_cast<unsigned long long>(stats.frames_duplicated));
        std::printf("frames_captured=%llu\n", static_cast<unsigned long long>(stats.frames_captured));
        std::printf("frames_dropped=%llu\n", static_cast<unsigned long long>(stats.frames_dropped));
        std::printf("capture_fps=%.1f\n", stats.capture_fps);
        std::printf("encode_fps=%.1f\n", stats.encode_fps);
        std::printf("log_level=%s\n", sr::diag::LogLevelName(log_level));
        std::printf("log_file=%s\n", options.log_file.c_str());
        std::printf("blocks_muxed=%llu\n", static_cast<unsigned long long>(stats.blocks_muxed));
        std::printf("clusters=%u\n", stats.clusters);
        std::printf("bytes_written=%llu\n", static_cast<unsigned long long>(stats.bytes_written));
        std::printf("wall_seconds=%.2f\n", stats.duration_seconds);
        std::printf("disk_free_bytes=%llu\n", static_cast<unsigned long long>(stats.disk_free_bytes));
        std::printf("estimated_record_seconds=%.0f\n", stats.estimated_seconds);
        std::printf("disk_warn=%d\n", stats.disk_warn ? 1 : 0);
        std::printf("disk_stop=%d\n", stats.disk_stop ? 1 : 0);
        std::printf("output=%s\n", stats.output_path.c_str());
        std::printf("backend=%s\n", stats.backend.c_str());
        std::printf("backend_changed=%d\n", stats.backend_changed ? 1 : 0);
        // M1-08：源尺寸变化次数与最后一次尺寸（窗口缩放跟随的观测量）。
        std::printf("source_size_changes=%u\n", stats.source_size_changes);
        std::printf("source_size=%ux%u\n", stats.source_width, stats.source_height);
        std::printf("fit_mode=%s\n", options.fit_mode.c_str());
        std::printf("paused_ms=%llu\n", static_cast<unsigned long long>(stats.paused_ms));
        std::printf("countdown_ms=%llu\n", static_cast<unsigned long long>(stats.countdown_ms));
        std::printf("resume_fills=%u\n", stats.resume_fills);
        std::printf("part_bytes_at_pause=%llu\n", static_cast<unsigned long long>(stats.part_bytes_at_pause));
        std::printf("part_bytes_at_resume=%llu\n", static_cast<unsigned long long>(stats.part_bytes_at_resume));
        std::printf("cancelled=%d\n", stats.cancelled ? 1 : 0);
        std::printf("pause_count=%u\n", stats.pause_count);
        std::printf("pause_after_stop_rejected=%d\n", stats.pause_after_stop_rejected ? 1 : 0);
        std::printf("state_sequence=%s\n", stats.state_sequence.c_str());
        std::printf("video_last_pts_ms=%lld\n", static_cast<long long>(stats.video_last_pts_ms));
        std::printf("audio_last_pts_ms=%lld\n", static_cast<long long>(stats.audio_last_pts_ms));
        std::printf("audio_tail_fill_frames=%llu\n",
                    static_cast<unsigned long long>(stats.audio_tail_fill_frames));
        std::printf("audio_tail_silence_frames=%llu\n",
                    static_cast<unsigned long long>(stats.audio_tail_silence_frames));
        std::printf("media_elapsed_ms=%llu\n", static_cast<unsigned long long>(stats.media_elapsed_ms));
        std::printf("write_errors=%llu\n", static_cast<unsigned long long>(stats.write_errors));
        std::printf("fallback_path=%s\n", stats.fallback_path.c_str());
        std::printf("fallback_source_pts_ms=%lld\n", static_cast<long long>(stats.fallback_source_pts_ms));
        std::printf("control=%s\n", stats.control.c_str());
        std::printf("commands_applied=%u\n", stats.commands_applied);
        std::printf("commands_rejected=%u\n", stats.commands_rejected);
        std::printf("pause_auto_resumed=%d\n", stats.pause_auto_resumed ? 1 : 0);
        std::printf("pause_timeout_hit=%d\n", stats.pause_timeout_hit ? 1 : 0);
        std::printf("control_reconnects=%u\n", stats.control_reconnects);
        // LT-01：内存与句柄取本进程真实峰值（外部轮询会漏掉峰值，也不反映段间回收）。
        PROCESS_MEMORY_COUNTERS_EX counters{};
        if (GetProcessMemoryInfo(GetCurrentProcess(),
                                 reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                                 sizeof(counters)) != FALSE) {
            std::printf("mem_peak_mb=%.1f\n",
                        static_cast<double>(counters.PeakWorkingSetSize) / 1048576.0);
            std::printf("mem_ws_mb=%.1f\n",
                        static_cast<double>(counters.WorkingSetSize) / 1048576.0);
        }
        DWORD handle_count = 0;
        if (GetProcessHandleCount(GetCurrentProcess(), &handle_count) != FALSE) {
            std::printf("handles=%lu\n", static_cast<unsigned long>(handle_count));
        }
        if (code != 0) {
            std::printf("error=%s\n", error.c_str());
        }
        std::fflush(stdout);
        sr::diag::Shutdown();
        return code;
    }

    uint32_t existing_pid = 0;
    HANDLE lock = AcquireSingleInstance(existing_pid);
    if (lock == INVALID_HANDLE_VALUE) {
        if (existing_pid != 0) {
            std::printf("sr-recorder already running, pid=%u\n", existing_pid);
        } else {
            std::printf("sr-recorder already running (pid unavailable)\n");
        }
        return static_cast<int>(kExitAlreadyRunning);
    }

    sr::win::QpcClock clock;
    const std::wstring pipe_name = sr::ipc::MakePipeName(GetCurrentProcessId());

    std::printf("sr-recorder %s\n", kVersion);
    std::printf("pid=%lu\n", static_cast<unsigned long>(GetCurrentProcessId()));
    std::printf("pipe=%ls\n", pipe_name.c_str());
    std::fflush(stdout);

    sr::recorder::SessionServer server(pipe_name, options.token, clock);
    const int code = server.Run();

    CloseHandle(lock);
    DeleteFileW(LockFilePath().c_str());
    return code;
}
