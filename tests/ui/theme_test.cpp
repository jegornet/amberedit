#include <doctest/doctest.h>

#include <set>
#include <string>

#include "config/cfg_file.hpp"
#include "config/text_util.hpp"
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
    CHECK(same(loaded.error, builtIn.error));
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
    // kludge, screen_buttons, dimmed and scroll_thumb are one color by default;
    // naming one of them moves that one alone.
    const Palette loaded = valueOf(parsePalette("kludge 196\n"));
    const Palette builtIn;
    CHECK(same(loaded.kludge, Color{196}));
    CHECK(same(loaded.screenButtons, builtIn.screenButtons));

    // The panel's bar is a role of its own and shares with nothing: naming it
    // moves it and leaves the two colors it could be mistaken for where they are.
    const Palette apart = valueOf(parsePalette("reader_sidebar_msglist_selected 196\n"));
    CHECK(same(apart.readerSidebarMsglistSelected, Color{196}));
    CHECK(same(apart.selection, builtIn.selection));
    CHECK(same(apart.dimmed, builtIn.dimmed));
}

TEST_CASE("Colors are palette numbers, across the whole range [theme]") {
    CHECK(same(valueOf(parsePalette("text 0")).text, Color{0}));      // ANSI black
    CHECK(same(valueOf(parsePalette("text 15")).text, Color{15}));    // bright white
    CHECK(same(valueOf(parsePalette("text 196")).text, Color{196}));  // in the cube
    CHECK(same(valueOf(parsePalette("text 255")).text, Color{255}));  // the grey ramp
}

TEST_CASE("A theme carries one setting that is not a color [theme]") {
    // `input_filler_show`, on or off as every other switch AmberEdit reads is
    // written, and the built-in palette's own answer where the file says
    // nothing — which is on, the fills it gives an idle field being steps of
    // near-black.
    CHECK(valueOf(parsePalette("input_filler_show on")).inputFillerShown);
    CHECK_FALSE(valueOf(parsePalette("input_filler_show off")).inputFillerShown);
    CHECK(valueOf(parsePalette("text 33")).inputFillerShown);

    // A number is not a switch, and neither is the palette complaint: the key
    // is answered as the setting it is.
    const std::string error = errorOf(parsePalette("input_filler_show 1", "theme.cfg"));
    REQUIRE_MESSAGE(contains(error, "on or off"), error);
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

TEST_CASE("The black theme is the built-in palette, written out [theme]") {
    // It is what AmberEdit draws with when the config names no theme, and it
    // ships as the thing to copy and edit, so the two drifting apart would both
    // change the program under everyone and hand every new theme a wrong
    // starting point. Reading it also proves every role has a key: a field the
    // file cannot name would fail this comparison.
    const Palette loaded = valueOf(amberedit::ui::theme::loadPalette(
        amberedit::test::projectPath("themes/black.cfg")));
    const Palette builtIn;

    CHECK(same(loaded.background, builtIn.background));
    CHECK(same(loaded.selection, builtIn.selection));
    CHECK(same(loaded.selectionText, builtIn.selectionText));
    CHECK(same(loaded.readerSidebarMsglistSelected, builtIn.readerSidebarMsglistSelected));
    CHECK(same(loaded.inputField, builtIn.inputField));
    CHECK(same(loaded.inputText, builtIn.inputText));
    CHECK(same(loaded.focusedField, builtIn.focusedField));
    CHECK(same(loaded.focusedText, builtIn.focusedText));
    CHECK(same(loaded.inputFiller, builtIn.inputFiller));
    CHECK(loaded.inputFillerShown == builtIn.inputFillerShown);
    CHECK(same(loaded.dialogBackground, builtIn.dialogBackground));
    CHECK(same(loaded.dialogText, builtIn.dialogText));
    CHECK(same(loaded.dialogTitle, builtIn.dialogTitle));
    CHECK(same(loaded.dialogLabel, builtIn.dialogLabel));
    CHECK(same(loaded.dialogHint, builtIn.dialogHint));
    CHECK(same(loaded.dialogField, builtIn.dialogField));
    CHECK(same(loaded.dialogFlash, builtIn.dialogFlash));
    CHECK(same(loaded.dialogBorder, builtIn.dialogBorder));
    CHECK(same(loaded.header, builtIn.header));
    CHECK(same(loaded.ownName, builtIn.ownName));
    CHECK(same(loaded.msglistUnread, builtIn.msglistUnread));
    CHECK(same(loaded.text, builtIn.text));
    CHECK(same(loaded.link, builtIn.link));
    CHECK(same(loaded.quoteEven, builtIn.quoteEven));
    CHECK(same(loaded.quoteOdd, builtIn.quoteOdd));
    CHECK(same(loaded.kludge, builtIn.kludge));
    CHECK(same(loaded.screenButtons, builtIn.screenButtons));
    CHECK(same(loaded.dimmed, builtIn.dimmed));
    CHECK(same(loaded.scrollThumb, builtIn.scrollThumb));
    CHECK(same(loaded.trailer, builtIn.trailer));
    CHECK(same(loaded.tableHeader, builtIn.tableHeader));
    CHECK(same(loaded.menuButton, builtIn.menuButton));
    CHECK(same(loaded.separator, builtIn.separator));
    CHECK(same(loaded.scrollTrack, builtIn.scrollTrack));
    CHECK(same(loaded.hintBar, builtIn.hintBar));
    CHECK(same(loaded.error, builtIn.error));
    CHECK(same(loaded.unsent, builtIn.unsent));
    CHECK(same(loaded.found, builtIn.found));
    CHECK(same(loaded.animatedButtonText, builtIn.animatedButtonText));
}

TEST_CASE("The sixteen-color theme loads and states every role [theme]") {
    // A shipped theme has to parse, and a sixteen-color palette has to reach
    // every role: one left at its default would put a default-palette color in
    // the middle of a DOS screen. Which color each role gets is the theme's
    // business and gets tuned — only that none was forgotten is checked here.
    const Palette loaded = valueOf(amberedit::ui::theme::loadPalette(
        amberedit::test::projectPath("themes/16_colors.cfg")));
    const Palette builtIn;

    CHECK_FALSE(same(loaded.background, builtIn.background));
    CHECK_FALSE(same(loaded.selection, builtIn.selection));
    CHECK_FALSE(same(loaded.selectionText, builtIn.selectionText));
    CHECK_FALSE(same(loaded.inputField, builtIn.inputField));
    CHECK_FALSE(same(loaded.inputText, builtIn.inputText));
    CHECK_FALSE(same(loaded.focusedField, builtIn.focusedField));
    CHECK_FALSE(same(loaded.focusedText, builtIn.focusedText));
    CHECK_FALSE(same(loaded.inputFiller, builtIn.inputFiller));
    // The one setting a theme carries that is not a color. It agrees with the
    // built-in palette here, and is stated in the file all the same, so that a
    // theme is the whole palette written out and not the difference from
    // another one.
    CHECK(loaded.inputFillerShown);
    CHECK_FALSE(same(loaded.dialogBackground, builtIn.dialogBackground));
    CHECK_FALSE(same(loaded.dialogText, builtIn.dialogText));
    CHECK_FALSE(same(loaded.dialogTitle, builtIn.dialogTitle));
    CHECK_FALSE(same(loaded.dialogLabel, builtIn.dialogLabel));
    CHECK_FALSE(same(loaded.dialogHint, builtIn.dialogHint));
    CHECK_FALSE(same(loaded.dialogField, builtIn.dialogField));
    CHECK_FALSE(same(loaded.dialogFlash, builtIn.dialogFlash));
    CHECK_FALSE(same(loaded.dialogBorder, builtIn.dialogBorder));
    CHECK_FALSE(same(loaded.header, builtIn.header));
    CHECK_FALSE(same(loaded.ownName, builtIn.ownName));
    CHECK_FALSE(same(loaded.msglistUnread, builtIn.msglistUnread));
    CHECK_FALSE(same(loaded.text, builtIn.text));
    CHECK_FALSE(same(loaded.quoteEven, builtIn.quoteEven));
    CHECK_FALSE(same(loaded.quoteOdd, builtIn.quoteOdd));
    CHECK_FALSE(same(loaded.kludge, builtIn.kludge));
    CHECK_FALSE(same(loaded.screenButtons, builtIn.screenButtons));
    CHECK_FALSE(same(loaded.dimmed, builtIn.dimmed));
    CHECK_FALSE(same(loaded.scrollThumb, builtIn.scrollThumb));
    CHECK_FALSE(same(loaded.trailer, builtIn.trailer));
    CHECK_FALSE(same(loaded.tableHeader, builtIn.tableHeader));
    CHECK_FALSE(same(loaded.menuButton, builtIn.menuButton));
    CHECK_FALSE(same(loaded.separator, builtIn.separator));
    CHECK_FALSE(same(loaded.scrollTrack, builtIn.scrollTrack));
    CHECK_FALSE(same(loaded.hintBar, builtIn.hintBar));
    CHECK_FALSE(same(loaded.error, builtIn.error));
    CHECK_FALSE(same(loaded.unsent, builtIn.unsent));
    CHECK_FALSE(same(loaded.found, builtIn.found));
    CHECK_FALSE(same(loaded.animatedButtonText, builtIn.animatedButtonText));
}

TEST_CASE("Every shipped theme states the same keys [theme]") {
    // `themes/black.cfg` is compared with the defaults field by field above, so
    // a role it forgot fails there. The others are held to it rather than to the
    // defaults: a key missing from one of them is a color out of the black theme
    // showing up in the middle of a blue or a sixteen-color screen.
    const auto keysOf = [](const char* file) {
        const auto text =
            amberedit::config::text::readFile(amberedit::test::projectPath(file));
        REQUIRE_MESSAGE(text.has_value(), file);
        const auto entries = amberedit::config::parseCfg(*text, file);
        REQUIRE_MESSAGE(entries.has_value(), file);
        std::set<std::string> keys;
        for (const auto& entry : *entries) keys.insert(entry.key);
        return keys;
    };

    const std::set<std::string> written = keysOf("themes/black.cfg");
    REQUIRE(written.size() > 30);
    for (const char* file :
         {"themes/blue.cfg", "themes/16_colors.cfg", "themes/white.cfg"}) {
        CAPTURE(file);
        CHECK(keysOf(file) == written);
    }
}

TEST_CASE("Nothing a shipped theme draws a box with is the box's own color [theme]") {
    // The rule the dialog palette exists for: a modal carries a fill of its
    // own, so every color drawn on that fill has to be something else. Left
    // unchecked it is an invisible confirmation rather than an ugly one — the
    // text is there, in the color of what is behind it.
    for (const char* file : {"themes/blue.cfg", "themes/16_colors.cfg",
                             "themes/black.cfg", "themes/white.cfg"}) {
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
        // The frame is drawn on the box's own fill, and `separator` is not:
        // the rules on a screen are the screen's.
        CHECK_FALSE(same(theme.dialogBorder, theme.dialogBackground));
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

TEST_CASE("A field a shipped theme draws is legible in either state [theme]") {
    // The same rule the dialog palette is held to, for the pair of fills the
    // compose screen puts down: a field standing idle and the one the typing is
    // in are both text on a fill of its own, and text the color of what is
    // behind it is a field that looks empty.
    for (const char* file : {"themes/blue.cfg", "themes/16_colors.cfg",
                             "themes/black.cfg", "themes/white.cfg"}) {
        CAPTURE(file);
        const Palette theme = valueOf(
            amberedit::ui::theme::loadPalette(amberedit::test::projectPath(file)));

        CHECK_FALSE(same(theme.inputText, theme.inputField));
        CHECK_FALSE(same(theme.focusedText, theme.focusedField));
        // The underscores standing in the room a field has left are a color of
        // their own and not the fill under them. How far they stand off it is
        // the theme's business — one theme sets them a whisker above the fill on
        // purpose, so that the field is felt rather than read — and against
        // `focused_field` they are held to nothing at all: that fill is on
        // screen exactly where the typing is, and a theme may let them go under
        // it rather than draw a second mark inside the first.
        CHECK_FALSE(same(theme.inputFiller, theme.inputField));
        // And inside a box they are `dialog_hint`, on the box's own two fills.
        CHECK_FALSE(same(theme.dialogHint, theme.dialogField));
        CHECK_FALSE(same(theme.dialogHint, theme.selection));
        // And the two fills apart from each other, which is what says which of
        // the fields the typing is in.
        CHECK_FALSE(same(theme.focusedField, theme.inputField));
    }
}

TEST_CASE("A broken theme is refused with the file and line named [theme]") {
    const std::string error = errorOf(parsePalette("\ntext\n", "theme.cfg"));
    REQUIRE_MESSAGE(contains(error, "theme.cfg:2"), error);
    // What a theme file written for the toml AmberEdit used to read has in it.
    const std::string error2 = errorOf(parsePalette("text = 33", "theme.cfg"));
    REQUIRE_MESSAGE(contains(error2, "old toml spelling"), error2);
}
