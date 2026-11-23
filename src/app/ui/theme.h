// 轮 55（0.2-1）：视觉 token —— 颜色 / 字号 / 间距 / 圆角 / DPI 换算。
// 规范来源：docs/29-0.2启动与UI重构范围.md §4。所有自绘代码只准从这里取值，
// 禁止在绘制代码里写死像素与颜色（ui_theme.ps1 的像素断言就是照 token 判的）。
#pragma once

#include <windows.h>

#include <cstdint>
#include <string>

namespace sr::ui {

struct Color {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 255;
};

constexpr Color Rgb(uint8_t r, uint8_t g, uint8_t b) { return Color{r, g, b, 255}; }
constexpr Color Rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) { return Color{r, g, b, a}; }

enum class ThemeKind { Dark, Light };

// 一份主题 = 颜色 + 字号 + 间距 + 圆角 + 组件尺寸（均为 96dpi 设计值，绘制时经 Scale()）。
struct Theme {
    const wchar_t* name = L"dark";
    ThemeKind kind = ThemeKind::Dark;

    // 颜色
    Color base = Rgb(0x0F, 0x11, 0x15);          // 窗口底
    Color card = Rgb(0x16, 0x1A, 0x20);          // 卡片 / Header / 状态栏
    Color overlay = Rgb(0x1D, 0x23, 0x2B);       // 浮层 / hover 底
    Color border = Rgb(0x2A, 0x31, 0x3B);        // 描边
    Color text_primary = Rgb(0xE6, 0xEA, 0xF0);
    Color text_secondary = Rgb(0x98, 0xA2, 0xB3);
    Color text_disabled = Rgb(0x5A, 0x64, 0x72);
    Color accent = Rgb(0x3B, 0x82, 0xF6);        // 主按钮 / 强调
    Color accent_hover = Rgb(0x5B, 0x9C, 0xF8);
    Color accent_press = Rgb(0x2F, 0x6F, 0xDB);
    Color danger = Rgb(0xEF, 0x44, 0x44);
    Color success = Rgb(0x22, 0xC5, 0x5E);
    Color warning = Rgb(0xF5, 0x9E, 0x0B);
    Color header = Rgb(0x16, 0x1A, 0x20);
    Color statusbar = Rgb(0x16, 0x1A, 0x20);

    // 字号（px @96dpi）
    int font_display = 20;
    int font_title = 15;
    int font_body = 13;
    int font_caption = 12;

    // 间距（4/8/12/16/24 五档）
    int space_xs = 4;
    int space_s = 8;
    int space_m = 12;
    int space_l = 16;
    int space_xl = 24;

    // 圆角
    int radius_card = 8;
    int radius_control = 6;
    int radius_pill = 999;

    // 组件尺寸（96dpi 设计值）
    int header_h = 48;
    int status_h = 32;
    int panel_w = 320;
    int button_h = 36;
    int field_h = 32;
    int row_h = 28;
    int pill_h = 22;
    int border_w = 1;
};

const Theme& Current();
ThemeKind Kind();
void SetTheme(ThemeKind kind);
// --theme dark|light 的解析；不认识的返回 false（调用方保留默认深色）。
bool ParseTheme(const std::wstring& text, ThemeKind& kind);

// ---------------- DPI ----------------
void SetDpi(UINT dpi);
UINT Dpi();
// 测试钩子：--dpi-scale 1.25 强制缩放（0 表示跟随真实 DPI）。
void SetScaleOverride(double scale);
double ScaleFactor();
int Scale(int value);

// ---------------- 颜色工具 ----------------
COLORREF ToColorRef(Color c);
// 把 b 按 percent（0~100）混到 a 上。
Color Blend(Color a, Color b, int percent);
Color Lighten(Color c, int amount);
Color Darken(Color c, int amount);
std::wstring HexOf(Color c);

}  // namespace sr::ui
