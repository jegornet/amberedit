#pragma once

#include <algorithm>

#include "ui/term/element.hpp"
#include "ui/theme.hpp"

/// The one-column bar drawn beside anything too long for the window: the
/// message being read, the message being written, the two lists and the
/// nodelist's box.
///
/// They all draw it from here rather than each from its own arithmetic, so that
/// a message being written looks like the one it will be read as, and a list
/// scrolls the way the message in it does. What differs is how it reaches the
/// screen: the reader and the lists hand over a column beside the whole of what
/// scrolls (`bar()`), while the editor's rows are counted one by one against the
/// height of the window — the blank fill under them follows from that — and the
/// nodelist's are written into a frame row by row, so those two take the bar a
/// cell at a time (`cell()`).
namespace amberedit::ui::scrollbar {

/// Which rows of the viewport the thumb covers, [first, last).
struct Thumb {
    int first{0};
    int last{0};
};

/// Where the thumb stands when `total` rows of text are seen `height` rows at a
/// time, scrolled to `scroll`. Text that fits fills the bar: there is nothing
/// for the thumb to point at, and an empty track would say the opposite.
[[nodiscard]] inline Thumb thumbOf(int height, int total, int scroll) {
    const int rows = std::max(1, height);
    if (total <= rows) return {0, rows};

    const int size = std::max(1, rows * rows / total);
    const int scrollRange = total - rows;
    const int thumbRange = rows - size;
    const int first = scrollRange > 0 ? scroll * thumbRange / scrollRange : 0;
    return {first, first + size};
}

/// The bar's cell on row `index` of the viewport, counted from its top.
[[nodiscard]] inline term::Element cell(int index, const Thumb& thumb) {
    const bool onThumb = index >= thumb.first && index < thumb.last;
    return term::text(onThumb ? "█" : "│") |
           term::color(onThumb ? theme::palette.scrollThumb
                               : theme::palette.scrollTrack);
}

/// The whole bar, as tall as the viewport.
[[nodiscard]] inline term::Element bar(int height, int total, int scroll) {
    const int rows = std::max(1, height);
    const Thumb thumb = thumbOf(rows, total, scroll);

    term::Elements cells;
    cells.reserve(static_cast<size_t>(rows));
    for (int i = 0; i < rows; ++i) cells.push_back(cell(i, thumb));
    return term::vbox(std::move(cells));
}

}  // namespace amberedit::ui::scrollbar
