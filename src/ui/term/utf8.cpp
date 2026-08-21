#include "ui/term/utf8.hpp"

#include <langinfo.h>

#include <array>
#include <cctype>
#include <clocale>
#include <cwchar>

namespace amberedit::ui::term {
namespace {

/// Whether a codeset name means UTF-8. Spelled `UTF-8`, `utf8` and `UTF8` in
/// the wild, so the separators come out before comparing.
bool isUtf8Codeset(std::string_view codeset) {
    std::string letters;
    for (const char c : codeset) {
        if (c == '-' || c == '_') continue;
        letters += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return letters == "UTF8";
}

/// Whether a codeset is one the terminal layer cannot work in at all. The C
/// locale reports one of these, and under it wcrtomb() drops every non-ASCII
/// character and wcwidth() refuses to measure one.
bool isAsciiOnlyCodeset(std::string_view codeset) {
    return codeset.empty() || codeset == "ANSI_X3.4-1968" || codeset == "US-ASCII" ||
           codeset == "ASCII";
}

/// The UTF-8 locales worth trying, most portable first. `C.UTF-8` is the one
/// that exists without any language pack installed, which is what a bare VPS
/// has; the bare `UTF-8` spelling is what macOS accepts.
constexpr std::array<const char*, 4> kUtf8Locales{"C.UTF-8", "C.utf8", "en_US.UTF-8",
                                                  "UTF-8"};

}  // namespace

size_t sequenceLength(unsigned char first) {
    if (first < 0x80) return 1;
    if ((first & 0xE0) == 0xC0) return 2;
    if ((first & 0xF0) == 0xE0) return 3;
    if ((first & 0xF8) == 0xF0) return 4;
    return 1;  // a broken byte counts as one character, if only to avoid looping
}

char32_t decodeUtf8(std::string_view text, size_t& pos) {
    if (pos >= text.size()) return 0;

    const auto first = static_cast<unsigned char>(text[pos]);
    const size_t length = sequenceLength(first);
    if (pos + length > text.size()) {
        ++pos;
        return 0;
    }

    char32_t code = first;
    if (length == 2) code = first & 0x1Fu;
    if (length == 3) code = first & 0x0Fu;
    if (length == 4) code = first & 0x07u;

    for (size_t i = 1; i < length; ++i) {
        const auto continuation = static_cast<unsigned char>(text[pos + i]);
        if ((continuation & 0xC0) != 0x80) {
            ++pos;
            return 0;
        }
        code = (code << 6) | (continuation & 0x3Fu);
    }
    pos += length;
    return code;
}

int codepointWidth(char32_t code) {
    // What wcwidth() answers depends on the locale, so the locale has to be
    // settled before the first measurement is taken — including in a test that
    // never opens a terminal, and in any code path that measures before the
    // interface is up. ensureUtf8Locale() is idempotent, so after the first call
    // this costs nothing.
    [[maybe_unused]] static const std::string& codeset = ensureUtf8Locale();

    if (code == 0) return 0;

    const int width = ::wcwidth(static_cast<wchar_t>(code));
    if (width >= 0) return width;

    // The library would not judge it. A C1 control or a stray byte is worth no
    // columns; anything else is guessed at one, which keeps a row that holds an
    // unknown character merely imperfect rather than misaligned by a whole cell.
    if (code < 0x20 || (code >= 0x7F && code < 0xA0)) return 0;
    return 1;
}

std::vector<std::string> toGlyphs(std::string_view text) {
    std::vector<std::string> glyphs;
    size_t pos = 0;
    while (pos < text.size()) {
        const size_t start = pos;
        const char32_t code = decodeUtf8(text, pos);
        const int width = codepointWidth(code);
        const std::string bytes(text.substr(start, pos - start));

        // A control character occupies no column, so it gets no cell — and it
        // must not be attached to the glyph before it either, or a stray newline
        // would be drawn into the middle of a row. Dropping it keeps one entry
        // here per column counted by stringWidth(), which everything else relies
        // on.
        if (code < 0x20 || (code >= 0x7F && code < 0xA0)) continue;

        // A combining mark belongs to the glyph before it. With nothing before
        // it there is nothing to combine with, so it stands on its own rather
        // than being dropped — the text is then malformed, and showing that is
        // more useful than hiding it.
        if (width == 0 && !glyphs.empty()) {
            // Walk back past the filler cell of a wide glyph, if that is what
            // the mark landed behind.
            size_t target = glyphs.size() - 1;
            while (target > 0 && glyphs[target].empty()) --target;
            glyphs[target] += bytes;
            continue;
        }

        glyphs.push_back(bytes);
        // The second column of a wide glyph. Empty rather than a space, so that
        // whoever draws it knows to step over it instead of blanking it.
        if (width == 2) glyphs.emplace_back();
    }
    return glyphs;
}

std::string encodeUtf8(char32_t code) {
    std::string out;
    if (code < 0x80) {
        out += static_cast<char>(code);
    } else if (code < 0x800) {
        out += static_cast<char>(0xC0 | (code >> 6));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else if (code < 0x10000) {
        out += static_cast<char>(0xE0 | (code >> 12));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (code >> 18));
        out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
    return out;
}

int stringWidth(std::string_view text) {
    int width = 0;
    size_t pos = 0;
    while (pos < text.size()) width += codepointWidth(decodeUtf8(text, pos));
    return width;
}

const std::string& ensureUtf8Locale() {
    // Function-local so that the answer is settled once, on the first call,
    // whether that comes from the application or from a test.
    static const std::string codeset = [] {
        // What the environment asks for comes first: a user who has chosen
        // KOI8-R means it, and overriding them would undo the one thing this
        // whole change was for.
        std::setlocale(LC_CTYPE, "");
        const char* current = nl_langinfo(CODESET);
        std::string name = current != nullptr ? current : "";
        if (!isAsciiOnlyCodeset(name)) return name;

        // Nothing usable was asked for. Rather than draw nothing but ASCII,
        // find a UTF-8 locale and use it — a terminal on a machine with no
        // locale configured is very nearly always UTF-8 these days.
        for (const char* candidate : kUtf8Locales) {
            if (std::setlocale(LC_CTYPE, candidate) == nullptr) continue;
            current = nl_langinfo(CODESET);
            name = current != nullptr ? current : "";
            if (isUtf8Codeset(name)) return name;
        }

        // None of them existed. Put back whatever the environment said and let
        // the caller report it: the interface will be ASCII-only, but it runs.
        std::setlocale(LC_CTYPE, "");
        current = nl_langinfo(CODESET);
        return std::string(current != nullptr ? current : "");
    }();
    return codeset;
}

}  // namespace amberedit::ui::term
