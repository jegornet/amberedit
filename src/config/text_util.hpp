#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

#include "support/error.hpp"

namespace amberedit::config::text {

/// Case folding and whitespace, for ASCII and deliberately for nothing else.
///
/// <cctype>'s tolower() answers from the locale, and there is now a locale to
/// answer from: the terminal layer installs one so that ncurses knows what to
/// encode its output as, and a user may well name a single-byte one such as
/// KOI8-R. Under such a locale tolower() folds the high half of the byte range
/// as well, and two Cyrillic names spelled differently would start comparing
/// equal — which is exactly what `iequals` promises not to do. Folding ASCII and
/// only ASCII keeps that promise whatever locale is in force, and keeps it the
/// same on every machine.
[[nodiscard]] constexpr char asciiLower(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

[[nodiscard]] constexpr char asciiUpper(char c) {
    return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
}

[[nodiscard]] constexpr bool asciiIsSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

inline std::string toLower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), asciiLower);
    return out;
}

inline std::string toUpper(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), asciiUpper);
    return out;
}

/// std::string_view::starts_with is C++20 and the project is C++17. Taking
/// string_view on both sides means std::string, string_view and a literal all
/// call it without a conversion being spelled out at the call site.
[[nodiscard]] inline bool startsWith(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

inline bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (asciiLower(a[i]) != asciiLower(b[i])) return false;
    }
    return true;
}

/// Whether the text holds a glob metacharacter — whether `globMatches` could
/// answer true for anything but the text itself.
///
/// What a caller does with the answer is decide between a name and a pattern: a
/// config line naming a file is looked up as it stands, and one holding a `*` is
/// matched against a directory. A file whose own name holds one of these two
/// characters can therefore not be named literally, which is a trade no FTN
/// distribution has ever had cause to mind.
[[nodiscard]] inline bool hasWildcard(std::string_view text) {
    return text.find_first_of("*?") != std::string_view::npos;
}

/// Whether the whole of `text` is what the glob `pattern` describes: `*` for
/// any run of characters, the empty one included, `?` for exactly one, and
/// everything else standing for itself. Case is folded for ASCII and
/// deliberately for nothing else — the reason `asciiLower` gives.
///
/// Anchored at both ends, so a pattern that is to be found *inside* something
/// says so with stars of its own: `Ivanov` is that name and `*Ivanov*` is every
/// name holding it. The one matcher behind `AreaTagPattern` and the `twit`
/// lines both, which is what keeps a glob meaning the same thing wherever the
/// config holds one.
[[nodiscard]] inline bool globMatches(std::string_view pattern, std::string_view text) {
    // The two-pointer glob with a single backtrack point: where a `*` was last
    // seen and how much of the text it had swallowed by then. Backtracking that
    // one place is enough because `*` is the only thing that can match more than
    // one character, so a failure downstream is always answered by feeding the
    // last star one more character. No recursion and no allocation, which is
    // what keeps a pattern cheap enough to run over every area on a rescan and
    // over every message of an area as it is opened.
    constexpr size_t kNone = std::string_view::npos;
    size_t p = 0;
    size_t t = 0;
    size_t star = kNone;
    size_t textAtStar = 0;

    while (t < text.size()) {
        // The star is looked at before the literal match, so that a `*` in the
        // text itself cannot be eaten by a `*` in the pattern as though the two
        // were the same character.
        if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            textAtStar = t;
        } else if (p < pattern.size() &&
                   (pattern[p] == '?' || asciiLower(pattern[p]) == asciiLower(text[t]))) {
            // Only the pattern's own `?` is a wildcard: the text is text, and a
            // character in it stands for itself whatever character it is.
            ++p;
            ++t;
        } else if (star != kNone) {
            p = star + 1;
            t = ++textAtStar;
        } else {
            return false;
        }
    }

    // Whatever is left of the pattern has to be able to match nothing at all,
    // which is to say be nothing but stars.
    for (size_t i = p; i < pattern.size(); ++i) {
        if (pattern[i] != '*') return false;
    }
    return true;
}

inline std::string_view trim(std::string_view s) {
    while (!s.empty() && asciiIsSpace(s.front())) s.remove_prefix(1);
    while (!s.empty() && asciiIsSpace(s.back())) s.remove_suffix(1);
    return s;
}

/// Splits a line into tokens on whitespace, honouring double quotes:
/// `-d "hello world"` becomes {"-d", "hello world"}.
inline std::vector<std::string> tokenize(std::string_view line) {
    std::vector<std::string> tokens;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && asciiIsSpace(line[i])) ++i;
        if (i >= line.size()) break;

        std::string token;
        if (line[i] == '"') {
            ++i;
            while (i < line.size() && line[i] != '"') token += line[i++];
            if (i < line.size()) ++i;  // closing quote
        } else {
            while (i < line.size() && !asciiIsSpace(line[i])) token += line[i++];
        }
        tokens.push_back(std::move(token));
    }
    return tokens;
}

/// Splits text into lines, stripping \r\n and \r.
inline std::vector<std::string> splitLines(std::string_view text) {
    std::vector<std::string> lines;
    std::string current;
    for (char c : text) {
        if (c == '\n') {
            lines.push_back(current);
            current.clear();
        } else if (c != '\r') {
            current += c;
        }
    }
    if (!current.empty()) lines.push_back(current);
    return lines;
}

/// Refuses a path that names a directory, which is what every read of a whole
/// file has to do before it starts reading.
///
/// A directory opens as a stream on both systems, and then the two libraries
/// part company: libstdc++ throws out of the first read — from inside
/// basic_filebuf, past any exception mask, so there is no catching it where it
/// happens — and libc++ hands back an empty string and sets no flag at all.
/// Neither is an answer a caller can do anything with, and a config that points
/// a setting at a directory is an ordinary enough mistake to be worth saying
/// out loud. So the question is asked here, through the non-throwing filesystem
/// overload, and every system answers the same way.
[[nodiscard]] tl::expected<void, ErrorPtr> insistItIsAFile(const std::string& path);

/// Reads a whole file, or says why it could not be read — naming the path,
/// which is the one thing a caller cannot add and a reader needs.
[[nodiscard]] tl::expected<std::string, ErrorPtr> readFile(const std::string& path);

}  // namespace amberedit::config::text
