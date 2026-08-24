#include "encoding/text_search.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace amberedit::encoding {

namespace {

/// A byte that is no part of a well-formed UTF-8 sequence stands for itself,
/// above every code point there is. Text reaching the reader is UTF-8 as a rule
/// — the adapter converted it — but a message stating a charset iconv does not
/// know comes through with its bytes untouched, and two different broken bytes
/// must not fold together into one wildcard.
constexpr char32_t kRawByte = 0x80000000U;

/// The code point at `at`, and `at` moved past it. A malformed byte is answered
/// with itself and one byte of progress, so nothing here can spin.
char32_t nextChar(std::string_view text, size_t& at) {
    const auto first = static_cast<unsigned char>(text[at]);
    const auto raw = [&] {
        ++at;
        return kRawByte | first;
    };

    if (first < 0x80) {
        ++at;
        return first;
    }

    size_t length = 0;
    char32_t code = 0;
    if ((first & 0xE0U) == 0xC0U) {
        length = 2;
        code = first & 0x1FU;
    } else if ((first & 0xF0U) == 0xE0U) {
        length = 3;
        code = first & 0x0FU;
    } else if ((first & 0xF8U) == 0xF0U) {
        length = 4;
        code = first & 0x07U;
    } else {
        return raw();
    }

    if (at + length > text.size()) return raw();
    for (size_t i = 1; i < length; ++i) {
        const auto next = static_cast<unsigned char>(text[at + i]);
        if ((next & 0xC0U) != 0x80U) return raw();
        code = (code << 6) | (next & 0x3FU);
    }
    at += length;
    return code;
}

}  // namespace

char32_t loweredCodePoint(char32_t code) {
    if (code >= U'A' && code <= U'Z') return code + 0x20;
    // À-Þ, less the multiplication sign standing in the middle of the block.
    if (code >= 0x00C0 && code <= 0x00DE && code != 0x00D7) return code + 0x20;
    // А-Я, and Ё and the other letters the Slavic alphabets add in front of it.
    if (code >= 0x0410 && code <= 0x042F) return code + 0x20;
    if (code >= 0x0400 && code <= 0x040F) return code + 0x50;
    if (code == 0x0490) return 0x0491;  // Ґ, which stands outside both runs
    return code;
}

namespace {

/// The Russian language support quirks, applied to an already lower-cased code point.
///
/// A message written in CP866 may spell н, р and у with the Latin h, p and y:
/// on a DOS screen the glyphs are the same and the keys are one layout apart, so
/// a word typed half in each is ordinary rather than exceptional. Folding the
/// pairs together is what lets it be found by either spelling — and it follows
/// from the lower-casing above that the capitals go with them, Н being н and H
/// being h before this is asked.
char32_t quirked(char32_t code) {
    switch (code) {
        case 0x043D: return U'h';  // н
        case 0x0440: return U'p';  // р
        case 0x0443: return U'y';  // у
        default: return code;
    }
}

/// Whether the charset is the one those quirks belong to. Compared without
/// regard to case and against the name `CharsetDetector::normalize()` settles
/// on, every alias of CP866 having become that one spelling by the time a
/// message's charset reaches anybody.
bool isCp866(std::string_view charset) {
    static constexpr std::string_view kName = "CP866";
    if (charset.size() != kName.size()) return false;
    for (size_t i = 0; i < charset.size(); ++i) {
        char c = charset[i];
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - ('a' - 'A'));
        if (c != kName[i]) return false;
    }
    return true;
}

}  // namespace

TextSearch::TextSearch(std::string_view query, std::string_view charset)
    : query_(query), charset_(charset), quirks_(isCp866(charset_)) {
    refold();
}

void TextSearch::setQuery(std::string_view query) {
    if (query == query_) return;
    query_.assign(query);
    refold();
}

void TextSearch::setCharset(std::string_view charset) {
    if (charset == charset_) return;
    charset_.assign(charset);
    const bool quirks = isCp866(charset_);
    if (quirks == quirks_) return;
    quirks_ = quirks;
    refold();
}

void TextSearch::refold() {
    needle_.clear();
    for (size_t at = 0; at < query_.size();) {
        const char32_t code = loweredCodePoint(nextChar(query_, at));
        needle_.push_back(quirks_ ? quirked(code) : code);
    }
}

std::vector<TextMatch> TextSearch::findAll(std::string_view text) const {
    return search(text, /*firstOnly=*/false);
}

bool TextSearch::contains(std::string_view text) const {
    return !search(text, /*firstOnly=*/true).empty();
}

std::vector<TextMatch> TextSearch::search(std::string_view text, bool firstOnly) const {
    std::vector<TextMatch> found;
    if (needle_.empty()) return found;

    // One unit per character, with the byte each of them began at kept beside
    // it: what comes back has to be offsets into the caller's own string, since
    // that is what is drawn.
    std::vector<char32_t> units;
    std::vector<size_t> offsets;
    units.reserve(text.size());
    offsets.reserve(text.size() + 1);
    for (size_t at = 0; at < text.size();) {
        offsets.push_back(at);
        const char32_t code = loweredCodePoint(nextChar(text, at));
        units.push_back(quirks_ ? quirked(code) : code);
    }
    offsets.push_back(text.size());

    // Plainly, character by character. A message is a few thousand characters
    // and a query a handful, so what a cleverer walk would save is not worth a
    // table built per message.
    const size_t span = needle_.size();
    if (units.size() < span) return found;
    for (size_t start = 0; start + span <= units.size();) {
        size_t i = 0;
        while (i < span && units[start + i] == needle_[i]) ++i;
        if (i < span) {
            ++start;
            continue;
        }
        found.push_back({offsets[start], offsets[start + span]});
        if (firstOnly) return found;
        // Occurrences do not overlap: what is painted has to be a set of
        // stretches of the line, and one starting inside another is not.
        start += span;
    }
    return found;
}

}  // namespace amberedit::encoding
