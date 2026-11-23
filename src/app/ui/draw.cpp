#include "app/ui/draw.h"

#include <algorithm>
#include <cmath>
#include <vector>

// MinGW 的 gdiplus 头里用到了 min/max 宏写法；本项目全局 NOMINMAX，这里补齐。
using std::max;
using std::min;

// gdiplus 依赖 PROPID（propidl/objidl），MinGW 下不会自动引入。
#include <objidl.h>
#include <propidl.h>
#include <gdiplus.h>

namespace sr::ui {
namespace {

Gdiplus::Color ToGdi(Color c) { return Gdiplus::Color(c.a, c.r, c.g, c.b); }

// 字体族只解析一次：Segoe UI Variable Text 优先，回退 Segoe UI。
Gdiplus::FontFamily* FontFamilyRef() {
    static Gdiplus::FontFamily* family = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        const wchar_t* candidates[] = {L"Segoe UI Variable Text", L"Segoe UI", L"Tahoma"};
        for (const wchar_t* name : candidates) {
            auto* candidate = new Gdiplus::FontFamily(name);
            if (candidate->IsAvailable()) {
                family = candidate;
                return family;
            }
            delete candidate;
        }
        family = new Gdiplus::FontFamily(L"Segoe UI");
    }
    return family;
}

Gdiplus::Font* MakeFont(int size_px, FontWeight weight) {
    const INT style = weight == FontWeight::Semibold ? Gdiplus::FontStyleBold
                                                    : Gdiplus::FontStyleRegular;
    return new Gdiplus::Font(FontFamilyRef(), static_cast<Gdiplus::REAL>(size_px), style,
                             Gdiplus::UnitPixel);
}

Gdiplus::Rect ToRect(const RECT& rc) {
    return Gdiplus::Rect(rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top);
}

Gdiplus::RectF ToRectF(const RECT& rc) {
    return Gdiplus::RectF(static_cast<Gdiplus::REAL>(rc.left), static_cast<Gdiplus::REAL>(rc.top),
                          static_cast<Gdiplus::REAL>(rc.right - rc.left),
                          static_cast<Gdiplus::REAL>(rc.bottom - rc.top));
}

Gdiplus::StringAlignment HAlign(TextAlign align) {
    switch (align) {
        case TextAlign::Center: return Gdiplus::StringAlignmentCenter;
        case TextAlign::Right: return Gdiplus::StringAlignmentFar;
        default: return Gdiplus::StringAlignmentNear;
    }
}

}  // namespace

bool InitializeGdiplus() {
    static ULONG_PTR token = 0;
    static bool started = false;
    if (started) {
        return token != 0;
    }
    started = true;
    Gdiplus::GdiplusStartupInput input;
    if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok) {
        token = 0;
    }
    return token != 0;
}

void ShutdownGdiplus() {
    // 进程退出时由系统回收；显式关闭会在静态析构顺序上踩坑，这里留空。
}

struct Canvas::Impl {
    HDC target = nullptr;
    Gdiplus::Graphics* graphics = nullptr;
    std::vector<Gdiplus::Rect> clip_stack;
};

Canvas::Canvas(HDC hdc, const RECT& clip) : impl_(new Impl()) {
    impl_->target = hdc;
    impl_->graphics = new Gdiplus::Graphics(hdc);
    Gdiplus::Graphics& g = *impl_->graphics;
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
    g.SetCompositingQuality(Gdiplus::CompositingQualityHighSpeed);
    impl_->clip_stack.push_back(ToRect(clip));
    g.SetClip(ToRect(clip));
}

Canvas::~Canvas() {
    delete impl_->graphics;
    delete impl_;
}

void Canvas::FillRect(const RECT& rc, Color color) {
    Gdiplus::SolidBrush brush(ToGdi(color));
    impl_->graphics->FillRectangle(&brush, ToRect(rc));
}

void Canvas::FillRoundRect(const RECT& rc, int radius, Color color) {
    Gdiplus::GraphicsPath path;
    const Gdiplus::Rect r = ToRect(rc);
    const int d = std::max(1, std::min(radius * 2, std::min(r.Width, r.Height)));
    if (d <= 1) {
        Gdiplus::SolidBrush brush(ToGdi(color));
        impl_->graphics->FillRectangle(&brush, r);
        return;
    }
    path.AddArc(r.X, r.Y, d, d, 180.0f, 90.0f);
    path.AddArc(r.X + r.Width - d, r.Y, d, d, 270.0f, 90.0f);
    path.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0.0f, 90.0f);
    path.AddArc(r.X, r.Y + r.Height - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
    Gdiplus::SolidBrush brush(ToGdi(color));
    impl_->graphics->FillPath(&brush, &path);
}

void Canvas::StrokeRoundRect(const RECT& rc, int radius, int width, Color color) {
    Gdiplus::GraphicsPath path;
    const Gdiplus::Rect r = ToRect(rc);
    const int d = std::max(1, std::min(radius * 2, std::min(r.Width, r.Height)));
    // 描边关抗锯齿 + 整数像素对齐：1px 线落在整像素上，颜色与 token 逐值一致
    // （ui_theme.ps1 的描边采样就是按 token 判的；抗锯齿会把 1px 线摊成半透明两行）。
    impl_->graphics->SetSmoothingMode(Gdiplus::SmoothingModeNone);
    impl_->graphics->SetPixelOffsetMode(Gdiplus::PixelOffsetModeNone);
    Gdiplus::Pen pen(ToGdi(color), static_cast<Gdiplus::REAL>(std::max(1, width)));
    if (d <= 1) {
        impl_->graphics->DrawRectangle(&pen, r);
    } else {
        path.AddArc(r.X, r.Y, d, d, 180.0f, 90.0f);
        path.AddArc(r.X + r.Width - d, r.Y, d, d, 270.0f, 90.0f);
        path.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0.0f, 90.0f);
        path.AddArc(r.X, r.Y + r.Height - d, d, d, 90.0f, 90.0f);
        path.CloseFigure();
        impl_->graphics->DrawPath(&pen, &path);
    }
    impl_->graphics->SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    impl_->graphics->SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
}

void Canvas::FillCircle(int center_x, int center_y, int radius, Color color) {
    Gdiplus::SolidBrush brush(ToGdi(color));
    impl_->graphics->FillEllipse(&brush, center_x - radius, center_y - radius, radius * 2,
                                 radius * 2);
}

void Canvas::Line(int x1, int y1, int x2, int y2, Color color, int width) {
    Gdiplus::Pen pen(ToGdi(color), static_cast<Gdiplus::REAL>(std::max(1, width)));
    impl_->graphics->DrawLine(&pen, x1, y1, x2, y2);
}

int Canvas::LineHeight(TextStyle style) {
    return static_cast<int>(std::ceil(static_cast<double>(style.size) * 1.5));
}

int Canvas::MeasureText(const std::wstring& text, TextStyle style) {
    Gdiplus::Font* font = MakeFont(style.size, style.weight);
    Gdiplus::RectF bounds;
    impl_->graphics->MeasureString(text.c_str(), -1, font, Gdiplus::PointF(0, 0), &bounds);
    delete font;
    return static_cast<int>(std::ceil(bounds.Width));
}

void Canvas::Text(const std::wstring& text, const RECT& rc, TextStyle style, Color color) {
    if (text.empty()) {
        return;
    }
    Gdiplus::Font* font = MakeFont(style.size, style.weight);
    Gdiplus::StringFormat format;
    format.SetAlignment(HAlign(style.align));
    switch (style.valign) {
        case VerticalAlign::Top: format.SetLineAlignment(Gdiplus::StringAlignmentNear); break;
        case VerticalAlign::Bottom: format.SetLineAlignment(Gdiplus::StringAlignmentFar); break;
        default: format.SetLineAlignment(Gdiplus::StringAlignmentCenter); break;
    }
    format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
    Gdiplus::SolidBrush brush(ToGdi(color));
    impl_->graphics->DrawString(text.c_str(), -1, font, ToRectF(rc), &format, &brush);
    delete font;
}

void Canvas::TextLines(const std::wstring& text, const RECT& rc, TextStyle style, Color color,
                       int line_height) {
    if (text.empty()) {
        return;
    }
    Gdiplus::Font* font = MakeFont(style.size, style.weight);
    Gdiplus::SolidBrush brush(ToGdi(color));
    int y = rc.top;
    size_t start = 0;
    while (start <= text.size() && y + line_height <= rc.bottom + line_height / 2) {
        size_t end = text.find(L'\n', start);
        const std::wstring line =
            text.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
        RECT line_rc{rc.left, y, rc.right, y + line_height};
        Gdiplus::StringFormat format;
        format.SetAlignment(HAlign(style.align));
        format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
        format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
        impl_->graphics->DrawString(line.c_str(), -1, font, ToRectF(line_rc), &format, &brush);
        y += line_height;
        if (end == std::wstring::npos) {
            break;
        }
        start = end + 1;
    }
    delete font;
}

void Canvas::Icon(IconKind kind, const RECT& rc, Color color) {
    if (kind == IconKind::None) {
        return;
    }
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    const int cx = rc.left + w / 2;
    const int cy = rc.top + h / 2;
    const int unit = std::max(2, std::min(w, h));
    Gdiplus::SolidBrush brush(ToGdi(color));
    Gdiplus::Pen pen(ToGdi(color), static_cast<Gdiplus::REAL>(std::max(1.0, unit / 10.0)));
    switch (kind) {
        case IconKind::Dot:
            impl_->graphics->FillEllipse(&brush, cx - unit / 4, cy - unit / 4, unit / 2, unit / 2);
            break;
        case IconKind::Stop:
            impl_->graphics->FillRectangle(&brush, cx - unit / 4, cy - unit / 4, unit / 2, unit / 2);
            break;
        case IconKind::ChevronDown:
            impl_->graphics->DrawLine(&pen, cx - unit / 4, cy - unit / 8, cx, cy + unit / 8);
            impl_->graphics->DrawLine(&pen, cx, cy + unit / 8, cx + unit / 4, cy - unit / 8);
            break;
        case IconKind::Gear:
            impl_->graphics->DrawEllipse(&pen, cx - unit / 3, cy - unit / 3, unit * 2 / 3,
                                         unit * 2 / 3);
            impl_->graphics->FillEllipse(&brush, cx - unit / 10, cy - unit / 10, unit / 5,
                                         unit / 5);
            break;
        case IconKind::Folder: {
            const int fw = unit * 2 / 3;
            const int fh = unit / 2;
            impl_->graphics->DrawRectangle(&pen, cx - fw / 2, cy - fh / 2, fw, fh);
            impl_->graphics->DrawLine(&pen, cx - fw / 2, cy - fh / 2 - unit / 8,
                                      cx - fw / 2 + fw / 3, cy - fh / 2 - unit / 8);
            break;
        }
        case IconKind::Close:
            impl_->graphics->DrawLine(&pen, cx - unit / 4, cy - unit / 4, cx + unit / 4,
                                      cy + unit / 4);
            impl_->graphics->DrawLine(&pen, cx + unit / 4, cy - unit / 4, cx - unit / 4,
                                      cy + unit / 4);
            break;
        default: break;
    }
}

void Canvas::PushClip(const RECT& rc) {
    impl_->clip_stack.push_back(ToRect(rc));
    impl_->graphics->SetClip(ToRect(rc), Gdiplus::CombineModeIntersect);
}

void Canvas::PopClip() {
    if (impl_->clip_stack.size() <= 1) {
        return;
    }
    impl_->clip_stack.pop_back();
    const Gdiplus::Rect& current = impl_->clip_stack.back();
    impl_->graphics->SetClip(current);
}

void DrawBuffered(HDC target, const RECT& client,
                  void (*body)(Canvas& canvas, const RECT& client, void* context), void* context) {
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width <= 0 || height <= 0 || body == nullptr) {
        return;
    }
    HDC memory = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, width, height);
    if (memory == nullptr || bitmap == nullptr) {
        if (bitmap != nullptr) {
            DeleteObject(bitmap);
        }
        if (memory != nullptr) {
            DeleteDC(memory);
        }
        Canvas canvas(target, client);
        body(canvas, client, context);
        return;
    }
    HGDIOBJ old_bitmap = SelectObject(memory, bitmap);
    // 内存 DC 的坐标原点是 (0,0)，把绘制区平移到客户区原点。
    SetViewportOrgEx(memory, -client.left, -client.top, nullptr);
    RECT local{0, 0, width, height};
    {
        Canvas canvas(memory, local);
        body(canvas, client, context);
    }
    SetViewportOrgEx(memory, 0, 0, nullptr);
    BitBlt(target, client.left, client.top, width, height, memory, 0, 0, SRCCOPY);
    SelectObject(memory, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(memory);
}

}  // namespace sr::ui
