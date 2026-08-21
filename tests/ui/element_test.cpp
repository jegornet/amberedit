#include <catch2/catch.hpp>

#include <string>
#include <utility>

#include "ui/term/element.hpp"
#include "ui/term/screen.hpp"
#include "ui/term/utf8.hpp"

using namespace amberedit::ui::term;

namespace {

/// What a row of the frame says, trailing blanks and all. Rendering into a
/// buffer and reading it back is the only way to test layout without a terminal
/// — and the buffer is what the terminal is handed, so what is checked here is
/// what would be drawn.
std::string rowText(const Screen& screen, int y) {
    std::string row;
    for (int x = 0; x < screen.width(); ++x) row += screen.at(x, y).glyph;
    return row;
}

}  // namespace

TEST_CASE("hbox shares its width out and flex takes what is left", "[element]") {
    Screen screen(20, 1);
    render(screen, hbox({text("ab"), flex(text("-")), text("yz")}));
    CHECK(rowText(screen, 0) == "ab-               yz");
}

TEST_CASE("vbox stacks its children a row each", "[element]") {
    Screen screen(6, 3);
    render(screen, vbox({text("one"), text("two")}));
    CHECK(rowText(screen, 0) == "one   ");
    CHECK(rowText(screen, 1) == "two   ");
    CHECK(rowText(screen, 2) == "      ");
}

TEST_CASE("border costs a row and a column on each side", "[element]") {
    Screen screen(7, 3);
    render(screen, border(text("hi")));
    CHECK(rowText(screen, 0) == "┌─────┐");
    CHECK(rowText(screen, 1) == "│hi   │");
    CHECK(rowText(screen, 2) == "└─────┘");
}

TEST_CASE("center places an element on both axes", "[element]") {
    Screen screen(9, 3);
    render(screen, center(text("ab")));
    CHECK(rowText(screen, 0) == "         ");
    CHECK(rowText(screen, 1) == "   ab    ");
}

TEST_CASE("the innermost colour is the one that shows", "[element]") {
    // Parents paint their whole box and then draw their children over it, so a
    // coloured run inside a coloured line wins. Every screen's colouring rests
    // on this: a URL in a quote keeps the link colour, the rest of the line
    // keeps the quote's.
    Screen screen(4, 1);
    const Color outer{240};
    const Color inner{111};
    render(screen, color(outer, hbox({color(inner, text("a")), text("b")})));
    CHECK(screen.at(0, 0).fg == inner);
    CHECK(screen.at(1, 0).fg == outer);
}

TEST_CASE("a dialog wipes what it covers and nothing else", "[element]") {
    // Composed the way the dialogs compose it — `dialog | clear_under | center`.
    // That order is what bounds the wipe: dbox hands every child the whole
    // screen, so it is the centring outside clear_under that keeps it to the
    // dialog. Getting this the wrong way round would blank the screen.
    Screen screen(5, 1);
    Element dialog = center(clear_under(text("x")));
    render(screen,
           bgcolor(Color{17}, dbox({text("....."), std::move(dialog)})));

    CHECK(rowText(screen, 0) == "..x..");
    CHECK(screen.at(2, 0).bg.defaulted);  // the dialog's own cell, back to bare
    CHECK(screen.at(0, 0).bg == Color{17});
    CHECK(screen.at(4, 0).bg == Color{17});
}

TEST_CASE("reflect reports where an element was actually drawn", "[element]") {
    // What the click handlers hit-test against. Computing the position a second
    // time instead is what lets a hit box drift away from what is on screen.
    Screen screen(11, 3);
    Box where;
    render(screen, center(text("abc") | reflect(where)));

    CHECK(where.x_min == 4);
    CHECK(where.x_max == 6);
    CHECK(where.y_min == 1);
    CHECK(where.y_max == 1);
    CHECK(where.Contain(5, 1));
    CHECK_FALSE(where.Contain(2, 1));
}

TEST_CASE("text longer than its box is cut, never overrun", "[element]") {
    Screen screen(4, 1);
    render(screen, text("abcdefgh"));
    CHECK(rowText(screen, 0) == "abcd");
}

TEST_CASE("a double-width glyph occupies two cells", "[element]") {
    // The second cell is left empty rather than blanked: whoever writes the
    // frame out steps over it, so the glyph is not cut in half.
    Screen screen(4, 1);
    render(screen, text("日x"));
    CHECK(screen.at(0, 0).glyph == "日");
    CHECK(screen.at(1, 0).glyph.empty());
    CHECK(screen.at(2, 0).glyph == "x");
}

TEST_CASE("a palette number the terminal has is used untouched", "[element]") {
    // The whole point of theming in palette numbers: what the theme wrote is
    // what the terminal is told, with nothing approximated in between.
    CHECK(nearestWithin(196, 256) == 196);
    CHECK(nearestWithin(255, 256) == 255);
    // And a sixteen-colour theme on a sixteen-colour terminal likewise, which is
    // what makes themes/ged_classic.cfg exact on a bare console.
    CHECK(nearestWithin(4, 16) == 4);
    CHECK(nearestWithin(15, 16) == 15);
}

TEST_CASE("a palette entry expands to the colour it stands for", "[element]") {
    // What a terminal in direct-colour mode is handed. It reads a colour number
    // as a triple rather than as an index, so an entry sent as it stands would
    // paint whatever its number happens to spell — 102 as #000066 instead of
    // grey, and a whole theme turning blue with nothing to show for it.
    CHECK(paletteRgb(196) == 0xff0000u);  // the cube's red corner
    CHECK(paletteRgb(102) == 0x878787u);  // the grey the built-in palette uses
    CHECK(paletteRgb(232) == 0x080808u);  // the ramp's dark end
    CHECK(paletteRgb(255) == 0xeeeeeeu);  // and its light one
    CHECK(paletteRgb(15) == 0xffffffu);   // bright white among the ANSI sixteen
}

TEST_CASE("a palette number the terminal lacks falls back", "[element]") {
    // Only when the terminal genuinely has fewer colours than the theme asks
    // for. Approximate matches, so these check the direction rather than an
    // exact index: bright red must not come out green.
    CHECK(nearestWithin(196, 8) == 1);  // cube red -> ANSI red
    CHECK(nearestWithin(46, 8) == 2);   // cube green -> ANSI green
    CHECK(nearestWithin(21, 8) == 4);   // cube blue -> ANSI blue
    CHECK(nearestWithin(232, 8) == 0);  // the darkest grey -> black
    CHECK(nearestWithin(255, 8) == 7);  // the lightest -> white
    // Never out of range, whatever it was handed.
    CHECK(nearestWithin(200, 8) < 8);
}
