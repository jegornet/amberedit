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

/// What `lower` adds to `upper` when the two are joined into one line.
///
/// Two lines of one quote joined are one line of it: the prefix the lower one
/// carried would otherwise land in the middle of the sentence. Only the same
/// initials at the same depth count — another writer's words, or a deeper
/// quote, are not the continuation of this one.
std::string joinedTail(const std::string& upper, std::string lower) {
    const auto above = app::parseQuotePrefix(upper);
    const auto below = app::parseQuotePrefix(lower);
    if (above.level > 0 && above.level == below.level &&
        above.initials == below.initials) {
        lower.erase(0, below.length);
        // The prefix's own space was what stood between the two words.
        if (!lower.empty() && upper.back() != ' ') lower.insert(0, 1, ' ');
    }
    return lower;
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

    const std::string lower = buffer.line();
    buffer.lines.erase(buffer.lines.begin() + buffer.row);
    --buffer.row;
    buffer.col = buffer.line().size();
    buffer.line() += joinedTail(buffer.line(), lower);
}

void deleteAt(TextBuffer& buffer) {
    if (buffer.col < buffer.line().size()) {
        buffer.line().erase(buffer.col, charLength(buffer.line(), buffer.col));
        return;
    }
    if (buffer.row + 1 >= static_cast<int>(buffer.lines.size())) return;

    buffer.line() +=
        joinedTail(buffer.line(), buffer.lines[static_cast<size_t>(buffer.row) + 1]);
    buffer.lines.erase(buffer.lines.begin() + buffer.row + 1);
}

void deleteLine(TextBuffer& buffer) {
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

void deleteWordBefore(TextBuffer& buffer) {
    // Nothing before it on this line: the line break is Backspace's to take
    // out, and a word erased across one would take the paragraph's shape with
    // it.
    if (buffer.col == 0) return;

    // Where `moveWordLeft()` would have left the cursor had it stayed on the
    // line: `foo bar ` back to `foo `, and never half of the space between two
    // words. Nothing but separators behind it and the whole of them go.
    const size_t at = wordStartBefore(buffer.line(), buffer.col).value_or(0);
    buffer.line().erase(at, buffer.col - at);
    buffer.col = at;
}

void deleteQuote(TextBuffer& buffer) {
    /// Whether the line belongs to a quoted block: a quote or a blank line.
    const auto quotedOrBlank = [](const std::string& line) {
        if (line.find_first_not_of(" \t") == std::string::npos) return true;
        return app::parseQuotePrefix(line).level > 0;
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

std::optional<size_t> wordStartBefore(const std::string& text, size_t at) {
    at = std::min(at, text.size());
    const auto before = [&text](size_t pos) { return text[prevBoundary(text, pos)]; };

    // The separators first and then the word, so the cursor lands on the front
    // of one and never in the middle of the space before it.
    while (at > 0 && !isWordByte(before(at))) at = prevBoundary(text, at);
    if (at == 0) return std::nullopt;
    while (at > 0 && isWordByte(before(at))) at = prevBoundary(text, at);
    return at;
}

std::optional<size_t> wordEndAfter(const std::string& text, size_t at) {
    at = std::min(at, text.size());
    while (at < text.size() && !isWordByte(text[at])) at += charLength(text, at);
    if (at >= text.size()) return std::nullopt;
    while (at < text.size() && isWordByte(text[at])) at += charLength(text, at);
    return at;
}

std::pair<size_t, size_t> wordAround(const std::string& text, size_t at) {
    if (text.empty()) return {0, 0};
    // Past the end of the line is the last character of it, as a pointer
    // beyond the text means the word it trails off from.
    at = std::min(at, text.size());
    if (at == text.size()) at = prevBoundary(text, at);
    while (at > 0 && (static_cast<unsigned char>(text[at]) & 0xC0u) == 0x80u) --at;

    // A run of one kind: letters and digits, or blanks. Anything else is a
    // character on its own — a bracket or a quote marker is not a word, and
    // taking the punctuation beside it would take half a line with it.
    const auto kind = [&text](size_t pos) {
        const char byte = text[pos];
        if (isWordByte(byte)) return 0;
        if (byte == ' ' || byte == '\t') return 1;
        return 2;
    };
    const int mine = kind(at);
    size_t begin = at;
    size_t end = at + charLength(text, at);
    if (mine == 2) return {begin, end};
    while (begin > 0 && kind(prevBoundary(text, begin)) == mine) {
        begin = prevBoundary(text, begin);
    }
    while (end < text.size() && kind(end) == mine) end += charLength(text, end);
    return {begin, end};
}

void moveWordRight(TextBuffer& buffer) {
    while (true) {
        if (const auto at = wordEndAfter(buffer.line(), buffer.col)) {
            buffer.col = *at;
            return;
        }
        // Nothing but separators to the end of this line — the next word is on
        // one of the lines below, if there is one at all.
        if (buffer.row + 1 >= static_cast<int>(buffer.lines.size())) {
            buffer.col = buffer.line().size();
            return;
        }
        ++buffer.row;
        buffer.col = 0;
    }
}

void moveWordLeft(TextBuffer& buffer) {
    while (true) {
        if (const auto at = wordStartBefore(buffer.line(), buffer.col)) {
            buffer.col = *at;
            return;
        }
        // Separators back to the start of this line, or the cursor already
        // there — the word to go to is on one of the lines above.
        if (buffer.row == 0) {
            buffer.col = 0;
            return;
        }
        --buffer.row;
        buffer.col = buffer.line().size();
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

namespace {

/// The point held to the text as it stands: a line that is there, and a byte
/// of it that is.
TextPoint clamped(const TextBuffer& buffer, TextPoint point) {
    point.row = std::clamp(point.row, 0, static_cast<int>(buffer.lines.size()) - 1);
    point.col = std::min(point.col, buffer.lines[static_cast<size_t>(point.row)].size());
    return point;
}

}  // namespace

std::optional<std::pair<TextPoint, TextPoint>> selectionOf(const TextBuffer& buffer) {
    if (!buffer.anchor || buffer.lines.empty()) return std::nullopt;
    const TextPoint anchor = clamped(buffer, *buffer.anchor);
    const TextPoint cursor = clamped(buffer, buffer.cursor());
    if (anchor == cursor) return std::nullopt;
    return anchor < cursor ? std::make_pair(anchor, cursor)
                           : std::make_pair(cursor, anchor);
}

std::string selectedText(const TextBuffer& buffer) {
    const auto range = selectionOf(buffer);
    if (!range) return {};
    const auto& [from, to] = *range;
    std::string out;
    for (int row = from.row; row <= to.row; ++row) {
        const std::string& line = buffer.lines[static_cast<size_t>(row)];
        const size_t begin = row == from.row ? from.col : 0;
        const size_t end = row == to.row ? to.col : line.size();
        if (row != from.row) out += '\n';
        out.append(line, begin, end - begin);
    }
    return out;
}

bool deleteSelection(TextBuffer& buffer) {
    const auto range = selectionOf(buffer);
    buffer.anchor.reset();
    if (!range) return false;
    const auto& [from, to] = *range;
    auto& lines = buffer.lines;
    const std::string tail = lines[static_cast<size_t>(to.row)].substr(to.col);
    lines[static_cast<size_t>(from.row)].erase(from.col);
    lines[static_cast<size_t>(from.row)] += tail;
    lines.erase(lines.begin() + from.row + 1, lines.begin() + to.row + 1);
    buffer.row = from.row;
    buffer.col = from.col;
    return true;
}

}  // namespace amberedit::ui
