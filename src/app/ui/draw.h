// 轮 55（0.2-1）：自绘基元 —— 圆角矩形 / 描边 / 单行文本 / 双缓冲。
// 实现用 GDI+（系统组件，不引三方库），开启抗锯齿；所有颜色与尺寸来自 theme token。
#pragma once

#include <windows.h>

#include <string>

#include "app/ui/theme.h"

namespace sr::ui {

enum class TextAlign { Left, Center, Right };
enum class VerticalAlign { Top, Middle, Bottom };
enum class FontWeight { Regular, Semibold };

enum class IconKind { None, Dot, ChevronDown, Gear, Folder, Close, Stop };

struct TextStyle {
    int size = 13;
    FontWeight weight = FontWeight::Regular;
    TextAlign align = TextAlign::Left;
    VerticalAlign valign = VerticalAlign::Middle;
};

// 一次绘制使用的画布。构造即绑定 HDC 与裁剪区，析构释放 GDI+ 资源。
class Canvas {
 public:
    Canvas(HDC hdc, const RECT& clip);
    ~Canvas();
    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;

    void FillRect(const RECT& rc, Color color);
    void FillRoundRect(const RECT& rc, int radius, Color color);
    void StrokeRoundRect(const RECT& rc, int radius, int width, Color color);
    void FillCircle(int center_x, int center_y, int radius, Color color);
    void Line(int x1, int y1, int x2, int y2, Color color, int width = 1);
    void Text(const std::wstring& text, const RECT& rc, TextStyle style, Color color);
    void TextLines(const std::wstring& text, const RECT& rc, TextStyle style, Color color,
                   int line_height);
    void Icon(IconKind kind, const RECT& rc, Color color);
    int MeasureText(const std::wstring& text, TextStyle style);
    int LineHeight(TextStyle style);

    void PushClip(const RECT& rc);
    void PopClip();

 private:
    struct Impl;
    Impl* impl_;
};

// GDI+ 生命周期（全进程一次；失败时自绘会退化为纯 GDI 填充，不影响功能）。
bool InitializeGdiplus();
void ShutdownGdiplus();

// 双缓冲绘制：在内存 DC 上画完一次性 BitBlt 到目标窗口，避免 WM_ERASEBKGND 白闪。
// usage: DrawBuffered(hdc, client, [](Canvas& c, const RECT& rc) { ... });
void DrawBuffered(HDC target, const RECT& client,
                  void (*body)(Canvas& canvas, const RECT& client, void* context), void* context);

}  // namespace sr::ui
