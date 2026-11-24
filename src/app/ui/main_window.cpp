// 轮 55（0.2-1）：主窗口（工作台）—— MainProc / UpdateUi / 提权提示，从 main.cpp 平移。
// 行为等价要求：消息分类、命令 ID、定时器、日志行、状态文本与轮 54 逐条一致；
// 变的只有「怎么画」——交给 ui/workbench + ui/controls 自绘。
#include "app/ui/app_state.h"

#include <filesystem>
#include <string>
#include <vector>

#include "app/ui/controls.h"
#include "app/ui/ids.h"
#include "app/ui/layout.h"
#include "app/ui/theme.h"

namespace {

constexpr const wchar_t* kUacWarnLabel =
    L"⚠ 当前非管理员运行：UAC 提权窗口等高权限窗口可能黑屏或无画面";


// 布局/探针日志只在尺寸真正变化（或显式强制）时打，避免拖动窗口刷屏。
void LogWorkbenchGeometry(AppState& app, bool force) {
    if (!g_log_enabled || app.hwnd == nullptr) {
        return;
    }
    static SIZE last_logged{0, 0};
    RECT client{};
    GetClientRect(app.hwnd, &client);
    const SIZE size{client.right - client.left, client.bottom - client.top};
    if (!force && size.cx == last_logged.cx && size.cy == last_logged.cy) {
        return;
    }
    last_logged = size;
    Log(L"%ls", app.workbench.LayoutLine().c_str());
    Log(L"%ls", app.workbench.ProbeLine().c_str());
    Log(L"ui_theme name=%ls base=%ls card=%ls accent=%ls text=%ls border=%ls",
        sr::ui::Current().name, sr::ui::HexOf(sr::ui::Current().base).c_str(),
        sr::ui::HexOf(sr::ui::Current().card).c_str(),
        sr::ui::HexOf(sr::ui::Current().accent).c_str(),
        sr::ui::HexOf(sr::ui::Current().text_primary).c_str(),
        sr::ui::HexOf(sr::ui::Current().border).c_str());
}

void RelayoutWorkbench(AppState& app) {
    if (app.hwnd == nullptr) {
        return;
    }
    RECT client{};
    GetClientRect(app.hwnd, &client);
    app.workbench.Layout(client, !app.elevated);
}

}  // namespace

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

    sr::ui::WorkbenchModel model;
    model.status_text = status;
    if (recording) {
        model.header_state = L"录制中";
        model.header_tone = sr::ui::PillTone::Danger;
    } else if (paused) {
        model.header_state = L"已暂停";
        model.header_tone = sr::ui::PillTone::Warn;
    } else if (active) {
        model.header_state = L"启动中";
        model.header_tone = sr::ui::PillTone::Warn;
    } else if (app.recorder.Exited() && app.recorder.ExitCode() != 0) {
        model.header_state = L"错误";
        model.header_tone = sr::ui::PillTone::Danger;
    } else {
        model.header_state = L"就绪";
        model.header_tone = sr::ui::PillTone::Ok;
    }
    model.warn_text = app.elevated ? L"" : kUacWarnLabel;
    model.output_dir = app.config.output_dir;
    model.filename_template = app.config.filename_template;
    model.fps_text = std::to_wstring(app.config.video_fps) + L" fps";
    model.bitrate_text = std::to_wstring(app.config.video_bitrate_kbps) + L" kbps · " +
                         app.config.rate_mode;
    model.encoder_text = app.config.quality_preset + L" · " + app.config.container;
    model.log_level_text = app.config.log_level;
    model.region_text = region_mode;
    model.version_text = std::wstring(L"v") + kUiVersion;
    model.recent_count = 0;
    app.workbench.SetModel(model);

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

void ShowMainWindow(AppState& app) {
    ShowWindow(app.hwnd, SW_SHOW);
    ShowWindow(app.hwnd, SW_RESTORE);
    SetForegroundWindow(app.hwnd);
}

LRESULT CALLBACK MainProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    AppState* app = g_app;
    switch (message) {
        case WM_CREATE: {
            app->hwnd = hwnd;
            // 轮 55：控件全部换成 ui/controls 自绘子窗口（真实 HWND，ID 与窗口文本不变）。
            app->workbench.Create(hwnd);
            RECT client{};
            GetClientRect(hwnd, &client);
            app->workbench.Layout(client, !app->elevated);
            app->workbench.SetSettingsOpen(app->settings_panel_open);
            LogWorkbenchGeometry(*app, true);

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
                case kPanelToggleButton:
                    app->workbench.ToggleSettings();
                    app->settings_panel_open = app->workbench.SettingsOpen();
                    Log(L"ui_panel open=%d auto_collapsed=%d",
                        app->workbench.SettingsOpen() ? 1 : 0,
                        app->workbench.AutoCollapsed() ? 1 : 0);
                    LogWorkbenchGeometry(*app, true);
                    InvalidateRect(hwnd, nullptr, FALSE);
                    break;
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
                return 0;
            }
            RelayoutWorkbench(*app);
            LogWorkbenchGeometry(*app, true);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_DPICHANGED: {
            // 轮 55：自绘控件必须重算布局，否则 125%/150% 下错位。
            const UINT dpi = HIWORD(wparam);
            sr::ui::SetDpi(dpi);
            const RECT* suggested = reinterpret_cast<const RECT*>(lparam);
            if (suggested != nullptr) {
                SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                             suggested->right - suggested->left,
                             suggested->bottom - suggested->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
            }
            RelayoutWorkbench(*app);
            LogWorkbenchGeometry(*app, false);
            InvalidateRect(hwnd, nullptr, TRUE);
            Log(L"ui_dpi_changed dpi=%u scale=%.2f header_h=%d status_h=%d", sr::ui::Dpi(),
                sr::ui::ScaleFactor(), sr::ui::Scale(sr::ui::Current().header_h),
                sr::ui::Scale(sr::ui::Current().status_h));
            return 0;
        }
        case WM_GETMINMAXINFO: {
            // 最小 860×560（96dpi 设计值，按当前缩放换算）。
            auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
            info->ptMinTrackSize.x = sr::ui::Scale(860);
            info->ptMinTrackSize.y = sr::ui::Scale(560);
            return 0;
        }
        case WM_ERASEBKGND:
            // 全部自绘 + 双缓冲：不需要系统刷底色，也就没有白闪。
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            RECT client{};
            GetClientRect(hwnd, &client);
            app->workbench.Paint(dc, client);
            EndPaint(hwnd, &ps);
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
