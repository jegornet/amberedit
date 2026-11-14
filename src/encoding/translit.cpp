#include "encoding/translit.hpp"

#include <cstddef>
#include <vector>

namespace amberedit::encoding {
namespace {

/// One character of the text: its code point where it is a two-byte sequence,
/// which every letter spelt out here is, and 0 for anything else, which is only
/// ever copied.
struct Piece {
    char32_t code;
    std::string_view bytes;
};

std::vector<Piece> piecesOf(std::string_view text) {
    std::vector<Piece> pieces;
    size_t pos = 0;
    while (pos < text.size()) {
        const auto first = static_cast<unsigned char>(text[pos]);
        if ((first & 0xE0u) == 0xC0u && pos + 1 < text.size() &&
            (static_cast<unsigned char>(text[pos + 1]) & 0xC0u) == 0x80u) {
            const char32_t code =
                ((first & 0x1Fu) << 6) | (static_cast<unsigned char>(text[pos + 1]) & 0x3Fu);
            pieces.push_back({code, text.substr(pos, 2)});
            pos += 2;
            continue;
        }
        size_t length = 1;
        while (pos + length < text.size() &&
               (static_cast<unsigned char>(text[pos + length]) & 0xC0u) == 0x80u) {
            ++length;
        }
        pieces.push_back({0, text.substr(pos, length)});
        pos += length;
    }
    return pieces;
}

/// The lower-case letter a Cyrillic capital is, or 0 where `code` is not a
/// capital. Unicode lays them out in runs: Ѐ to Џ sit 0x50 below their small
/// letters, А to Я 0x20 below, and from Ґ on each capital is the first code
/// point of a pair — the even one, except between Ӂ and ӎ, where the run is
/// shifted by one and the capital is odd.
char32_t lowerOf(char32_t code) {
    if (code >= 0x0400 && code <= 0x040F) return code + 0x50;
    if (code >= 0x0410 && code <= 0x042F) return code + 0x20;
    if ((code >= 0x0490 && code <= 0x04BF) || (code >= 0x04D0 && code <= 0x04FF)) {
        return code % 2 == 0 ? code + 1 : 0;
    }
    if (code >= 0x04C1 && code <= 0x04CE) return code % 2 == 1 ? code + 1 : 0;
    return 0;
}

/// How a lower-case letter is spelt, or nullptr where it is not one spelt out.
const char* spellingOf(char32_t code) {
    static const char* const kRussian[32] = {
        "a",  "b",  "v",   "g", "d", "e", "zh", "z",  "i",  "j", "k",
        "l",  "m",  "n",   "o", "p", "r", "s",  "t",  "u",  "f", "h",
        "c",  "ch", "sh", "sch", "\"", "y", "'", "e", "yu", "ya"};
    if (code >= 0x0430 && code <= 0x044F) return kRussian[code - 0x0430];
    switch (code) {
        case 0x0451: return "yo";  // ё
        // Ukrainian and Belarusian.
        case 0x0454: return "ye";  // є
        case 0x0456: return "i";   // і, Kazakh too
        case 0x0457: return "yi";  // ї
        case 0x045E: return "u";   // ў, Uzbek too
        case 0x0491: return "g";   // ґ
        // Serbian, Macedonian and Bulgarian.
        case 0x0450: return "e";   // ѐ
        case 0x045D: return "i";   // ѝ
        case 0x0452: return "dj";  // ђ
        case 0x0453: return "gj";  // ѓ
        case 0x0455: return "dz";  // ѕ
        case 0x0458: return "j";   // ј
        case 0x0459: return "lj";  // љ
        case 0x045A: return "nj";  // њ
        case 0x045B: return "c";   // ћ
        case 0x045C: return "kj";  // ќ
        case 0x045F: return "dz";  // џ
        // The Turkic languages written in Cyrillic — Kazakh, Kyrgyz, Uzbek,
        // Turkmen, Tatar, Bashkir, Chuvash, Sakha, Altai, Khakas, Tuvan,
        // Gagauz and the rest — and Tajik, which shares their letters.
        case 0x0493: return "gh";  // ғ
        case 0x0495: return "gh";  // ҕ
        case 0x0497: return "zh";  // җ
        case 0x0499: return "z";   // ҙ
        case 0x049B: return "q";   // қ
        case 0x04A1: return "q";   // ҡ
        case 0x04A3: return "ng";  // ң
        case 0x04A5: return "ng";  // ҥ
        case 0x04AB: return "s";   // ҫ
        case 0x04AF: return "u";   // ү
        case 0x04B1: return "u";   // ұ
        case 0x04B3: return "h";   // ҳ
        case 0x04B7: return "j";   // ҷ
        case 0x04BB: return "h";   // һ
        case 0x04CC: return "ch";  // ӌ
        case 0x04D1: return "a";   // ӑ
        case 0x04D3: return "a";   // ӓ
        case 0x04D7: return "e";   // ӗ
        case 0x04D9: return "a";   // ә
        case 0x04E3: return "i";   // ӣ
        case 0x04E7: return "o";   // ӧ
        case 0x04E9: return "o";   // ө
        case 0x04EF: return "u";   // ӯ
        case 0x04F1: return "u";   // ӱ
        case 0x04F3: return "u";   // ӳ
        default: return nullptr;
    }
}

bool isCapital(const Piece& piece) {
    if (piece.code != 0) return lowerOf(piece.code) != 0;
    return piece.bytes.size() == 1 && piece.bytes[0] >= 'A' && piece.bytes[0] <= 'Z';
}

bool isLetter(const Piece& piece) {
    if (piece.code != 0) return spellingOf(piece.code) != nullptr || lowerOf(piece.code) != 0;
    if (piece.bytes.size() != 1) return false;
    const char c = piece.bytes[0];
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

/// Whether the capital at `at` stands in a word written in capitals: the letter
/// after it decides, and the one before it where it ends the word.
bool inCapitals(const std::vector<Piece>& pieces, size_t at) {
    if (at + 1 < pieces.size() && isLetter(pieces[at + 1])) return isCapital(pieces[at + 1]);
    if (at > 0 && isLetter(pieces[at - 1])) return isCapital(pieces[at - 1]);
    return false;
}

void appendUpper(std::string& out, const char* spelling, bool whole) {
    for (const char* c = spelling; *c != '\0'; ++c) {
        const bool upper = (c == spelling || whole) && *c >= 'a' && *c <= 'z';
        out += upper ? static_cast<char>(*c - 'a' + 'A') : *c;
    }
}

}  // namespace

std::string transliterated(std::string_view utf8,
                           const std::function<bool(std::string_view)>& carried) {
    const std::vector<Piece> pieces = piecesOf(utf8);
    std::string out;
    out.reserve(utf8.size());
    for (size_t i = 0; i < pieces.size(); ++i) {
        const Piece& piece = pieces[i];
        const char32_t lower = lowerOf(piece.code);
        const char* spelling = spellingOf(lower != 0 ? lower : piece.code);
        if (spelling == nullptr || carried(piece.bytes)) {
            out += piece.bytes;
            continue;
        }
        if (lower == 0) {
            out += spelling;
        } else {
            appendUpper(out, spelling, inCapitals(pieces, i));
        }
    }
    return out;
}

}  // namespace amberedit::encoding
