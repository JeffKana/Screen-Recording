#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <share.h>
#include <string>
#include <vector>

#include "app/AppConfig.h"
#include "app/Elevation.h"
#include "app/FileOps.h"
#include "app/RecorderController.h"
#include "core/state/RecorderState.h"
#include "core/util/DiskEstimate.h"
#include "core/util/PathProbe.h"
#include "diag/DiagPack.h"
#include "diag/Logger.h"
#include "ipc/Protocol.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
// per-monitor v2 / GetDpiForWindow 需要 Win10 目标；MinGW 的默认值较旧。
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x0A000006
#endif
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <mfapi.h>
#include <mftransform.h>
#include <mfidl.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>

#include <algorithm>
#include <cwctype>

namespace {

constexpr const wchar_t* kWindowClass = L"SrUiMainWindow";
constexpr const wchar_t* kSettingsClass = L"SrUiSettingsWindow";
constexpr const wchar_t* kRegionOverlayClass = L"SrUiRegionOverlay";
constexpr const wchar_t* kRegionEditorClass = L"SrUiRegionWindow";
constexpr const wchar_t* kListClass = L"SrUiListWindow";
constexpr const wchar_t* kAppTitle = L"Screen Recorder";
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT_PTR kPollTimer = 1;
constexpr UINT_PTR kPreviewTimer = 2;
constexpr UINT_PTR kExitTimer = 3;
constexpr UINT kHotkeyStart = 1;
constexpr UINT kHotkeyPause = 2;
constexpr UINT kHotkeyStop = 3;

constexpr int kStatusId = 200;
constexpr int kStartButton = 201;
constexpr int kPauseButton = 202;
constexpr int kStopButton = 203;
constexpr int kHideButton = 204;
constexpr int kSavePathButton = 205;
constexpr int kSettingsButton = 206;
constexpr int kRegionButton = 207;
constexpr int kListButton = 208;
// M1-27：以管理员身份重启入口（主窗口按钮）。
constexpr int kElevateButton = 209;
// M1-27：UAC 提权窗口采集限制的常驻提示文本控件。
constexpr int kUacWarnId = 210;

constexpr int kRegionTextId = 600;
constexpr int kRegionFull = 601;
constexpr int kRegion169 = 602;
constexpr int kRegion43 = 603;
constexpr int kRegion11 = 604;
constexpr int kRegionPick = 605;
constexpr int kRegionOk = 606;
constexpr int kRegionCancel = 607;

constexpr int kTrayStart = 300;
constexpr int kTrayPause = 301;
constexpr int kTrayStop = 302;
constexpr int kTrayOpenFolder = 303;
constexpr int kTraySettings = 304;
constexpr int kTrayShow = 305;
constexpr int kTrayExit = 306;
constexpr int kTrayToolbar = 307;
constexpr int kTrayDiag = 308;
constexpr int kTrayList = 309;
// M1-27：托盘菜单里的「以管理员身份重启」。
constexpr int kTrayElevate = 310;
constexpr int kListView = 700;
constexpr UINT_PTR kListTimer = 4;

constexpr int kSettingsOk = 400;
constexpr int kSettingsCancel = 401;
constexpr int kSettingsBrowse = 402;
constexpr int kSettingsTemplate = 103;
// M3-03 / M3-04：帧率与码率模式
constexpr int kSettingsFpsCombo = 104;
constexpr int kSettingsFpsCustom = 105;
constexpr int kSettingsRateCombo = 106;
// M3-05：可用编码器列表（只读）
constexpr int kSettingsEncoderList = 107;
// M3-10：画质预设档位下拉（极致 / 高清 / 标准 / 省空间 / 自定义）。
constexpr int kSettingsPresetCombo = 108;
// M3-10#2：把当前预设的联动值显示在界面上（只读文本，验收可读取控件文本）。
constexpr int kSettingsPresetValues = 109;
// M3-03 的五个预设档；第 6 档「自定义」由编辑框给出 1~144。
constexpr uint32_t kFpsPresets[5] = {15, 24, 25, 30, 60};

constexpr const wchar_t* kToolbarClass = L"SrUiToolbar";
// M1-27：UAC 提权窗口采集限制的提示文案（常驻标签 + 状态栏）。
// 文案只说「可能」与「怎么办」：实测发现 WGC 在部分环境能采到高权限窗口内容，
// 但兜底 PrintWindow 路径取不到帧——所以不能断言「一定黑屏」。
constexpr const wchar_t* kUacWarnLabel = L"⚠ 当前非管理员运行：UAC 提权窗口等高权限窗口可能黑屏或无画面";
constexpr const wchar_t* kUacWarnFull =
    L"⚠ 当前非管理员运行：录制 UAC 提权窗口等高权限窗口时可能黑屏或无画面。"
    L"请点「以管理员身份重启」后再录。";
constexpr int kTbTimer = 500;
constexpr int kTbRemaining = 501;
constexpr int kTbPause = 502;
constexpr int kTbStop = 503;
constexpr int kTbHide = 504;
constexpr int kTbMetrics = 505;

bool g_log_enabled = false;
int g_argc = 0;
wchar_t** g_argv = nullptr;

constexpr const wchar_t* kUiVersion = L"0.1.0";

// F3：测试注入开关（--preflight-fail / --set-*）由 SR_TEST_HOOKS 隔离。
// 默认构建（含自动化验收）打开；G6 打包用 -DSR_TEST_HOOKS=OFF。
#if defined(SR_TEST_HOOKS)
constexpr bool kTestHooks = true;
constexpr const wchar_t* kBuildTag = L"dev+test-hooks";
#else
constexpr bool kTestHooks = false;
constexpr const wchar_t* kBuildTag = L"release";
#endif

std::string Narrow(const std::wstring& text) {
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

void LogImpl(sr::diag::LogLevel level, const wchar_t* format, va_list args) {
    if (!g_log_enabled) {
        return;
    }
    wchar_t message[2048] = {};
    _vsnwprintf_s(message, _TRUNCATE, format, args);
    // 二进制写 + 显式 UTF-8：避免 ccs 文本模式与共享读的兼容问题。
    // 行格式 `t=<ms> <message>` 由 sr::diag 统一负责，验收脚本的解析不变。
    char utf8[9000] = {};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, message, -1, utf8,
                                          static_cast<int>(sizeof(utf8)), nullptr, nullptr);
    if (bytes > 1) {
        sr::diag::Log(level, std::string(utf8, static_cast<size_t>(bytes - 1)));
    }
}

void Log(const wchar_t* format, ...) {
    if (!g_log_enabled) {
        return;
    }
    va_list args;
    va_start(args, format);
    LogImpl(sr::diag::LogLevel::Info, format, args);
    va_end(args);
}

// 轮 30：debug 行只在 --log-level debug 时落盘，用于「以文查证」的现场排查。
void LogDebug(const wchar_t* format, ...) {
    if (!g_log_enabled) {
        return;
    }
    va_list args;
    va_start(args, format);
    LogImpl(sr::diag::LogLevel::Debug, format, args);
    va_end(args);
}

void LogWarn(const wchar_t* format, ...) {
    if (!g_log_enabled) {
        return;
    }
    va_list args;
    va_start(args, format);
    LogImpl(sr::diag::LogLevel::Warn, format, args);
    va_end(args);
}

void CloseLog() {
    sr::diag::Shutdown();
    g_log_enabled = false;
}

HMENU ControlId(int id) {
    return reinterpret_cast<HMENU>(static_cast<INT_PTR>(id));
}

std::wstring Widen(const char* text) {
    std::wstring out;
    if (text == nullptr) {
        return out;
    }
    for (; *text != '\0'; ++text) {
        out.push_back(static_cast<wchar_t>(*text));
    }
    return out;
}

bool EnsureDirectory(const std::wstring& path, std::wstring& error) {
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    if (ec) {
        error = L"无法创建目录: " + path;
        return false;
    }
    return true;
}

bool EnsureParentDirectory(const std::wstring& path, std::wstring& error) {
    const std::filesystem::path parent = std::filesystem::path(path).parent_path();
    if (parent.empty()) {
        return true;
    }
    return EnsureDirectory(parent.wstring(), error);
}

bool ReadConfigWriteTime(const std::wstring& path, FILETIME& out) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        return false;
    }
    out = data.ftLastWriteTime;
    return true;
}

std::wstring FormatElapsed(uint64_t elapsed_ms) {
    const uint64_t total_seconds = elapsed_ms / 1000;
    const uint64_t hours = total_seconds / 3600;
    const uint64_t minutes = (total_seconds / 60) % 60;
    const uint64_t seconds = total_seconds % 60;
    wchar_t buffer[32] = {};
    if (hours > 0) {
        std::swprintf(buffer, std::size(buffer), L"%02llu:%02llu:%02llu",
                      static_cast<unsigned long long>(hours),
                      static_cast<unsigned long long>(minutes),
                      static_cast<unsigned long long>(seconds));
    } else {
        std::swprintf(buffer, std::size(buffer), L"%02llu:%02llu",
                      static_cast<unsigned long long>(minutes),
                      static_cast<unsigned long long>(seconds));
    }
    return buffer;
}

std::wstring TrimText(std::wstring text) {
    size_t begin = 0;
    while (begin < text.size() && std::iswspace(text[begin])) {
        ++begin;
    }
    size_t end = text.size();
    while (end > begin && std::iswspace(text[end - 1])) {
        --end;
    }
    return text.substr(begin, end - begin);
}

std::wstring ReplaceAll(std::wstring text, const std::wstring& from, const std::wstring& to) {
    if (from.empty()) {
        return text;
    }
    size_t position = 0;
    while ((position = text.find(from, position)) != std::wstring::npos) {
        text.replace(position, from.size(), to);
        position += to.size();
    }
    return text;
}

bool EndsWithNoCase(const std::wstring& text, const std::wstring& suffix) {
    if (text.size() < suffix.size()) {
        return false;
    }
    return _wcsicmp(text.c_str() + (text.size() - suffix.size()), suffix.c_str()) == 0;
}

std::wstring SanitizeName(const std::wstring& text) {
    std::wstring out;
    out.reserve(text.size());
    for (wchar_t c : text) {
        if (c < 0x20 || c == L'<' || c == L'>' || c == L':' || c == L'"' ||
            c == L'/' || c == L'\\' || c == L'|' || c == L'?' || c == L'*') {
            out.push_back(L'_');
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::wstring FormatIndex(int index) {
    wchar_t buffer[16] = {};
    std::swprintf(buffer, std::size(buffer), L"%03d", index);
    return buffer;
}

std::wstring ExpandTemplate(const std::wstring& filename_template, const SYSTEMTIME& now) {
    wchar_t date[16] = {};
    wchar_t time[16] = {};
    std::swprintf(date, std::size(date), L"%04d%02d%02d", now.wYear, now.wMonth, now.wDay);
    std::swprintf(time, std::size(time), L"%02d%02d%02d", now.wHour, now.wMinute, now.wSecond);
    std::wstring expanded = ReplaceAll(filename_template, L"{date}", date);
    expanded = ReplaceAll(expanded, L"{time}", time);
    expanded = ReplaceAll(expanded, L"{source}", L"screen");  // UI-2 仅全屏
    expanded = ReplaceAll(expanded, L"{app}", L"sr-ui");
    return expanded;
}

// M5-02：模板在 UI 侧展开，Recorder 只收最终路径（不改 IPC 契约）。
// 展开后仍按「不覆盖」原则取第一个空位；{index} 缺失时自动补 _001。
bool BuildOutputPath(const std::wstring& out_override,
                     const sr::app::AppConfig& config,
                     std::wstring& output,
                     std::wstring& error) {
    if (!out_override.empty()) {
        output = out_override;
        return true;
    }
    if (config.output_dir.empty()) {
        error = L"保存路径为空";
        return false;
    }
    if (config.filename_template.empty()) {
        error = L"命名模板为空";
        return false;
    }

    SYSTEMTIME now{};
    GetLocalTime(&now);
    std::wstring expanded = SanitizeName(ExpandTemplate(config.filename_template, now));
    const bool has_index = expanded.find(L"{index}") != std::wstring::npos;
    std::wstring stem = expanded;
    if (EndsWithNoCase(stem, L".mkv") || EndsWithNoCase(stem, L".mp4")) {
        stem.resize(stem.size() - 4);
    }

    const std::filesystem::path directory(config.output_dir);
    std::error_code ec;
    for (int index = 1; index <= 999; ++index) {
        std::wstring name = has_index ? ReplaceAll(stem, L"{index}", FormatIndex(index))
                                      : stem + L"_" + FormatIndex(index);
        const std::filesystem::path candidate = directory / (name + L".mkv");
        ec.clear();
        if (!std::filesystem::exists(candidate, ec)) {
            output = candidate.wstring();
            error.clear();
            return true;
        }
    }
    error = L"同名文件已达 999 个上限: " + stem;
    return false;
}

bool ProbeFreeBytes(const std::wstring& path, uint64_t& free_bytes) {
    std::filesystem::path probe(path);
    std::error_code ec;
    while (!probe.empty() && !std::filesystem::is_directory(probe, ec)) {
        probe = probe.parent_path();
        ec.clear();
    }
    if (probe.empty()) {
        return false;
    }
    ULARGE_INTEGER available{};
    ULARGE_INTEGER total{};
    ULARGE_INTEGER total_free{};
    if (!GetDiskFreeSpaceExW(probe.wstring().c_str(), &available, &total, &total_free)) {
        return false;
    }
    free_bytes = available.QuadPart;
    return true;
}

const wchar_t* HotkeyName(int index) {
    switch (index) {
        case 0: return L"start";
        case 1: return L"pause";
        default: return L"stop";
    }
}

const sr::app::HotkeyCombo& HotkeyAt(const sr::app::AppConfig& config, int index) {
    switch (index) {
        case 0: return config.start;
        case 1: return config.pause;
        default: return config.stop;
    }
}

void SetHotkeyAt(sr::app::AppConfig& config, int index, const sr::app::HotkeyCombo& combo) {
    switch (index) {
        case 0: config.start = combo; break;
        case 1: config.pause = combo; break;
        default: config.stop = combo; break;
    }
}

struct AppState {
    HINSTANCE instance = nullptr;
    HWND hwnd = nullptr;
    sr::app::AppConfig config;
    sr::app::RecorderController recorder;
    std::wstring config_path;
    std::wstring log_path;
    std::wstring out_override;
    std::wstring recorder_override;
    std::wstring last_output;
    std::wstring status_extra;
    FILETIME config_write{};
    bool have_config_write = false;
    ULONGLONG next_config_check = 0;
    bool hidden = false;
    bool exit_on_stop = false;
    bool preview_menu = false;
    bool preview_done = false;
    DWORD exit_timeout_ms = 0;
    bool session_active = false;
    bool settings_open = false;
    bool tray_added = false;
    bool registered[3] = {false, false, false};
    ULONGLONG spawn_t = 0;
    bool indicator_logged = false;
    int exit_code = 0;
    HICON icon_idle = nullptr;
    HICON icon_active = nullptr;
    NOTIFYICONDATAW nid{};
    // M7-13：录制中状态提示。边框窗口为可选（--record-border off 关闭），
    // indicator_state: 0=隐藏 / 1=录制中（红实线） / 2=暂停（黄虚线）。
    HWND border_hwnd = nullptr;
    bool show_record_border = true;
    // -1 = 尚未结算过；保证首次（含「默认关闭」）也会输出一条 record_indicator 日志。
    int indicator_state = -1;
    // 验收注入：0=真实状态 / 1=录制中 / 2=暂停。走同一条 UI 渲染分支，
    // 与 --simulate-backpressure 同族（M7-13 可见性断言的可重跑入口）。
    int simulate_indicator_state = 0;

    // M2-09：剩余可录时长（UI 本地探针，公式复用 core/util/DiskEstimate）。
    uint64_t remaining_seconds = 0;
    uint64_t disk_free_bytes = 0;
    ULONGLONG next_disk_check = 0;
    // M2-14：开录前自检。encoder 结果缓存，后续启动不再枚举 MFT。
    std::wstring preflight_fail;
    bool encoder_checked = false;
    bool encoder_ok = false;
    ULONGLONG last_preflight_ms = 0;
    // M1-27：本进程是否已提权。非提权时给提示 + 「以管理员身份重启」入口。
    bool elevated = false;
    bool elevate_entry_shown = false;
    // M8-03：磁盘水位提示（非阻塞提示线 / 强提示线，单位 MB）。
    bool disk_warned = false;
    bool disk_critical = false;
    // M6-11#2 验收注入：模拟队列积压产生的丢帧数，走与真实丢帧相同的可见标识分支。
    uint64_t backpressure_inject = 0;
    // M1-04 / M1-05：区域框选与微调。
    HWND region_overlay = nullptr;
    HWND region_editor = nullptr;
    sr::app::CaptureRegion region_edit;
    bool region_selecting = false;
    POINT region_start{};
    RECT region_current{};
    UINT region_dpi = 96;

    // M2-08：录制中悬浮工具条。
    HWND toolbar = nullptr;
    HWND toolbar_timer = nullptr;
    HWND toolbar_remaining = nullptr;
    HWND toolbar_metrics = nullptr;
    HWND toolbar_pause = nullptr;
    HWND toolbar_stop = nullptr;
    bool toolbar_user_hidden = false;

    // M5-06：录制文件列表。
    HWND list_window = nullptr;
    ULONGLONG list_next_reload = 0;
    int list_rows = 0;
    bool show_list_on_start = false;
    // M3-05：测试钩子——启动即打开设置页（验收脚本不必模拟点击托盘菜单）。
    bool show_settings_on_start = false;
    // M5-07：文件操作。测试钩子与菜单共用同一条实现，避免「验收路径 ≠ 产品路径」。
    std::wstring list_op;
    std::wstring list_from;
    std::wstring list_to;
    bool list_confirm_yes = false;
    bool list_op_done = false;
    std::wstring pending_rename_old;
    std::wstring pending_rename_new;
};

AppState* g_app = nullptr;

// M5-06：一行列表数据（来自 sr-recorder --list，state 为 recording 时由 UI 合成）。
struct ListRow {
    std::wstring name;
    std::wstring state = L"complete";
    uint64_t size = 0;
    int64_t mtime_ms = 0;
    int64_t duration_ms = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

struct SettingsContext {
    AppState* app = nullptr;
    HWND hwnd = nullptr;
    HWND edits[3] = {nullptr, nullptr, nullptr};
    HWND template_edit = nullptr;
    HWND fps_combo = nullptr;
    HWND fps_edit = nullptr;
    HWND rate_combo = nullptr;
    // M3-05：实际可用的编码器列表（只读）。
    HWND encoder_list = nullptr;
    // M3-10：画质预设档位下拉 + 当前联动值文本。
    HWND preset_combo = nullptr;
    HWND preset_values = nullptr;
};

void UpdateUi(AppState& app);
void ApplyElevationUi(AppState& app);
void ShowTrayMenu(AppState& app);
void ShowMainWindow(AppState& app);
void StartRecording(AppState& app);
void PauseResume(AppState& app);
void StopRecording(AppState& app);
void ShowSettings(AppState& app);
void RefreshDiskRemaining(AppState& app);
void UpdateToolbar(AppState& app);
void ShowToolbar(AppState& app, bool show);
void ToggleToolbar(AppState& app);
void BrowseOutputDir(AppState& app);
void ExportDiagnosticPack(AppState& app);
void ShowRecordingList(AppState& app);
void RefreshRecordingList(AppState& app);
void UpdateRecordingListLive(AppState& app);
// M3-05：向 recorder 索取编码器列表（探测只在设置页首次打开时做）。
bool RunRecorderEncoderList(AppState& app,
                             std::wstring& canonical,
                             std::vector<std::wstring>& rows,
                             std::wstring& error);

bool RegisterHotkeys(AppState& app, bool report_errors) {
    for (int i = 0; i < 3; ++i) {
        if (app.registered[i]) {
            UnregisterHotKey(app.hwnd, i == 0 ? kHotkeyStart : (i == 1 ? kHotkeyPause : kHotkeyStop));
            app.registered[i] = false;
        }
    }
    app.status_extra.clear();
    bool all_ok = true;
    for (int i = 0; i < 3; ++i) {
        const UINT id = i == 0 ? kHotkeyStart : (i == 1 ? kHotkeyPause : kHotkeyStop);
        const sr::app::HotkeyCombo& combo = HotkeyAt(app.config, i);
        const std::wstring text = sr::app::FormatHotkey(combo);
        if (RegisterHotKey(app.hwnd, id, combo.modifiers | MOD_NOREPEAT, combo.vk)) {
            app.registered[i] = true;
            Log(L"hotkey_register id=%ls combo=%ls ok=1", HotkeyName(i), text.c_str());
        } else {
            const DWORD error = GetLastError();
            Log(L"hotkey_register_failed id=%ls combo=%ls error=%lu",
                HotkeyName(i), text.c_str(), error);
            if (report_errors) {
                if (!app.status_extra.empty()) {
                    app.status_extra += L"；";
                }
                app.status_extra += text + L" 注册失败（已被占用）";
            }
            all_ok = false;
        }
    }
    return all_ok;
}

void UnregisterHotkeys(AppState& app) {
    for (int i = 0; i < 3; ++i) {
        if (app.registered[i]) {
            UnregisterHotKey(app.hwnd, i == 0 ? kHotkeyStart : (i == 1 ? kHotkeyPause : kHotkeyStop));
            app.registered[i] = false;
        }
    }
}

void UpdateTray(AppState& app, const std::wstring& tooltip, HICON icon) {
    app.nid.cbSize = sizeof(app.nid);
    app.nid.hWnd = app.hwnd;
    app.nid.uID = 1;
    app.nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    app.nid.uCallbackMessage = kTrayMessage;
    app.nid.hIcon = icon != nullptr ? icon : app.icon_idle;
    wcsncpy_s(app.nid.szTip, tooltip.c_str(), _TRUNCATE);
    const DWORD flags = app.tray_added ? NIM_MODIFY : NIM_ADD;
    if (Shell_NotifyIconW(flags, &app.nid)) {
        app.tray_added = true;
    } else {
        Log(L"tray_notify_failed flags=%lu error=%lu", flags, GetLastError());
    }
}

void RemoveTray(AppState& app) {
    if (app.tray_added) {
        Shell_NotifyIconW(NIM_DELETE, &app.nid);
        app.tray_added = false;
    }
}

// ---------------- M7-13：录制中托盘红点 + 可选屏幕边框 ----------------

static const wchar_t* const kBorderWindowClass = L"SrRecordBorderWindow";

// 自绘托盘图标：蓝色圆底 + 右上角红点。用 32bpp DIB + CreateIconIndirect，
// 不依赖资源文件，也不引三方依赖；发布构建同样可用。
HICON MakeRedDotIcon() {
    constexpr int kSize = 32;
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = kSize;
    bmi.bmiHeader.biHeight = kSize;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (color == nullptr || bits == nullptr) {
        if (color) { DeleteObject(color); }
        return nullptr;
    }
    auto* pixels = static_cast<uint32_t*>(bits);
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const int dx = x - 15;
            const int dy = y - 15;
            const uint32_t idx = static_cast<uint32_t>(y * kSize + x);
            if (dx * dx + dy * dy <= 13 * 13) {
                // BGRA：蓝色圆底，不透明。
                pixels[idx] = 0xFFC08020u;
            } else {
                pixels[idx] = 0x00000000u;  // alpha=0 透明
            }
        }
    }
    // 右上角红点（7px 半径），覆盖在底图上，保证「带红点」可见。
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const int dx = x - 24;
            const int dy = y - 7;
            if (dx * dx + dy * dy <= 7 * 7) {
                pixels[static_cast<uint32_t>(y * kSize + x)] = 0xFF0000FFu;  // BGRA 红
            }
        }
    }
    // 真实像素自检：红点中心与蓝色底图各取一点，日志给出 BGRA 值，
    // 供验收用例核验「图标确实带红点」，而不是只写一句文案。
    const uint32_t dot_pixel = pixels[7u * kSize + 24u];
    const uint32_t base_pixel = pixels[15u * kSize + 10u];
    Log(L"tray_icon red_dot_pixel=0x%08X base_pixel=0x%08X", dot_pixel, base_pixel);
    std::vector<uint8_t> mask_bytes(static_cast<size_t>(kSize) * kSize / 8, 0);
    HBITMAP mask = CreateBitmap(kSize, kSize, 1, 1, mask_bytes.data());
    ICONINFO info{};
    info.fIcon = TRUE;
    info.hbmColor = color;
    info.hbmMask = mask;
    const HICON icon = CreateIconIndirect(&info);
    DeleteObject(mask);
    DeleteObject(color);
    return icon;
}

LRESULT CALLBACK BorderWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc{};
        GetClientRect(hwnd, &rc);
        // LWA_COLORKEY 把纯黑变透明；只留下边框本身可见。
        HBRUSH bg = CreateSolidBrush(RGB(0, 0, 0));
        FillRect(dc, &rc, bg);
        DeleteObject(bg);
        const LONG_PTR state = GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        const bool recording = (state == 1);
        const COLORREF tint = recording ? RGB(255, 0, 0) : RGB(255, 200, 0);
        // 暂停用细虚线 + 黄色，与录制中的粗红实线可区分（M7-13#2）。
        const int width = recording ? 6 : 2;
        const int style = recording ? PS_SOLID : PS_DASH;
        HPEN pen = CreatePen(style, width, tint);
        HGDIOBJ old_pen = SelectObject(dc, pen);
        HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, 3, 3, rc.right - 3, rc.bottom - 3);
        SelectObject(dc, old_pen);
        SelectObject(dc, old_brush);
        DeleteObject(pen);
        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

HWND CreateBorderWindow(HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = BorderWndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kBorderWindowClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (RegisterClassExW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return nullptr;
    }
    HWND hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        kBorderWindowClass, L"", WS_POPUP,
        0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
        nullptr, nullptr, instance, nullptr);
    if (hwnd != nullptr) {
        SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), 0, LWA_COLORKEY);
    }
    return hwnd;
}

void UpdateRecordIndicator(AppState& app, bool recording, bool paused) {
    const int wanted = (!app.show_record_border || (!recording && !paused))
                           ? 0 : (paused ? 2 : 1);
    if (wanted == app.indicator_state) {
        return;
    }
    app.indicator_state = wanted;
    if (app.border_hwnd == nullptr) {
        app.border_hwnd = CreateBorderWindow(app.instance);
    }
    if (app.border_hwnd == nullptr) {
        return;
    }
    if (wanted == 0) {
        ShowWindow(app.border_hwnd, SW_HIDE);
        Log(L"record_indicator state=hidden border=0");
        return;
    }
    SetWindowLongPtrW(app.border_hwnd, GWLP_USERDATA, wanted);
    InvalidateRect(app.border_hwnd, nullptr, TRUE);
    ShowWindow(app.border_hwnd, SW_SHOWNA);
    Log(L"record_indicator state=%s border=1 red_dot=1",
        wanted == 1 ? L"recording" : L"paused");
}

void RefreshDiskRemaining(AppState& app) {
    uint64_t free_bytes = 0;
    if (!ProbeFreeBytes(app.config.output_dir, free_bytes)) {
        app.disk_free_bytes = 0;
        app.remaining_seconds = 0;
        Log(L"disk_remaining unavailable path=%ls", app.config.output_dir.c_str());
        return;
    }
    app.disk_free_bytes = free_bytes;
    const uint64_t stop_bytes =
        static_cast<uint64_t>(app.config.disk_stop_mb) * 1024ull * 1024ull;
    const uint32_t total_kbps = app.config.video_bitrate_kbps + sr::util::kAudioBitrateKbps;
    app.remaining_seconds = static_cast<uint64_t>(
        sr::util::EstimateRecordSeconds(free_bytes, stop_bytes, total_kbps));
    Log(L"disk_remaining seconds=%llu free_bytes=%llu stop_bytes=%llu bitrate_kbps=%u path=%ls",
        static_cast<unsigned long long>(app.remaining_seconds),
        static_cast<unsigned long long>(free_bytes),
        static_cast<unsigned long long>(stop_bytes),
        total_kbps, app.config.output_dir.c_str());
    // M8-03：两级提示。非阻塞提示线（默认 2GB）只提示不停录；强提示线（默认 500MB）
    // 升级为强提示。状态迁移只在跨越阈值时记一行，避免每秒刷屏。
    const uint64_t warn_bytes =
        static_cast<uint64_t>(app.config.disk_warn_mb) * 1024ull * 1024ull;
    const uint64_t critical_bytes =
        static_cast<uint64_t>(app.config.disk_critical_mb) * 1024ull * 1024ull;
    const bool critical = critical_bytes > 0 && free_bytes <= critical_bytes;
    const bool warned = warn_bytes > 0 && free_bytes <= warn_bytes;
    if (critical != app.disk_critical) {
        app.disk_critical = critical;
        LogWarn(L"disk_critical=%d free_mb=%llu threshold_mb=%u",
                critical ? 1 : 0,
                static_cast<unsigned long long>(free_bytes / (1024ull * 1024ull)),
                app.config.disk_critical_mb);
    }
    if (warned != app.disk_warned) {
        app.disk_warned = warned;
        Log(L"disk_warn=%d free_mb=%llu threshold_mb=%u",
            warned ? 1 : 0,
            static_cast<unsigned long long>(free_bytes / (1024ull * 1024ull)),
            app.config.disk_warn_mb);
    }
}

// M6-03：三项指标只做定点数还原与排版，数值全部来自 recorder 快照。
std::wstring FormatMetrics(const sr::ipc::StateSnapshotPayload& snapshot) {
    if (!snapshot.has_metrics) {
        return {};
    }
    wchar_t buffer[128] = {};
    std::swprintf(buffer, std::size(buffer), L"采集 %.1f / 编码 %.1f fps · 丢帧 %llu",
                  static_cast<double>(snapshot.metrics.capture_fps_x100) / 100.0,
                  static_cast<double>(snapshot.metrics.encode_fps_x100) / 100.0,
                  static_cast<unsigned long long>(snapshot.metrics.frames_dropped));
    return buffer;
}

void UpdateUi(AppState& app) {
    if (app.hwnd == nullptr) {
        return;
    }
    const bool active = app.recorder.Active();
    const bool has_snapshot = app.recorder.HasSnapshot();
    const sr::RecorderState state = has_snapshot
                                        ? static_cast<sr::RecorderState>(app.recorder.Snapshot().state)
                                        : sr::RecorderState::Idle;
    bool recording = active && has_snapshot && state == sr::RecorderState::Recording;
    bool paused = active && has_snapshot && state == sr::RecorderState::Paused;
    // M7-13 验收注入：让状态提示走同一条渲染分支，便于不启录制也能重跑可见性断言。
    if (app.simulate_indicator_state == 1) {
        recording = true; paused = false;
    } else if (app.simulate_indicator_state == 2) {
        recording = false; paused = true;
    }
    const uint64_t elapsed = has_snapshot ? app.recorder.Snapshot().session_elapsed_ms : 0;
    const std::wstring remaining = L"剩余可录 " + FormatElapsed(app.remaining_seconds * 1000);
    const std::wstring region_mode = app.config.region.enabled()
                                         ? L"区域 " + std::to_wstring(app.config.region.width) +
                                               L"×" + std::to_wstring(app.config.region.height)
                                         : L"全屏";

    std::wstring status;
    if (active) {
        if (!has_snapshot) {
            status = L"正在启动录制…  " + remaining + L"  ·  " + region_mode;
        } else if (state == sr::RecorderState::Recording) {
            const std::wstring metrics = FormatMetrics(app.recorder.Snapshot());
            // 第一行：状态/计时/剩余/区域；第二行：三项指标 + 文件名。
            // 定死的两行比让静态控件自动折行更稳（折行点会随 DPI 变）。
            const std::wstring label = std::filesystem::path(app.last_output).filename().wstring();
            status = L"录制中  " + FormatElapsed(elapsed) + L"  ·  " + remaining + L"  ·  " +
                     region_mode + L"\n" +
                     (metrics.empty() ? L"指标等待 recorder…" : metrics) + L"  ·  " + label;
        } else if (state == sr::RecorderState::Paused) {
            const std::wstring metrics = FormatMetrics(app.recorder.Snapshot());
            const std::wstring label = std::filesystem::path(app.last_output).filename().wstring();
            status = L"已暂停  " + FormatElapsed(elapsed) + L"（暂停不计时）  ·  " + remaining +
                     L"  ·  " + region_mode + L"\n" +
                     (metrics.empty() ? L"指标等待 recorder…" : metrics) + L"  ·  " + label;
        } else if (state == sr::RecorderState::Stopping) {
            status = L"正在收尾…  ·  " + remaining + L"  ·  " + region_mode;
        } else {
            status = L"录制会话进行中（" + Widen(sr::ToString(state)) + L"）  ·  " + remaining +
                     L"  ·  " + region_mode;
        }
    } else if (app.recorder.Exited()) {
        if (app.recorder.ExitCode() == 0) {
            status = L"已保存: " + app.last_output;
        } else {
            status = L"Recorder 退出，code=" + std::to_wstring(app.recorder.ExitCode()) +
                     L"（文件可能不可用）";
        }
    } else {
        status = L"就绪。输出目录: " + app.config.output_dir + L"  ·  " + remaining + L"  ·  " +
                 region_mode;
    }
    if (!app.status_extra.empty()) {
        status += L"\n⚠ " + app.status_extra;
    }
    // M6-11#2：背压（队列积压）的可见标识。丢帧是积压的可观测信号；
    // --simulate-backpressure 是测试注入手段（与 --preflight-fail 同族），
    // 走的是**同一条 UI 渲染分支**，不是把日志文案冒充界面。
    const uint64_t dropped_now = has_snapshot
        ? static_cast<uint64_t>(app.recorder.Snapshot().metrics.frames_dropped) : 0;
    const uint64_t effective_dropped = dropped_now + app.backpressure_inject;
    if (effective_dropped > 0) {
        status = L"⚠ 背压（丢帧 " + std::to_wstring(effective_dropped) + L"）  " + status;
    }
    // M8-03：磁盘提示放在第一行最前面——状态栏只有两行，追加到末尾会被挤出可视区。
    if (app.disk_critical) {
        status = L"⚠ 磁盘剩余不足 " + std::to_wstring(app.config.disk_critical_mb) +
                 L"MB，录制将随时停止！  " + status;
    } else if (app.disk_warned) {
        status = L"⚠ 磁盘剩余不足 " + std::to_wstring(app.config.disk_warn_mb) + L"MB  " + status;
    }

    SetWindowTextW(GetDlgItem(app.hwnd, kStatusId), status.c_str());
    EnableWindow(GetDlgItem(app.hwnd, kStartButton), !active);
    EnableWindow(GetDlgItem(app.hwnd, kPauseButton), recording || paused);
    EnableWindow(GetDlgItem(app.hwnd, kStopButton), active);
    SetWindowTextW(GetDlgItem(app.hwnd, kPauseButton), paused ? L"继续" : L"暂停");

    std::wstring tooltip = L"Screen Recorder — ";
    if (recording) {
        tooltip += L"录制中 " + FormatElapsed(elapsed);
    } else if (paused) {
        tooltip += L"已暂停 " + FormatElapsed(elapsed);
    } else if (active) {
        tooltip += L"启动中";
    } else {
        tooltip += L"就绪";
    }
    if (!app.status_extra.empty()) {
        tooltip += L"（热键冲突）";
    }
    // M1-27：最小化到托盘后主窗口看不见了，提示要跟着托盘提示走。
    if (!app.elevated) {
        tooltip += L"（未提权：UAC 提权窗口可能黑屏）";
    }
    UpdateTray(app, tooltip, (recording || paused) ? app.icon_active : app.icon_idle);
    UpdateRecordIndicator(app, recording, paused);
    UpdateToolbar(app);
    ApplyElevationUi(app);
}

// ---------------- M2-14：开录前自检 ----------------

bool CheckDisplayPresent() {
    return GetSystemMetrics(SM_CXSCREEN) > 0 && GetSystemMetrics(SM_CYSCREEN) > 0;
}

bool CheckAudioEndpoint(std::wstring& reason) {
    IMMDeviceEnumerator* enumerator = nullptr;
    const HRESULT created = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                             IID_PPV_ARGS(&enumerator));
    if (FAILED(created) || enumerator == nullptr) {
        reason = L"无法访问音频设备枚举器";
        return false;
    }
    IMMDevice* device = nullptr;
    const HRESULT found = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (device != nullptr) {
        device->Release();
    }
    enumerator->Release();
    if (FAILED(found)) {
        reason = L"未检测到音频输出设备";
        return false;
    }
    return true;
}

bool CheckEncoderAvailable(AppState& app, std::wstring& reason) {
    if (app.encoder_checked) {
        Log(L"preflight_encoder_cache ok=%d", app.encoder_ok ? 1 : 0);
        if (!app.encoder_ok) {
            reason = L"未找到 H.264 编码器（硬编/软编均不可用）";
        }
        return app.encoder_ok;
    }
    app.encoder_checked = true;

    MFT_REGISTER_TYPE_INFO output_type{MFMediaType_Video, MFVideoFormat_H264};
    MFT_REGISTER_TYPE_INFO input_type{MFMediaType_Video, MFVideoFormat_NV12};
    IMFActivate** activates = nullptr;
    UINT32 count = 0;
    HRESULT result = MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER, MFT_ENUM_FLAG_ALL, &input_type,
                               &output_type, &activates, &count);
    if (SUCCEEDED(result) && count == 0) {
        // 某些 MFT 不声明 NV12 输入类型；退化为只按输出类型枚举。
        if (activates != nullptr) {
            CoTaskMemFree(activates);
            activates = nullptr;
        }
        result = MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER, MFT_ENUM_FLAG_ALL, nullptr,
                           &output_type, &activates, &count);
    }
    if (activates != nullptr) {
        for (UINT32 i = 0; i < count; ++i) {
            activates[i]->Release();
        }
        CoTaskMemFree(activates);
    }
    app.encoder_ok = SUCCEEDED(result) && count > 0;
    if (!app.encoder_ok) {
        reason = L"未找到 H.264 编码器（硬编/软编均不可用）";
    }
    return app.encoder_ok;
}

// M1-27：以管理员身份重启。
//
// 交互约定（轮 40 指令提醒①）：**不自动弹 UAC**。这里只做「构造命令行 + 发起提权请求」，
// UAC 对话框由系统弹出、由用户确认；用户取消或失败都要有可见提示，不静默吞掉。
// 结果只写状态栏与日志，不弹模态框——模态框会在自动化里把 UI 线程卡住。
void RestartAsAdmin(AppState& app) {
    const std::wstring parameters = sr::app::BuildElevatedParameters(app.log_path);
    const std::wstring command = sr::app::BuildElevatedCommandLine(app.log_path);
    Log(L"uac_elevate_command=%ls", command.c_str());
    std::wstring error;
    const sr::app::ElevationLaunch result =
        sr::app::LaunchElevated(sr::app::CurrentExecutablePath(), parameters, error);
    if (result == sr::app::ElevationLaunch::kStarted) {
        Log(L"uac_elevate_launch=ok");
        app.status_extra = L"已请求以管理员身份重启，请在提权后的新窗口中继续录制";
    } else if (result == sr::app::ElevationLaunch::kCancelled) {
        LogWarn(L"uac_elevate_launch=cancelled reason=%ls", error.c_str());
        app.status_extra = L"以管理员身份重启已取消：" + error;
    } else {
        LogWarn(L"uac_elevate_launch=failed reason=%ls", error.c_str());
        app.status_extra = L"以管理员身份重启失败：" + error;
    }
    UpdateUi(app);
}

// M1-27：提权提示的常驻标签与提权入口按钮（仅在未提权时可见）。
void ApplyElevationUi(AppState& app) {
    if (app.hwnd == nullptr) {
        return;
    }
    HWND label = GetDlgItem(app.hwnd, kUacWarnId);
    HWND button = GetDlgItem(app.hwnd, kElevateButton);
    if (label != nullptr) {
        SetWindowTextW(label, app.elevated ? L"" : kUacWarnLabel);
        ShowWindow(label, app.elevated ? SW_HIDE : SW_SHOW);
    }
    if (button != nullptr) {
        ShowWindow(button, app.elevated ? SW_HIDE : SW_SHOW);
        EnableWindow(button, app.elevated ? FALSE : TRUE);
    }
}

bool RunPreflight(AppState& app, std::wstring& error) {
    const ULONGLONG begin = GetTickCount64();
    struct Item {
        const wchar_t* name;
        bool ok;
        std::wstring reason;
        // blocking=false 的项只是「限制提示」，不阻断开录（M1-27 的提权检查）。
        bool blocking;
    };
    std::vector<Item> items;

    uint64_t free_bytes = 0;
    const uint64_t stop_bytes =
        static_cast<uint64_t>(app.config.disk_stop_mb) * 1024ull * 1024ull;
    const uint32_t total_kbps = app.config.video_bitrate_kbps + sr::util::kAudioBitrateKbps;
    const bool disk_read = ProbeFreeBytes(app.config.output_dir, free_bytes);
    const double seconds = disk_read
                               ? sr::util::EstimateRecordSeconds(free_bytes, stop_bytes, total_kbps)
                               : 0.0;
    const bool disk_ok = disk_read && seconds >= 60.0;
    items.push_back({L"disk", disk_ok,
                     disk_ok ? L"" : (disk_read ? L"剩余空间不足 1 分钟" : L"无法读取磁盘空间"), true});

    const bool display_ok = CheckDisplayPresent();
    items.push_back({L"display", display_ok, display_ok ? L"" : L"未检测到显示器", true});

    std::wstring audio_reason;
    const bool audio_ok = CheckAudioEndpoint(audio_reason);
    items.push_back({L"audio", audio_ok, audio_reason, true});

    std::wstring encoder_reason;
    const bool encoder_ok = CheckEncoderAvailable(app, encoder_reason);
    items.push_back({L"encoder", encoder_ok, encoder_reason, true});

    // M1-27：提权检查。**不是自检失败，是采集限制提示**——非管理员录 UAC 提权窗口
    // 可能黑屏或取不到画面。产品能做的是「明确提示 + 给提权入口」，不阻断开录。
    items.push_back({L"elevation", app.elevated,
                     app.elevated ? L"" : L"not elevated: UAC 提权窗口等高权限窗口可能黑屏或无画面",
                     false});

    bool all_ok = true;
    std::wstring first_error;
    for (Item& item : items) {
#if defined(SR_TEST_HOOKS)
        if (!app.preflight_fail.empty() && app.preflight_fail == item.name) {
            item.ok = false;
            item.reason = L"injected by --preflight-fail";
        }
#endif
        if (!item.ok && item.blocking) {
            all_ok = false;
            if (first_error.empty()) {
                // 把检查项名称带进可见原因，注入失败也能定位是哪一项。
                first_error = std::wstring(item.name) + L": " + item.reason;
            }
        }
        Log(L"preflight_check name=%ls ok=%d blocking=%d reason=%ls",
            item.name, item.ok ? 1 : 0, item.blocking ? 1 : 0, item.reason.c_str());
    }
    // M1-27：提权限制单独记一行，便于验收 grep（不混在 preflight_check 的成败语义里）。
    if (!app.elevated) {
        Log(L"uac_preflight_warning elevated=0 reason=not_elevated action=restart_as_admin");
    }
    app.last_preflight_ms = GetTickCount64() - begin;
    if (!all_ok) {
        error = L"开录前自检失败: " + first_error;
        // 自检失败是异常路径：用 warn 级别，--log-level warn 下也能看到原因。
        LogWarn(L"preflight_failed reason=%ls ms=%llu",
                first_error.c_str(), static_cast<unsigned long long>(app.last_preflight_ms));
        return false;
    }
    Log(L"preflight_pass=1 ms=%llu", static_cast<unsigned long long>(app.last_preflight_ms));
    return true;
}

void StartRecording(AppState& app) {
    if (app.recorder.Active()) {
        Log(L"start_ignored already_active");
        return;
    }

    std::wstring preflight_error;
    if (!RunPreflight(app, preflight_error)) {
        app.status_extra = preflight_error;
        UpdateUi(app);
        if (!app.hidden) {
            MessageBoxW(app.hwnd, preflight_error.c_str(), kAppTitle, MB_ICONERROR);
        }
        return;
    }

    std::wstring output;
    std::wstring error;
    if (!BuildOutputPath(app.out_override, app.config, output, error)) {
        Log(L"start_failed error=%ls", error.c_str());
        if (!app.hidden) {
            MessageBoxW(app.hwnd, error.c_str(), kAppTitle, MB_ICONERROR);
        }
        return;
    }
    // Recorder 子进程的工作目录是它自己的 exe 目录；相对路径会在那里落地。
    // UI 与验收脚本都按「UI 进程的当前目录」理解 --out，这里统一转绝对路径。
    {
        std::error_code ec;
        const std::filesystem::path absolute = std::filesystem::absolute(output, ec);
        if (!ec) {
            output = absolute.wstring();
        }
    }
    // 提前提示（M8-06 复用）：不可写目录当场拒绝；真正权威仍是 Recorder 的 exit 13。
    const std::wstring parent = std::filesystem::path(output).parent_path().wstring();
    if (!parent.empty()) {
        std::wstring probe_error;
        if (!sr::util::EnsureDirectoryWritable(parent, probe_error)) {
            Log(L"start_precheck_failed out=%ls reason=%ls", output.c_str(), probe_error.c_str());
            app.status_extra = L"保存路径不可写: " + probe_error;
            UpdateUi(app);
            if (!app.hidden) {
                MessageBoxW(app.hwnd, app.status_extra.c_str(), kAppTitle, MB_ICONERROR);
            }
            return;
        }
    }

    app.spawn_t = GetTickCount64();
    app.indicator_logged = false;
    app.last_output = output;
    app.session_active = true;
    app.toolbar_user_hidden = false;
    Log(L"start_request out=%ls", output.c_str());
    // M1-27：录制中同样要有提示（开录前自检已经提示过一次，这里补「录制中」这一份）。
    if (!app.elevated) {
        Log(L"uac_warning phase=recording elevated=0 reason=not_elevated action=restart_as_admin");
    }
    ShowToolbar(app, true);

    if (!app.recorder.Start(app.config.recorder_path, output, app.config.region,
                            app.config.video_bitrate_kbps, app.config.disk_stop_mb,
                            app.config.video_fps, app.config.rate_mode,
                            [](const std::wstring& text) { Log(L"%ls", text.c_str()); },
                            [&app](const sr::ipc::StateSnapshotPayload& snapshot) {
                                const auto state = static_cast<sr::RecorderState>(snapshot.state);
                                // M6-03：把 recorder 下发的三项指标原样记进日志（不推算），
                                // 自动化据此断言「UI 拿到的是 recorder 的计数」。
                                Log(L"snapshot state=%ls elapsed_ms=%llu capture_fps=%.2f encode_fps=%.2f dropped=%llu",
                                    Widen(sr::ToString(state)).c_str(),
                                    static_cast<unsigned long long>(snapshot.session_elapsed_ms),
                                    static_cast<double>(snapshot.metrics.capture_fps_x100) / 100.0,
                                    static_cast<double>(snapshot.metrics.encode_fps_x100) / 100.0,
                                    static_cast<unsigned long long>(snapshot.metrics.frames_dropped));
                                LogDebug(L"snapshot_metrics has_metrics=%d captured=%llu",
                                         snapshot.has_metrics ? 1 : 0,
                                         static_cast<unsigned long long>(
                                             snapshot.metrics.frames_captured));
                                if (state == sr::RecorderState::Recording && !app.indicator_logged) {
                                    app.indicator_logged = true;
                                    const ULONGLONG now = GetTickCount64();
                                    Log(L"recording_indicator latency_ms=%llu",
                                        static_cast<unsigned long long>(now - app.spawn_t));
                                }
                                UpdateUi(app);
                                if (app.preview_menu && state == sr::RecorderState::Recording &&
                                    !app.preview_done) {
                                    app.preview_done = true;
                                    SetTimer(app.hwnd, kPreviewTimer, 700, nullptr);
                                }
                            },
                            error)) {
        Log(L"start_failed error=%ls", error.c_str());
        app.session_active = false;
        if (!app.hidden) {
            MessageBoxW(app.hwnd, error.c_str(), kAppTitle, MB_ICONERROR);
        }
        UpdateUi(app);
        return;
    }
    UpdateUi(app);
}

void PauseResume(AppState& app) {
    if (!app.recorder.Active() || !app.recorder.HasSnapshot()) {
        Log(L"hotkey_ignored pause reason=no_session");
        return;
    }
    const auto state = static_cast<sr::RecorderState>(app.recorder.Snapshot().state);
    std::wstring error;
    if (state == sr::RecorderState::Recording) {
        if (!app.recorder.SendCommand(sr::RecorderCommand::Pause, error)) {
            Log(L"pause_failed error=%ls", error.c_str());
        }
    } else if (state == sr::RecorderState::Paused) {
        if (!app.recorder.SendCommand(sr::RecorderCommand::Resume, error)) {
            Log(L"resume_failed error=%ls", error.c_str());
        }
    } else {
        Log(L"hotkey_ignored pause state=%ls", Widen(sr::ToString(state)).c_str());
    }
}

void StopRecording(AppState& app) {
    if (!app.recorder.Active()) {
        Log(L"hotkey_ignored stop reason=no_session");
        return;
    }
    std::wstring error;
    if (!app.recorder.SendCommand(sr::RecorderCommand::Stop, error)) {
        Log(L"stop_failed error=%ls", error.c_str());
    }
}

void OpenOutputFolder(AppState& app) {
    if (app.last_output.empty()) {
        return;
    }
    const std::filesystem::path path(app.last_output);
    const std::wstring folder = path.parent_path().wstring();
    ShellExecuteW(app.hwnd, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void ShowMainWindow(AppState& app) {
    ShowWindow(app.hwnd, SW_SHOW);
    ShowWindow(app.hwnd, SW_RESTORE);
    SetForegroundWindow(app.hwnd);
}

// ---------------- M8-14：一键导出诊断包 ----------------

void ExportDiagnosticPack(AppState& app) {
    std::wstring root = app.config.output_dir;
    IFileOpenDialog* dialog = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&dialog)))) {
        DWORD options = 0;
        if (SUCCEEDED(dialog->GetOptions(&options))) {
            dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        }
        if (FAILED(dialog->Show(app.hwnd))) {
            dialog->Release();
            Log(L"diag_pack cancelled");
            return;
        }
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)) && item != nullptr) {
            PWSTR picked = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &picked)) && picked != nullptr) {
                root = picked;
                CoTaskMemFree(picked);
            }
            item->Release();
        }
        dialog->Release();
    }

    sr::diag::DiagRequest request;
    request.output_root = root;
    request.config_path = app.config_path;
    request.ui_log_path = app.log_path;
    if (!app.last_output.empty()) {
        request.recorder_log_path = app.last_output + L".recorder.log";
    }
    request.product = "sr-ui";
    request.version = Narrow(kUiVersion);
    request.protocol = std::to_string(static_cast<unsigned>(sr::ipc::kProtocolVersion));
    request.build = Narrow(kBuildTag);
    request.test_hooks = kTestHooks;
    request.session = "mode=tray-menu\nlog_level=";
    request.session += sr::diag::LogLevelName(sr::diag::GetLogLevel());
    request.session += "\noutput_dir=";
    request.session += Narrow(app.config.output_dir);
    request.session += "\nregion=";
    request.session += (app.config.region.enabled() ? "custom" : "full");
    request.session += "\nsession_active=";
    request.session += (app.session_active ? "1" : "0");
    request.session += "\n";

    const sr::diag::DiagResult result = sr::diag::Export(request);
    Log(L"diag_pack ok=%d files=%zu bytes=%llu ms=%llu path=%ls",
        result.ok ? 1 : 0, result.files.size(),
        static_cast<unsigned long long>(result.bytes),
        static_cast<unsigned long long>(result.elapsed_ms), result.directory.c_str());

    wchar_t message[1024] = {};
    if (result.ok) {
        std::swprintf(message, std::size(message),
                      L"诊断包已导出\n%ls\n%zu 个文件 / %llu 字节 / %llu ms\n"
                      L"（仅版本、硬件、日志、配置，不含画面与音频）",
                      result.directory.c_str(), result.files.size(),
                      static_cast<unsigned long long>(result.bytes),
                      static_cast<unsigned long long>(result.elapsed_ms));
    } else {
        std::swprintf(message, std::size(message), L"导出失败: %ls",
                      Widen(result.error.c_str()).c_str());
    }
    MessageBoxW(app.hwnd, message, kAppTitle, result.ok ? MB_ICONINFORMATION : MB_ICONERROR);
}

// ---------------- M1-04 / M1-05：区域框选与微调 ----------------

void StartRegionOverlay(AppState& app);
void ShowRegionEditor(AppState& app);

void UpdateRegionEditorText(AppState& app, HWND editor) {
    wchar_t text[256] = {};
    if (app.region_edit.enabled()) {
        std::swprintf(text, std::size(text),
                      L"区域: x=%d y=%d  %d×%d  物理像素  DPI %u",
                      app.region_edit.x, app.region_edit.y, app.region_edit.width,
                      app.region_edit.height, app.region_dpi);
    } else {
        std::swprintf(text, std::size(text), L"区域: 全屏  物理像素  DPI %u", app.region_dpi);
    }
    SetWindowTextW(GetDlgItem(editor, kRegionTextId), text);
}

void ApplyRegion(AppState& app, const sr::app::CaptureRegion& region, const wchar_t* source) {
    if (!region.enabled()) {
        app.config.region = {};
        std::wstring error;
        const bool saved = sr::app::SaveRegion(app.config_path, app.config.region, error);
        Log(L"region_applied source=%ls full=1 dpi=%u saved=%d",
            source, app.region_dpi, saved ? 1 : 0);
        if (!saved) {
            app.status_extra = L"区域已切换为全屏，但配置写入失败: " + error;
        }
        UpdateUi(app);
        return;
    }
    const sr::app::CaptureRegion normalized = sr::app::NormalizeRegion(region);
    if (normalized.width < 16 || normalized.height < 16) {
        app.status_extra = L"选区太小（至少 16×16）";
        UpdateUi(app);
        return;
    }
    app.config.region = normalized;
    std::wstring error;
    const bool saved = sr::app::SaveRegion(app.config_path, normalized, error);
    Log(L"region_applied source=%ls x=%d y=%d w=%d h=%d dpi=%u saved=%d",
        source, normalized.x, normalized.y, normalized.width, normalized.height,
        app.region_dpi, saved ? 1 : 0);
    if (!saved) {
        app.status_extra = L"区域已生效，但配置写入失败: " + error;
    }
    UpdateUi(app);
}

void PresetRegion(AppState& app, int numerator, int denominator, const wchar_t* label) {
    int width = app.region_edit.enabled() ? app.region_edit.width : 800;
    if (width < 16) {
        width = 800;
    }
    const int height = (width * denominator + numerator / 2) / numerator;
    const int x = app.region_edit.enabled() ? app.region_edit.x : 100;
    const int y = app.region_edit.enabled() ? app.region_edit.y : 100;
    app.region_edit = sr::app::NormalizeRegion(sr::app::CaptureRegion{x, y, width, height});
    UpdateRegionEditorText(app, app.region_editor);
    Log(L"region_preset %ls x=%d y=%d w=%d h=%d",
        label, app.region_edit.x, app.region_edit.y, app.region_edit.width, app.region_edit.height);
}

LRESULT CALLBACK RegionOverlayProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    AppState* app = g_app;
    switch (message) {
        case WM_CREATE:
            if (app != nullptr) {
                app->region_overlay = hwnd;
                app->region_dpi = GetDpiForWindow(hwnd);
                app->region_selecting = false;
                app->region_current = RECT{0, 0, 0, 0};
                Log(L"region_overlay_created dpi=%u virtual=%d,%d,%d,%d",
                    app->region_dpi, GetSystemMetrics(SM_XVIRTUALSCREEN),
                    GetSystemMetrics(SM_YVIRTUALSCREEN), GetSystemMetrics(SM_CXVIRTUALSCREEN),
                    GetSystemMetrics(SM_CYVIRTUALSCREEN));
            }
            return 0;
        case WM_LBUTTONDOWN: {
            if (app == nullptr) {
                return 0;
            }
            SetCapture(hwnd);
            app->region_selecting = true;
            app->region_start = POINT{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            app->region_current = RECT{app->region_start.x, app->region_start.y,
                                       app->region_start.x, app->region_start.y};
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (app == nullptr || !app->region_selecting) {
                return 0;
            }
            const int x = GET_X_LPARAM(lparam);
            const int y = GET_Y_LPARAM(lparam);
            const int start_x = static_cast<int>(app->region_start.x);
            const int start_y = static_cast<int>(app->region_start.y);
            app->region_current = RECT{std::min(start_x, x), std::min(start_y, y),
                                       std::max(start_x, x), std::max(start_y, y)};
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }
        case WM_LBUTTONUP: {
            if (app == nullptr || !app->region_selecting) {
                return 0;
            }
            ReleaseCapture();
            app->region_selecting = false;
            const int x = GET_X_LPARAM(lparam);
            const int y = GET_Y_LPARAM(lparam);
            const int start_x = static_cast<int>(app->region_start.x);
            const int start_y = static_cast<int>(app->region_start.y);
            const int left = std::min(start_x, x);
            const int top = std::min(start_y, y);
            const int width = std::abs(x - start_x);
            const int height = std::abs(y - start_y);
            const int virtual_x = GetSystemMetrics(SM_XVIRTUALSCREEN);
            const int virtual_y = GetSystemMetrics(SM_YVIRTUALSCREEN);
            app->region_dpi = GetDpiForWindow(hwnd);
            Log(L"region_selected dpi=%u client=%d,%d,%d,%d physical=%d,%d,%d,%d",
                app->region_dpi, left, top, width, height, left + virtual_x, top + virtual_y,
                width, height);
            DestroyWindow(hwnd);
            if (width >= 16 && height >= 16) {
                ApplyRegion(*app,
                            sr::app::CaptureRegion{left + virtual_x, top + virtual_y, width, height},
                            L"drag");
            }
            return 0;
        }
        case WM_KEYDOWN:
            if (wparam == VK_ESCAPE) {
                DestroyWindow(hwnd);
            }
            return 0;
        case WM_RBUTTONUP:
            DestroyWindow(hwnd);
            return 0;
        case WM_SETCURSOR:
            SetCursor(LoadCursorW(nullptr, IDC_CROSS));
            return TRUE;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(hwnd, &paint);
            RECT client{};
            GetClientRect(hwnd, &client);
            FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            if (app != nullptr && app->region_current.right > app->region_current.left &&
                app->region_current.bottom > app->region_current.top) {
                const RECT& r = app->region_current;
                HPEN pen = CreatePen(PS_SOLID, 3, RGB(255, 64, 64));
                HGDIOBJ old_pen = SelectObject(dc, pen);
                HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
                Rectangle(dc, r.left, r.top, r.right, r.bottom);
                SelectObject(dc, old_brush);
                SelectObject(dc, old_pen);
                DeleteObject(pen);
                wchar_t text[128] = {};
                std::swprintf(text, std::size(text), L"%d×%d  @%d,%d",
                              r.right - r.left, r.bottom - r.top,
                              r.left + GetSystemMetrics(SM_XVIRTUALSCREEN),
                              r.top + GetSystemMetrics(SM_YVIRTUALSCREEN));
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, RGB(255, 255, 255));
                TextOutW(dc, r.left + 6, r.top + 6, text, static_cast<int>(wcslen(text)));
            }
            EndPaint(hwnd, &paint);
            return 0;
        }
        case WM_DESTROY:
            if (app != nullptr && app->region_overlay == hwnd) {
                app->region_overlay = nullptr;
            }
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

LRESULT CALLBACK RegionEditorProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    AppState* app = g_app;
    switch (message) {
        case WM_CREATE: {
            if (app == nullptr) {
                return -1;
            }
            app->region_editor = hwnd;
            app->region_edit = app->config.region;
            app->region_dpi = GetDpiForWindow(hwnd);
            const WPARAM font = reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT));
            auto make = [&](const wchar_t* cls, const wchar_t* text, DWORD style,
                            int x, int y, int width, int height, int id) {
                HWND control = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                                               x, y, width, height, hwnd, ControlId(id),
                                               app->instance, nullptr);
                SendMessageW(control, WM_SETFONT, font, TRUE);
                return control;
            };
            make(L"STATIC", L"", SS_LEFT, 16, 12, 420, 26, kRegionTextId);
            make(L"BUTTON", L"全屏", BS_PUSHBUTTON, 16, 48, 90, 28, kRegionFull);
            make(L"BUTTON", L"16:9", BS_PUSHBUTTON, 116, 48, 90, 28, kRegion169);
            make(L"BUTTON", L"4:3", BS_PUSHBUTTON, 216, 48, 90, 28, kRegion43);
            make(L"BUTTON", L"1:1", BS_PUSHBUTTON, 316, 48, 90, 28, kRegion11);
            make(L"BUTTON", L"框选…", BS_PUSHBUTTON, 16, 88, 120, 30, kRegionPick);
            make(L"BUTTON", L"确定", BS_PUSHBUTTON, 146, 88, 120, 30, kRegionOk);
            make(L"BUTTON", L"取消", BS_PUSHBUTTON, 276, 88, 120, 30, kRegionCancel);
            make(L"STATIC", L"方向键移动 1px，Shift+方向键 10px；框选按物理像素，尺寸自动偶数对齐。",
                 SS_LEFT, 16, 134, 420, 40, 0);
            UpdateRegionEditorText(*app, hwnd);
            SetFocus(hwnd);
            return 0;
        }
        case WM_KEYDOWN: {
            if (app == nullptr) {
                return 0;
            }
            if (!app->region_edit.enabled()) {
                app->region_edit = sr::app::NormalizeRegion(sr::app::CaptureRegion{100, 100, 800, 600});
            }
            const int step = (GetKeyState(VK_SHIFT) & 0x8000) != 0 ? 10 : 1;
            switch (wparam) {
                case VK_LEFT: app->region_edit.x -= step; break;
                case VK_RIGHT: app->region_edit.x += step; break;
                case VK_UP: app->region_edit.y -= step; break;
                case VK_DOWN: app->region_edit.y += step; break;
                default: return 0;
            }
            UpdateRegionEditorText(*app, hwnd);
            Log(L"region_edit x=%d y=%d w=%d h=%d step=%d",
                app->region_edit.x, app->region_edit.y, app->region_edit.width,
                app->region_edit.height, step);
            return 0;
        }
        case WM_COMMAND: {
            if (app == nullptr) {
                return 0;
            }
            switch (LOWORD(wparam)) {
                case kRegionFull:
                    app->region_edit = {};
                    UpdateRegionEditorText(*app, hwnd);
                    Log(L"region_edit full=1");
                    break;
                case kRegion169: PresetRegion(*app, 16, 9, L"16:9"); break;
                case kRegion43: PresetRegion(*app, 4, 3, L"4:3"); break;
                case kRegion11: PresetRegion(*app, 1, 1, L"1:1"); break;
                case kRegionPick:
                    DestroyWindow(hwnd);
                    StartRegionOverlay(*app);
                    break;
                case kRegionOk:
                    ApplyRegion(*app, app->region_edit, L"editor");
                    DestroyWindow(hwnd);
                    break;
                case kRegionCancel:
                    DestroyWindow(hwnd);
                    break;
                default: break;
            }
            return 0;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (app != nullptr && app->region_editor == hwnd) {
                app->region_editor = nullptr;
            }
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

void StartRegionOverlay(AppState& app) {
    if (app.region_overlay != nullptr) {
        SetForegroundWindow(app.region_overlay);
        return;
    }
    const int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    HWND overlay = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
                                   kRegionOverlayClass, L"选择录制区域",
                                   WS_POPUP, x, y, width, height, nullptr, nullptr,
                                   app.instance, nullptr);
    if (overlay == nullptr) {
        Log(L"region_overlay_failed error=%lu", GetLastError());
        return;
    }
    SetLayeredWindowAttributes(overlay, 0, 110, LWA_ALPHA);
    ShowWindow(overlay, SW_SHOW);
    SetForegroundWindow(overlay);
    SetFocus(overlay);
    Log(L"region_overlay_shown x=%d y=%d w=%d h=%d", x, y, width, height);
}

void ShowRegionEditor(AppState& app) {
    if (app.region_editor != nullptr) {
        ShowWindow(app.region_editor, SW_SHOW);
        SetForegroundWindow(app.region_editor);
        return;
    }
    RECT parent{};
    GetWindowRect(app.hwnd, &parent);
    const int width = 460;
    const int height = 220;
    const int x = parent.left + ((parent.right - parent.left) - width) / 2;
    const int y = parent.top + ((parent.bottom - parent.top) - height) / 2;
    HWND editor = CreateWindowExW(WS_EX_DLGMODALFRAME, kRegionEditorClass, L"录制区域",
                                  WS_CAPTION | WS_SYSMENU | WS_POPUP,
                                  x, y, width, height, app.hwnd, nullptr, app.instance, nullptr);
    if (editor == nullptr) {
        Log(L"region_editor_failed error=%lu", GetLastError());
        return;
    }
    ShowWindow(editor, SW_SHOW);
    SetForegroundWindow(editor);
    SetFocus(editor);
}

// ---------------- 悬浮工具条（M2-08） ----------------

LRESULT CALLBACK ToolbarProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    AppState* app = g_app;
    switch (message) {
        case WM_CREATE: {
            if (app == nullptr) {
                return -1;
            }
            app->toolbar = hwnd;
            const WPARAM font = reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT));
            auto make = [&](const wchar_t* cls, const wchar_t* text, DWORD style,
                            int x, int y, int width, int height, int id) {
                HWND control = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                                               x, y, width, height, hwnd, ControlId(id),
                                               app->instance, nullptr);
                SendMessageW(control, WM_SETFONT, font, TRUE);
                return control;
            };
            app->toolbar_timer = make(L"STATIC", L"00:00", SS_LEFT, 10, 6, 150, 20, kTbTimer);
            app->toolbar_remaining =
                make(L"STATIC", L"剩余 --", SS_LEFT, 10, 30, 150, 20, kTbRemaining);
            app->toolbar_pause =
                make(L"BUTTON", L"暂停", BS_PUSHBUTTON, 170, 8, 68, 22, kTbPause);
            app->toolbar_stop =
                make(L"BUTTON", L"停止", BS_PUSHBUTTON, 244, 8, 68, 22, kTbStop);
            make(L"BUTTON", L"隐藏", BS_PUSHBUTTON, 170, 34, 142, 22, kTbHide);
            // M6-03：悬浮条展示三项指标（与状态栏同一份快照数据）。
            app->toolbar_metrics =
                make(L"STATIC", L"采集 -- / 编码 -- fps · 丢帧 0", SS_LEFT, 10, 56, 300, 20, kTbMetrics);
            return 0;
        }
        case WM_NCHITTEST:
            return HTCAPTION;  // 按住背景即可拖动
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;  // 点击按钮不抢被测窗口焦点
        case WM_COMMAND: {
            if (app == nullptr) {
                return 0;
            }
            switch (LOWORD(wparam)) {
                case kTbPause: PauseResume(*app); break;
                case kTbStop: StopRecording(*app); break;
                case kTbHide:
                    ShowWindow(hwnd, SW_HIDE);
                    app->toolbar_user_hidden = true;
                    Log(L"toolbar_hidden_by_user");
                    break;
                default: break;
            }
            return 0;
        }
        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        case WM_DESTROY:
            if (app != nullptr && app->toolbar == hwnd) {
                app->toolbar = nullptr;
                app->toolbar_timer = nullptr;
                app->toolbar_remaining = nullptr;
                app->toolbar_metrics = nullptr;
                app->toolbar_pause = nullptr;
                app->toolbar_stop = nullptr;
            }
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

void EnsureToolbar(AppState& app) {
    if (app.toolbar != nullptr) {
        return;
    }
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int width = 320;
    const int height = 88;
    const int x = work.right - width - 16;
    const int y = work.bottom - height - 16;
    app.toolbar = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                                  kToolbarClass, L"sr-ui 悬浮条", WS_POPUP,
                                  x, y, width, height, app.hwnd, nullptr, app.instance, nullptr);
    if (app.toolbar == nullptr) {
        Log(L"toolbar_create_failed error=%lu", GetLastError());
        return;
    }
    SetWindowPos(app.toolbar, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE);
    Log(L"toolbar_created hwnd=%llu", static_cast<unsigned long long>(
                                          reinterpret_cast<ULONG_PTR>(app.toolbar)));
}

void ShowToolbar(AppState& app, bool show) {
    if (show) {
        EnsureToolbar(app);
        if (app.toolbar == nullptr) {
            return;
        }
        app.toolbar_user_hidden = false;
        SetWindowPos(app.toolbar, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        UpdateToolbar(app);
        Log(L"toolbar_shown");
    } else if (app.toolbar != nullptr) {
        ShowWindow(app.toolbar, SW_HIDE);
        Log(L"toolbar_hidden");
    }
}

void ToggleToolbar(AppState& app) {
    if (app.toolbar != nullptr && IsWindowVisible(app.toolbar)) {
        ShowToolbar(app, false);
    } else {
        ShowToolbar(app, true);
    }
}

void UpdateToolbar(AppState& app) {
    if (app.toolbar == nullptr) {
        return;
    }
    const bool active = app.recorder.Active();
    const bool has_snapshot = app.recorder.HasSnapshot();
    const sr::RecorderState state = has_snapshot
                                        ? static_cast<sr::RecorderState>(app.recorder.Snapshot().state)
                                        : sr::RecorderState::Idle;
    const bool recording = active && has_snapshot && state == sr::RecorderState::Recording;
    const bool paused = active && has_snapshot && state == sr::RecorderState::Paused;
    const uint64_t elapsed = has_snapshot ? app.recorder.Snapshot().session_elapsed_ms : 0;

    if (app.toolbar_timer != nullptr) {
        SetWindowTextW(app.toolbar_timer, FormatElapsed(elapsed).c_str());
    }
    if (app.toolbar_remaining != nullptr) {
        const std::wstring remaining = L"剩余 " + FormatElapsed(app.remaining_seconds * 1000);
        SetWindowTextW(app.toolbar_remaining, remaining.c_str());
    }
    if (app.toolbar_metrics != nullptr) {
        std::wstring metrics;
        if (has_snapshot) {
            metrics = FormatMetrics(app.recorder.Snapshot());
        }
        if (metrics.empty()) {
            metrics = L"指标等待 recorder…";
        }
        SetWindowTextW(app.toolbar_metrics, metrics.c_str());
    }
    if (app.toolbar_pause != nullptr) {
        EnableWindow(app.toolbar_pause, recording || paused);
        SetWindowTextW(app.toolbar_pause, paused ? L"继续" : L"暂停");
    }
    if (app.toolbar_stop != nullptr) {
        EnableWindow(app.toolbar_stop, active);
    }
}

// ---------------- M5-01：保存路径 ----------------

bool ValidateAndSaveOutputDir(AppState& app, const std::wstring& directory, std::wstring& error) {
    std::wstring probe_error;
    if (!sr::util::EnsureDirectoryWritable(directory, probe_error)) {
        error = L"保存路径不可写: " + probe_error;
        return false;
    }
    if (!sr::app::SaveOutputDir(app.config_path, directory, error)) {
        return false;
    }
    app.config.output_dir = directory;
    Log(L"output_dir_saved dir=%ls", directory.c_str());
    RefreshDiskRemaining(app);
    UpdateUi(app);
    error.clear();
    return true;
}

void BrowseOutputDir(AppState& app) {
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dialog)))) {
        Log(L"output_dir_picker_failed error=%lu", GetLastError());
        return;
    }
    DWORD options = 0;
    if (SUCCEEDED(dialog->GetOptions(&options))) {
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    }
    if (SUCCEEDED(dialog->Show(app.hwnd))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)) && item != nullptr) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path != nullptr) {
                const std::wstring directory(path);
                CoTaskMemFree(path);
                std::wstring error;
                if (!ValidateAndSaveOutputDir(app, directory, error)) {
                    Log(L"output_dir_rejected dir=%ls reason=%ls",
                        directory.c_str(), error.c_str());
                    MessageBoxW(app.hwnd, error.c_str(), kAppTitle, MB_ICONWARNING);
                }
            }
            item->Release();
        }
    }
    dialog->Release();
}

void ShowTrayMenu(AppState& app) {
    HMENU menu = CreatePopupMenu();
    const bool active = app.recorder.Active();
    const bool has_snapshot = app.recorder.HasSnapshot();
    const sr::RecorderState state = has_snapshot
                                        ? static_cast<sr::RecorderState>(app.recorder.Snapshot().state)
                                        : sr::RecorderState::Idle;
    const bool recording = active && has_snapshot && state == sr::RecorderState::Recording;
    const bool paused = active && has_snapshot && state == sr::RecorderState::Paused;

    AppendMenuW(menu, MF_STRING | (active ? MF_GRAYED : MF_ENABLED), kTrayStart, L"开始录制");
    AppendMenuW(menu, MF_STRING | ((recording || paused) ? MF_ENABLED : MF_GRAYED), kTrayPause,
                paused ? L"继续" : L"暂停");
    AppendMenuW(menu, MF_STRING | (active ? MF_ENABLED : MF_GRAYED), kTrayStop, L"停止");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (app.last_output.empty() ? MF_GRAYED : MF_ENABLED),
                kTrayOpenFolder, L"打开目录");
    AppendMenuW(menu, MF_STRING, kTrayList, L"录制列表…");
    AppendMenuW(menu, MF_STRING, kTrayDiag, L"导出诊断包…");
    const bool toolbar_visible = app.toolbar != nullptr && IsWindowVisible(app.toolbar);
    AppendMenuW(menu, MF_STRING | (active || toolbar_visible ? MF_ENABLED : MF_GRAYED),
                kTrayToolbar, toolbar_visible ? L"隐藏悬浮工具条" : L"显示悬浮工具条");
    AppendMenuW(menu, MF_STRING | (active ? MF_GRAYED : MF_ENABLED), kTraySettings, L"设置…");
    // M1-27：托盘里的提权入口（只在未提权时给出，已提权就没有意义了）。
    if (!app.elevated) {
        AppendMenuW(menu, MF_STRING, kTrayElevate, L"以管理员身份重启…");
    }
    AppendMenuW(menu, MF_STRING, kTrayShow, L"显示主窗口");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kTrayExit, L"退出");

    POINT point{};
    GetCursorPos(&point);
    SetForegroundWindow(app.hwnd);
    const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                                        point.x, point.y, 0, app.hwnd, nullptr);
    DestroyMenu(menu);
    PostMessageW(app.hwnd, WM_NULL, 0, 0);

    switch (command) {
        case kTrayStart:
            StartRecording(app);
            break;
        case kTrayPause:
            PauseResume(app);
            break;
        case kTrayStop:
            StopRecording(app);
            break;
        case kTrayOpenFolder:
            OpenOutputFolder(app);
            break;
        case kTrayList:
            ShowRecordingList(app);
            break;
        case kTrayDiag:
            ExportDiagnosticPack(app);
            break;
        case kTrayToolbar:
            ToggleToolbar(app);
            break;
        case kTraySettings:
            ShowSettings(app);
            break;
        case kTrayShow:
            ShowMainWindow(app);
            break;
        case kTrayExit:
            PostMessageW(app.hwnd, WM_CLOSE, 0, 0);
            break;
        default:
            break;
    }
}

// ---------------- 设置窗口 ----------------

void RefreshPresetValues(SettingsContext& context);

void LayoutSettings(SettingsContext& context) {
    const int label_x = 18;
    const int edit_x = 130;
    const int edit_w = 250;
    const int row_y[3] = {20, 60, 100};
    const wchar_t* labels[3] = {L"开始热键", L"暂停/继续热键", L"停止热键"};
    for (int i = 0; i < 3; ++i) {
        CreateWindowExW(0, L"STATIC", labels[i], WS_CHILD | WS_VISIBLE,
                        label_x, row_y[i] + 4, 100, 22, context.hwnd, nullptr,
                        context.app->instance, nullptr);
        const std::wstring value = sr::app::FormatHotkey(HotkeyAt(context.app->config, i));
        context.edits[i] = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", value.c_str(),
                                           WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                           edit_x, row_y[i], edit_w, 24, context.hwnd,
                                           ControlId(100 + i),
                                           context.app->instance, nullptr);
        SendMessageW(context.edits[i], WM_SETFONT,
                     reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    }
    CreateWindowExW(0, L"STATIC", L"命名模板", WS_CHILD | WS_VISIBLE,
                    label_x, 144, 100, 22, context.hwnd, nullptr,
                    context.app->instance, nullptr);
    context.template_edit = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", context.app->config.filename_template.c_str(),
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, edit_x, 140, edit_w, 24, context.hwnd,
        ControlId(kSettingsTemplate), context.app->instance, nullptr);
    SendMessageW(context.template_edit, WM_SETFONT,
                 reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    CreateWindowExW(0, L"STATIC", L"变量: {date} {time} {source} {app} {index}",
                    WS_CHILD | WS_VISIBLE, label_x, 170, 360, 20, context.hwnd, nullptr,
                    context.app->instance, nullptr);

    // M3-03：六档帧率（15/24/25/30/60/自定义 1~144）。
    CreateWindowExW(0, L"STATIC", L"帧率", WS_CHILD | WS_VISIBLE, label_x, 200, 100, 22,
                    context.hwnd, nullptr, context.app->instance, nullptr);
    context.fps_combo = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX",
                                        L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                        edit_x, 196, 110, 200, context.hwnd,
                                        ControlId(kSettingsFpsCombo), context.app->instance, nullptr);
    SendMessageW(context.fps_combo, WM_SETFONT,
                 reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    const wchar_t* fps_items[6] = {L"15 fps", L"24 fps", L"25 fps", L"30 fps", L"60 fps", L"自定义"};
    for (const wchar_t* item : fps_items) {
        SendMessageW(context.fps_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
    }
    int fps_index = 5;
    for (int i = 0; i < 5; ++i) {
        if (kFpsPresets[i] == context.app->config.video_fps) {
            fps_index = i;
        }
    }
    SendMessageW(context.fps_combo, CB_SETCURSEL, static_cast<WPARAM>(fps_index), 0);
    wchar_t fps_text[16] = {};
    std::swprintf(fps_text, std::size(fps_text), L"%u", context.app->config.video_fps);
    context.fps_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
                                       fps_index == 5 ? fps_text : L"", WS_CHILD | WS_VISIBLE | ES_NUMBER,
                                       250, 196, 60, 24, context.hwnd,
                                       ControlId(kSettingsFpsCustom), context.app->instance, nullptr);
    SendMessageW(context.fps_edit, WM_SETFONT,
                 reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    EnableWindow(context.fps_edit, fps_index == 5);

    // M3-04：码率模式。CBR = 固定码率；VBR = 平均码率 + 2× 峰值上限。
    CreateWindowExW(0, L"STATIC", L"码率模式", WS_CHILD | WS_VISIBLE, label_x, 236, 100, 22,
                    context.hwnd, nullptr, context.app->instance, nullptr);
    context.rate_combo = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
                                         WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                         edit_x, 232, 220, 200, context.hwnd,
                                         ControlId(kSettingsRateCombo), context.app->instance, nullptr);
    SendMessageW(context.rate_combo, WM_SETFONT,
                 reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    SendMessageW(context.rate_combo, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(L"CBR（固定码率）"));
    SendMessageW(context.rate_combo, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(L"VBR（平均码率 + 峰值上限）"));
    SendMessageW(context.rate_combo, CB_SETCURSEL,
                 context.app->config.rate_mode == L"vbr" ? 1 : 0, 0);
    CreateWindowExW(0, L"STATIC", L"VBR 峰值上限 = 2× 码率；码率见 [output] bitrate_kbps",
                    WS_CHILD | WS_VISIBLE, label_x, 262, 380, 20, context.hwnd, nullptr,
                    context.app->instance, nullptr);

    // M3-05：实际可用的视频编码器（探测在设置页首次打开时做，不在录制启动路径上）。
    // 不可用项也列出来并标注原因——用户看不到「为什么没有硬编」就会去报 bug。
    CreateWindowExW(0, L"STATIC", L"可用编码器", WS_CHILD | WS_VISIBLE, label_x, 296, 100, 22,
                    context.hwnd, nullptr, context.app->instance, nullptr);
    context.encoder_list = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL,
        edit_x, 292, 380, 120, context.hwnd, ControlId(kSettingsEncoderList),
        context.app->instance, nullptr);
    SendMessageW(context.encoder_list, WM_SETFONT,
                 reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    CreateWindowExW(0, L"STATIC", L"（探测由 sr-recorder --list-encoders 提供；不可用项带原因）",
                    WS_CHILD | WS_VISIBLE, label_x, 416, 380, 20, context.hwnd, nullptr,
                    context.app->instance, nullptr);

    // M3-10：画质预设档位。切换联动 fps / 码率 / 画质；当前联动值以只读文本展示，
    // 便于用户与验收直接读到「预设值在界面可见」。
    CreateWindowExW(0, L"STATIC", L"画质档位", WS_CHILD | WS_VISIBLE, label_x, 442, 100, 22,
                    context.hwnd, nullptr, context.app->instance, nullptr);
    context.preset_combo = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        edit_x, 438, 150, 200, context.hwnd, ControlId(kSettingsPresetCombo),
        context.app->instance, nullptr);
    SendMessageW(context.preset_combo, WM_SETFONT,
                 reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    const wchar_t* const* preset_names = sr::app::QualityPresetNames();
    int preset_index = static_cast<int>(sr::app::QualityPresetCount()) - 1;
    for (size_t i = 0; i < sr::app::QualityPresetCount(); ++i) {
        SendMessageW(context.preset_combo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(preset_names[i]));
        if (context.app->config.quality_preset == preset_names[i]) {
            preset_index = static_cast<int>(i);
        }
    }
    SendMessageW(context.preset_combo, CB_SETCURSEL, static_cast<WPARAM>(preset_index), 0);
    context.preset_values = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE,
        label_x, 470, 400, 22, context.hwnd, ControlId(kSettingsPresetValues),
        context.app->instance, nullptr);
    RefreshPresetValues(context);

    CreateWindowExW(0, L"BUTTON", L"保存", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                    180, 502, 80, 28, context.hwnd, ControlId(kSettingsOk),
                    context.app->instance, nullptr);
    CreateWindowExW(0, L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE,
                    280, 502, 80, 28, context.hwnd, ControlId(kSettingsCancel),
                    context.app->instance, nullptr);
}

// M3-10#2：把当前预设联动的三项值写成界面文本（fps / 码率 / 画质档位）。
void RefreshPresetValues(SettingsContext& context) {
    if (context.preset_values == nullptr) {
        return;
    }
    const auto& config = context.app->config;
    wchar_t buffer[192] = {};
    std::swprintf(buffer, std::size(buffer),
                  L"当前预设：%ls  ·  %u fps  ·  %u kbps  ·  画质 %u（高级面板可单独覆盖）",
                  config.quality_preset.c_str(), config.video_fps,
                  config.video_bitrate_kbps, config.quality);
    SetWindowTextW(context.preset_values, buffer);
}

bool ApplyHotkeys(AppState& app, const sr::app::HotkeyCombo combos[3], std::wstring& error) {
    const sr::app::HotkeyCombo previous[3] = {app.config.start, app.config.pause, app.config.stop};
    for (int i = 0; i < 3; ++i) {
        const std::wstring text = sr::app::FormatHotkey(combos[i]);
        for (int j = 0; j < i; ++j) {
            if (combos[i] == combos[j]) {
                error = L"热键重复: " + text;
                return false;
            }
        }
    }

    UnregisterHotkeys(app);
    for (int i = 0; i < 3; ++i) {
        SetHotkeyAt(app.config, i, combos[i]);
    }
    if (!RegisterHotkeys(app, true)) {
        error = app.status_extra.empty() ? L"热键注册失败" : app.status_extra;
        UnregisterHotkeys(app);
        for (int i = 0; i < 3; ++i) {
            SetHotkeyAt(app.config, i, previous[i]);
        }
        RegisterHotkeys(app, false);
        return false;
    }

    for (int i = 0; i < 3; ++i) {
        std::wstring save_error;
        if (!sr::app::SaveHotkey(app.config_path, HotkeyName(i), combos[i], save_error)) {
            error = save_error;
            // 落盘失败：回滚已生效的热键与 INI，避免「界面生效但重启丢失」。
            UnregisterHotkeys(app);
            for (int j = 0; j < 3; ++j) {
                SetHotkeyAt(app.config, j, previous[j]);
            }
            RegisterHotkeys(app, false);
            for (int j = 0; j < i; ++j) {
                std::wstring ignored;
                sr::app::SaveHotkey(app.config_path, HotkeyName(j), previous[j], ignored);
            }
            return false;
        }
    }
    Log(L"hotkeys_applied start=%ls pause=%ls stop=%ls",
        sr::app::FormatHotkey(app.config.start).c_str(),
        sr::app::FormatHotkey(app.config.pause).c_str(),
        sr::app::FormatHotkey(app.config.stop).c_str());
    Log(L"hotkeys_saved path=%ls", app.config_path.c_str());
    error.clear();
    return true;
}

LRESULT CALLBACK SettingsProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* context = reinterpret_cast<SettingsContext*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (message) {
        case WM_CREATE: {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
            context = reinterpret_cast<SettingsContext*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(context));
            context->hwnd = hwnd;
            LayoutSettings(*context);
            SetFocus(context->edits[0]);
            return 0;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wparam);
            if (id == kSettingsFpsCombo && HIWORD(wparam) == CBN_SELCHANGE && context != nullptr) {
                const int selection =
                    static_cast<int>(SendMessageW(context->fps_combo, CB_GETCURSEL, 0, 0));
                const bool custom = selection == 5;
                EnableWindow(context->fps_edit, custom);
                if (!custom && selection >= 0 && selection < 5) {
                    const std::wstring text = std::to_wstring(kFpsPresets[selection]);
                    SetWindowTextW(context->fps_edit, text.c_str());
                }
                // M3-10#1：高级面板单独覆盖帧率后，档位显示切到「自定义」。
                if (!custom && context->preset_combo != nullptr) {
                    const auto count = static_cast<int>(sr::app::QualityPresetCount());
                    SendMessageW(context->preset_combo, CB_SETCURSEL, count - 1, 0);
                    context->app->config.quality_preset = L"自定义";
                    context->app->config.video_fps = kFpsPresets[selection];
                    RefreshPresetValues(*context);
                }
                return 0;
            }
            if (id == kSettingsPresetCombo && HIWORD(wparam) == CBN_SELCHANGE && context != nullptr) {
                // M3-10#1：切档位 -> 联动 fps / 码率 / 画质三项，并即时刷新可见文本。
                const int selection =
                    static_cast<int>(SendMessageW(context->preset_combo, CB_GETCURSEL, 0, 0));
                const auto count = static_cast<int>(sr::app::QualityPresetCount());
                if (selection >= 0 && selection < count) {
                    const std::wstring name = sr::app::QualityPresetNames()[selection];
                    sr::app::ApplyQualityPreset(name, context->app->config);
                    // 把联动后的 fps 同步到高级面板控件，保持界面一致。
                    int fps_index = 5;
                    for (int i = 0; i < 5; ++i) {
                        if (kFpsPresets[i] == context->app->config.video_fps) { fps_index = i; }
                    }
                    SendMessageW(context->fps_combo, CB_SETCURSEL, fps_index, 0);
                    EnableWindow(context->fps_edit, fps_index == 5);
                    wchar_t fps_text[16] = {};
                    std::swprintf(fps_text, std::size(fps_text), L"%u", context->app->config.video_fps);
                    SetWindowTextW(context->fps_edit, fps_index == 5 ? fps_text : L"");
                    RefreshPresetValues(*context);
                    Log(L"preset_linked name=%ls fps=%u bitrate_kbps=%u quality=%u",
                        name.c_str(), context->app->config.video_fps,
                        context->app->config.video_bitrate_kbps, context->app->config.quality);
                }
                return 0;
            }
            if (id == kSettingsCancel) {
                DestroyWindow(hwnd);
                return 0;
            }
            if (id == kSettingsOk && context != nullptr) {
                sr::app::HotkeyCombo combos[3];
                for (int i = 0; i < 3; ++i) {
                    wchar_t buffer[128] = {};
                    GetWindowTextW(context->edits[i], buffer, static_cast<int>(std::size(buffer)));
                    std::wstring parse_error;
                    if (!sr::app::ParseHotkey(buffer, combos[i], parse_error)) {
                        std::wstring text = std::wstring(HotkeyName(i)) + L": " + parse_error;
                        MessageBoxW(hwnd, text.c_str(), L"热键格式无效", MB_ICONWARNING);
                        return 0;
                    }
                }
                std::wstring error;
                if (!ApplyHotkeys(*context->app, combos, error)) {
                    MessageBoxW(hwnd, error.c_str(), L"该热键已被占用", MB_ICONWARNING);
                    return 0;
                }
                wchar_t template_buffer[512] = {};
                GetWindowTextW(context->template_edit, template_buffer,
                               static_cast<int>(std::size(template_buffer)));
                const std::wstring filename_template = TrimText(template_buffer);
                if (filename_template.empty()) {
                    MessageBoxW(hwnd, L"命名模板不能为空", L"模板无效", MB_ICONWARNING);
                    return 0;
                }
                std::wstring save_error;
                if (!sr::app::SaveTemplate(context->app->config_path, filename_template,
                                           save_error)) {
                    MessageBoxW(hwnd, save_error.c_str(), L"保存失败", MB_ICONWARNING);
                    return 0;
                }
                context->app->config.filename_template = filename_template;
                Log(L"template_saved template=%ls", filename_template.c_str());

                // M3-03：帧率六档；M3-04：CBR/VBR。保存失败不关窗口，避免「看着保存了其实没落盘」。
                const int fps_selection =
                    static_cast<int>(SendMessageW(context->fps_combo, CB_GETCURSEL, 0, 0));
                uint32_t fps = 30;
                if (fps_selection == 5) {
                    wchar_t text[16] = {};
                    GetWindowTextW(context->fps_edit, text, static_cast<int>(std::size(text)));
                    const unsigned long value = std::wcstoul(text, nullptr, 10);
                    if (value < 1 || value > 144) {
                        MessageBoxW(hwnd, L"自定义帧率必须是 1~144 的整数", L"帧率无效",
                                    MB_ICONWARNING);
                        return 0;
                    }
                    fps = static_cast<uint32_t>(value);
                } else if (fps_selection >= 0 && fps_selection < 5) {
                    fps = kFpsPresets[fps_selection];
                }
                const int rate_selection =
                    static_cast<int>(SendMessageW(context->rate_combo, CB_GETCURSEL, 0, 0));
                const std::wstring rate_mode = rate_selection == 1 ? L"vbr" : L"cbr";
                std::wstring video_error;
                if (!sr::app::SaveVideoSettings(context->app->config_path, fps, rate_mode,
                                                video_error)) {
                    MessageBoxW(hwnd, video_error.c_str(), L"保存失败", MB_ICONWARNING);
                    return 0;
                }
                context->app->config.video_fps = fps;
                context->app->config.rate_mode = rate_mode;
                Log(L"video_saved fps=%u rate_mode=%ls", fps, rate_mode.c_str());

                // M3-10：档位与联动值一起落盘（写入后回读校验），供重启后仍可见。
                const int preset_selection =
                    static_cast<int>(SendMessageW(context->preset_combo, CB_GETCURSEL, 0, 0));
                const auto preset_count = static_cast<int>(sr::app::QualityPresetCount());
                const std::wstring preset_name = (preset_selection >= 0 && preset_selection < preset_count)
                    ? sr::app::QualityPresetNames()[preset_selection] : L"自定义";
                std::wstring preset_error;
                if (!sr::app::SaveQualityPreset(context->app->config_path, preset_name, fps,
                                                context->app->config.video_bitrate_kbps,
                                                context->app->config.quality, preset_error)) {
                    MessageBoxW(hwnd, preset_error.c_str(), L"保存失败", MB_ICONWARNING);
                    return 0;
                }
                context->app->config.quality_preset = preset_name;
                Log(L"preset_saved name=%ls fps=%u quality=%u", preset_name.c_str(), fps,
                    context->app->config.quality);
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

void ShowSettings(AppState& app) {
    if (app.settings_open || app.recorder.Active()) {
        // 录制中不允许改热键；避免命令与重新注册交叉。
        MessageBoxW(app.hwnd, L"请先停止录制，再修改热键。", kAppTitle, MB_ICONINFORMATION);
        return;
    }
    app.settings_open = true;
    EnableWindow(app.hwnd, FALSE);

    auto context = std::make_unique<SettingsContext>();
    context->app = &app;

    RECT parent{};
    GetWindowRect(app.hwnd, &parent);
    const int width = 440;
    const int height = 570;   // 轮 42 M3-10：为画质档位与联动值两行留出空间
    const int x = parent.left + ((parent.right - parent.left) - width) / 2;
    const int y = parent.top + ((parent.bottom - parent.top) - height) / 2;

    HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME, kSettingsClass, L"设置",
                                  WS_CAPTION | WS_SYSMENU | WS_POPUP,
                                  x, y, width, height, app.hwnd, nullptr, app.instance, context.get());
    if (dialog == nullptr) {
        app.settings_open = false;
        EnableWindow(app.hwnd, TRUE);
        return;
    }
    // M3-05：设置页首次打开时拉一次编码器列表（探测在这里做，不在录制启动路径上）。
    // 规范单行由 recorder 产出、这里原样复制到 UI 日志 → 两边逐字一致。
    {
        std::wstring canonical;
        std::vector<std::wstring> encoder_rows;
        std::wstring encoder_error;
        if (RunRecorderEncoderList(app, canonical, encoder_rows, encoder_error)) {
            // canonical 就是 recorder 的整行 `encoders=[...]`，原样落日志（不再包一层）。
            Log(L"%ls", canonical.c_str());
            Log(L"encoder_rows=%zu", encoder_rows.size());
            for (const std::wstring& row : encoder_rows) {
                SendMessageW(context->encoder_list, LB_ADDSTRING, 0,
                             reinterpret_cast<LPARAM>(row.c_str()));
            }
        } else {
            Log(L"encoder_list_failed error=%ls", encoder_error.c_str());
            const std::wstring text = L"编码器列表获取失败：" + encoder_error;
            SendMessageW(context->encoder_list, LB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(text.c_str()));
        }
    }
    ShowWindow(dialog, SW_SHOW);
    SetForegroundWindow(dialog);

    MSG message;
    while (IsWindow(dialog) && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dialog, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    app.settings_open = false;
    EnableWindow(app.hwnd, TRUE);
    SetForegroundWindow(app.hwnd);
}

// ---------------- 配置热更新 ----------------

void ReloadConfigIfChanged(AppState& app) {
    if (app.settings_open) {
        return;  // 设置窗口自己负责注册，避免双写交叉
    }
    FILETIME current{};
    if (!ReadConfigWriteTime(app.config_path, current)) {
        return;
    }
    if (app.have_config_write && CompareFileTime(&current, &app.config_write) == 0) {
        return;
    }
    app.config_write = current;
    app.have_config_write = true;

    sr::app::AppConfig loaded;
    std::wstring error;
    if (!sr::app::LoadConfig(app.config_path, loaded, error)) {
        Log(L"config_reload_failed %ls", error.c_str());
        app.status_extra = error;
        UpdateUi(app);
        return;
    }
    if (!app.recorder_override.empty()) {
        loaded.recorder_path = app.recorder_override;
    }
    const bool hotkeys_changed = !(loaded.start == app.config.start) ||
                                 !(loaded.pause == app.config.pause) ||
                                 !(loaded.stop == app.config.stop);
    app.config = loaded;
    if (hotkeys_changed) {
        UnregisterHotkeys(app);
        RegisterHotkeys(app, true);
        Log(L"hotkeys_reloaded start=%ls pause=%ls stop=%ls",
            sr::app::FormatHotkey(app.config.start).c_str(),
            sr::app::FormatHotkey(app.config.pause).c_str(),
            sr::app::FormatHotkey(app.config.stop).c_str());
    } else {
        Log(L"config_reloaded");
    }
    UpdateUi(app);
}

// ---------------- M5-06：录制文件列表 ----------------

std::wstring WidenUtf8(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int chars = MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                          static_cast<int>(text.size()), nullptr, 0);
    if (chars <= 0) {
        return {};
    }
    std::wstring out(static_cast<size_t>(chars), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), chars);
    return out;
}

bool ParseListOutput(const std::string& text, std::vector<ListRow>& rows) {
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) {
            end = text.size();
        }
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        // recorder 的 stdout 是文本模式，行尾带 \r；不剪掉会污染 name 字段。
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.rfind("entry=", 0) != 0) {
            continue;
        }
        ListRow row;
        size_t field = 6;
        while (field <= line.size()) {
            const size_t tab = line.find('\t', field);
            const std::string token =
                line.substr(field, tab == std::string::npos ? std::string::npos : tab - field);
            const size_t eq = token.find('=');
            if (eq != std::string::npos) {
                const std::string key = token.substr(0, eq);
                const std::string value = token.substr(eq + 1);
                // name 放最后且可能含 Tab/分号：直接取本 token 起始之后的全部内容。
                if (key == "name") {
                    const size_t name_start = field + eq + 1;
                    row.name = WidenUtf8(line.substr(name_start));
                    break;
                }
                if (key == "state") {
                    row.state = WidenUtf8(value);
                } else if (key == "size") {
                    row.size = _strtoui64(value.c_str(), nullptr, 10);
                } else if (key == "mtime_ms") {
                    row.mtime_ms = _strtoi64(value.c_str(), nullptr, 10);
                } else if (key == "duration_ms") {
                    row.duration_ms = _strtoi64(value.c_str(), nullptr, 10);
                } else if (key == "width") {
                    row.width = static_cast<uint32_t>(std::strtoul(value.c_str(), nullptr, 10));
                } else if (key == "height") {
                    row.height = static_cast<uint32_t>(std::strtoul(value.c_str(), nullptr, 10));
                }
            }
            if (tab == std::string::npos) {
                break;
            }
            field = tab + 1;
        }
        rows.push_back(row);
    }
    return true;
}

bool RunRecorderList(AppState& app,
                     std::vector<ListRow>& rows,
                     std::wstring& error) {
    rows.clear();
    wchar_t temp_dir[MAX_PATH] = {};
    if (GetTempPathW(MAX_PATH, temp_dir) == 0) {
        error = L"cannot resolve temp directory";
        return false;
    }
    wchar_t temp_file[MAX_PATH] = {};
    if (GetTempFileNameW(temp_dir, L"sr", 0, temp_file) == 0) {
        error = L"cannot create temp file";
        return false;
    }
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;
    HANDLE output = CreateFileW(temp_file, GENERIC_WRITE | GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, &attributes, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) {
        DeleteFileW(temp_file);
        error = L"cannot open temp file";
        return false;
    }

    std::wstring command = L"\"" + app.config.recorder_path + L"\" --list \"" +
                           app.config.output_dir + L"\"";
    // 录制中的 .part 由实时快照补一行，这里按前缀跳过，避免反复扫描增长中的文件。
    if (app.recorder.Active() && !app.last_output.empty()) {
        command += L" --list-skip \"" + app.last_output + L"\"";
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdOutput = output;
    startup.hStdError = output;
    startup.hStdInput = nullptr;
    PROCESS_INFORMATION process{};
    std::wstring mutable_command = command;
    const BOOL started = CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
    CloseHandle(output);
    if (!started) {
        DeleteFileW(temp_file);
        error = L"cannot run sr-recorder --list (error=" + std::to_wstring(GetLastError()) + L")";
        return false;
    }
    WaitForSingleObject(process.hProcess, 20000);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

    std::string text;
    {
        std::ifstream stream(std::filesystem::path(temp_file), std::ios::binary);
        if (stream) {
            text.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
        }
    }
    DeleteFileW(temp_file);
    if (text.empty()) {
        error = L"empty list output";
        return false;
    }
    ParseListOutput(text, rows);
    return true;
}

// M3-05：向 recorder 索取编码器列表（探测只在这里做，不在录制启动路径上）。
//
// 「日志 encoders=[...] 与 UI 列表逐项一致」的做法：规范单行由 recorder 产出
// （`sr-recorder --list-encoders` 的 `encoders=[...]` 行），UI **原样复制**到自己的日志，
// 不在 UI 侧重新拼一遍格式——两边格式不一致的可能性从构造上就不存在。
bool RunRecorderEncoderList(AppState& app,
                             std::wstring& canonical,
                             std::vector<std::wstring>& rows,
                             std::wstring& error) {
    rows.clear();
    canonical.clear();
    wchar_t temp_dir[MAX_PATH] = {};
    if (GetTempPathW(MAX_PATH, temp_dir) == 0) {
        error = L"cannot resolve temp directory";
        return false;
    }
    wchar_t temp_file[MAX_PATH] = {};
    if (GetTempFileNameW(temp_dir, L"sr", 0, temp_file) == 0) {
        error = L"cannot create temp file";
        return false;
    }
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;
    HANDLE output = CreateFileW(temp_file, GENERIC_WRITE | GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, &attributes, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) {
        DeleteFileW(temp_file);
        error = L"cannot open temp file";
        return false;
    }

    std::wstring command = L"\"" + app.config.recorder_path + L"\" --list-encoders";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdOutput = output;
    startup.hStdError = output;
    startup.hStdInput = nullptr;
    PROCESS_INFORMATION process{};
    std::wstring mutable_command = command;
    const BOOL started = CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
    CloseHandle(output);
    if (!started) {
        DeleteFileW(temp_file);
        error = L"cannot run sr-recorder --list-encoders (error=" +
                std::to_wstring(GetLastError()) + L")";
        return false;
    }
    WaitForSingleObject(process.hProcess, 20000);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

    std::string text;
    {
        std::ifstream stream(std::filesystem::path(temp_file), std::ios::binary);
        if (stream) {
            text.assign((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        }
    }
    DeleteFileW(temp_file);

    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) {
            end = text.size();
        }
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        if (line.rfind("encoders=[", 0) == 0) {
            canonical = WidenUtf8(line);
        } else if (line.rfind("encoder=", 0) == 0) {
            // 字段用 \t 分隔：id / name / hardware / available / reason
            std::string id;
            std::string name;
            std::string available;
            std::string reason;
            size_t field = 0;
            size_t tab = 0;
            while (field <= line.size()) {
                tab = line.find('\t', field);
                const std::string piece =
                    line.substr(field, tab == std::string::npos ? std::string::npos : tab - field);
                if (piece.rfind("id=", 0) == 0) {
                    id = piece.substr(3);
                } else if (piece.rfind("name=", 0) == 0) {
                    name = piece.substr(5);
                } else if (piece.rfind("available=", 0) == 0) {
                    available = piece.substr(10);
                } else if (piece.rfind("reason=", 0) == 0) {
                    reason = piece.substr(7);
                }
                if (tab == std::string::npos) {
                    break;
                }
                field = tab + 1;
            }
            std::wstring line_text = WidenUtf8(name);
            if (available == "1") {
                line_text += L"（可用）";
            } else {
                line_text += L"（不可用：";
                line_text += WidenUtf8(reason);
                line_text += L"）";
            }
            rows.push_back(line_text);
        }
        if (end == text.size()) {
            break;
        }
    }
    if (canonical.empty()) {
        error = L"sr-recorder --list-encoders 未返回 encoders=[...]";
        return false;
    }
    return true;
}

std::wstring FormatBytes(uint64_t bytes) {
    wchar_t buffer[64] = {};
    if (bytes >= 1024ull * 1024ull * 1024ull) {
        std::swprintf(buffer, std::size(buffer), L"%.2f GB",
                      static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
    } else if (bytes >= 1024ull * 1024ull) {
        std::swprintf(buffer, std::size(buffer), L"%.1f MB",
                      static_cast<double>(bytes) / (1024.0 * 1024.0));
    } else {
        std::swprintf(buffer, std::size(buffer), L"%llu KB",
                      static_cast<unsigned long long>(bytes / 1024ull));
    }
    return buffer;
}

std::wstring FormatDurationMs(int64_t ms) {
    if (ms <= 0) {
        return L"--:--";
    }
    const int64_t total_seconds = ms / 1000;
    wchar_t buffer[32] = {};
    if (total_seconds >= 3600) {
        std::swprintf(buffer, std::size(buffer), L"%lld:%02lld:%02lld",
                      static_cast<long long>(total_seconds / 3600),
                      static_cast<long long>((total_seconds / 60) % 60),
                      static_cast<long long>(total_seconds % 60));
    } else {
        std::swprintf(buffer, std::size(buffer), L"%02lld:%02lld",
                      static_cast<long long>(total_seconds / 60),
                      static_cast<long long>(total_seconds % 60));
    }
    return buffer;
}

std::wstring FormatFileTimeMs(int64_t unix_ms) {
    if (unix_ms <= 0) {
        return L"--";
    }
    ULARGE_INTEGER value{};
    value.QuadPart = static_cast<ULONGLONG>(unix_ms + 11644473600000LL) * 10000ull;
    FILETIME file_time{};
    file_time.dwLowDateTime = value.LowPart;
    file_time.dwHighDateTime = value.HighPart;
    SYSTEMTIME local{};
    if (!FileTimeToSystemTime(&file_time, &local)) {
        return L"--";
    }
    SYSTEMTIME display{};
    if (!SystemTimeToTzSpecificLocalTime(nullptr, &local, &display)) {
        display = local;
    }
    wchar_t buffer[32] = {};
    std::swprintf(buffer, std::size(buffer), L"%04u-%02u-%02u %02u:%02u", display.wYear,
                  display.wMonth, display.wDay, display.wHour, display.wMinute);
    return buffer;
}

std::wstring ListStateText(const std::wstring& state) {
    if (state == L"recording") {
        return L"录制中";
    }
    if (state == L"recoverable") {
        return L"可恢复";
    }
    if (state == L"broken") {
        return L"不可恢复";
    }
    return L"已完成";
}

constexpr int kListColumns = 6;

void FillListRow(HWND list, int index, const ListRow& row) {
    const std::wstring cells[kListColumns] = {
        row.name,
        FormatDurationMs(row.duration_ms),
        row.size > 0 ? FormatBytes(row.size) : std::wstring(L"--"),
        (row.width > 0 && row.height > 0)
            ? std::to_wstring(row.width) + L"×" + std::to_wstring(row.height)
            : std::wstring(L"--"),
        FormatFileTimeMs(row.mtime_ms),
        ListStateText(row.state)};
    for (int column = 0; column < kListColumns; ++column) {
        if (column == 0) {
            LVITEMW item{};
            item.mask = LVIF_TEXT;
            item.iItem = index;
            item.iSubItem = 0;
            item.pszText = const_cast<wchar_t*>(cells[column].c_str());
            ListView_InsertItem(list, &item);
        } else {
            ListView_SetItemText(list, index, column,
                                 const_cast<wchar_t*>(cells[column].c_str()));
        }
    }
}

void RefreshRecordingList(AppState& app) {
    if (app.list_window == nullptr) {
        return;
    }
    HWND list = GetDlgItem(app.list_window, kListView);
    if (list == nullptr) {
        return;
    }
    std::vector<ListRow> rows;
    std::wstring error;
    const bool ok = RunRecorderList(app, rows, error);
    ListView_DeleteAllItems(list);

    // 录制中的条目：文件还没落地，时长取 recorder 的媒体时间（暂停冻结）。
    const bool active = app.recorder.Active();
    int index = 0;
    int64_t live_ms = 0;
    if (active) {
        ListRow live;
        live.name = std::filesystem::path(app.last_output).filename().wstring() + L".part";
        live.state = L"recording";
        live.duration_ms = static_cast<int64_t>(app.recorder.Snapshot().session_elapsed_ms);
        live_ms = live.duration_ms;
        FillListRow(list, index++, live);
        Log(L"list_entry idx=0 name=%ls state=recording size=0 duration_ms=%lld w=0 h=0",
            live.name.c_str(), static_cast<long long>(live.duration_ms));
        Log(L"list_live name=%ls duration_ms=%lld", live.name.c_str(),
            static_cast<long long>(live.duration_ms));
    }
    for (const ListRow& row : rows) {
        FillListRow(list, index++, row);
        Log(L"list_entry idx=%d name=%ls state=%ls size=%llu duration_ms=%lld w=%u h=%u",
            index - 1, row.name.c_str(), row.state.c_str(),
            static_cast<unsigned long long>(row.size), static_cast<long long>(row.duration_ms),
            row.width, row.height);
    }
    app.list_rows = index;
    Log(L"list_reload ok=%d count=%d active=%d live_ms=%lld error=%ls", ok ? 1 : 0,
        index, active ? 1 : 0, static_cast<long long>(live_ms), error.c_str());
}

void UpdateRecordingListLive(AppState& app) {
    if (app.list_window == nullptr || !app.recorder.Active()) {
        return;
    }
    HWND list = GetDlgItem(app.list_window, kListView);
    if (list == nullptr) {
        return;
    }
    const int64_t elapsed = static_cast<int64_t>(app.recorder.Snapshot().session_elapsed_ms);
    const std::wstring text = FormatDurationMs(elapsed);
    ListView_SetItemText(list, 0, 1, const_cast<wchar_t*>(text.c_str()));
    const std::wstring name =
        std::filesystem::path(app.last_output).filename().wstring() + L".part";
    Log(L"list_live name=%ls duration_ms=%lld", name.c_str(), static_cast<long long>(elapsed));
}

// ---------------- M5-07：文件操作 ----------------

std::vector<std::wstring> SplitListNames(const std::wstring& text) {
    std::vector<std::wstring> names;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t end = text.find(L';', start);
        std::wstring item =
            text.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
        const size_t first = item.find_first_not_of(L" \t\r\n");
        const size_t last = item.find_last_not_of(L" \t\r\n");
        if (first != std::wstring::npos) {
            item = item.substr(first, last - first + 1);
            if (!item.empty()) {
                names.push_back(item);
            }
        }
        if (end == std::wstring::npos) {
            break;
        }
        start = end + 1;
    }
    return names;
}

void PerformRename(AppState& app, const std::wstring& old_name, const std::wstring& new_name) {
    std::wstring path;
    std::wstring error;
    const bool ok = sr::app::RenameRecording(app.config.output_dir, old_name, new_name, path, error);
    Log(L"file_op op=rename ok=%d old=%ls new=%ls path=%ls error=%ls", ok ? 1 : 0,
        old_name.c_str(), new_name.c_str(), ok ? path.c_str() : L"", error.c_str());
    if (ok) {
        RefreshRecordingList(app);
    }
}

void PerformDelete(AppState& app,
                   const std::vector<std::wstring>& names,
                   bool confirmed) {
    if (names.empty()) {
        Log(L"file_op op=delete ok=0 count=0 confirm=%d error=empty", confirmed ? 1 : 0);
        return;
    }
    if (!confirmed) {
        // 批量删除的二次确认被取消：不改磁盘，只留日志供验收断言。
        Log(L"file_op op=delete ok=0 count=%zu confirm=0", names.size());
        return;
    }
    std::vector<std::wstring> paths;
    paths.reserve(names.size());
    for (const std::wstring& name : names) {
        paths.push_back((std::filesystem::path(app.config.output_dir) / name).wstring());
    }
    std::wstring error;
    const bool ok = sr::app::DeleteToRecycleBin(paths, error);
    Log(L"file_op op=delete ok=%d count=%zu confirm=1 recycle=%d error=%ls", ok ? 1 : 0,
        names.size(), ok ? 1 : 0, error.c_str());
    if (ok) {
        RefreshRecordingList(app);
    }
}

void RunPendingListOp(AppState& app) {
    app.list_op_done = true;
    if (app.list_op == L"rename") {
        PerformRename(app, app.list_from, app.list_to);
    } else if (app.list_op == L"delete") {
        PerformDelete(app, SplitListNames(app.list_from), app.list_confirm_yes);
    } else {
        Log(L"file_op op=%ls ok=0 error=unknown_op", app.list_op.c_str());
    }
}

std::vector<std::wstring> SelectedListNames(HWND list) {
    std::vector<std::wstring> names;
    for (int index = ListView_GetNextItem(list, -1, LVNI_SELECTED); index >= 0;
         index = ListView_GetNextItem(list, index, LVNI_SELECTED)) {
        wchar_t text[MAX_PATH] = {};
        ListView_GetItemText(list, index, 0, text, static_cast<int>(std::size(text)));
        names.push_back(text);
    }
    return names;
}

void ShowListContextMenu(AppState& app, int x, int y) {
    HWND list = GetDlgItem(app.list_window, kListView);
    if (list == nullptr) {
        return;
    }
    const std::vector<std::wstring> names = SelectedListNames(list);
    if (names.empty()) {
        return;
    }
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, L"重命名");
    AppendMenuW(menu, MF_STRING, 2, L"删除到回收站");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 3, L"打开所在目录");
    AppendMenuW(menu, MF_STRING, 4, L"复制路径");
    const int command =
        TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, x, y, 0, app.list_window, nullptr);
    DestroyMenu(menu);
    if (command == 0) {
        return;
    }
    const std::wstring path =
        (std::filesystem::path(app.config.output_dir) / names.front()).wstring();
    if (command == 1) {
        if (names.size() != 1) {
            MessageBoxW(app.list_window, L"重命名一次只能选一个文件", L"重命名", MB_ICONINFORMATION);
            return;
        }
        ListView_EditLabel(list, ListView_GetNextItem(list, -1, LVNI_SELECTED));
        return;
    }
    if (command == 2) {
        const std::wstring prompt = names.size() > 1
                                        ? L"确认把这 " + std::to_wstring(names.size()) +
                                              L" 个文件删除到回收站？"
                                        : L"确认把 " + names.front() + L" 删除到回收站？";
        if (MessageBoxW(app.list_window, prompt.c_str(), L"删除确认",
                        MB_ICONQUESTION | MB_YESNO) == IDYES) {
            PerformDelete(app, names, true);
        } else {
            PerformDelete(app, names, false);
        }
        return;
    }
    if (command == 3) {
        std::wstring error;
        if (!sr::app::RevealInExplorer(path, error)) {
            MessageBoxW(app.list_window, error.c_str(), L"打开失败", MB_ICONWARNING);
        }
        return;
    }
    if (command == 4) {
        std::wstring error;
        if (sr::app::CopyPathToClipboard(path, error)) {
            Log(L"file_op op=copy_path ok=1 path=%ls", path.c_str());
        } else {
            Log(L"file_op op=copy_path ok=0 error=%ls", error.c_str());
        }
    }
}

LRESULT CALLBACK ListProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    AppState* app = g_app;
    switch (message) {
        case WM_CREATE: {
            HWND list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS |
                                            LVS_EDITLABELS,
                                        12, 12, 736, 400, hwnd, ControlId(kListView),
                                        app->instance, nullptr);
            ListView_SetExtendedListViewStyle(list,
                                              LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
            const wchar_t* headers[kListColumns] = {L"文件名", L"时长", L"大小",
                                                    L"分辨率", L"录制时间", L"状态"};
            const int widths[kListColumns] = {300, 90, 100, 100, 150, 90};
            for (int column = 0; column < kListColumns; ++column) {
                LVCOLUMNW info{};
                info.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
                info.pszText = const_cast<wchar_t*>(headers[column]);
                info.cx = widths[column];
                info.iSubItem = column;
                ListView_InsertColumn(list, column, &info);
            }
            app->list_window = hwnd;
            app->list_next_reload = 0;
            RefreshRecordingList(*app);
            if (!app->list_op.empty() && !app->list_op_done) {
                RunPendingListOp(*app);
            }
            SetTimer(hwnd, kListTimer, 1000, nullptr);
            Log(L"list_open dir=%ls", app->config.output_dir.c_str());
            return 0;
        }
        case WM_APP + 1: {
            if (app != nullptr && !app->pending_rename_old.empty()) {
                PerformRename(*app, app->pending_rename_old, app->pending_rename_new);
                app->pending_rename_old.clear();
                app->pending_rename_new.clear();
            }
            return 0;
        }
        case WM_CONTEXTMENU: {
            HWND list = GetDlgItem(hwnd, kListView);
            if (reinterpret_cast<HWND>(wparam) == list) {
                POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
                if (point.x == -1 && point.y == -1) {
                    GetCursorPos(&point);
                }
                ShowListContextMenu(*app, point.x, point.y);
                return 0;
            }
            break;
        }
        case WM_NOTIFY: {
            const auto* header = reinterpret_cast<NMHDR*>(lparam);
            if (header != nullptr && header->idFrom == kListView &&
                header->code == LVN_ENDLABELEDIT) {
                auto* display = reinterpret_cast<NMLVDISPINFOW*>(lparam);
                if (display->item.pszText == nullptr) {
                    return TRUE;  // 用户取消
                }
                HWND list = GetDlgItem(hwnd, kListView);
                wchar_t old_name[MAX_PATH] = {};
                ListView_GetItemText(list, display->item.iItem, 0, old_name,
                                     static_cast<int>(std::size(old_name)));
                const std::wstring new_name = display->item.pszText;
                if (new_name != old_name) {
                    // 在通知里刷新 ListView 会踩到控件自己的编辑状态；转成异步消息再改。
                    app->pending_rename_old = old_name;
                    app->pending_rename_new = new_name;
                    PostMessageW(hwnd, WM_APP + 1, 0, 0);
                }
                return FALSE;  // 不直接改 label，由刷新后的真实列表决定
            }
            break;
        }
        case WM_TIMER: {
            if (wparam == kListTimer) {
                const ULONGLONG now = GetTickCount64();
                if (now >= app->list_next_reload) {
                    app->list_next_reload = now + (app->recorder.Active() ? 5000 : 3000);
                    RefreshRecordingList(*app);
                } else {
                    UpdateRecordingListLive(*app);
                }
                return 0;
            }
            break;
        }
        case WM_SIZE: {
            HWND list = GetDlgItem(hwnd, kListView);
            if (list != nullptr) {
                RECT client{};
                GetClientRect(hwnd, &client);
                SetWindowPos(list, nullptr, 12, 12, client.right - 24, client.bottom - 24,
                             SWP_NOZORDER);
            }
            return 0;
        }
        case WM_CLOSE: {
            KillTimer(hwnd, kListTimer);
            app->list_window = nullptr;
            DestroyWindow(hwnd);
            return 0;
        }
        case WM_DESTROY: {
            app->list_window = nullptr;
            return 0;
        }
        default:
            break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

void ShowRecordingList(AppState& app) {
    if (app.list_window != nullptr) {
        ShowWindow(app.list_window, SW_SHOW);
        SetForegroundWindow(app.list_window);
        RefreshRecordingList(app);
        return;
    }
    HWND window = CreateWindowExW(WS_EX_TOOLWINDOW, kListClass, L"录制文件列表",
                                  WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX, CW_USEDEFAULT,
                                  CW_USEDEFAULT, 780, 470, app.hwnd, nullptr, app.instance,
                                  nullptr);
    if (window == nullptr) {
        Log(L"list_open_failed error=%lu", GetLastError());
        return;
    }
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
}

// ---------------- 主窗口 ----------------

LRESULT CALLBACK MainProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    AppState* app = g_app;
    switch (message) {
        case WM_CREATE: {
            app->hwnd = hwnd;
            HWND status = CreateWindowExW(0, L"STATIC", L"就绪", WS_CHILD | WS_VISIBLE | SS_LEFT,
                                         16, 14, 470, 46, hwnd,
                                         ControlId(kStatusId), app->instance, nullptr);
            SendMessageW(status, WM_SETFONT,
                         reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
            const wchar_t* labels[3] = {L"开始录制", L"暂停", L"停止"};
            const int ids[3] = {kStartButton, kPauseButton, kStopButton};
            for (int i = 0; i < 3; ++i) {
                HWND button = CreateWindowExW(0, L"BUTTON", labels[i],
                                              WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                              16 + i * 120, 78, 110, 32, hwnd,
                                              ControlId(ids[i]), app->instance, nullptr);
                SendMessageW(button, WM_SETFONT,
                             reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
            }
            HWND hide = CreateWindowExW(0, L"BUTTON", L"最小化到托盘",
                                        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                        376, 78, 110, 32, hwnd,
                                        ControlId(kHideButton), app->instance, nullptr);
            SendMessageW(hide, WM_SETFONT,
                         reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);

            const wchar_t* tools[4] = {L"保存路径…", L"设置…", L"区域…", L"录制列表…"};
            const int tool_ids[4] = {kSavePathButton, kSettingsButton, kRegionButton, kListButton};
            for (int i = 0; i < 4; ++i) {
                HWND button = CreateWindowExW(0, L"BUTTON", tools[i],
                                              WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                              16 + i * 120, 120, 110, 28, hwnd,
                                              ControlId(tool_ids[i]), app->instance, nullptr);
                SendMessageW(button, WM_SETFONT,
                             reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
            }

            // M1-27：提权限制常驻提示 + 「以管理员身份重启」入口。
            // 提示常驻可见 → 开录前与录制中都在（状态栏只有两行，塞不下第三条）。
            HWND warn = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | SS_LEFT,
                                         16, 152, 336, 32, hwnd,
                                         ControlId(kUacWarnId), app->instance, nullptr);
            SendMessageW(warn, WM_SETFONT,
                         reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
            HWND elevate = CreateWindowExW(0, L"BUTTON", L"以管理员身份重启",
                                           WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                           360, 152, 130, 28, hwnd,
                                           ControlId(kElevateButton), app->instance, nullptr);
            SendMessageW(elevate, WM_SETFONT,
                         reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);

            app->icon_idle = LoadIconW(nullptr, IDI_APPLICATION);
            // M7-13#1：录制/暂停态用自绘「带红点」图标，而非系统信息图标。
            app->icon_active = MakeRedDotIcon();
            if (app->icon_active == nullptr) {
                app->icon_active = LoadIconW(nullptr, IDI_INFORMATION);
            }
            app->border_hwnd = CreateBorderWindow(app->instance);
            UpdateTray(*app, L"Screen Recorder — 就绪", app->icon_idle);
            RegisterHotkeys(*app, true);
            SetTimer(hwnd, kPollTimer, 50, nullptr);
            if (app->exit_timeout_ms > 0) {
                SetTimer(hwnd, kExitTimer, app->exit_timeout_ms, nullptr);
            }
            RefreshDiskRemaining(*app);
            app->next_disk_check = GetTickCount64() + 1000;
            Log(L"app_ready pid=%lu", static_cast<unsigned long>(GetCurrentProcessId()));
            UpdateUi(*app);
            if (app->show_list_on_start) {
                // 测试钩子：启动即打开列表，验收脚本不必模拟点击。
                PostMessageW(hwnd, WM_COMMAND, static_cast<WPARAM>(kListButton), 0);
            }
            if (app->show_settings_on_start) {
                // 测试钩子（M3-05）：启动即打开设置页，验收脚本直接读编码器列表。
                PostMessageW(hwnd, WM_COMMAND, static_cast<WPARAM>(kSettingsButton), 0);
            }
            return 0;
        }
        case WM_COMMAND: {
            switch (LOWORD(wparam)) {
                case kStartButton: StartRecording(*app); break;
                case kPauseButton: PauseResume(*app); break;
                case kStopButton: StopRecording(*app); break;
                case kHideButton:
                    ShowWindow(hwnd, SW_HIDE);
                    Log(L"minimized_to_tray");
                    break;
                case kSavePathButton: BrowseOutputDir(*app); break;
                case kSettingsButton: ShowSettings(*app); break;
                case kRegionButton: ShowRegionEditor(*app); break;
                case kListButton: ShowRecordingList(*app); break;
                case kTrayList: ShowRecordingList(*app); break;
                case kElevateButton: RestartAsAdmin(*app); break;
                case kTrayToolbar: ToggleToolbar(*app); break;
                case kTrayElevate: RestartAsAdmin(*app); break;
                default: break;
            }
            return 0;
        }
        case WM_HOTKEY: {
            // 轮 35 / F2：标题可能为空（桌面、无标题窗口），不能作为「是否失焦」的判据。
            // 同时记录 HWND，验收用 `foreground_hwnd != 主窗口 HWND` 判定；标题只作日志。
            const HWND foreground_hwnd = GetForegroundWindow();
            wchar_t foreground[256] = {};
            GetWindowTextW(foreground_hwnd, foreground, static_cast<int>(std::size(foreground)));
            Log(L"hotkey id=%llu foreground=%ls foreground_hwnd=%llu",
                static_cast<unsigned long long>(wparam), foreground,
                static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(foreground_hwnd)));
            if (wparam == kHotkeyStart) {
                StartRecording(*app);
            } else if (wparam == kHotkeyPause) {
                PauseResume(*app);
            } else if (wparam == kHotkeyStop) {
                StopRecording(*app);
            }
            return 0;
        }
        case WM_TIMER: {
            if (wparam == kPollTimer) {
                app->recorder.Poll();
                if (GetTickCount64() >= app->next_config_check) {
                    app->next_config_check = GetTickCount64() + 1000;
                    ReloadConfigIfChanged(*app);
                }
                if (GetTickCount64() >= app->next_disk_check) {
                    app->next_disk_check = GetTickCount64() + 1000;
                    RefreshDiskRemaining(*app);
                }
                if (app->session_active && !app->recorder.Active()) {
                    app->session_active = false;
                    ShowToolbar(*app, false);
                    const DWORD code = app->recorder.ExitCode();
                    const bool exists = std::filesystem::exists(app->last_output);
                    Log(L"session_end exit=%lu output_exists=%d out=%ls", code, exists ? 1 : 0,
                        app->last_output.c_str());
                    app->exit_code = (code == 0 && exists) ? 0 : 1;
                    if (app->exit_on_stop) {
                        Log(L"auto_exit code=%d", app->exit_code);
                        PostMessageW(hwnd, WM_CLOSE, 0, 0);
                    }
                }
                UpdateUi(*app);
                return 0;
            }
            if (wparam == kPreviewTimer) {
                KillTimer(hwnd, kPreviewTimer);
                Log(L"preview_tray_menu");
                ShowTrayMenu(*app);
                return 0;
            }
            if (wparam == kExitTimer) {
                KillTimer(hwnd, kExitTimer);
                Log(L"exit_timer_fired");
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
                return 0;
            }
            break;
        }
        case kTrayMessage: {
            const UINT notification = LOWORD(lparam);
            if (notification == WM_RBUTTONUP || notification == WM_CONTEXTMENU) {
                ShowTrayMenu(*app);
            } else if (notification == WM_LBUTTONDBLCLK) {
                ShowMainWindow(*app);
            }
            return 0;
        }
        case WM_SIZE: {
            if (wparam == SIZE_MINIMIZED) {
                ShowWindow(hwnd, SW_HIDE);
                Log(L"minimized_to_tray");
            }
            return 0;
        }
        case WM_CLOSE: {
            if (app->recorder.Active()) {
                app->recorder.Shutdown();
                const DWORD code = app->recorder.ExitCode();
                const bool exists = std::filesystem::exists(app->last_output);
                app->exit_code = (code == 0 && exists) ? 0 : 1;
                Log(L"shutdown_done exit=%lu output_exists=%d", code, exists ? 1 : 0);
            }
            RemoveTray(*app);
            if (app->region_overlay != nullptr) {
                DestroyWindow(app->region_overlay);
            }
            if (app->region_editor != nullptr) {
                DestroyWindow(app->region_editor);
            }
            if (app->toolbar != nullptr) {
                DestroyWindow(app->toolbar);
            }
            DestroyWindow(hwnd);
            return 0;
        }
        case WM_DESTROY: {
            KillTimer(hwnd, kPollTimer);
            UnregisterHotkeys(*app);
            RemoveTray(*app);
            if (app->region_overlay != nullptr) {
                DestroyWindow(app->region_overlay);
            }
            if (app->region_editor != nullptr) {
                DestroyWindow(app->region_editor);
            }
            if (app->toolbar != nullptr) {
                DestroyWindow(app->toolbar);
            }
            Log(L"app_exit code=%d", app->exit_code);
            PostQuitMessage(app->exit_code);
            return 0;
        }
        default:
            break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

bool RegisterClasses(HINSTANCE instance) {
    WNDCLASSEXW main_class{};
    main_class.cbSize = sizeof(main_class);
    main_class.lpfnWndProc = MainProc;
    main_class.hInstance = instance;
    main_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    main_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    main_class.lpszClassName = kWindowClass;
    main_class.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    main_class.hIconSm = main_class.hIcon;
    if (RegisterClassExW(&main_class) == 0) {
        return false;
    }

    WNDCLASSEXW settings_class{};
    settings_class.cbSize = sizeof(settings_class);
    settings_class.lpfnWndProc = SettingsProc;
    settings_class.hInstance = instance;
    settings_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    settings_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    settings_class.lpszClassName = kSettingsClass;
    if (RegisterClassExW(&settings_class) == 0) {
        return false;
    }

    WNDCLASSEXW toolbar_class{};
    toolbar_class.cbSize = sizeof(toolbar_class);
    toolbar_class.lpfnWndProc = ToolbarProc;
    toolbar_class.hInstance = instance;
    toolbar_class.hCursor = LoadCursorW(nullptr, IDC_SIZEALL);
    toolbar_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    toolbar_class.lpszClassName = kToolbarClass;
    if (RegisterClassExW(&toolbar_class) == 0) {
        return false;
    }

    WNDCLASSEXW overlay_class{};
    overlay_class.cbSize = sizeof(overlay_class);
    overlay_class.lpfnWndProc = RegionOverlayProc;
    overlay_class.hInstance = instance;
    overlay_class.hCursor = LoadCursorW(nullptr, IDC_CROSS);
    overlay_class.hbrBackground = nullptr;  // WM_PAINT 自绘
    overlay_class.lpszClassName = kRegionOverlayClass;
    if (RegisterClassExW(&overlay_class) == 0) {
        return false;
    }

    WNDCLASSEXW region_class{};
    region_class.cbSize = sizeof(region_class);
    region_class.lpfnWndProc = RegionEditorProc;
    region_class.hInstance = instance;
    region_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    region_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    region_class.lpszClassName = kRegionEditorClass;
    if (RegisterClassExW(&region_class) == 0) {
        return false;
    }

    WNDCLASSEXW list_class{};
    list_class.cbSize = sizeof(list_class);
    list_class.lpfnWndProc = ListProc;
    list_class.hInstance = instance;
    list_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    list_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    list_class.lpszClassName = kListClass;
    if (RegisterClassExW(&list_class) == 0) {
        return false;
    }
    return true;
}

void AttachParentConsole() {
    if (AttachConsole(ATTACH_PARENT_PROCESS) == 0 && GetLastError() != ERROR_ACCESS_DENIED) {
        return;
    }
    FILE* stream = nullptr;
    freopen_s(&stream, "CONOUT$", "w", stdout);
    freopen_s(&stream, "CONOUT$", "w", stderr);
}

void PrintUsage() {
    std::wprintf(
        L"sr-ui %s — Win32 录屏前端（轮 30）\n"
        L"usage:\n"
        L"  sr-ui [--hidden] [--out <file>] [--config <ini>] [--log <log>]\n"
        L"        [--log-level debug|info|warn（重启生效）] [--recorder <exe>]\n"
        L"        [--exit-on-stop] [--exit-after-ms <n>] [--preview-tray-menu]\n"
        L"        [--record-border on|off 录制中屏幕边框，默认 on（M7-13）]\n"
        L"  sr-ui --export-diag <dir> [--config <ini>] [--log <log>]\n"
        L"  sr-ui --version | --help\n",
        kUiVersion);
#if defined(SR_TEST_HOOKS)
    std::wprintf(
        L"调试开关（SR_TEST_HOOKS=ON 的构建才有，Release 交付包不含）:\n"
        L"  sr-ui --set-hotkey <start|pause|stop> <combo> [--config <ini>]\n"
        L"  sr-ui --set-output <dir> / --set-template <template> / --set-region <x,y,w,h|full>\n"
        L"  sr-ui --preflight-fail <disk|display|audio|encoder>\n"
        L"  sr-ui --show-list  （启动即打开录制列表，供 M5-06 验收）\n"
        L"  sr-ui --show-settings （启动即打开设置页，供 M3-05 验收）\n"
        L"  sr-ui --set-fps <1~144> / --set-rate-mode <cbr|vbr>\n"
        L"  sr-ui --set-preset <极致|高清|标准|省空间|自定义> 画质档位联动并落盘（M3-10）\n"
        L"  sr-ui --simulate-backpressure <n> 注入丢帧以验证背压可见标识（M6-11#2）\n"
        L"  sr-ui --list-op <rename|delete> --list-from <name[;name]> [--list-to <name>]\n"
        L"        [--list-confirm yes|no]  （复用列表菜单的同一条文件操作实现）\n"
        L"  sr-ui --elevate-dry-run  （只构造「以管理员身份重启」的命令行并打印，不执行）\n"
        L"  sr-ui --elevate-launch  （真实发起提权请求；UAC 对话框由系统弹出、由用户确认）\n");
#endif
}

bool ParseRegionText(const std::wstring& text,
                     sr::app::CaptureRegion& region,
                     std::wstring& error) {
    if (_wcsicmp(text.c_str(), L"full") == 0) {
        region = {};
        return true;
    }
    long values[4] = {};
    const wchar_t* cursor = text.c_str();
    for (int i = 0; i < 4; ++i) {
        wchar_t* end = nullptr;
        values[i] = std::wcstol(cursor, &end, 10);
        if (end == cursor) {
            error = L"region 含非法整数: " + text;
            return false;
        }
        cursor = end;
        if (i < 3) {
            if (*cursor != L',') {
                error = L"region 格式应为 x,y,w,h 或 full: " + text;
                return false;
            }
            ++cursor;
        } else if (*cursor != L'\0') {
            error = L"region 格式应为 x,y,w,h 或 full: " + text;
            return false;
        }
    }
    region = sr::app::NormalizeRegion(sr::app::CaptureRegion{
        static_cast<int>(values[0]), static_cast<int>(values[1]),
        static_cast<int>(values[2]), static_cast<int>(values[3])});
    if (region.width < 2 || region.height < 2) {
        error = L"region 尺寸无效: " + text;
        return false;
    }
    return true;
}

bool SetHotkeyCommand(const std::wstring& action,
                      const std::wstring& combo_text,
                      const std::wstring& config_path,
                      std::wstring& error) {
    sr::app::AppConfig config;
    std::wstring load_error;
    sr::app::LoadConfig(config_path, config, load_error);

    sr::app::HotkeyCombo combo;
    if (!sr::app::ParseHotkey(combo_text, combo, error)) {
        return false;
    }
    if ((action != L"start" && combo == config.start) ||
        (action != L"pause" && combo == config.pause) ||
        (action != L"stop" && combo == config.stop)) {
        error = L"该组合已被另一个动作占用";
        return false;
    }

    if (RegisterHotKey(nullptr, 1, combo.modifiers | MOD_NOREPEAT, combo.vk) == 0) {
        error = L"该热键已被占用, GetLastError=" + std::to_wstring(GetLastError());
        return false;
    }
    UnregisterHotKey(nullptr, 1);

    return sr::app::SaveHotkey(config_path, action, combo, error);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    g_argc = 0;
    g_argv = CommandLineToArgvW(GetCommandLineW(), &g_argc);

    std::wstring config_path = sr::app::DefaultConfigPath();
    std::wstring log_path = sr::app::DefaultLogPath();
    std::wstring out_override;
    std::wstring recorder_override;
    bool hidden = false;
    bool exit_on_stop = false;
    bool preview_menu = false;
    // M7-13：「可选显示屏幕边框」，默认开启；off 时只保留托盘红点。
    bool show_record_border = true;
    DWORD exit_timeout_ms = 0;
    std::wstring set_hotkey_action;
    std::wstring set_hotkey_combo;
    std::wstring set_output_dir;
    std::wstring set_template_text;
    std::wstring set_region_text;
    std::wstring preflight_fail;
    std::wstring set_fps_text;
    std::wstring set_rate_text;
    std::wstring set_preset_text;
    uint64_t simulate_backpressure = 0;
    int simulate_indicator_state = 0;
    std::wstring list_op;
    std::wstring list_from;
    std::wstring list_to;
    std::wstring list_confirm;
    bool show_list = false;
    bool show_settings = false;
    std::wstring log_level_text;
    std::wstring export_diag_dir;
    bool help = false;
    bool show_version = false;
    // M1-27：提权相关的测试钩子（只在 SR_TEST_HOOKS 构建里存在）。
    bool elevate_dry_run = false;
    bool elevate_launch = false;

    for (int i = 1; g_argv != nullptr && i < g_argc; ++i) {
        const std::wstring arg = g_argv[i];
        auto take = [&](std::wstring& target) {
            if (i + 1 < g_argc) {
                target = g_argv[++i];
                return true;
            }
            return false;
        };
        if (arg == L"--config") {
            take(config_path);
        } else if (arg == L"--log") {
            take(log_path);
        } else if (arg == L"--log-level") {
            take(log_level_text);
        } else if (arg == L"--export-diag") {
            take(export_diag_dir);
        } else if (arg == L"--version") {
            show_version = true;
        } else if (arg == L"--out") {
            take(out_override);
        } else if (arg == L"--recorder") {
            take(recorder_override);
        } else if (arg == L"--hidden") {
            hidden = true;
        } else if (arg == L"--record-border") {
            std::wstring value;
            if (take(value) && value == L"off") { show_record_border = false; }
        } else if (arg == L"--exit-on-stop") {
            exit_on_stop = true;
        } else if (arg == L"--preview-tray-menu") {
            preview_menu = true;
        } else if (arg == L"--exit-after-ms") {
            std::wstring value;
            if (take(value)) {
                exit_timeout_ms = static_cast<DWORD>(std::wcstoul(value.c_str(), nullptr, 10));
            }
#if defined(SR_TEST_HOOKS)
        } else if (arg == L"--set-hotkey") {
            take(set_hotkey_action);
            take(set_hotkey_combo);
        } else if (arg == L"--set-output") {
            take(set_output_dir);
        } else if (arg == L"--set-template") {
            take(set_template_text);
        } else if (arg == L"--set-region") {
            take(set_region_text);
        } else if (arg == L"--preflight-fail") {
            take(preflight_fail);
        } else if (arg == L"--show-list") {
            show_list = true;
        } else if (arg == L"--show-settings") {
            show_settings = true;
        } else if (arg == L"--set-fps") {
            take(set_fps_text);
        } else if (arg == L"--set-rate-mode") {
            take(set_rate_text);
        } else if (arg == L"--set-preset") {
            take(set_preset_text);
        } else if (arg == L"--simulate-backpressure") {
            std::wstring value;
            if (take(value)) { simulate_backpressure = std::wcstoull(value.c_str(), nullptr, 10); }
        } else if (arg == L"--simulate-record-state") {
            std::wstring value;
            if (take(value)) {
                if (value == L"recording") { simulate_indicator_state = 1; }
                else if (value == L"paused") { simulate_indicator_state = 2; }
            }
        } else if (arg == L"--list-op") {
            take(list_op);
        } else if (arg == L"--list-from") {
            take(list_from);
        } else if (arg == L"--list-to") {
            take(list_to);
        } else if (arg == L"--list-confirm") {
            take(list_confirm);
        } else if (arg == L"--elevate-dry-run") {
            // M1-27：只构造提权重启的命令行，不执行（验收用来断言命令构造正确）。
            elevate_dry_run = true;
        } else if (arg == L"--elevate-launch") {
            // M1-27：真实发起提权请求（UAC 对话框由系统弹出、由用户确认）。
            elevate_launch = true;
#endif
        } else if (arg == L"--help" || arg == L"-h") {
            help = true;
        }
    }

    std::wstring log_error;
    EnsureParentDirectory(log_path, log_error);
    // 轮 30（日志分级）：--log-level 优先，其次 INI [log] level，最后 info。
    // 这里直接读 INI：logger 必须在任何 Log() 之前初始化。
    std::wstring level_text = log_level_text;
    if (level_text.empty()) {
        wchar_t buffer[32] = {};
        GetPrivateProfileStringW(L"log", L"level", L"info", buffer, 32, config_path.c_str());
        level_text = buffer;
    }
    sr::diag::LogLevel level = sr::diag::LogLevel::Info;
    const std::string level_ascii = Narrow(level_text);
    if (!sr::diag::ParseLogLevel(level_ascii, level)) {
        AttachParentConsole();
        std::wprintf(L"invalid --log-level: %s (debug|info|warn)\n", level_text.c_str());
        return 1;
    }
    // 共享读：验收脚本需要边跑边 tail；否则日志会被独占，自动化只能等进程退出。
    // 导出诊断包时 `--log` 同时是**输入**（要被读进 log.txt），只能追加、不能截断。
    sr::diag::Init("", log_path, level, false, !export_diag_dir.empty());
    g_log_enabled = true;
    Log(L"log_level=%ls build=%ls test_hooks=%d", level_text.c_str(), kBuildTag,
        kTestHooks ? 1 : 0);
    LogDebug(L"debug_pipeline_enabled level=%ls", level_text.c_str());

    if (help) {
        AttachParentConsole();
        PrintUsage();
        CloseLog();
        return 0;
    }

    if (show_version) {
        AttachParentConsole();
        std::wprintf(L"sr-ui %s (ipc protocol v%u, build %s, test_hooks=%d)\n", kUiVersion,
                     static_cast<unsigned>(sr::ipc::kProtocolVersion), kBuildTag,
                     kTestHooks ? 1 : 0);
        Log(L"version_requested");
        CloseLog();
        return 0;
    }

#if defined(SR_TEST_HOOKS)
    // M1-27：提权重启的两个测试钩子。
    //   --elevate-dry-run：只构造命令行并打印/记日志，**不执行**（断言命令构造）。
    //   --elevate-launch：真实发起 ShellExecute(runas)；UAC 对话框由系统弹、由用户确认。
    //     已经提权时直接退出（否则提权实例会拿同样的参数再提权一次 → 无限循环）。
    if (elevate_dry_run || elevate_launch) {
        AttachParentConsole();
        // 提权状态在这里也要记一行：提权实例带着同样的 --elevate-launch 参数启动，
        // 它会在下面的「已提权」分支里直接退出，必须先留下 elevated=1 的证据。
        Log(L"uac_elevation_state elevated=%d", sr::app::IsProcessElevated() ? 1 : 0);
        const std::wstring command = sr::app::BuildElevatedCommandLine(log_path);
        if (elevate_dry_run) {
            Log(L"uac_elevate_dry_run verb=runas command=%ls", command.c_str());
            std::wprintf(L"uac_elevate_command=%ls\n", command.c_str());
            std::wprintf(L"uac_elevated=%d\n", sr::app::IsProcessElevated() ? 1 : 0);
            CloseLog();
            return 0;
        }
        if (sr::app::IsProcessElevated()) {
            Log(L"uac_elevate_launch=already_elevated");
            std::wprintf(L"uac_elevate_launch=already_elevated\n");
            CloseLog();
            return 0;
        }
        std::wstring elevate_error;
        const sr::app::ElevationLaunch launch_result = sr::app::LaunchElevated(
            sr::app::CurrentExecutablePath(), sr::app::BuildElevatedParameters(log_path),
            elevate_error);
        if (launch_result == sr::app::ElevationLaunch::kStarted) {
            Log(L"uac_elevate_launch=ok command=%ls", command.c_str());
            std::wprintf(L"uac_elevate_launch=ok\n");
            CloseLog();
            return 0;
        }
        LogWarn(L"uac_elevate_launch=failed reason=%ls", elevate_error.c_str());
        std::wprintf(L"uac_elevate_launch=failed reason=%hs\n", Narrow(elevate_error).c_str());
        CloseLog();
        return 1;
    }
#endif

    if (!export_diag_dir.empty()) {
        AttachParentConsole();
        sr::app::AppConfig diag_config;
        std::wstring diag_config_error;
        sr::app::LoadConfig(config_path, diag_config, diag_config_error);
        sr::diag::DiagRequest request;
        request.output_root = export_diag_dir;
        request.config_path = config_path;
        request.ui_log_path = log_path;
        request.product = "sr-ui";
        request.version = Narrow(kUiVersion);
        request.protocol = std::to_string(static_cast<unsigned>(sr::ipc::kProtocolVersion));
        request.build = Narrow(kBuildTag);
        request.test_hooks = kTestHooks;
        request.session = "mode=export-diag\nlog_level=";
        request.session += sr::diag::LogLevelName(level);
        request.session += "\noutput_dir=";
        request.session += Narrow(diag_config.output_dir);
        request.session += "\nregion=";
        request.session += (diag_config.region.enabled() ? "custom" : "full");
        request.session += "\n";
        const sr::diag::DiagResult result = sr::diag::Export(request);
        Log(L"diag_pack ok=%d files=%zu bytes=%llu ms=%llu path=%ls",
            result.ok ? 1 : 0, result.files.size(),
            static_cast<unsigned long long>(result.bytes),
            static_cast<unsigned long long>(result.elapsed_ms), result.directory.c_str());
        std::wprintf(L"diag_pack=%s\n", result.ok ? L"ok" : L"fail");
        std::wprintf(L"diag_path=%s\n", result.directory.c_str());
        std::wprintf(L"diag_files=%zu\n", result.files.size());
        std::wprintf(L"diag_bytes=%llu\n", static_cast<unsigned long long>(result.bytes));
        std::wprintf(L"diag_elapsed_ms=%llu\n", static_cast<unsigned long long>(result.elapsed_ms));
        if (!result.ok) {
            std::wprintf(L"error=%hs\n", sr::diag::SanitizeText(result.error).c_str());
        }
        CloseLog();
        return result.ok ? 0 : 17;
    }

    if (!set_hotkey_action.empty() || !set_hotkey_combo.empty()) {
        AttachParentConsole();
        std::wstring error;
        const bool ok = SetHotkeyCommand(set_hotkey_action, set_hotkey_combo, config_path, error);
        Log(ok ? L"hotkey_saved action=%ls combo=%ls path=%ls"
               : L"hotkey_rejected action=%ls combo=%ls reason=%ls",
            set_hotkey_action.c_str(), set_hotkey_combo.c_str(),
            ok ? config_path.c_str() : error.c_str());
        if (ok) {
            std::wprintf(L"hotkey_saved %s=%s\n", set_hotkey_action.c_str(), set_hotkey_combo.c_str());
        } else {
            std::wprintf(L"hotkey_rejected: %s\n", error.c_str());
        }
        CloseLog();
        return ok ? 0 : 3;
    }

    if (!set_region_text.empty()) {
        AttachParentConsole();
        sr::app::CaptureRegion region;
        std::wstring error;
        bool ok = ParseRegionText(set_region_text, region, error);
        if (ok) {
            ok = sr::app::SaveRegion(config_path, region, error);
        }
        if (ok) {
            Log(L"region_saved x=%d y=%d w=%d h=%d text=%ls",
                region.x, region.y, region.width, region.height, set_region_text.c_str());
            std::wprintf(L"region_saved %d,%d,%d,%d\n",
                         region.x, region.y, region.width, region.height);
        } else {
            Log(L"region_rejected text=%ls reason=%ls", set_region_text.c_str(), error.c_str());
            std::wprintf(L"region_rejected: %s\n", error.c_str());
        }
        CloseLog();
        return ok ? 0 : 13;
    }

    // M3-10：档位持久化入口（设置面板与自动化共用同一个联动函数 ApplyQualityPreset）。
    if (!set_preset_text.empty()) {
        AttachParentConsole();
        sr::app::AppConfig preset_config;
        std::wstring load_error;
        sr::app::LoadConfig(config_path, preset_config, load_error);
        std::wstring preset_error;
        if (!sr::app::ApplyQualityPreset(set_preset_text, preset_config)) {
            Log(L"preset_rejected name=%ls", set_preset_text.c_str());
            std::wprintf(L"preset_rejected: %ls\n", set_preset_text.c_str());
            CloseLog();
            return 13;
        }
        if (!sr::app::SaveQualityPreset(config_path, set_preset_text, preset_config.video_fps,
                                        preset_config.video_bitrate_kbps, preset_config.quality,
                                        preset_error)) {
            Log(L"preset_save_failed reason=%ls", preset_error.c_str());
            CloseLog();
            return 13;
        }
        Log(L"preset_saved name=%ls fps=%u bitrate_kbps=%u quality=%u path=%ls",
            set_preset_text.c_str(), preset_config.video_fps,
            preset_config.video_bitrate_kbps, preset_config.quality, config_path.c_str());
        std::wprintf(L"preset_saved name=%ls fps=%u bitrate_kbps=%u quality=%u\n",
                     set_preset_text.c_str(), preset_config.video_fps,
                     preset_config.video_bitrate_kbps, preset_config.quality);
        CloseLog();
        return 0;
    }

    // M3-03 / M3-04：帧率与码率模式的持久化入口（设置面板与自动化共用同一写入函数）。
    if (!set_fps_text.empty() || !set_rate_text.empty()) {
        AttachParentConsole();
        sr::app::AppConfig video_config;
        std::wstring load_error;
        sr::app::LoadConfig(config_path, video_config, load_error);
        uint32_t fps = video_config.video_fps;
        std::wstring rate_mode = video_config.rate_mode;
        bool ok = true;
        std::wstring error;
        if (!set_fps_text.empty()) {
            const unsigned long value = std::wcstoul(set_fps_text.c_str(), nullptr, 10);
            wchar_t* end = nullptr;
            const unsigned long checked = std::wcstoul(set_fps_text.c_str(), &end, 10);
            if (set_fps_text.empty() || end == nullptr || *end != L'\0' || checked < 1 ||
                checked > 144) {
                ok = false;
                error = L"帧率必须是 1~144 的整数: " + set_fps_text;
            } else {
                fps = static_cast<uint32_t>(value);
            }
        }
        if (ok && !set_rate_text.empty()) {
            std::wstring lower = set_rate_text;
            for (wchar_t& c : lower) {
                c = static_cast<wchar_t>(std::towlower(c));
            }
            if (lower != L"cbr" && lower != L"vbr") {
                ok = false;
                error = L"码率模式必须是 cbr/vbr: " + set_rate_text;
            } else {
                rate_mode = lower;
            }
        }
        if (ok) {
            ok = sr::app::SaveVideoSettings(config_path, fps, rate_mode, error);
        }
        if (ok) {
            Log(L"video_saved fps=%u rate_mode=%ls path=%ls", fps, rate_mode.c_str(),
                config_path.c_str());
            std::wprintf(L"video_saved fps=%u rate_mode=%s\n", fps, rate_mode.c_str());
        } else {
            Log(L"video_rejected reason=%ls", error.c_str());
            std::wprintf(L"video_rejected: %s\n", error.c_str());
        }
        CloseLog();
        return ok ? 0 : 13;
    }

    if (!set_output_dir.empty() || !set_template_text.empty()) {
        AttachParentConsole();
        bool ok = true;
        std::wstring error;
        if (!set_output_dir.empty()) {
            std::wstring probe_error;
            if (!sr::util::EnsureDirectoryWritable(set_output_dir, probe_error)) {
                ok = false;
                error = probe_error;
                Log(L"output_dir_rejected dir=%ls reason=%ls",
                    set_output_dir.c_str(), error.c_str());
                std::wprintf(L"output_dir_rejected: %s\n", error.c_str());
            } else if (!sr::app::SaveOutputDir(config_path, set_output_dir, error)) {
                ok = false;
                Log(L"output_dir_rejected dir=%ls reason=%ls",
                    set_output_dir.c_str(), error.c_str());
                std::wprintf(L"output_dir_rejected: %s\n", error.c_str());
            } else {
                Log(L"output_dir_saved dir=%ls", set_output_dir.c_str());
                std::wprintf(L"output_dir_saved %s\n", set_output_dir.c_str());
            }
        }
        if (ok && !set_template_text.empty()) {
            if (!sr::app::SaveTemplate(config_path, set_template_text, error)) {
                ok = false;
                Log(L"template_rejected template=%ls reason=%ls",
                    set_template_text.c_str(), error.c_str());
                std::wprintf(L"template_rejected: %s\n", error.c_str());
            } else {
                Log(L"template_saved template=%ls", set_template_text.c_str());
                std::wprintf(L"template_saved %s\n", set_template_text.c_str());
            }
        }
        CloseLog();
        return ok ? 0 : 13;
    }

    const HRESULT com_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const HRESULT mf_result = MFStartup(MF_VERSION, MFSTARTUP_LITE);
    // M5-06：列表用 ListView（comctl32 v6 才有完整外观），必须先初始化。
    INITCOMMONCONTROLSEX common_controls{};
    common_controls.dwSize = sizeof(common_controls);
    common_controls.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&common_controls);
    // M1-04：区域框选按物理像素；per-monitor v2 下 GetCursorPos/窗口坐标不再被系统缩放。
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        SetProcessDPIAware();
    }

    int result = 0;
    {
        AppState app;
        app.instance = instance;
        app.config_path = config_path;
        app.log_path = log_path;
        app.out_override = out_override;
        app.recorder_override = recorder_override;
        app.preflight_fail = preflight_fail;
        app.show_record_border = show_record_border;
        app.simulate_indicator_state = simulate_indicator_state;
        app.show_list_on_start = show_list || !list_op.empty();
        app.show_settings_on_start = show_settings;
        app.list_op = list_op;
        app.list_from = list_from;
        app.list_to = list_to;
        app.list_confirm_yes = list_confirm == L"yes";
        app.hidden = hidden;
        app.exit_on_stop = exit_on_stop;
        app.preview_menu = preview_menu;
        app.exit_timeout_ms = exit_timeout_ms;
        app.backpressure_inject = simulate_backpressure;   // M6-11#2 验收注入
        g_app = &app;

        std::wstring config_error;
        if (!sr::app::LoadConfig(config_path, app.config, config_error)) {
            Log(L"config_error %ls", config_error.c_str());
            app.status_extra = config_error;
        }
        if (!recorder_override.empty()) {
            app.config.recorder_path = recorder_override;
        }
        app.recorder.SetLogLevel(app.config.log_level);
        // M1-27：提权状态在启动时判定一次（进程提权状态在生命周期内不会变）。
        app.elevated = sr::app::IsProcessElevated();
        Log(L"uac_elevation_state elevated=%d", app.elevated ? 1 : 0);
        if (ReadConfigWriteTime(config_path, app.config_write)) {
            app.have_config_write = true;
        }
        app.next_config_check = GetTickCount64() + 1000;
        Log(L"app_start pid=%lu config=%ls start=%ls pause=%ls stop=%ls fps=%u rate_mode=%ls output=%ls recorder=%ls",
            static_cast<unsigned long>(GetCurrentProcessId()),
            config_path.c_str(),
            sr::app::FormatHotkey(app.config.start).c_str(),
            sr::app::FormatHotkey(app.config.pause).c_str(),
            sr::app::FormatHotkey(app.config.stop).c_str(),
            app.config.video_fps,
            app.config.rate_mode.c_str(),
            app.config.output_dir.c_str(),
            app.config.recorder_path.c_str());

        if (!RegisterClasses(instance)) {
            Log(L"register_class_failed error=%lu", GetLastError());
            CloseLog();
            return 1;
        }

        HWND hwnd = CreateWindowExW(0, kWindowClass, kAppTitle, WS_OVERLAPPEDWINDOW,
                                    CW_USEDEFAULT, CW_USEDEFAULT, 520, 230,
                                    nullptr, nullptr, instance, nullptr);
        if (hwnd == nullptr) {
            Log(L"create_window_failed error=%lu", GetLastError());
            CloseLog();
            return 1;
        }
        ShowWindow(hwnd, hidden ? SW_HIDE : SW_SHOW);
        UpdateWindow(hwnd);

        MSG message;
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        result = static_cast<int>(message.wParam);
        g_app = nullptr;
    }

    if (SUCCEEDED(mf_result)) {
        MFShutdown();
    }
    if (SUCCEEDED(com_result)) {
        CoUninitialize();
    }
    CloseLog();
    if (g_argv != nullptr) {
        LocalFree(g_argv);
        g_argv = nullptr;
    }
    return result;
}
