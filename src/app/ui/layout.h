// 轮 55（0.2-1）：布局原语 —— 纵向栈 / 横向栈 / 网格 / 最小尺寸约束。
// 只做几何计算，不碰绘制；尺寸一律取 theme token（96dpi 设计值），由调用方先 Scale()。
#pragma once

#include <windows.h>

#include <vector>

namespace sr::ui {

RECT MakeRect(int x, int y, int w, int h);
int RectWidth(const RECT& rc);
int RectHeight(const RECT& rc);
// 内缩：all 或 (dx,dy)
RECT Inset(const RECT& rc, int all);
RECT Inset(const RECT& rc, int dx, int dy);
bool Contains(const RECT& rc, POINT pt);
bool RectEmpty(const RECT& rc);

// 纵向栈：每次 Next() 取一行，自动扣 gap。
class VStack {
 public:
    VStack(const RECT& bounds, int gap);

    RECT Next(int height);                 // 占满宽度
    RECT Next(int height, int width);      // 左对齐固定宽度
    RECT NextRight(int height, int width); // 右对齐固定宽度（不推进游标）
    void Skip(int pixels);
    RECT Remaining() const;
    int Cursor() const { return cursor_; }
    int Available() const;

 private:
    RECT bounds_{};
    int gap_ = 0;
    int cursor_ = 0;
    bool first_ = true;
};

// 横向栈：用于 Header 里的标题 + 状态药丸 + 右侧按钮组。
class HStack {
 public:
    HStack(const RECT& bounds, int gap);

    RECT Next(int width);
    RECT NextRight(int width);
    void Skip(int pixels);
    RECT Remaining() const;
    int Available() const;

 private:
    RECT bounds_{};
    int gap_ = 0;
    int cursor_ = 0;
    int tail_ = 0;
    bool first_ = false;
};

// 等宽网格（成片列表的空态占位、设置面板的字段行）。
class Grid {
 public:
    Grid(const RECT& bounds, int columns, int gap_x, int gap_y);

    RECT Cell(int index) const;
    RECT Cell(int column, int row) const;
    int Columns() const { return columns_; }
    int Rows() const;

 private:
    RECT bounds_{};
    int columns_ = 1;
    int gap_x_ = 0;
    int gap_y_ = 0;
};

// 最小尺寸约束：resize 时保证控件不小于可用空间，返回实际可用值。
int ClampMin(int value, int minimum);
int ClampMax(int value, int maximum);
// 在 [minimum, maximum] 之间取值。
int ClampRange(int value, int minimum, int maximum);
// 判断两个矩形是否重叠（resize 断言用：布局结果不得互相压盖）。
bool Overlaps(const RECT& a, const RECT& b);
// 判断矩形是否完全落在容器内（越界断言用）。
bool Within(const RECT& inner, const RECT& outer);

}  // namespace sr::ui
