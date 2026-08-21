#include "ui/term/screen.hpp"

#include <algorithm>

namespace amberedit::ui::term {

Screen::Screen(int width, int height) { resize(width, height); }

Cell& Screen::at(int x, int y) {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) {
        scratch_ = Cell{};
        return scratch_;
    }
    return cells_[static_cast<size_t>(y) * static_cast<size_t>(width_) +
                  static_cast<size_t>(x)];
}

const Cell& Screen::at(int x, int y) const {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return scratch_;
    return cells_[static_cast<size_t>(y) * static_cast<size_t>(width_) +
                  static_cast<size_t>(x)];
}

void Screen::clear() { std::fill(cells_.begin(), cells_.end(), Cell{}); }

void Screen::resize(int width, int height) {
    width_ = std::max(0, width);
    height_ = std::max(0, height);
    cells_.assign(static_cast<size_t>(width_) * static_cast<size_t>(height_), Cell{});
    stencil = Box{0, width_ - 1, 0, height_ - 1};
}

}  // namespace amberedit::ui::term
