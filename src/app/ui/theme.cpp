#include "app/ui/theme.h"

#include <algorithm>
#include <cmath>
#include <cwctype>

namespace sr::ui {
namespace {

Theme MakeDark() {
    Theme t;
    t.name = L"dark";
    t.kind = ThemeKind::Dark;
    return t;
}

Theme MakeLight() {
    Theme t;
    t.name = L"light";
    t.kind = ThemeKind::Light;
    t.base = Rgb(0xF7, 0xF8, 0xFA);
    t.card = Rgb(0xFF, 0xFF, 0xFF);
    t.overlay = Rgb(0xEE, 0xF1, 0xF5);
    t.border = Rgb(0xD0, 0xD5, 0xDD);
    t.text_primary = Rgb(0x10, 0x18, 0x28);
    t.text_secondary = Rgb(0x47, 0x54, 0x67);
    t.text_disabled = Rgb(0x98, 0xA2, 0xB3);
    t.accent = Rgb(0x3B, 0x82, 0xF6);
    t.accent_hover = Rgb(0x2F, 0x6F, 0xDB);
    t.accent_press = Rgb(0x25, 0x5C, 0xB8);
    t.danger = Rgb(0xDC, 0x26, 0x26);
    t.success = Rgb(0x16, 0xA3, 0x4A);
    t.warning = Rgb(0xD9, 0x77, 0x06);
    t.header = Rgb(0xFF, 0xFF, 0xFF);
    t.statusbar = Rgb(0xF0, 0xF2, 0xF5);
    return t;
}

struct State {
    Theme dark = MakeDark();
    Theme light = MakeLight();
    ThemeKind kind = ThemeKind::Dark;
    UINT dpi = 96;
    double scale_override = 0.0;
};

State& Get() {
    static State state;
    return state;
}

}  // namespace

const Theme& Current() {
    State& state = Get();
    return state.kind == ThemeKind::Light ? state.light : state.dark;
}

ThemeKind Kind() { return Get().kind; }

void SetTheme(ThemeKind kind) { Get().kind = kind; }

bool ParseTheme(const std::wstring& text, ThemeKind& kind) {
    std::wstring lower = text;
    for (wchar_t& c : lower) {
        c = static_cast<wchar_t>(std::towlower(c));
    }
    if (lower == L"dark") {
        kind = ThemeKind::Dark;
        return true;
    }
    if (lower == L"light") {
        kind = ThemeKind::Light;
        return true;
    }
    return false;
}

void SetDpi(UINT dpi) { Get().dpi = dpi == 0 ? 96 : dpi; }

UINT Dpi() { return Get().dpi; }

void SetScaleOverride(double scale) { Get().scale_override = scale; }

double ScaleFactor() {
    State& state = Get();
    if (state.scale_override > 0.0) {
        return state.scale_override;
    }
    return static_cast<double>(state.dpi) / 96.0;
}

int Scale(int value) {
    const double scaled = static_cast<double>(value) * ScaleFactor();
    return static_cast<int>(std::lround(scaled));
}

COLORREF ToColorRef(Color c) { return RGB(c.r, c.g, c.b); }

Color Blend(Color a, Color b, int percent) {
    const int p = std::clamp(percent, 0, 100);
    auto mix = [p](uint8_t x, uint8_t y) {
        return static_cast<uint8_t>((static_cast<int>(x) * (100 - p) + static_cast<int>(y) * p) / 100);
    };
    return Color{mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b), 255};
}

Color Lighten(Color c, int amount) {
    const int delta = std::clamp(amount, 0, 255);
    auto up = [delta](uint8_t v) {
        return static_cast<uint8_t>(std::min(255, static_cast<int>(v) + delta));
    };
    return Color{up(c.r), up(c.g), up(c.b), c.a};
}

Color Darken(Color c, int amount) { return Blend(c, Rgb(0, 0, 0), std::clamp(amount, 0, 100)); }

std::wstring HexOf(Color c) {
    wchar_t buffer[16] = {};
    std::swprintf(buffer, std::size(buffer), L"#%02X%02X%02X", c.r, c.g, c.b);
    return buffer;
}

}  // namespace sr::ui
