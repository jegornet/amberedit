#include <catch2/catch.hpp>

#include <string>

#include "test_paths.hpp"
#include "ui/theme.hpp"

using amberedit::ui::theme::Palette;
using amberedit::ui::theme::parsePalette;
using amberedit::ui::theme::Color;

namespace {

bool same(Color a, Color b) { return a == b; }

}  // namespace

TEST_CASE("An empty theme file is the built-in palette", "[theme]") {
    const Palette loaded = parsePalette("");
    const Palette builtIn;
    CHECK(same(loaded.background, builtIn.background));
    CHECK(same(loaded.text, builtIn.text));
    CHECK(same(loaded.warning, builtIn.warning));
}

TEST_CASE("A theme file states only what it changes", "[theme]") {
    // The point of the defaults: a file with one line is a valid theme, and
    // everything it says nothing about keeps the color it had.
    const Palette loaded = parsePalette("text 33\n");
    const Palette builtIn;
    CHECK(same(loaded.text, Color{33}));
    CHECK(same(loaded.background, builtIn.background));
}

TEST_CASE("Roles the built-in palette shares can be taken apart", "[theme]") {
    // kludge, footer, dimmed and scroll_thumb are one color by default; naming
    // one of them moves that one alone.
    const Palette loaded = parsePalette("kludge 196\n");
    const Palette builtIn;
    CHECK(same(loaded.kludge, Color{196}));
    CHECK(same(loaded.footer, builtIn.footer));
}

TEST_CASE("Colors are palette numbers, across the whole range", "[theme]") {
    CHECK(same(parsePalette("text 0").text, Color{0}));      // ANSI black
    CHECK(same(parsePalette("text 15").text, Color{15}));    // bright white
    CHECK(same(parsePalette("text 196").text, Color{196}));  // in the cube
    CHECK(same(parsePalette("text 255").text, Color{255}));  // the grey ramp
}

TEST_CASE("A key that is not a color is refused", "[theme]") {
    // Silently ignoring it would leave a typo looking like a color that does
    // not work.
    REQUIRE_THROWS_WITH(parsePalette("txet 33", "theme.cfg"),
                        Catch::Matchers::Contains("txet"));
}

TEST_CASE("A value that is not a palette number is refused", "[theme]") {
    CHECK_THROWS(parsePalette("text 256"));  // past the end of the palette
    CHECK_THROWS(parsePalette("text -1"));
    CHECK_THROWS(parsePalette("text 1.5"));
    CHECK_THROWS(parsePalette("text true"));
    CHECK_THROWS(parsePalette("text 33 34"));  // a role is one color
}

TEST_CASE("The old #rrggbb spelling is refused by name", "[theme]") {
    // What a file predating palette numbers still has in it. Saying only "not an
    // integer" would leave the user to work out what happened.
    REQUIRE_THROWS_WITH(parsePalette("text \"#1c1e2a\"", "theme.cfg"),
                        Catch::Matchers::Contains("256-color palette"));
}

TEST_CASE("The example theme is the built-in palette, written out", "[theme]") {
    // It ships as the thing to copy and edit, so the two drifting apart would
    // hand every new theme a wrong starting point. Reading it also proves every
    // role has a key: a field the file cannot name would fail this comparison.
    const Palette loaded =
        amberedit::ui::theme::loadPalette(amberedit::test::projectPath("themes/default.cfg"));
    const Palette builtIn;

    CHECK(same(loaded.background, builtIn.background));
    CHECK(same(loaded.selection, builtIn.selection));
    CHECK(same(loaded.selectionText, builtIn.selectionText));
    CHECK(same(loaded.inputField, builtIn.inputField));
    CHECK(same(loaded.header, builtIn.header));
    CHECK(same(loaded.ownName, builtIn.ownName));
    CHECK(same(loaded.msglistUnread, builtIn.msglistUnread));
    CHECK(same(loaded.text, builtIn.text));
    CHECK(same(loaded.link, builtIn.link));
    CHECK(same(loaded.quoteEven, builtIn.quoteEven));
    CHECK(same(loaded.quoteOdd, builtIn.quoteOdd));
    CHECK(same(loaded.kludge, builtIn.kludge));
    CHECK(same(loaded.footer, builtIn.footer));
    CHECK(same(loaded.dimmed, builtIn.dimmed));
    CHECK(same(loaded.scrollThumb, builtIn.scrollThumb));
    CHECK(same(loaded.trailer, builtIn.trailer));
    CHECK(same(loaded.tableHeader, builtIn.tableHeader));
    CHECK(same(loaded.menuButton, builtIn.menuButton));
    CHECK(same(loaded.separator, builtIn.separator));
    CHECK(same(loaded.scrollTrack, builtIn.scrollTrack));
    CHECK(same(loaded.hintBar, builtIn.hintBar));
    CHECK(same(loaded.warning, builtIn.warning));
    CHECK(same(loaded.error, builtIn.error));
    CHECK(same(loaded.unsent, builtIn.unsent));
    CHECK(same(loaded.found, builtIn.found));
    CHECK(same(loaded.animatedButtonText, builtIn.animatedButtonText));
}

TEST_CASE("The GoldED Classic theme loads and states every role", "[theme]") {
    // A shipped theme has to parse, and a sixteen-color palette has to reach
    // every role: one left at its default would put a default-palette color in
    // the middle of a DOS screen. Which color each role gets is the theme's
    // business and gets tuned — only that none was forgotten is checked here.
    const Palette loaded = amberedit::ui::theme::loadPalette(
        amberedit::test::projectPath("themes/ged_classic.cfg"));
    const Palette builtIn;

    CHECK_FALSE(same(loaded.background, builtIn.background));
    CHECK_FALSE(same(loaded.selection, builtIn.selection));
    CHECK_FALSE(same(loaded.selectionText, builtIn.selectionText));
    CHECK_FALSE(same(loaded.inputField, builtIn.inputField));
    CHECK_FALSE(same(loaded.header, builtIn.header));
    CHECK_FALSE(same(loaded.ownName, builtIn.ownName));
    CHECK_FALSE(same(loaded.msglistUnread, builtIn.msglistUnread));
    CHECK_FALSE(same(loaded.text, builtIn.text));
    CHECK_FALSE(same(loaded.quoteEven, builtIn.quoteEven));
    CHECK_FALSE(same(loaded.quoteOdd, builtIn.quoteOdd));
    CHECK_FALSE(same(loaded.kludge, builtIn.kludge));
    CHECK_FALSE(same(loaded.footer, builtIn.footer));
    CHECK_FALSE(same(loaded.dimmed, builtIn.dimmed));
    CHECK_FALSE(same(loaded.scrollThumb, builtIn.scrollThumb));
    CHECK_FALSE(same(loaded.trailer, builtIn.trailer));
    CHECK_FALSE(same(loaded.tableHeader, builtIn.tableHeader));
    CHECK_FALSE(same(loaded.menuButton, builtIn.menuButton));
    CHECK_FALSE(same(loaded.separator, builtIn.separator));
    CHECK_FALSE(same(loaded.scrollTrack, builtIn.scrollTrack));
    // The hint bar is the one role both shipped themes state the same, and the
    // same as the default: dark grey on black wherever it is drawn, so that the
    // quiet row along the bottom does not change shade with the theme. It is
    // stated in the file all the same, so that a theme is still the whole
    // palette written out.
    CHECK(same(loaded.hintBar, builtIn.hintBar));
    // `warning` is deliberately absent from both shipped themes: nothing in
    // the interface draws with it, so there is no DOS screen for a leftover
    // default-palette color to appear on.
    CHECK_FALSE(same(loaded.error, builtIn.error));
    CHECK_FALSE(same(loaded.unsent, builtIn.unsent));
    CHECK_FALSE(same(loaded.found, builtIn.found));
    CHECK_FALSE(same(loaded.animatedButtonText, builtIn.animatedButtonText));
}

TEST_CASE("A broken theme is refused with the file and line named", "[theme]") {
    REQUIRE_THROWS_WITH(parsePalette("\ntext\n", "theme.cfg"),
                        Catch::Matchers::Contains("theme.cfg:2"));
    // What a theme file written for the toml AmberEdit used to read has in it.
    REQUIRE_THROWS_WITH(parsePalette("text = 33", "theme.cfg"),
                        Catch::Matchers::Contains("old toml spelling"));
}
