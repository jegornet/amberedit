#include <doctest/doctest.h>

#include <string>

#include "test_paths.hpp"
#include "test_strings.hpp"
#include "ui/theme.hpp"

using amberedit::test::contains;
using amberedit::test::errorOf;
using amberedit::test::valueOf;
using amberedit::ui::theme::Color;
using amberedit::ui::theme::Palette;
using amberedit::ui::theme::parsePalette;

namespace {

bool same(Color a, Color b) { return a == b; }

}  // namespace

TEST_CASE("An empty theme file is the built-in palette [theme]") {
    const Palette loaded = valueOf(parsePalette(""));
    const Palette builtIn;
    CHECK(same(loaded.background, builtIn.background));
    CHECK(same(loaded.text, builtIn.text));
    CHECK(same(loaded.warning, builtIn.warning));
}

TEST_CASE("A theme file states only what it changes [theme]") {
    // The point of the defaults: a file with one line is a valid theme, and
    // everything it says nothing about keeps the color it had.
    const Palette loaded = valueOf(parsePalette("text 33\n"));
    const Palette builtIn;
    CHECK(same(loaded.text, Color{33}));
    CHECK(same(loaded.background, builtIn.background));
}

TEST_CASE("Roles the built-in palette shares can be taken apart [theme]") {
    // kludge, footer, dimmed and scroll_thumb are one color by default; naming
    // one of them moves that one alone.
    const Palette loaded = valueOf(parsePalette("kludge 196\n"));
    const Palette builtIn;
    CHECK(same(loaded.kludge, Color{196}));
    CHECK(same(loaded.footer, builtIn.footer));
}

TEST_CASE("Colors are palette numbers, across the whole range [theme]") {
    CHECK(same(valueOf(parsePalette("text 0")).text, Color{0}));      // ANSI black
    CHECK(same(valueOf(parsePalette("text 15")).text, Color{15}));    // bright white
    CHECK(same(valueOf(parsePalette("text 196")).text, Color{196}));  // in the cube
    CHECK(same(valueOf(parsePalette("text 255")).text, Color{255}));  // the grey ramp
}

TEST_CASE("A key that is not a color is refused [theme]") {
    // Silently ignoring it would leave a typo looking like a color that does
    // not work.
    const std::string error = errorOf(parsePalette("txet 33", "theme.cfg"));
    REQUIRE_MESSAGE(contains(error, "txet"), error);
}

TEST_CASE("A value that is not a palette number is refused [theme]") {
    CHECK_FALSE(parsePalette("text 256").has_value());  // past the end of the palette
    CHECK_FALSE(parsePalette("text -1").has_value());
    CHECK_FALSE(parsePalette("text 1.5").has_value());
    CHECK_FALSE(parsePalette("text true").has_value());
    CHECK_FALSE(parsePalette("text 33 34").has_value());  // a role is one color
}

TEST_CASE("The old #rrggbb spelling is refused by name [theme]") {
    // What a file predating palette numbers still has in it. Saying only "not an
    // integer" would leave the user to work out what happened.
    const std::string error = errorOf(parsePalette("text \"#1c1e2a\"", "theme.cfg"));
    REQUIRE_MESSAGE(contains(error, "256-color palette"), error);
}

TEST_CASE("The example theme is the built-in palette, written out [theme]") {
    // It ships as the thing to copy and edit, so the two drifting apart would
    // hand every new theme a wrong starting point. Reading it also proves every
    // role has a key: a field the file cannot name would fail this comparison.
    const Palette loaded = valueOf(amberedit::ui::theme::loadPalette(
        amberedit::test::projectPath("themes/default.cfg")));
    const Palette builtIn;

    CHECK(same(loaded.background, builtIn.background));
    CHECK(same(loaded.selection, builtIn.selection));
    CHECK(same(loaded.selectionText, builtIn.selectionText));
    CHECK(same(loaded.inputField, builtIn.inputField));
    CHECK(same(loaded.dialogBackground, builtIn.dialogBackground));
    CHECK(same(loaded.dialogText, builtIn.dialogText));
    CHECK(same(loaded.dialogTitle, builtIn.dialogTitle));
    CHECK(same(loaded.dialogLabel, builtIn.dialogLabel));
    CHECK(same(loaded.dialogHint, builtIn.dialogHint));
    CHECK(same(loaded.dialogField, builtIn.dialogField));
    CHECK(same(loaded.dialogFlash, builtIn.dialogFlash));
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

TEST_CASE("The GoldED Classic theme loads and states every role [theme]") {
    // A shipped theme has to parse, and a sixteen-color palette has to reach
    // every role: one left at its default would put a default-palette color in
    // the middle of a DOS screen. Which color each role gets is the theme's
    // business and gets tuned — only that none was forgotten is checked here.
    const Palette loaded = valueOf(amberedit::ui::theme::loadPalette(
        amberedit::test::projectPath("themes/ged_classic.cfg")));
    const Palette builtIn;

    CHECK_FALSE(same(loaded.background, builtIn.background));
    CHECK_FALSE(same(loaded.selection, builtIn.selection));
    CHECK_FALSE(same(loaded.selectionText, builtIn.selectionText));
    CHECK_FALSE(same(loaded.inputField, builtIn.inputField));
    CHECK_FALSE(same(loaded.dialogBackground, builtIn.dialogBackground));
    CHECK_FALSE(same(loaded.dialogText, builtIn.dialogText));
    CHECK_FALSE(same(loaded.dialogTitle, builtIn.dialogTitle));
    CHECK_FALSE(same(loaded.dialogLabel, builtIn.dialogLabel));
    CHECK_FALSE(same(loaded.dialogHint, builtIn.dialogHint));
    CHECK_FALSE(same(loaded.dialogField, builtIn.dialogField));
    CHECK_FALSE(same(loaded.dialogFlash, builtIn.dialogFlash));
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

TEST_CASE("Nothing a shipped theme draws a box with is the box's own color [theme]") {
    // The rule the dialog palette exists for: a modal carries a fill of its
    // own, so every color drawn on that fill has to be something else. Left
    // unchecked it is an invisible confirmation rather than an ugly one — the
    // text is there, in the color of what is behind it.
    for (const char* file : {"themes/default.cfg", "themes/ged_classic.cfg"}) {
        CAPTURE(file);
        const Palette theme = valueOf(
            amberedit::ui::theme::loadPalette(amberedit::test::projectPath(file)));

        // Written straight onto the box.
        CHECK_FALSE(same(theme.dialogText, theme.dialogBackground));
        CHECK_FALSE(same(theme.dialogTitle, theme.dialogBackground));
        CHECK_FALSE(same(theme.dialogLabel, theme.dialogBackground));
        CHECK_FALSE(same(theme.dialogHint, theme.dialogBackground));
        CHECK_FALSE(same(theme.dialogFlash, theme.dialogBackground));
        CHECK_FALSE(same(theme.menuButton, theme.dialogBackground));
        CHECK_FALSE(same(theme.separator, theme.dialogBackground));
        CHECK_FALSE(same(theme.error, theme.dialogBackground));

        // The two fills a box puts down over its own: the bar on whatever Enter
        // would act on, and the slot that takes typing.
        CHECK_FALSE(same(theme.selection, theme.dialogBackground));
        CHECK_FALSE(same(theme.dialogField, theme.dialogBackground));

        // And what is written on each of them. A click lands on the selected
        // button as readily as on the other one, which is what puts
        // `dialog_flash` on the bar; a field standing idle is `dialog_label` on
        // the slot.
        CHECK_FALSE(same(theme.selectionText, theme.selection));
        CHECK_FALSE(same(theme.dialogFlash, theme.selection));
        CHECK_FALSE(same(theme.dialogLabel, theme.dialogField));
    }
}

TEST_CASE("A broken theme is refused with the file and line named [theme]") {
    const std::string error = errorOf(parsePalette("\ntext\n", "theme.cfg"));
    REQUIRE_MESSAGE(contains(error, "theme.cfg:2"), error);
    // What a theme file written for the toml AmberEdit used to read has in it.
    const std::string error2 = errorOf(parsePalette("text = 33", "theme.cfg"));
    REQUIRE_MESSAGE(contains(error2, "old toml spelling"), error2);
}
