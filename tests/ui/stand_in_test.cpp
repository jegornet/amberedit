#include <doctest/doctest.h>

#include <set>

#include "ui/term/utf8.hpp"

using amberedit::ui::term::standIn;

namespace {

/// What a charset like CP866 or KOI8-R carries of the glyphs the interface
/// draws with: ASCII and the square box drawing, and none of the rounded
/// corners, arrows or symbols.
bool eightBit(char32_t code) {
    static const std::set<char32_t> boxes{U'─', U'│', U'┌', U'┐', U'└', U'┘', U'├', U'┤',
                                          U'√'};
    return code < 0x80 || boxes.count(code) != 0;
}

bool asciiOnly(char32_t code) { return code < 0x80; }

}  // namespace

TEST_CASE("a glyph the charset carries is drawn as itself [glyph]") {
    CHECK(standIn(U'─', eightBit) == U'─');
    CHECK(standIn(U'┌', eightBit) == U'┌');
    CHECK(standIn(U'A', asciiOnly) == U'A');
}

TEST_CASE("a rounded corner falls back to the square one, then to + [glyph]") {
    CHECK(standIn(U'╭', eightBit) == U'┌');
    CHECK(standIn(U'╮', eightBit) == U'┐');
    CHECK(standIn(U'╰', eightBit) == U'└');
    CHECK(standIn(U'╯', eightBit) == U'┘');
    CHECK(standIn(U'╭', asciiOnly) == U'+');
    CHECK(standIn(U'│', asciiOnly) == U'|');
}

TEST_CASE("the buttons' glyphs fall back to ASCII the charset carries [glyph]") {
    CHECK(standIn(U'←', eightBit) == U'<');
    CHECK(standIn(U'≡', eightBit) == U'=');
    CHECK(standIn(U'✓', eightBit) == U'√');
    CHECK(standIn(U'✓', asciiOnly) == U'v');
    CHECK(standIn(U'—', eightBit) == U'-');
}

TEST_CASE("a glyph nothing stands in for is a question mark [glyph]") {
    CHECK(standIn(U'⚲', eightBit) == U'?');
    CHECK(standIn(U'漢', asciiOnly) == U'?');
}
