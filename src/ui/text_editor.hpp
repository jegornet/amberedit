#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace amberedit::ui {

/// The text of a message being written, and where the cursor stands in it.
///
/// Lines rather than one string, because that is what a message is: FTS-0001
/// ends each with a hard carriage return, and nothing here has to remember it.
/// The column is a byte offset and always sits on a character boundary.
struct TextBuffer {
    std::vector<std::string> lines{std::string{}};
    int row{0};
    size_t col{0};

    [[nodiscard]] const std::string& line() const {
        return lines[static_cast<size_t>(row)];
    }
    [[nodiscard]] std::string& line() { return lines[static_cast<size_t>(row)]; }
};

/// The column a quoted line is wrapped at while it is being typed, under its
/// own prefix — which is what makes this an editor for FTN mail rather than a
/// general one. Lines the user writes themselves are left alone: the margin is
/// about how quoted text is laid out, not about how long a sentence may be.
struct EditOptions {
    int margin{78};
};

/// Inserts text at the cursor, and wraps the line under its quote prefix if
/// that took a quote past the margin. The text is one character in practice —
/// what the terminal reported.
void insertText(TextBuffer& buffer, std::string_view text, const EditOptions& options);

/// Splits the line at the cursor. A line that is a quote carries its prefix
/// onto the new line: what is split out of the middle of a quote is more of
/// the same quote. Splitting at either end of one does not — there the new
/// line is empty, which is where an answer to the quote goes.
void insertNewline(TextBuffer& buffer);

/// Deletes the character before the cursor, joining the line to the one above
/// when the cursor stands at its start.
void deleteBefore(TextBuffer& buffer);

/// Deletes the character under the cursor, joining the line below to this one
/// when the cursor stands at its end.
void deleteAt(TextBuffer& buffer);

/// Deletes the whole line the cursor is on.
void deleteLine(TextBuffer& buffer);

/// Takes out the block of quoted text the cursor stands in: from the line it
/// is on down to the first line that is neither a quote nor blank, which stays.
///
/// This is what the reply of somebody who quotes the whole message and then
/// answers one paragraph of it is made with. On a line that is neither a quote
/// nor blank it does nothing at all: there is no block there to take out, and
/// deleting the answer instead of the quote would be the one mistake worth
/// avoiding here.
void deleteQuote(TextBuffer& buffer);

/// Deletes the word before the cursor, and the separators between the two —
/// what Ctrl-W erases in every terminal that ever had a line discipline.
///
/// Within the line and no further: a cursor standing at the start of one has no
/// word before it and nothing happens, the line break being Backspace's to take
/// out. A word is what `moveWordLeft()` walks over, so the two agree about where
/// one begins.
void deleteWordBefore(TextBuffer& buffer);

void moveLeft(TextBuffer& buffer);
void moveRight(TextBuffer& buffer);
void moveToLineStart(TextBuffer& buffer);
void moveToLineEnd(TextBuffer& buffer);

/// Up and down are not here: a line wider than the window is drawn over
/// several rows, and moving by lines would jump the cursor over all of them at
/// once. `ui::moveByRows()` in `edit_layout.hpp` moves by what is on screen,
/// which is what the arrows are for.

/// A word at a time, the way Alt+F and Alt+B have always moved: forward to the
/// end of the next word, back to the start of the previous one. A word is a run
/// of letters and digits, so the markers and spaces of a quote prefix are
/// stepped over like any other punctuation, and a line with no word left on it
/// carries the cursor on to the next.
void moveWordRight(TextBuffer& buffer);
void moveWordLeft(TextBuffer& buffer);

/// The text with the trailing empty lines dropped — what a message is saved
/// as. A blank line the user left at the end is padding, not content.
[[nodiscard]] std::vector<std::string> trimmedLines(const TextBuffer& buffer);

}  // namespace amberedit::ui
