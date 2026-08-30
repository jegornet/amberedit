#include "encoding/charset_detector.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <unordered_set>

namespace amberedit::encoding {
namespace {

constexpr char kSoh = '\x01';  // ^A marks a kludge line (FTS-0001)

/// ASCII, and deliberately not the locale's idea of it: a charset name is
/// ASCII by definition, and the terminal layer may well have installed a
/// single-byte locale under which toupper() would also fold bytes that are
/// half of a character rather than a letter.
constexpr char asciiUpper(char c) {
    return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
}

constexpr bool asciiIsSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

std::string upper(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), asciiUpper);
    return out;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && asciiIsSpace(s.front())) s.remove_prefix(1);
    while (!s.empty() && asciiIsSpace(s.back())) s.remove_suffix(1);
    return s;
}

/// Fidonet charset names mapped onto iconv names.
/// FTS-5003 (CHRS) and its predecessor FSC-0054 (CHARSET).
const std::unordered_map<std::string, std::string>& charsetMap() {
    static const std::unordered_map<std::string, std::string> map = {
        // Cyrillic — by far the common case in Russian echoes.
        {"CP866", "CP866"},
        {"+7_FIDO", "CP866"},
        {"IBM866", "CP866"},
        {"KOI8-R", "KOI8-R"},
        {"KOI8", "KOI8-R"},
        {"KOI8R", "KOI8-R"},
        {"KOI8-U", "KOI8-U"},
        {"CP1251", "CP1251"},
        {"WINDOWS-1251", "CP1251"},
        {"WIN1251", "CP1251"},
        {"1251", "CP1251"},
        {"ISO-8859-5", "ISO-8859-5"},
        // Latin and the rest.
        {"LATIN-1", "ISO-8859-1"},
        {"LATIN1", "ISO-8859-1"},
        {"ISO-8859-1", "ISO-8859-1"},
        {"LATIN-2", "ISO-8859-2"},
        {"ISO-8859-2", "ISO-8859-2"},
        {"LATIN-5", "ISO-8859-9"},
        {"LATIN-9", "ISO-8859-15"},
        {"CP437", "CP437"},
        {"IBM437", "CP437"},
        {"CP850", "CP850"},
        {"IBM850", "CP850"},
        {"CP852", "CP852"},
        {"CP1252", "CP1252"},
        {"MAC", "MACINTOSH"},
        {"MACINTOSH", "MACINTOSH"},
        {"UTF-8", "UTF-8"},
        {"UTF8", "UTF-8"},
        {"ASCII", "US-ASCII"},
        {"US-ASCII", "US-ASCII"},
    };
    return map;
}

/// Names that identify no particular encoding.
///
/// FTS-5003 keeps IBMPC only as an obsolete level-2 name: it says "some IBM PC
/// code page" and nothing more. Which one depends on where the message came
/// from — CP866 in Russian echoes, CP437 or CP850 in western ones, CP852 in
/// central Europe — so the name cannot be mapped, and the reader's default for
/// the area is a better answer than a guess made here.
bool namesNothingInParticular(const std::string& upperName) {
    static const std::unordered_set<std::string> names = {"IBMPC"};
    return names.count(upperName) != 0;
}

/// The charset name out of a CHRS value: "CP866 2" -> "CP866", upper case.
std::string baseName(std::string_view chrsValue) {
    std::string_view value = trim(chrsValue);
    // A CHRS value is "<name> <level>", e.g. "CP866 2"; drop the level.
    const size_t space = value.find_first_of(" \t");
    if (space != std::string_view::npos) value = value.substr(0, space);
    return upper(value);
}

}  // namespace

CharsetDetector::CharsetDetector(std::string_view defaultCharset)
    : defaultCharset_(normalize(defaultCharset)) {}

std::string CharsetDetector::extractChrsKludge(std::string_view rawBody) {
    // Kludges are lines starting with ^A. They cluster at the top of a message,
    // but CHRS also turns up after the text, so scan the whole body.
    size_t pos = 0;
    while (pos < rawBody.size()) {
        size_t lineEnd = rawBody.find_first_of("\r\n", pos);
        if (lineEnd == std::string_view::npos) lineEnd = rawBody.size();
        std::string_view line = rawBody.substr(pos, lineEnd - pos);
        pos = lineEnd + 1;

        if (line.empty() || line.front() != kSoh) continue;
        line.remove_prefix(1);

        const size_t colon = line.find(':');
        if (colon == std::string_view::npos) continue;

        const std::string name = upper(trim(line.substr(0, colon)));
        if (name == "CHRS" || name == "CHARSET" || name == "CODEPAGE") {
            return std::string(trim(line.substr(colon + 1)));
        }
    }
    return {};
}

std::string CharsetDetector::normalize(std::string_view chrsValue) {
    std::string key = baseName(chrsValue);
    if (key.empty() || namesNothingInParticular(key)) return {};

    const auto it = charsetMap().find(key);
    if (it != charsetMap().end()) return it->second;
    return key;
}

bool CharsetDetector::namesSpecificCharset(std::string_view chrsValue) {
    return !normalize(chrsValue).empty();
}

std::string CharsetDetector::defaultCharset() const {
    return defaultCharset_;
}

std::string CharsetDetector::detect(std::string_view rawBody) const {
    // An unspecific CHRS ("IBMPC") is as good as none: it says the message is
    // in some 8-bit code page, which was never in question.
    std::string normalized = normalize(extractChrsKludge(rawBody));
    if (!normalized.empty()) return normalized;
    return defaultCharset_;
}

}  // namespace amberedit::encoding
