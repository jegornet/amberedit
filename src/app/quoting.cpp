#include "app/quoting.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <sstream>

namespace amberedit::app {
namespace {

constexpr size_t kMaxLeadingSpaces = 2;
constexpr int kMaxInitials = 6;

/// The code point at `pos`, advancing it past the character. Returns 0 without
/// moving on anything that does not start a character.
uint32_t decodeUtf8(std::string_view text, size_t& pos) {
    if (pos >= text.size()) return 0;
    const auto first = static_cast<unsigned char>(text[pos]);

    size_t extra = 0;
    uint32_t code = 0;
    if (first < 0x80) {
        code = first;
    } else if ((first & 0xE0u) == 0xC0u) {
        extra = 1;
        code = first & 0x1Fu;
    } else if ((first & 0xF0u) == 0xE0u) {
        extra = 2;
        code = first & 0x0Fu;
    } else if ((first & 0xF8u) == 0xF0u) {
        extra = 3;
        code = first & 0x07u;
    } else {
        return 0;
    }
    if (pos + extra >= text.size() && extra > 0) return 0;

    for (size_t i = 1; i <= extra; ++i) {
        const auto next = static_cast<unsigned char>(text[pos + i]);
        if ((next & 0xC0u) != 0x80u) return 0;
        code = (code << 6) | (next & 0x3Fu);
    }
    pos += extra + 1;
    return code;
}

/// Steps over one character, whether or not it decodes — a stray byte still
/// takes up a place on the line.
void stepChar(std::string_view text, size_t& pos) {
    const size_t before = pos;
    decodeUtf8(text, pos);
    if (pos == before) ++pos;
}

/// Whether the character can stand in a set of initials. The same answer
/// ui::quoteDepth() gives, and for the same reason: Russian echoes write
/// initials in Cyrillic as a matter of course.
bool isInitialLetter(uint32_t code) {
    if (code < 128) return std::isalpha(static_cast<int>(code)) != 0;
    return code >= 0x0400 && code <= 0x04FF;  // Cyrillic
}

/// Characters, not bytes and not terminal columns: the margin is about how
/// long the line is in the message, and the charsets FTN messages are stored
/// in are one byte to the character.
size_t charCount(std::string_view text) {
    size_t count = 0;
    for (size_t pos = 0; pos < text.size(); ++count) stepChar(text, pos);
    return count;
}

/// Where the `count`-th character begins, or the end of the text.
size_t byteOffsetOf(std::string_view text, size_t count) {
    size_t pos = 0;
    for (size_t i = 0; i < count && pos < text.size(); ++i) stepChar(text, pos);
    return pos;
}

/// The first character of a word, whole — an initial may be two bytes.
std::string firstLetter(std::string_view word) {
    return std::string(word.substr(0, byteOffsetOf(word, 1)));
}

/// The quote string taken apart: what stands before the initials, the template
/// they are written into, and how many markers it asks for.
struct QuoteStringParts {
    std::string lead;
    std::string letters;
    int markers{1};
};

QuoteStringParts splitQuoteString(std::string_view quoteString) {
    QuoteStringParts parts;
    size_t pos = 0;
    while (pos < quoteString.size() && quoteString[pos] == ' ') ++pos;
    parts.lead = std::string(quoteString.substr(0, pos));

    const size_t lettersAt = pos;
    while (pos < quoteString.size() && quoteString[pos] != '>') ++pos;
    parts.letters = std::string(quoteString.substr(lettersAt, pos - lettersAt));

    int markers = 0;
    while (pos < quoteString.size() && quoteString[pos] == '>') {
        ++markers;
        ++pos;
    }
    parts.markers = std::max(1, markers);
    return parts;
}

/// The initials of a name, written into the quote string's template: F for the
/// first name, L for the last, M for the names in between. A one-word name has
/// a first name and nothing else, so F is all it yields.
std::string initialsOf(std::string_view author, std::string_view letters) {
    std::istringstream stream{std::string(author)};
    std::vector<std::string> words;
    for (std::string word; stream >> word;) words.push_back(word);

    std::string first;
    std::string middle;
    std::string last;
    if (!words.empty()) first = firstLetter(words.front());
    if (words.size() >= 2) last = firstLetter(words.back());
    for (size_t i = 1; i + 1 < words.size(); ++i) middle += firstLetter(words[i]);

    std::string out;
    for (const char letter : letters) {
        switch (letter) {
            case 'F': out += first; break;
            case 'M': out += middle; break;
            case 'L': out += last; break;
            // Anything else in the template is a literal the config asked for.
            default: out += letter; break;
        }
    }
    return out;
}

/// Wraps text to `limit` characters, breaking at spaces. A word too long for a
/// line of its own is cut: there is nowhere else to break it.
std::vector<std::string> wrapToChars(std::string_view text, size_t limit) {
    std::vector<std::string> out;
    std::istringstream words{std::string(text)};

    std::string current;
    for (std::string word; words >> word;) {
        while (charCount(word) > limit) {
            if (!current.empty()) {
                out.push_back(current);
                current.clear();
            }
            const size_t cut = byteOffsetOf(word, limit);
            out.push_back(word.substr(0, cut));
            word = word.substr(cut);
        }

        if (current.empty()) {
            current = word;
        } else if (charCount(current) + 1 + charCount(word) <= limit) {
            current += " " + word;
        } else {
            out.push_back(current);
            current = word;
        }
    }
    if (!current.empty()) out.push_back(current);
    if (out.empty()) out.emplace_back();
    return out;
}

std::string rstrip(std::string text) {
    while (!text.empty() && text.back() == ' ') text.pop_back();
    return text;
}

/// What the line says after its quote prefix.
std::string_view textOf(std::string_view line, const QuotePrefix& prefix) {
    return line.substr(std::min(prefix.length, line.size()));
}

/// Whether there is anything there to answer. Empty, spaces, and a quote prefix
/// and no more are all nothing.
bool carriesText(std::string_view text) {
    return text.find_first_not_of(" \t") != std::string_view::npos;
}

/// The word the text begins with, which is what decides whether it would have
/// fitted on the line above.
std::string_view firstWord(std::string_view text) {
    const size_t begin = text.find_first_not_of(' ');
    if (begin == std::string_view::npos) return {};
    const size_t end = text.find(' ', begin);
    return text.substr(begin, end == std::string_view::npos ? end : end - begin);
}

/// Whether the text is laid out rather than written as prose: indented, tabbed,
/// or spaced into columns. Joining such a line onto the one above would lose the
/// shape it was written in, which is the same reason a line that fits is quoted
/// exactly as it stands. Two spaces are not enough to say so — that is how a
/// good many people still close a sentence.
bool isLaidOut(std::string_view text) {
    if (!text.empty() && (text.front() == ' ' || text.front() == '\t')) return true;
    if (text.find('\t') != std::string_view::npos) return true;
    return text.find("   ") != std::string_view::npos;
}

/// Whether the text begins a point of its own — a bullet or a number — rather
/// than carrying on the line above. A list is paragraphed by its items, and
/// nothing else says where one ends.
bool startsItem(std::string_view text) {
    size_t pos = 0;
    const uint32_t first = decodeUtf8(text, pos);
    if (first == '-' || first == '*' || first == '+' || first == 0x00B7 ||
        first == 0x2022) {
        return pos < text.size() && text[pos] == ' ';
    }
    while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) ++pos;
    if (pos == 0) return false;
    if (pos + 1 >= text.size()) return false;
    return (text[pos] == '.' || text[pos] == ')') && text[pos + 1] == ' ';
}

/// The least width a run has to have been written to before its breaks are read
/// as a wrap. Under this the lines are short because somebody wanted them short.
constexpr size_t kMinFillWidth = 40;

/// Joins the lines a paragraph was hard-wrapped into back into one, so that the
/// quote is wrapped at our own margin instead of carrying the breaks of the
/// editor it was written in.
///
/// A run is the lines standing together at one quote level under one set of
/// initials — a break, a deeper quote or another writer's initials all end it.
/// The width the run was written to is its longest line, which is as near as
/// anyone can tell what the other editor's margin was; a line is a continuation
/// of the one above when that line had no room left for its first word. Nothing
/// laid out and nothing beginning a point of its own is joined.
std::vector<std::string> unwrapRuns(const std::vector<std::string>& lines) {
    std::vector<std::string> out;
    size_t at = 0;
    while (at < lines.size()) {
        const QuotePrefix head = parseQuotePrefix(lines[at]);
        if (!carriesText(textOf(lines[at], head))) {
            out.push_back(lines[at++]);
            continue;
        }

        size_t end = at + 1;
        for (; end < lines.size(); ++end) {
            const QuotePrefix next = parseQuotePrefix(lines[end]);
            if (next.level != head.level || next.initials != head.initials) break;
            if (!carriesText(textOf(lines[end], next))) break;
        }

        size_t width = 0;
        for (size_t i = at; i < end; ++i) {
            width = std::max(width, charCount(rstrip(lines[i])));
        }

        std::string current = rstrip(lines[at]);
        // The line as it was written, which is what had the room or did not —
        // `current` grows past the width as soon as anything is joined onto it.
        std::string previous = current;
        for (size_t i = at + 1; i < end; ++i) {
            const std::string line = rstrip(lines[i]);
            const std::string_view text = textOf(line, parseQuotePrefix(line));
            const std::string_view above = textOf(previous, parseQuotePrefix(previous));

            const bool continues =
                width >= kMinFillWidth &&
                charCount(previous) + 1 + charCount(firstWord(text)) > width &&
                !isLaidOut(above) && !isLaidOut(text) && !startsItem(text);
            if (continues) {
                current += " " + std::string(text);
            } else {
                out.push_back(current);
                current = line;
            }
            previous = line;
        }
        out.push_back(current);
        at = end;
    }
    return out;
}

}  // namespace

QuotePrefix parseQuotePrefix(std::string_view line) {
    QuotePrefix prefix;

    size_t pos = 0;
    while (pos < kMaxLeadingSpaces && pos < line.size() && line[pos] == ' ') ++pos;

    const size_t initialsAt = pos;
    for (int letters = 0; letters < kMaxInitials; ++letters) {
        size_t next = pos;
        const uint32_t code = decodeUtf8(line, next);
        if (code == 0 || !isInitialLetter(code)) break;
        pos = next;
    }
    const size_t initialsEnd = pos;

    // A single '-' may stand before the markers: QWK gateways quote with "-> ".
    // It is read as part of the prefix and not written back — a reply deepens
    // such a line into a quote of our own shape.
    if (pos < line.size() && line[pos] == '-') ++pos;

    int markers = 0;
    while (pos < line.size() && line[pos] == '>') {
        ++markers;
        ++pos;
    }
    if (markers == 0) return prefix;
    // The space after the markers is what tells a quote from a line that
    // merely begins with a '>'.
    if (pos >= line.size() || line[pos] != ' ') return prefix;

    prefix.initials = std::string(line.substr(initialsAt, initialsEnd - initialsAt));
    prefix.level = markers;
    prefix.length = pos + 1;
    return prefix;
}

std::string quotePrefixFor(std::string_view quoteString, std::string_view author) {
    const QuoteStringParts parts = splitQuoteString(quoteString);
    return parts.lead + initialsOf(author, parts.letters) +
           std::string(static_cast<size_t>(parts.markers), '>') + " ";
}

std::string deeperQuotePrefix(std::string_view quoteString, const QuotePrefix& prefix) {
    const QuoteStringParts parts = splitQuoteString(quoteString);
    return parts.lead + prefix.initials +
           std::string(static_cast<size_t>(prefix.level + 1), '>') + " ";
}

std::vector<std::string> quoteLines(const std::vector<std::string>& lines,
                                    std::string_view author, std::string_view quoteString,
                                    int margin, bool unwrap) {
    const std::string fresh = quotePrefixFor(quoteString, author);
    const auto limit = static_cast<size_t>(std::max(1, margin));

    // The paragraphs are put back together before anything is quoted, so that
    // the prefix and the margin are applied to the text as it reads rather than
    // to the lines somebody else's editor happened to break it into.
    const std::vector<std::string>& source = unwrap ? unwrapRuns(lines) : lines;

    std::vector<std::string> out;
    // Set by a line with nothing on it and cleared by the empty line it is
    // written out as, so that a run of them yields one break and no more.
    bool pendingBreak = false;
    for (const auto& line : source) {
        const QuotePrefix existing = parseQuotePrefix(line);
        const std::string prefix =
            existing.level > 0 ? deeperQuotePrefix(quoteString, existing) : fresh;
        const std::string_view rest =
            std::string_view(line).substr(std::min(existing.length, line.size()));

        // A line with nothing on it is not quoted at all — neither an empty
        // one, nor one of nothing but spaces, nor one that is a quote prefix
        // and no more. There is nothing there to answer, and a reply padded
        // with empty quotes is harder to read than one without them. It stays
        // as the empty line it is: the paragraphs of the answered message are
        // what the reply is written between.
        if (rest.find_first_not_of(" \t") == std::string_view::npos) {
            pendingBreak = true;
            continue;
        }
        if (pendingBreak) {
            out.emplace_back();
            pendingBreak = false;
        }

        // A line that fits is quoted as it stands. Indentation, tables and
        // ASCII art survive that way; only what overflows is rearranged.
        const size_t prefixChars = charCount(prefix);
        if (prefixChars + charCount(rest) <= limit) {
            out.push_back(rstrip(prefix + std::string(rest)));
            continue;
        }

        // There has to be room for something after the prefix, however narrow
        // the margin, or the line could never end.
        const size_t room = prefixChars + 1 >= limit ? 1 : limit - prefixChars;
        for (const auto& piece : wrapToChars(rest, room)) {
            out.push_back(rstrip(prefix + piece));
        }
    }
    if (pendingBreak) out.emplace_back();
    return out;
}

}  // namespace amberedit::app
