#include "ui/text_editor.hpp"

#include <algorithm>
#include <cctype>

#include "app/quoting.hpp"

namespace amberedit::ui {
namespace {

/// Where the character before `at` starts. Continuation bytes are stepped over
/// so the cursor moves by characters: a Cyrillic letter is two bytes, and
/// half of one is not a place a cursor may stand.
size_t prevBoundary(const std::string& text, size_t at) {
    if (at == 0) return 0;
    size_t i = at - 1;
    while (i > 0 && (static_cast<unsigned char>(text[i]) & 0xC0u) == 0x80u) --i;
    return i;
}

size_t charLength(const std::string& text, size_t at) {
    if (at >= text.size()) return 0;
    size_t end = at + 1;
    while (end < text.size() && (static_cast<unsigned char>(text[end]) & 0xC0u) == 0x80u)
        ++end;
    return end - at;
}

size_t charCount(std::string_view text) {
    size_t count = 0;
    for (const char byte : text) {
        if ((static_cast<unsigned char>(byte) & 0xC0u) != 0x80u) ++count;
    }
    return count;
}

/// Whether the byte belongs to a word. Everything outside ASCII counts as a
/// letter: a Cyrillic word is a word, and looking any closer than that would
/// mean carrying a character-class table for no gain here.
bool isWordByte(char byte) {
    const auto value = static_cast<unsigned char>(byte);
    return value >= 0x80 || std::isalnum(value) != 0;
}

/// The byte offset of the `count`-th character.
size_t byteOffsetOf(const std::string& text, size_t count) {
    size_t pos = 0;
    for (size_t i = 0; i < count && pos < text.size(); ++i) pos += charLength(text, pos);
    return pos;
}

/// Puts the cursor back on a character boundary — where a line has just been
/// rebuilt underneath it.
void clampColumn(TextBuffer& buffer) {
    buffer.row = std::clamp(buffer.row, 0, static_cast<int>(buffer.lines.size()) - 1);
    auto& line = buffer.line();
    if (buffer.col > line.size()) buffer.col = line.size();
    while (buffer.col > 0 &&
           (static_cast<unsigned char>(line[buffer.col]) & 0xC0u) == 0x80u) {
        --buffer.col;
    }
}

/// The quote prefix a line's continuation carries, empty when the line is not
/// a quote. What wraps under " AB> " has to come out as " AB> " too, or the
/// second half of a quoted sentence reads as the answer to it.
std::string continuationPrefix(const std::string& line) {
    const auto prefix = app::parseQuotePrefix(line);
    if (prefix.level == 0) return {};
    return line.substr(0, prefix.length);
}

/// Wraps the quoted line the cursor is on if it has grown past the margin,
/// carrying the cursor along when it stood in the part that moved down.
void wrapCurrentLine(TextBuffer& buffer, const EditOptions& options) {
    const auto margin = static_cast<size_t>(std::max(1, options.margin));

    // Wrapping walks forward from the line that grew: what breaks off the end
    // of a quote is a quote too, and can be past the margin in its turn.
    for (int row = buffer.row; row < static_cast<int>(buffer.lines.size()); ++row) {
        const std::string line = buffer.lines[static_cast<size_t>(row)];

        const std::string prefix = continuationPrefix(line);
        // The margin belongs to quoted text alone. What the user writes is
        // their own line, however long they make it: an editor that breaks it
        // under the typing hand is fighting whoever is typing.
        if (prefix.empty()) break;
        if (charCount(line) <= margin) break;

        // A quote prefix as wide as the whole margin leaves nowhere to put the
        // text; the line stays long rather than being cut to pieces of nothing.
        if (charCount(prefix) >= margin) break;

        // Break at the last space that leaves the head inside the margin. One
        // long word has nowhere to break and is cut where the margin falls.
        const size_t limit = byteOffsetOf(line, margin);
        size_t breakAt = line.find_last_of(' ', limit);
        if (breakAt == std::string::npos || breakAt <= prefix.size()) breakAt = limit;

        size_t tailAt = breakAt;
        while (tailAt < line.size() && line[tailAt] == ' ') ++tailAt;
        if (tailAt >= line.size()) break;  // only spaces past the margin

        buffer.lines[static_cast<size_t>(row)] = line.substr(0, breakAt);
        buffer.lines.insert(buffer.lines.begin() + row + 1, prefix + line.substr(tailAt));

        if (buffer.row == row && buffer.col >= tailAt) {
            // The cursor stood in the part that moved down; it goes with it.
            buffer.row = row + 1;
            buffer.col = prefix.size() + (buffer.col - tailAt);
        } else if (buffer.row > row) {
            ++buffer.row;
        }
    }
    clampColumn(buffer);
}

}  // namespace

void insertText(TextBuffer& buffer, std::string_view text, const EditOptions& options) {
    buffer.line().insert(buffer.col, text);
    buffer.col += text.size();
    wrapCurrentLine(buffer, options);
}

void insertNewline(TextBuffer& buffer) {
    const std::string line = buffer.line();
    std::string prefix = continuationPrefix(line);
    // Only what is split off after the prefix needs one of its own. Splitting
    // inside the prefix leaves it on the tail, where it already was — so Enter
    // at the start of a quoted line puts an empty line above it rather than
    // quoting the quote a second time.
    if (buffer.col < prefix.size()) prefix.clear();
    // Nothing is split off at the end of the line, so there is nothing to carry
    // the prefix for: whoever presses Enter there is done with the quote and is
    // about to answer it in their own words.
    if (buffer.col >= line.size()) prefix.clear();

    std::string tail = line.substr(buffer.col);
    buffer.line() = line.substr(0, buffer.col);
    buffer.lines.insert(buffer.lines.begin() + buffer.row + 1, prefix + tail);
    ++buffer.row;
    // At the start of the new line, prefix or no prefix: a split leaves the
    // cursor where an unquoted one leaves it, and the prefix it carried is
    // text ahead of the cursor like any other.
    buffer.col = 0;
}

void deleteBefore(TextBuffer& buffer) {
    if (buffer.col > 0) {
        const size_t from = prevBoundary(buffer.line(), buffer.col);
        buffer.line().erase(from, buffer.col - from);
        buffer.col = from;
        return;
    }
    if (buffer.row == 0) return;

    const std::string tail = buffer.line();
    buffer.lines.erase(buffer.lines.begin() + buffer.row);
    --buffer.row;
    buffer.col = buffer.line().size();
    buffer.line() += tail;
}

void deleteAt(TextBuffer& buffer) {
    if (buffer.col < buffer.line().size()) {
        buffer.line().erase(buffer.col, charLength(buffer.line(), buffer.col));
        return;
    }
    if (buffer.row + 1 >= static_cast<int>(buffer.lines.size())) return;

    buffer.line() += buffer.lines[static_cast<size_t>(buffer.row) + 1];
    buffer.lines.erase(buffer.lines.begin() + buffer.row + 1);
}

void deleteLine(TextBuffer& buffer) {
    // Onto the stack before anything else, blank line and all: a blank one was
    // as much a press of Ctrl-Y as any other, and leaving it out would have
    // Ctrl-U put back the line above it instead.
    buffer.deleted.push_back(DeletedLine{buffer.line(), buffer.row});

    if (buffer.lines.size() == 1) {
        buffer.lines[0].clear();
        buffer.col = 0;
        return;
    }
    buffer.lines.erase(buffer.lines.begin() + buffer.row);
    if (buffer.row >= static_cast<int>(buffer.lines.size())) {
        buffer.row = static_cast<int>(buffer.lines.size()) - 1;
    }
    buffer.col = 0;
}

bool restoreLine(TextBuffer& buffer) {
    if (buffer.deleted.empty()) return false;
    const DeletedLine taken = buffer.deleted.back();
    buffer.deleted.pop_back();

    // The one line of an empty buffer is the blank `deleteLine()` left standing
    // where the whole text used to be; the line goes back into it rather than
    // above it, or undoing the deletion would leave a blank line behind.
    if (buffer.lines.size() == 1 && buffer.lines[0].empty()) {
        buffer.lines[0] = taken.text;
        buffer.row = 0;
        buffer.col = 0;
        return true;
    }

    // Above the line the cursor stands on, which is where Ctrl-Y took one out —
    // so the two undo each other, and a line put back somewhere else goes where
    // the cursor was carried to. The end of the text is the exception: a line
    // deleted off the bottom left the cursor on the line above it, and going in
    // above that one would swap the pair.
    auto at = static_cast<size_t>(buffer.row);
    const bool atEnd = at + 1 == buffer.lines.size();
    if (atEnd && taken.row >= static_cast<int>(buffer.lines.size()))
        at = buffer.lines.size();

    buffer.lines.insert(buffer.lines.begin() + static_cast<ptrdiff_t>(at), taken.text);
    buffer.row = static_cast<int>(at);
    buffer.col = 0;
    return true;
}

void deleteWordBefore(TextBuffer& buffer) {
    // Nothing before it on this line: the line break is Backspace's to take
    // out, and a word erased across one would take the paragraph's shape with
    // it.
    if (buffer.col == 0) return;

    const std::string& line = buffer.line();
    const auto before = [&line](size_t at) { return line[prevBoundary(line, at)]; };

    // The separators first and then the word, which is where `moveWordLeft()`
    // would have left the cursor had it stayed on the line: `foo bar ` back to
    // `foo `, and never half of the space between two words.
    size_t at = buffer.col;
    while (at > 0 && !isWordByte(before(at))) at = prevBoundary(line, at);
    while (at > 0 && isWordByte(before(at))) at = prevBoundary(line, at);

    buffer.line().erase(at, buffer.col - at);
    buffer.col = at;
}

void deleteQuote(TextBuffer& buffer) {
    /// Whether the line belongs to a quoted block: a quote, a blank line, or a
    /// quote prefix with nothing after it. That last one is what quoting an
    /// empty line leaves behind — nothing trails the markers, so a reader does
    /// not count it as a quote, but it is part of the block all the same and
    /// stopping there would leave half of one behind.
    const auto quotedOrBlank = [](const std::string& line) {
        if (line.find_first_not_of(" \t") == std::string::npos) return true;
        if (app::parseQuotePrefix(line).level > 0) return true;
        const auto padded = app::parseQuotePrefix(line + " ");
        return padded.level > 0 && padded.length >= line.size() + 1;
    };
    if (!quotedOrBlank(buffer.line())) return;

    const auto first = buffer.lines.begin() + buffer.row;
    auto last = first;
    while (last != buffer.lines.end() && quotedOrBlank(*last)) ++last;
    buffer.lines.erase(first, last);

    // A buffer always has a line for the cursor to stand on, even when the
    // quote was the whole of what was there.
    if (buffer.lines.empty()) buffer.lines.emplace_back();
    buffer.row = std::min(buffer.row, static_cast<int>(buffer.lines.size()) - 1);
    buffer.col = 0;
}

void moveLeft(TextBuffer& buffer) {
    if (buffer.col > 0) {
        buffer.col = prevBoundary(buffer.line(), buffer.col);
        return;
    }
    if (buffer.row == 0) return;
    --buffer.row;
    buffer.col = buffer.line().size();
}

void moveRight(TextBuffer& buffer) {
    if (buffer.col < buffer.line().size()) {
        buffer.col += charLength(buffer.line(), buffer.col);
        return;
    }
    if (buffer.row + 1 >= static_cast<int>(buffer.lines.size())) return;
    ++buffer.row;
    buffer.col = 0;
}

void moveWordRight(TextBuffer& buffer) {
    while (true) {
        // Nothing left on this line — the next word is on one of the lines
        // below, if there is one at all.
        if (buffer.col >= buffer.line().size()) {
            if (buffer.row + 1 >= static_cast<int>(buffer.lines.size())) return;
            ++buffer.row;
            buffer.col = 0;
            continue;
        }

        const std::string& line = buffer.line();
        while (buffer.col < line.size() && !isWordByte(line[buffer.col])) {
            buffer.col += charLength(line, buffer.col);
        }
        if (buffer.col >= line.size()) continue;  // separators to the end of it

        while (buffer.col < line.size() && isWordByte(line[buffer.col])) {
            buffer.col += charLength(line, buffer.col);
        }
        return;
    }
}

void moveWordLeft(TextBuffer& buffer) {
    while (true) {
        if (buffer.col == 0) {
            if (buffer.row == 0) return;
            --buffer.row;
            buffer.col = buffer.line().size();
            if (buffer.col == 0) continue;  // an empty line has nothing on it
        }

        const std::string& line = buffer.line();
        const auto before = [&] { return line[prevBoundary(line, buffer.col)]; };

        while (buffer.col > 0 && !isWordByte(before())) {
            buffer.col = prevBoundary(line, buffer.col);
        }
        if (buffer.col == 0) continue;  // separators back to the start of it

        while (buffer.col > 0 && isWordByte(before())) {
            buffer.col = prevBoundary(line, buffer.col);
        }
        return;
    }
}

void moveToLineStart(TextBuffer& buffer) {
    buffer.col = 0;
}

void moveToLineEnd(TextBuffer& buffer) {
    buffer.col = buffer.line().size();
}

std::vector<std::string> trimmedLines(const TextBuffer& buffer) {
    std::vector<std::string> out = buffer.lines;
    while (!out.empty() && out.back().find_first_not_of(' ') == std::string::npos) {
        out.pop_back();
    }
    return out;
}

}  // namespace amberedit::ui
