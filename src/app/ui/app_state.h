// 轮 55（0.2-1）：主窗口/列表窗口共享的状态与内部 API 声明。
//
// 为什么需要这个头：轮 55 把主窗口呈现（UpdateUi / MainProc）与成片列表窗口从
// main.cpp 搬到 ui/ 下，这些模块必须能拿到 AppState 与日志入口。**只搬声明**：
// 结构体字段、函数签名、调用路径与轮 54 完全一致；56 轮再按 docs/29 §5 拆分
// 到 main_window / settings_panel，届时这个头会被拆掉。
#pragma once

#include <windows.h>
#include <shellapi.h>

#include <string>
#include <vector>

#include "app/AppConfig.h"
#include "app/RecorderController.h"
#include "app/ui/ids.h"
#include "app/ui/workbench.h"
#include "ipc/Protocol.h"

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

    // 轮 55（0.2-1）：工作台骨架。settings_panel_open 只影响右侧面板显隐，
    // 与 settings_open（设置对话框）不是一回事。
    sr::ui::Workbench workbench;
    bool settings_panel_open = true;
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

extern AppState* g_app;
extern bool g_log_enabled;
// UI 版本号（状态栏与 --version 同源）。
extern const wchar_t* const kUiVersion;

// ---------------- 内部 API（实现留在 main.cpp） ----------------
void Log(const wchar_t* format, ...);
void LogDebug(const wchar_t* format, ...);
void LogWarn(const wchar_t* format, ...);
void CloseLog();

std::string Narrow(const std::wstring& text);
std::wstring Widen(const char* text);
std::wstring WidenUtf8(const std::string& text);
HMENU ControlId(int id);
std::wstring FormatElapsed(uint64_t elapsed_ms);
// M6-03：采集/编码/丢帧三项指标（悬浮条与状态栏共用同一排版）。
std::wstring FormatMetrics(const sr::ipc::StateSnapshotPayload& snapshot);

bool ParseListOutput(const std::string& text, std::vector<ListRow>& rows);
bool RunRecorderList(AppState& app, std::vector<ListRow>& rows, std::wstring& error);
bool RunRecorderEncoderList(AppState& app, std::wstring& canonical,
                            std::vector<std::wstring>& rows, std::wstring& error);

// ---------------- 主窗口 / 列表窗口（实现在 ui/ 下） ----------------
void UpdateUi(AppState& app);
void ApplyElevationUi(AppState& app);
void ShowMainWindow(AppState& app);
void UpdateToolbar(AppState& app);
LRESULT CALLBACK MainProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

// ---------------- 其余窗口与动作（实现留在 main.cpp） ----------------
void StartRecording(AppState& app);
void PauseResume(AppState& app);
void StopRecording(AppState& app);
void BrowseOutputDir(AppState& app);
void ShowSettings(AppState& app);
void ShowRegionEditor(AppState& app);
void RestartAsAdmin(AppState& app);
void ToggleToolbar(AppState& app);
void ShowToolbar(AppState& app, bool show);
void ShowTrayMenu(AppState& app);
void ReloadConfigIfChanged(AppState& app);
void RefreshDiskRemaining(AppState& app);
void UpdateTray(AppState& app, const std::wstring& tooltip, HICON icon);
void RemoveTray(AppState& app);
void UpdateRecordIndicator(AppState& app, bool recording, bool paused);
HICON MakeRedDotIcon();
HWND CreateBorderWindow(HINSTANCE instance);
bool RegisterHotkeys(AppState& app, bool report_errors);
void UnregisterHotkeys(AppState& app);

void RefreshRecordingList(AppState& app);
void UpdateRecordingListLive(AppState& app);
void ShowRecordingList(AppState& app);
LRESULT CALLBACK ListProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
