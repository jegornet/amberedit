#include "ui/edit_layout.hpp"

#include <algorithm>
#include <string_view>

#include "ui/text_layout.hpp"

namespace amberedit::ui {
namespace {

/// The part of `line` a row shows. Clamped rather than trusted: a layout is
/// worked out from the buffer as it stood, and the buffer can have changed
/// under it since — a click is answered after the template may have been
/// expanded again.
std::string_view textOf(const std::string& line, const EditRow& row) {
    const size_t begin = std::min(row.begin, line.size());
    const size_t end = std::clamp(row.end, begin, line.size());
    return std::string_view(line).substr(begin, end - begin);
}

/// The byte of `line` the cursor stands on when it comes down onto `row` at
/// `column`, counted in terminal columns from the row's left edge.
size_t offsetAtColumn(const std::string& line, const EditRow& row, int column,
                      int textWidth) {
    const std::string_view text = textOf(line, row);

    // How far into the row the cursor may go. A row that closes its line has
    // the place past its last character — that is where the typing carries on
    // from. A row the line goes on past has not: the byte one past its end is
    // the first byte of the row below, and the cursor would be drawn there.
    int limit = displayWidth(text);
    if (row.end < line.size()) limit = std::max(0, std::min(limit, textWidth) - 1);

    return std::min(row.begin, line.size()) +
           substrByWidth(text, 0, std::min(column, limit)).size();
}

/// The columns the text is broken at in a window `width` wide: one short of it,
/// the last being the cursor's. A window of one column has nowhere to keep it
/// and holds text instead — there is no window that narrow, and a width of
/// nothing to write in would be worse than a cursor drawn over a letter.
int textWidthOf(int width) { return std::max(1, width - 1); }

}  // namespace

std::vector<EditRow> layoutRows(const TextBuffer& buffer, int width) {
    std::vector<EditRow> rows;
    rows.reserve(buffer.lines.size());
    const int textWidth = textWidthOf(width);
    for (size_t i = 0; i < buffer.lines.size(); ++i) {
        const std::string& line = buffer.lines[i];

        // A column short of the window, so that the cursor standing past the
        // end of a row has one the window holds to be drawn in. Every line is
        // broken the same way, the one the cursor is on included: a line whose
        // width followed the cursor would rewrap under the arrow keys.
        const std::vector<size_t> starts = softWrapOffsets(line, textWidth);
        for (size_t k = 0; k < starts.size(); ++k) {
            const size_t begin = std::min(starts[k], line.size());
            const size_t end = k + 1 < starts.size()
                                   ? std::min(starts[k + 1], line.size())
                                   : line.size();
            rows.push_back({static_cast<int>(i), begin, end});
        }
    }
    // A buffer with no lines at all is not one the editor ever has, but a row
    // for the cursor to be on costs one line here and saves every caller a
    // check for an empty vector.
    if (rows.empty()) rows.push_back(EditRow{});
    return rows;
}

size_t rowOfCursor(const std::vector<EditRow>& rows, int line, size_t col) {
    size_t last = 0;
    for (size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].line != line) continue;
        if (col < rows[i].end) return i;
        last = i;
    }
    return last;
}

void moveByRows(TextBuffer& buffer, const std::vector<EditRow>& rows, int delta,
                int width) {
    if (rows.empty() || buffer.lines.empty()) return;

    const size_t at = rowOfCursor(rows, buffer.row, buffer.col);
    const EditRow& from = rows[at];
    const int last = static_cast<int>(buffer.lines.size()) - 1;
    const std::string& line =
        buffer.lines[static_cast<size_t>(std::clamp(from.line, 0, last))];
    const std::string_view text = textOf(line, from);
    const size_t within =
        std::min(buffer.col > from.begin ? buffer.col - from.begin : 0, text.size());
    const int column = displayWidth(text.substr(0, within));

    const int target =
        std::clamp(static_cast<int>(at) + delta, 0, static_cast<int>(rows.size()) - 1);
    const EditRow& onto = rows[static_cast<size_t>(target)];
    buffer.row = std::clamp(onto.line, 0, static_cast<int>(buffer.lines.size()) - 1);
    buffer.col = offsetAtColumn(buffer.line(), onto, column, textWidthOf(width));
}

}  // namespace amberedit::ui
