#include "app/ui/layout.h"

#include <algorithm>

namespace sr::ui {

RECT MakeRect(int x, int y, int w, int h) { return RECT{x, y, x + w, y + h}; }

int RectWidth(const RECT& rc) { return rc.right - rc.left; }

int RectHeight(const RECT& rc) { return rc.bottom - rc.top; }

RECT Inset(const RECT& rc, int all) { return Inset(rc, all, all); }

RECT Inset(const RECT& rc, int dx, int dy) {
    return RECT{rc.left + dx, rc.top + dy, rc.right - dx, rc.bottom - dy};
}

bool Contains(const RECT& rc, POINT pt) {
    return pt.x >= rc.left && pt.x < rc.right && pt.y >= rc.top && pt.y < rc.bottom;
}

bool RectEmpty(const RECT& rc) { return RectWidth(rc) <= 0 || RectHeight(rc) <= 0; }

VStack::VStack(const RECT& bounds, int gap) : bounds_(bounds), gap_(gap), cursor_(bounds.top) {}

RECT VStack::Next(int height) { return Next(height, RectWidth(bounds_)); }

RECT VStack::Next(int height, int width) {
    const int x = bounds_.left;
    const int width_used = std::min(width, RectWidth(bounds_));
    if (!first_) {
        cursor_ += gap_;
    }
    first_ = false;
    RECT rc = MakeRect(x, cursor_, width_used, height);
    cursor_ = rc.bottom;
    return rc;
}

RECT VStack::NextRight(int height, int width) {
    int y = cursor_;
    if (!first_) {
        y += gap_;
    }
    return MakeRect(bounds_.right - width, y, width, height);
}

void VStack::Skip(int pixels) {
    cursor_ += pixels;
    first_ = false;
}

RECT VStack::Remaining() const {
    RECT rc = bounds_;
    rc.top = cursor_;
    if (RectHeight(rc) < 0) {
        return MakeRect(bounds_.left, cursor_, RectWidth(bounds_), 0);
    }
    return rc;
}

int VStack::Available() const {
    return std::max(0, static_cast<int>(bounds_.bottom - cursor_));
}

HStack::HStack(const RECT& bounds, int gap)
    : bounds_(bounds), gap_(gap), cursor_(bounds.left), tail_(bounds.right) {}

RECT HStack::Next(int width) {
    if (first_) {
        first_ = false;
    } else {
        cursor_ += gap_;
    }
    const int available = std::max(0, static_cast<int>(tail_ - cursor_));
    const int used = std::min(width, available);
    RECT rc = MakeRect(cursor_, bounds_.top, used, RectHeight(bounds_));
    cursor_ = rc.right;
    return rc;
}

RECT HStack::NextRight(int width) {
    const int used = std::min(width, std::max(0, static_cast<int>(tail_ - cursor_)));
    tail_ -= used;
    RECT rc = MakeRect(tail_, bounds_.top, used, RectHeight(bounds_));
    if (tail_ > cursor_) {
        tail_ -= gap_;
    }
    return rc;
}

void HStack::Skip(int pixels) { cursor_ += pixels; }

RECT HStack::Remaining() const {
    return MakeRect(cursor_, bounds_.top, std::max(0, static_cast<int>(tail_ - cursor_)), RectHeight(bounds_));
}

int HStack::Available() const { return std::max(0, static_cast<int>(tail_ - cursor_)); }

Grid::Grid(const RECT& bounds, int columns, int gap_x, int gap_y)
    : bounds_(bounds),
      columns_(std::max(1, columns)),
      gap_x_(gap_x),
      gap_y_(gap_y) {}

RECT Grid::Cell(int index) const { return Cell(index % columns_, index / columns_); }

RECT Grid::Cell(int column, int row) const {
    const int total_w = RectWidth(bounds_) - gap_x_ * (columns_ - 1);
    const int cell_w = std::max(0, total_w / columns_);
    const int cell_h = std::max(0, RectHeight(bounds_) - gap_y_ * (Rows() - 1)) /
                       std::max(1, Rows());
    const int x = bounds_.left + column * (cell_w + gap_x_);
    const int y = bounds_.top + row * (cell_h + gap_y_);
    return MakeRect(x, y, cell_w, cell_h);
}

int Grid::Rows() const {
    const int h = RectHeight(bounds_);
    if (h <= 0) {
        return 1;
    }
    return std::max(1, (h + gap_y_) / std::max(1, 28 + gap_y_));
}

int ClampMin(int value, int minimum) { return std::max(value, minimum); }

int ClampMax(int value, int maximum) { return std::min(value, maximum); }

int ClampRange(int value, int minimum, int maximum) {
    return std::min(std::max(value, minimum), maximum);
}

bool Overlaps(const RECT& a, const RECT& b) {
    return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
}

bool Within(const RECT& inner, const RECT& outer) {
    return inner.left >= outer.left && inner.top >= outer.top && inner.right <= outer.right &&
           inner.bottom <= outer.bottom;
}

}  // namespace sr::ui
