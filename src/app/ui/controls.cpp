#include "app/ui/controls.h"

#include <algorithm>

#include "app/ui/layout.h"

#ifndef BM_CLICK
#define BM_CLICK 0x00F5
#endif

namespace sr::ui {
namespace {

struct ControlData {
    ControlKind kind = ControlKind::Button;
    int id = 0;
    ButtonKind button_kind = ButtonKind::Secondary;
    PillTone tone = PillTone::Neutral;
    bool on = false;
    bool warn = false;
    bool error = false;
    bool hover = false;
    bool press = false;
    bool focus = false;
    bool tracking_leave = false;
    Surface surface = Surface::Base;
    std::wstring value;
};

Color SurfaceColor(Surface surface) {
    const Theme& t = Current();
    switch (surface) {
        case Surface::Card: return t.card;
        case Surface::Header: return t.header;
        case Surface::Overlay: return t.overlay;
        case Surface::Warn: return Blend(t.base, t.warning, 10);
        default: return t.base;
    }
}

ControlData* DataOf(HWND hwnd) {
    return reinterpret_cast<ControlData*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

std::wstring WindowText(HWND hwnd) {
    const int length = GetWindowTextLengthW(hwnd);
    if (length <= 0) {
        return {};
    }
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(hwnd, text.data(), length + 1);
    text.resize(static_cast<size_t>(length));
    return text;
}

Color ToneColor(PillTone tone) {
    const Theme& t = Current();
    switch (tone) {
        case PillTone::Ok: return t.success;
        case PillTone::Warn: return t.warning;
        case PillTone::Danger: return t.danger;
        default: return t.text_secondary;
    }
}

void Repaint(HWND hwnd) { InvalidateRect(hwnd, nullptr, FALSE); }

void NotifyClick(HWND hwnd, ControlData* data) {
    if (data == nullptr) {
        return;
    }
    if (data->kind == ControlKind::Toggle) {
        data->on = !data->on;
        Repaint(hwnd);
    }
    if (data->kind != ControlKind::Button && data->kind != ControlKind::Toggle) {
        return;
    }
    SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(static_cast<WORD>(data->id), BN_CLICKED),
                 reinterpret_cast<LPARAM>(hwnd));
}

void PaintButton(Canvas& canvas, const RECT& rc, const ControlData& data, const std::wstring& text,
                 bool enabled) {
    const Theme& t = Current();
    const int radius = Scale(t.radius_control);
    const int border = Scale(t.border_w);
    Color fill = t.card;
    Color text_color = t.text_primary;
    bool filled = true;
    bool with_border = true;

    if (!enabled) {
        fill = t.card;
        text_color = t.text_disabled;
        with_border = data.button_kind != ButtonKind::Ghost;
    } else if (data.button_kind == ButtonKind::Primary) {
        fill = data.press ? t.accent_press : (data.hover ? t.accent_hover : t.accent);
        text_color = Rgb(0xFF, 0xFF, 0xFF);
        with_border = false;
    } else if (data.button_kind == ButtonKind::Secondary) {
        fill = data.press ? Blend(t.card, t.text_primary, 10)
                          : (data.hover ? Blend(t.card, t.overlay, 100) : t.card);
        text_color = t.text_primary;
    } else {
        fill = data.press ? Blend(t.base, t.overlay, 100)
                          : (data.hover ? Blend(t.base, t.overlay, 100) : t.card);
        filled = data.hover || data.press;
        text_color = data.hover ? t.text_primary : t.text_secondary;
        with_border = false;
    }

    if (filled) {
        canvas.FillRoundRect(rc, radius, fill);
    }
    if (with_border) {
        canvas.StrokeRoundRect(rc, radius, border, t.border);
    }
    if (data.focus) {
        // 焦点矩形画在客户区内侧（画到窗口外会被裁掉），且必须在填充之后画，
        // 否则会被按钮底色整块盖住——看起来就像「键盘焦点不可见」。
        const int inset = Scale(1);
        RECT ring = Inset(rc, inset);
        canvas.StrokeRoundRect(ring, std::max(1, radius - inset), Scale(2),
                               data.button_kind == ButtonKind::Primary ? Rgb(0xFF, 0xFF, 0xFF)
                                                                       : t.accent);
    }
    TextStyle style;
    style.size = Scale(t.font_body);
    style.align = TextAlign::Center;
    canvas.Text(text, rc, style, text_color);
}

void PaintToggle(Canvas& canvas, const RECT& rc, const ControlData& data, const std::wstring& text) {
    const Theme& t = Current();
    const int track_w = Scale(38);
    const int track_h = Scale(22);
    const int cy = rc.top + RectHeight(rc) / 2;
    RECT track = MakeRect(rc.left, cy - track_h / 2, std::min(track_w, RectWidth(rc)), track_h);
    canvas.FillRoundRect(track, track_h / 2, data.on ? t.accent : t.border);
    const int knob_r = track_h / 2 - Scale(2);
    const int knob_cx = data.on ? track.right - knob_r - Scale(2) : track.left + knob_r + Scale(2);
    canvas.FillCircle(knob_cx, cy, knob_r, Rgb(0xFF, 0xFF, 0xFF));
    if (data.focus) {
        canvas.StrokeRoundRect(Inset(track, -Scale(2)), track_h / 2 + Scale(2), Scale(2), t.accent);
    }
    RECT label_rc = rc;
    label_rc.left = std::min(rc.right, track.right + Scale(t.space_s));
    TextStyle style;
    style.size = Scale(t.font_body);
    canvas.Text(text, label_rc, style, t.text_primary);
}

void PaintFieldBlockImpl(Canvas& canvas, const RECT& rc, const std::wstring& label,
                         const std::wstring& value, bool focused, bool error) {
    const Theme& t = Current();
    TextStyle caption;
    caption.size = Scale(t.font_caption);
    const int caption_h = Scale(t.font_caption * 3 / 2);
    RECT label_rc = MakeRect(rc.left, rc.top, RectWidth(rc), caption_h);
    canvas.Text(label, label_rc, caption, t.text_secondary);

    RECT box = MakeRect(rc.left, label_rc.bottom, RectWidth(rc),
                        std::max(0, RectHeight(rc) - caption_h));
    const int radius = Scale(t.radius_control);
    canvas.FillRoundRect(box, radius, t.base);
    Color border = error ? t.danger : (focused ? t.accent : t.border);
    canvas.StrokeRoundRect(box, radius, Scale(t.border_w), border);
    TextStyle body;
    body.size = Scale(t.font_body);
    RECT text_rc = Inset(box, Scale(t.space_s), 0);
    canvas.Text(value, text_rc, body, value.empty() ? t.text_disabled : t.text_primary);
}

void PaintLabel(Canvas& canvas, const RECT& rc, const ControlData& data, const std::wstring& text) {
    const Theme& t = Current();
    TextStyle style;
    style.size = Scale(t.font_body);
    canvas.Text(text, rc, style, data.warn ? t.warning : t.text_secondary);
}

void PaintPill(Canvas& canvas, const RECT& rc, const ControlData& data, const std::wstring& text) {
    DrawStatusPill(canvas, rc, text, data.tone);
}

void PaintControl(Canvas& canvas, const RECT& rc, HWND hwnd, ControlData* data) {
    if (data == nullptr) {
        return;
    }
    // 先铺底：子窗口区域不受父窗口绘制影响，不铺底就会露出未绘制内容。
    canvas.FillRect(rc, SurfaceColor(data->surface));
    const std::wstring text = WindowText(hwnd);
    switch (data->kind) {
        case ControlKind::Button:
            PaintButton(canvas, rc, *data, text, IsWindowEnabled(hwnd) != FALSE);
            break;
        case ControlKind::Toggle: PaintToggle(canvas, rc, *data, text); break;
        case ControlKind::Field:
            PaintFieldBlockImpl(canvas, rc, text, data->value, data->focus, data->error);
            break;
        case ControlKind::Label: PaintLabel(canvas, rc, *data, text); break;
        case ControlKind::Pill: PaintPill(canvas, rc, *data, text); break;
    }
}

void PaintStage(Canvas& canvas, const RECT& client, void* context) {
    auto* hwnd = static_cast<HWND>(context);
    PaintControl(canvas, client, hwnd, DataOf(hwnd));
}

LRESULT CALLBACK ControlProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_CREATE: {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
            auto* data = static_cast<ControlData*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(data));
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            RECT client{};
            GetClientRect(hwnd, &client);
            DrawBuffered(dc, client, PaintStage, hwnd);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_MOUSEMOVE: {
            ControlData* data = DataOf(hwnd);
            if (data == nullptr) {
                break;
            }
            if (!data->hover) {
                data->hover = true;
                Repaint(hwnd);
            }
            if (!data->tracking_leave) {
                TRACKMOUSEEVENT track{};
                track.cbSize = sizeof(track);
                track.dwFlags = TME_LEAVE;
                track.hwndTrack = hwnd;
                TrackMouseEvent(&track);
                data->tracking_leave = true;
            }
            return 0;
        }
        case WM_MOUSELEAVE: {
            ControlData* data = DataOf(hwnd);
            if (data != nullptr) {
                data->hover = false;
                data->press = false;
                data->tracking_leave = false;
                Repaint(hwnd);
            }
            return 0;
        }
        case WM_LBUTTONDOWN: {
            ControlData* data = DataOf(hwnd);
            if (data != nullptr && IsWindowEnabled(hwnd)) {
                data->press = true;
                SetCapture(hwnd);
                SetFocus(hwnd);
                Repaint(hwnd);
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            ControlData* data = DataOf(hwnd);
            if (GetCapture() == hwnd) {
                ReleaseCapture();
            }
            if (data == nullptr) {
                return 0;
            }
            const bool pressed = data->press;
            data->press = false;
            Repaint(hwnd);
            POINT point{};
            GetCursorPos(&point);
            ScreenToClient(hwnd, &point);
            RECT client{};
            GetClientRect(hwnd, &client);
            if (pressed && IsWindowEnabled(hwnd) && Contains(client, point)) {
                NotifyClick(hwnd, data);
            }
            return 0;
        }
        case BM_CLICK: {
            if (IsWindowEnabled(hwnd)) {
                NotifyClick(hwnd, DataOf(hwnd));
            }
            return 0;
        }
        case WM_KEYDOWN: {
            ControlData* data = DataOf(hwnd);
            if (data == nullptr) {
                break;
            }
            if (wparam == VK_SPACE || wparam == VK_RETURN) {
                data->press = true;
                Repaint(hwnd);
                return 0;
            }
            break;
        }
        case WM_KEYUP: {
            ControlData* data = DataOf(hwnd);
            if (data == nullptr) {
                break;
            }
            if (wparam == VK_SPACE || wparam == VK_RETURN) {
                data->press = false;
                Repaint(hwnd);
                NotifyClick(hwnd, data);
                return 0;
            }
            break;
        }
        case WM_SETFOCUS:
        case WM_KILLFOCUS: {
            ControlData* data = DataOf(hwnd);
            if (data != nullptr) {
                data->focus = message == WM_SETFOCUS;
                Repaint(hwnd);
            }
            return 0;
        }
        case WM_ENABLE:
            Repaint(hwnd);
            return 0;
        case WM_SETTEXT: {
            const LRESULT result = DefWindowProcW(hwnd, message, wparam, lparam);
            Repaint(hwnd);
            return result;
        }
        case WM_NCDESTROY: {
            delete DataOf(hwnd);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            break;
        }
        default: break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

HWND CreateControl(HWND parent, ControlKind kind, int id, const std::wstring& text,
                   const RECT& rc, ControlData* data, bool tabstop) {
    data->kind = kind;
    data->id = id;
    DWORD style = WS_CHILD | WS_VISIBLE;
    if (tabstop) {
        style |= WS_TABSTOP;
    }
    HWND control = CreateWindowExW(
        0, kControlClass, text.c_str(), style, rc.left, rc.top, RectWidth(rc), RectHeight(rc),
        parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(parent, GWLP_HINSTANCE)), data);
    if (control == nullptr) {
        delete data;
    }
    return control;
}

}  // namespace

bool RegisterControlClasses(HINSTANCE instance) {
    static bool registered = false;
    if (registered) {
        return true;
    }
    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls);
    cls.lpfnWndProc = ControlProc;
    cls.hInstance = instance;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hbrBackground = nullptr;  // 全部自绘，绝不让系统刷灰底
    cls.lpszClassName = kControlClass;
    if (RegisterClassExW(&cls) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }
    registered = true;
    return true;
}

HWND CreateButton(HWND parent, const ButtonDesc& desc, const RECT& rc) {
    auto* data = new ControlData();
    data->button_kind = desc.kind;
    data->surface = desc.surface;
    return CreateControl(parent, ControlKind::Button, desc.id, desc.text, rc, data, true);
}

HWND CreateToggle(HWND parent, const ToggleDesc& desc, const RECT& rc) {
    auto* data = new ControlData();
    data->on = desc.on;
    data->surface = desc.surface;
    return CreateControl(parent, ControlKind::Toggle, desc.id, desc.text, rc, data, true);
}

HWND CreateField(HWND parent, const FieldDesc& desc, const RECT& rc) {
    auto* data = new ControlData();
    data->value = desc.value;
    data->surface = desc.surface;
    return CreateControl(parent, ControlKind::Field, desc.id, desc.label, rc, data, true);
}

HWND CreateLabel(HWND parent, const LabelDesc& desc, const RECT& rc) {
    auto* data = new ControlData();
    data->warn = desc.warn;
    data->surface = desc.surface;
    return CreateControl(parent, ControlKind::Label, desc.id, desc.text, rc, data, false);
}

HWND CreatePill(HWND parent, const PillDesc& desc, const RECT& rc) {
    auto* data = new ControlData();
    data->tone = desc.tone;
    data->surface = desc.surface;
    return CreateControl(parent, ControlKind::Pill, desc.id, desc.text, rc, data, false);
}

void SetControlText(HWND control, const std::wstring& text) {
    if (control != nullptr) {
        SetWindowTextW(control, text.c_str());
    }
}

std::wstring GetControlText(HWND control) { return WindowText(control); }

void SetToggleValue(HWND control, bool on) {
    ControlData* data = DataOf(control);
    if (data != nullptr && data->on != on) {
        data->on = on;
        Repaint(control);
    }
}

bool ToggleValue(HWND control) {
    ControlData* data = DataOf(control);
    return data != nullptr && data->on;
}

void SetFieldValue(HWND control, const std::wstring& value) {
    ControlData* data = DataOf(control);
    if (data != nullptr) {
        data->value = value;
        Repaint(control);
    }
}

void SetFieldError(HWND control, bool error) {
    ControlData* data = DataOf(control);
    if (data != nullptr && data->error != error) {
        data->error = error;
        Repaint(control);
    }
}

void SetPillTone(HWND control, PillTone tone) {
    ControlData* data = DataOf(control);
    if (data != nullptr && data->tone != tone) {
        data->tone = tone;
        Repaint(control);
    }
}

void InvalidateControl(HWND control) {
    if (control != nullptr) {
        Repaint(control);
    }
}

void DrawCard(Canvas& canvas, const RECT& rc, bool elevated) {
    const Theme& t = Current();
    canvas.FillRoundRect(rc, Scale(t.radius_card), elevated ? t.overlay : t.card);
    canvas.StrokeRoundRect(rc, Scale(t.radius_card), Scale(t.border_w), t.border);
}

void DrawCardTitle(Canvas& canvas, const RECT& rc, const std::wstring& title) {
    const Theme& t = Current();
    TextStyle style;
    style.size = Scale(t.font_title);
    style.weight = FontWeight::Semibold;
    canvas.Text(title, rc, style, t.text_primary);
}

void DrawStatusPill(Canvas& canvas, const RECT& rc, const std::wstring& text, PillTone tone) {
    const Theme& t = Current();
    const Color accent = ToneColor(tone);
    RECT pill = rc;
    const int height = std::min(RectHeight(rc), Scale(t.pill_h));
    pill.top = rc.top + (RectHeight(rc) - height) / 2;
    pill.bottom = pill.top + height;
    canvas.FillRoundRect(pill, height / 2, Blend(t.card, accent, 18));
    const int dot_r = std::max(2, Scale(3));
    const int dot_cx = pill.left + Scale(t.space_s) + dot_r;
    canvas.FillCircle(dot_cx, pill.top + height / 2, dot_r, accent);
    RECT text_rc = pill;
    text_rc.left = dot_cx + dot_r + Scale(t.space_xs);
    text_rc.right = pill.right - Scale(t.space_s);
    TextStyle style;
    style.size = Scale(t.font_caption);
    canvas.Text(text, text_rc, style, accent);
}

void DrawFieldBlock(Canvas& canvas, const RECT& rc, const std::wstring& label,
                    const std::wstring& value, bool focused) {
    PaintFieldBlockImpl(canvas, rc, label, value, focused, false);
}

void DrawEmptyState(Canvas& canvas, const RECT& rc, const std::wstring& title,
                    const std::wstring& hint) {
    const Theme& t = Current();
    const int line = Scale(t.font_body * 3 / 2);
    const int total = line * 2 + Scale(t.space_s);
    int y = rc.top + std::max(0, (RectHeight(rc) - total) / 2);
    TextStyle body;
    body.size = Scale(t.font_body);
    body.align = TextAlign::Center;
    RECT title_rc = MakeRect(rc.left, y, RectWidth(rc), line);
    canvas.Text(title, title_rc, body, t.text_secondary);
    TextStyle caption;
    caption.size = Scale(t.font_caption);
    caption.align = TextAlign::Center;
    RECT hint_rc = MakeRect(rc.left, y + line + Scale(t.space_s), RectWidth(rc), line);
    canvas.Text(hint, hint_rc, caption, t.text_disabled);
}

}  // namespace sr::ui
