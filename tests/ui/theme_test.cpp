#include <doctest/doctest.h>

#include <algorithm>
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
using amberedit::ui::theme::approximatedRoles;
using amberedit::ui::theme::Color;
using amberedit::ui::theme::colorWarning;
using amberedit::ui::theme::Palette;
using amberedit::ui::theme::parsePalette;

namespace {

bool same(Color a, Color b) {
    return a == b;
}

/// A palette with every color role set to `index`, and how many roles that was.
struct Everywhere {
    Palette palette;
    int roles{0};
};

/// Builds one, from the keys `themes/black.cfg` names rather than from a list
/// written here: a role added to the palette is then covered by the checks below
/// without a line being added to them, the same way the shipped themes are held
/// to the same key set further up. Nothing about the numbers in that file is
/// read — only which keys are colors, which is the keys whose value is a number.
Everywhere allRolesAt(int index) {
    const std::string text = valueOf(amberedit::config::text::readFile(
        amberedit::test::projectPath("themes/black.cfg")));
    const auto entries = valueOf(amberedit::config::parseCfg(text, "black.cfg"));

    std::string written;
    int roles = 0;
    for (const auto& entry : entries) {
        // The switches take on/off rather than a number, and are not colors.
        if (entry.values.size() != 1) continue;
        const char first = entry.values.front().front();
        if (first < '0' || first > '9') continue;
        written += entry.key + " " + std::to_string(index) + "\n";
        ++roles;
    }
    REQUIRE(roles > 30);
    return {valueOf(parsePalette(written, "built-here.cfg")), roles};
}

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

TEST_CASE("A theme carries settings that are not colors [theme]") {
    // `input_filler_show`, on or off as every other switch AmberEdit reads is
    // written, and the built-in palette's own answer where the file says
    // nothing — which is on, the fills it gives an idle field being steps of
    // near-black.
    CHECK(valueOf(parsePalette("input_filler_show on")).inputFillerShown);
    CHECK_FALSE(valueOf(parsePalette("input_filler_show off")).inputFillerShown);
    CHECK(valueOf(parsePalette("text 33")).inputFillerShown);

    // `selection_bold` the same way, and on where the file says nothing: the
    // interface has drawn what the selection fill covers bold since before
    // there was a switch for it, and a theme that says nothing gets that.
    CHECK(valueOf(parsePalette("selection_bold on")).selectionBold);
    CHECK_FALSE(valueOf(parsePalette("selection_bold off")).selectionBold);
    CHECK(valueOf(parsePalette("text 33")).selectionBold);

    // A number is not a switch, and neither is the palette complaint: the key
    // is answered as the setting it is.
    const std::string error = errorOf(parsePalette("input_filler_show 1", "theme.cfg"));
    REQUIRE_MESSAGE(contains(error, "on or off"), error);
    const std::string other = errorOf(parsePalette("selection_bold 1", "theme.cfg"));
    REQUIRE_MESSAGE(contains(other, "on or off"), other);
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

TEST_CASE("Six hex digits are the color itself [theme]") {
    // The other spelling a color takes: the color asked for and nothing else,
    // where a palette number is an entry the terminal draws as it is configured
    // to. Nothing can ask a terminal whether it means 24-bit color, so this is
    // taken as it stands rather than refused.
    CHECK(same(valueOf(parsePalette("text 1c1e2a")).text, Color::rgb(0x1c1e2au)));
    // Hex is hex either way round, as it is everywhere else it is written.
    CHECK(same(valueOf(parsePalette("text AAFFB2")).text, Color::rgb(0xaaffb2u)));
    // The ends of the range, one of them six decimal digits and a color all the
    // same: it is the length that says which spelling this is.
    CHECK(same(valueOf(parsePalette("text 000000")).text, Color::rgb(0x000000u)));
    CHECK(same(valueOf(parsePalette("text ffffff")).text, Color::rgb(0xffffffu)));
    CHECK(same(valueOf(parsePalette("text 123456")).text, Color::rgb(0x123456u)));

    // A color and the palette entry numbered the same are different colors:
    // 000021 is a color, and 33 is whatever entry 33 is drawn as.
    CHECK_FALSE(same(Color::rgb(33), Color{33}));

    // A theme may hold both, and one spelling says nothing about the other.
    const Palette mixed = valueOf(parsePalette("text d8dbe4\nbackground 232\n"));
    CHECK(same(mixed.text, Color::rgb(0xd8dbe4u)));
    CHECK(same(mixed.background, Color{232}));
}

TEST_CASE("A color is six hex digits or a number, and nothing between [theme]") {
    // The two lengths are what tells them apart, so anything else is neither.
    // Five hex digits and seven are not colors; four decimal digits are not a
    // palette number, whatever they add up to.
    for (const char* line : {"text 1c1e2", "text 1c1e2aa", "text 0232", "text 1c1e2g",
                             "text ffffffff", "text #1c1e2a"}) {
        CAPTURE(line);
        CHECK_FALSE(parsePalette(line, "theme.cfg").has_value());
    }

    // And the complaint names both spellings: a theme file is written by hand.
    const std::string error = errorOf(parsePalette("text 1c1e2", "theme.cfg"));
    REQUIRE_MESSAGE(contains(error, "six hex digits"), error);
    REQUIRE_MESSAGE(contains(error, "0 to 255"), error);

    // A color written with a '#' in front of it is a comment, so the role is
    // left with no value at all. Saying only "takes exactly one value" would
    // leave the user looking at a line whose color is plainly there.
    const std::string hashed = errorOf(parsePalette("text #1c1e2a", "theme.cfg"));
    REQUIRE_MESSAGE(contains(hashed, "starts a comment"), hashed);
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
    CHECK(
        same(loaded.readerSidebarMsglistSelected, builtIn.readerSidebarMsglistSelected));
    CHECK(same(loaded.inputField, builtIn.inputField));
    CHECK(same(loaded.inputText, builtIn.inputText));
    CHECK(same(loaded.focusedField, builtIn.focusedField));
    CHECK(same(loaded.focusedText, builtIn.focusedText));
    CHECK(same(loaded.inputFiller, builtIn.inputFiller));
    CHECK(loaded.inputFillerShown == builtIn.inputFillerShown);
    CHECK(loaded.selectionBold == builtIn.selectionBold);
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
    // The two settings a theme carries that are not colors. Both agree with the
    // built-in palette here, and both are stated in the file all the same, so
    // that a theme is the whole palette written out and not the difference from
    // another one.
    CHECK(loaded.inputFillerShown);
    CHECK(loaded.selectionBold);
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

TEST_CASE("The truecolor theme is written in colors and states every role [theme]") {
    // A shipped theme has to parse, and this one has to reach every role in six
    // hex digits: a role left at its default, or written as a palette number,
    // would put a color the terminal draws as it is configured to in the middle
    // of a screen whose whole point is that it does not. Which color each role
    // gets is the theme's business and gets tuned — only that none was forgotten
    // and that all of them are triples is checked here.
    const Palette loaded = valueOf(amberedit::ui::theme::loadPalette(
        amberedit::test::projectPath("themes/truecolor_bg_night.cfg")));

    const auto text = valueOf(amberedit::config::text::readFile(
        amberedit::test::projectPath("themes/truecolor_bg_night.cfg")));
    const auto entries =
        valueOf(amberedit::config::parseCfg(text, "truecolor_bg_night.cfg"));
    int colors = 0;
    for (const auto& entry : entries) {
        REQUIRE(entry.values.size() == 1);
        const std::string& value = entry.values.front();
        // The two switches every theme carries; everything else is a color.
        if (value == "on" || value == "off") continue;
        CAPTURE(entry.key);
        CHECK(value.size() == 6);
        CHECK(std::all_of(value.begin(), value.end(),
                          amberedit::config::text::asciiIsHexDigit));
        ++colors;
    }
    CHECK(colors == allRolesAt(240).roles);

    // And what was parsed is triples and not entries, role for role. Reading it
    // back through the palette rather than off the file is what proves the
    // parser kept them apart.
    const Palette numbered = allRolesAt(240).palette;
    CHECK(loaded.background.trueColor);
    CHECK(loaded.text.trueColor);
    CHECK(loaded.dialogBackground.trueColor);
    CHECK(loaded.found.trueColor);
    CHECK_FALSE(numbered.background.trueColor);
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
    for (const char* file : {"themes/blue.cfg", "themes/16_colors.cfg",
                             "themes/white.cfg", "themes/truecolor_bg_night.cfg"}) {
        CAPTURE(file);
        CHECK(keysOf(file) == written);
    }
}

TEST_CASE("Nothing a shipped theme draws a box with is the box's own color [theme]") {
    // The rule the dialog palette exists for: a modal carries a fill of its
    // own, so every color drawn on that fill has to be something else. Left
    // unchecked it is an invisible confirmation rather than an ugly one — the
    // text is there, in the color of what is behind it.
    for (const char* file :
         {"themes/blue.cfg", "themes/16_colors.cfg", "themes/black.cfg",
          "themes/white.cfg", "themes/truecolor_bg_night.cfg"}) {
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
    for (const char* file :
         {"themes/blue.cfg", "themes/16_colors.cfg", "themes/black.cfg",
          "themes/white.cfg", "themes/truecolor_bg_night.cfg"}) {
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

TEST_CASE("A theme is measured against the colors the terminal has [theme]") {
    const Everywhere high = allRolesAt(240);
    const Everywhere ansi = allRolesAt(12);

    // The whole palette a theme is written in, and a direct-color terminal
    // reporting a whole 24-bit range. Nothing is out of reach at either.
    CHECK(approximatedRoles(high.palette, 256) == 0);
    CHECK(approximatedRoles(high.palette, 1 << 24) == 0);

    // A terminal with sixteen, and one with eighty-eight: 240 is beyond both.
    CHECK(approximatedRoles(high.palette, 16) == high.roles);
    CHECK(approximatedRoles(high.palette, 88) == high.roles);
    // And one with none at all, which has_colors() answering false leaves.
    CHECK(approximatedRoles(high.palette, 0) == high.roles);

    // A theme written inside the sixteen ANSI colors is never counted, whatever
    // the terminal reported: those sixteen are the ones every terminal with
    // color has, and what it draws them as is its own configuration. This is
    // what makes themes/16_colors.cfg pass in silence everywhere.
    CHECK(approximatedRoles(ansi.palette, 256) == 0);
    CHECK(approximatedRoles(ansi.palette, 16) == 0);
    CHECK(approximatedRoles(ansi.palette, 8) == 0);
    CHECK(approximatedRoles(ansi.palette, 0) == 0);

    // The line between the two is between 15 and 16, and is drawn on the number
    // in the theme rather than on the number the terminal gave.
    CHECK(approximatedRoles(allRolesAt(15).palette, 8) == 0);
    const Everywhere sixteen = allRolesAt(16);
    CHECK(approximatedRoles(sixteen.palette, 16) == sixteen.roles);
    CHECK(approximatedRoles(sixteen.palette, 256) == 0);

    // A role left as the terminal's own color asks for no palette entry, so
    // there is none for it to fall short of.
    Palette defaulted = high.palette;
    defaulted.background = Color{};
    CHECK(approximatedRoles(defaulted, 16) == high.roles - 1);
}

TEST_CASE("The color warning names the setting that switches it off [theme]") {
    // The palette in force is a global another test may have written, so it is
    // set here rather than trusted, and put back afterwards.
    struct InForce {
        Palette kept = amberedit::ui::theme::palette;
        ~InForce() { amberedit::ui::theme::palette = kept; }
    } restore;

    // The tests never open a screen, so the terminal has reported no color at
    // all and this is the line written for one.
    amberedit::ui::theme::palette = allRolesAt(240).palette;
    const std::string said = colorWarning();
    REQUIRE_FALSE(said.empty());
    // The count can be wrong where a TERM names less than the emulator behind
    // it, so the line has to say what to switch off rather than leave it to be
    // found. This is the whole of why `color_warning` exists.
    CHECK_MESSAGE(contains(said, "color_warning off"), said);

    // And a theme inside the sixteen ANSI colors says nothing at all.
    amberedit::ui::theme::palette = allRolesAt(12).palette;
    CHECK(colorWarning().empty());
}
