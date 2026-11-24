// 轮 55（0.2-1）：自绘控件 —— Button(primary/secondary/ghost) / Toggle / Card / StatusPill / Field。
// 控件是真实子窗口（自定义窗口类）：既有验收脚本按 ID + 窗口文本读取的能力保持不变，
// 但外观 100% 由 draw/theme 绘制，不再出现系统灰底 BUTTON/STATIC。
// 三态：hover / press / focus（Tab 可达，焦点矩形可见）。
#pragma once

#include <windows.h>

#include <string>

#include "app/ui/draw.h"
#include "app/ui/theme.h"

namespace sr::ui {

constexpr const wchar_t* kControlClass = L"SrUiControl";

enum class ButtonKind { Primary, Secondary, Ghost };

enum class ControlKind { Button, Toggle, Field, Label, Pill };

// 控件所在底色。自绘子窗口必须自己铺底（父窗口的绘制被子窗口裁剪掉），
// 否则子窗口区域会露出未绘制内容（表现为一块异色方块）。
enum class Surface { Base, Card, Header, Overlay, Warn };

// 状态药丸色调
enum class PillTone { Neutral, Ok, Warn, Danger };

struct ButtonDesc {
    int id = 0;
    std::wstring text;
    ButtonKind kind = ButtonKind::Secondary;
    Surface surface = Surface::Base;
};

struct ToggleDesc {
    int id = 0;
    std::wstring text;
    bool on = false;
    Surface surface = Surface::Base;
};

struct FieldDesc {
    int id = 0;
    std::wstring label;
    std::wstring value;
    Surface surface = Surface::Card;
};

struct LabelDesc {
    int id = 0;
    std::wstring text;
    bool warn = false;
    Surface surface = Surface::Base;
};

struct PillDesc {
    int id = 0;
    std::wstring text;
    PillTone tone = PillTone::Neutral;
    Surface surface = Surface::Header;
};

// 注册自定义控件窗口类（幂等）。
bool RegisterControlClasses(HINSTANCE instance);

HWND CreateButton(HWND parent, const ButtonDesc& desc, const RECT& rc);
HWND CreateToggle(HWND parent, const ToggleDesc& desc, const RECT& rc);
HWND CreateField(HWND parent, const FieldDesc& desc, const RECT& rc);
HWND CreateLabel(HWND parent, const LabelDesc& desc, const RECT& rc);
HWND CreatePill(HWND parent, const PillDesc& desc, const RECT& rc);

// 文本统一走窗口文本（GetWindowTextW 可读，SetWindowTextW 立即重绘）。
void SetControlText(HWND control, const std::wstring& text);
std::wstring GetControlText(HWND control);

void SetToggleValue(HWND control, bool on);
bool ToggleValue(HWND control);
void SetFieldValue(HWND control, const std::wstring& value);
void SetFieldError(HWND control, bool error);
void SetPillTone(HWND control, PillTone tone);
void InvalidateControl(HWND control);

// 纯绘制原语：Card 与 StatusPill 由外壳直接画（无交互，不需要子窗口）。
void DrawCard(Canvas& canvas, const RECT& rc, bool elevated = false);
void DrawCardTitle(Canvas& canvas, const RECT& rc, const std::wstring& title);
void DrawStatusPill(Canvas& canvas, const RECT& rc, const std::wstring& text, PillTone tone);
void DrawFieldBlock(Canvas& canvas, const RECT& rc, const std::wstring& label,
                    const std::wstring& value, bool focused);
// 空态占位（列表/面板共用）
void DrawEmptyState(Canvas& canvas, const RECT& rc, const std::wstring& title,
                    const std::wstring& hint);

}  // namespace sr::ui
