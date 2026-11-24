// 轮 55（0.2-1）：窗口类名 / 命令 ID / 定时器 ID 常量表。
// 只做搬家：数值与轮 27~54 完全一致，验收脚本与既有调用路径不受影响。
#pragma once

#include <windows.h>

// ---------------- 窗口类名 ----------------
constexpr const wchar_t* kWindowClass = L"SrUiMainWindow";
constexpr const wchar_t* kSettingsClass = L"SrUiSettingsWindow";
constexpr const wchar_t* kRegionOverlayClass = L"SrUiRegionOverlay";
constexpr const wchar_t* kRegionEditorClass = L"SrUiRegionWindow";
constexpr const wchar_t* kListClass = L"SrUiListWindow";
constexpr const wchar_t* kToolbarClass = L"SrUiToolbar";
constexpr const wchar_t* kAppTitle = L"Screen Recorder";

// ---------------- 定时器 / 消息 ----------------
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT_PTR kPollTimer = 1;
constexpr UINT_PTR kPreviewTimer = 2;
constexpr UINT_PTR kExitTimer = 3;
constexpr UINT_PTR kListTimer = 4;

// ---------------- 热键 ----------------
constexpr UINT kHotkeyStart = 1;
constexpr UINT kHotkeyPause = 2;
constexpr UINT kHotkeyStop = 3;

// ---------------- 主窗口命令 ID ----------------
constexpr int kStatusId = 200;
constexpr int kStartButton = 201;
constexpr int kPauseButton = 202;
constexpr int kStopButton = 203;
constexpr int kHideButton = 204;
constexpr int kSavePathButton = 205;
constexpr int kSettingsButton = 206;
constexpr int kRegionButton = 207;
constexpr int kListButton = 208;
constexpr int kElevateButton = 209;
constexpr int kUacWarnId = 210;
// 轮 55：工作台自绘控件（Header 状态药丸 / 右侧设置面板折叠开关）。
constexpr int kHeaderPill = 212;
constexpr int kPanelToggleButton = 211;

// 轮 55：设置面板占位字段（只读展示，56 轮改为即时保存）
constexpr int kFieldSavePath = 820;
constexpr int kFieldTemplate = 821;
constexpr int kFieldFps = 822;
constexpr int kFieldBitrate = 823;
constexpr int kFieldEncoder = 824;
constexpr int kFieldLogLevel = 825;

// ---------------- 区域编辑窗口 ----------------
constexpr int kRegionTextId = 600;
constexpr int kRegionFull = 601;
constexpr int kRegion169 = 602;
constexpr int kRegion43 = 603;
constexpr int kRegion11 = 604;
constexpr int kRegionPick = 605;
constexpr int kRegionOk = 606;
constexpr int kRegionCancel = 607;

// ---------------- 托盘菜单 ----------------
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
constexpr int kTrayElevate = 310;

// ---------------- 列表窗口 ----------------
constexpr int kListView = 700;

// ---------------- 设置窗口 ----------------
constexpr int kSettingsOk = 400;
constexpr int kSettingsCancel = 401;
constexpr int kSettingsBrowse = 402;
constexpr int kSettingsTemplate = 103;
constexpr int kSettingsFpsCombo = 104;
constexpr int kSettingsFpsCustom = 105;
constexpr int kSettingsRateCombo = 106;
constexpr int kSettingsEncoderList = 107;
constexpr int kSettingsPresetCombo = 108;
constexpr int kSettingsPresetValues = 109;

// ---------------- 悬浮条 ----------------
constexpr int kTbTimer = 500;
constexpr int kTbRemaining = 501;
constexpr int kTbPause = 502;
constexpr int kTbStop = 503;
constexpr int kTbHide = 504;
constexpr int kTbMetrics = 505;
