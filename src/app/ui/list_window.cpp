// 轮 55（0.2-1）：成片列表窗口 —— 从 main.cpp 整体平移（纯搬运，逻辑逐行未改）。
// 0.2 的成片列表最终会成为工作台的一部分（docs/29 §3），本轮先搬出 main.cpp，
// 56 轮再把列表内容内联进工作台；此期间行为与轮 54 完全一致。
#include "app/ui/app_state.h"

#include <commctrl.h>
#include <windowsx.h>
#include <shellapi.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "app/FileOps.h"
#include "app/ui/ids.h"

namespace {

constexpr int kListColumns = 6;

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

}  // namespace

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
