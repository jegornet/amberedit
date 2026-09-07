#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "ui/text_editor.hpp"

namespace amberedit::ui {

/// One row of the editor as it is drawn: which line of the buffer stands on it,
/// and which bytes of that line.
///
/// A line wider than the window occupies several rows. Nothing is put into the
/// text to make that happen — a message keeps the lines it was written with,
/// and a carriage return the user did not type is one the reader at the other
/// end would show as a line the user did not write. Where the window breaks a
/// line is the window's business alone, and this is where it is decided.
struct EditRow {
    /// The line of the buffer, its index in `TextBuffer::lines`.
    int line{0};
    /// The bytes of that line the row shows, [begin, end). The rows of a line
    /// divide it: every byte stands on exactly one of them.
    size_t begin{0};
    size_t end{0};
};

/// The buffer as it is drawn `width` columns wide, top to bottom. Every line
/// has a row, an empty one included — there is a cursor to put on it.
///
/// **The rightmost column of the window is the cursor's**: the text is broken a
/// column short of it and nothing is ever drawn there. A cursor at the end of a
/// row stands in that column, so no row is ever scrolled sideways to keep it on
/// screen — the text sliding out from under the first column at the very moment
/// the window has a row to spare is what reserving the column avoids. It is
/// also what the user is shown next: with a letter in the second column from
/// the edge and the cursor in the last, the next letter typed does not go into
/// that column, it takes its whole word down to the row below.
///
/// The width is the same for every line, the cursor's among them. A line broken
/// against where the cursor happens to stand would rewrap under the arrow keys.
[[nodiscard]] std::vector<EditRow> layoutRows(const TextBuffer& buffer, int width);

/// Which row the cursor stands on. A cursor on a break belongs to the row
/// below, that being where the character it is on is drawn; at the end of a
/// line it stays on the last row of it, where it is what the typing goes on
/// from.
[[nodiscard]] size_t rowOfCursor(const std::vector<EditRow>& rows, int line, size_t col);

/// Moves the cursor `delta` rows down the text as it is drawn, negative for up,
/// keeping the column it stands in as nearly as the row it lands on allows.
///
/// This is what the arrows move by: a line that takes four rows is four presses
/// tall, because four rows is what the user sees. A row that a line goes on
/// past holds the cursor on its last character rather than past its end — past
/// the end of such a row is the beginning of the next one, and a Down that
/// landed there would look like it had moved two.
void moveByRows(TextBuffer& buffer, const std::vector<EditRow>& rows, int delta,
                int width);

}  // namespace amberedit::ui
