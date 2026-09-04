#include "ui/hint_bar.hpp"

#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <vector>

#include "i18n/i18n.hpp"
#include "msgbase/null_lastread_store.hpp"
#include "sys/env.hpp"
#include "test_strings.hpp"
#include "ui/app_state.hpp"
#include "ui/keys.hpp"
#include "ui/term/element.hpp"
#include "ui/term/screen.hpp"
#include "ui/theme.hpp"

using amberedit::app::AreaManager;
using amberedit::app::ScreenId;
using amberedit::config::AppConfig;
using amberedit::config::Command;
using amberedit::config::Visibility;
using amberedit::domain::AreaConfig;
using amberedit::ui::AppState;
using amberedit::ui::KeyMap;

namespace hint_bar = amberedit::ui::hint_bar;
namespace term = amberedit::ui::term;

namespace {

term::Event clickAt(int x, int y) {
    term::MouseEvent mouse;
    mouse.button = term::MouseEvent::Button::Left;
    mouse.motion = term::MouseEvent::Motion::Pressed;
    mouse.x = x;
    mouse.y = y;
    return term::Event::Mouse(mouse);
}

class EmptyAreaSource final : public amberedit::ports::IAreaConfigSource {
public:
    tl::expected<std::vector<AreaConfig>, amberedit::ErrorPtr> loadAreas() override {
        return {};
    }
};

/// A state on whichever screen the test is about, and nothing else: the row is
/// built from the layout and the screen, so no base is needed to ask for one.
struct Fixture {
    explicit Fixture(ScreenId screen)
        : manager(std::make_unique<EmptyAreaSource>(),
                  std::make_unique<amberedit::msgbase::NullLastReadStore>(), config),
          state(manager, config) {
        state.width = 100;
        while (state.navigator.current() != screen) {
            // The stack starts on the area list; the rest are pushed onto it in
            // the order the interface reaches them.
            state.navigator.push(screen);
        }
    }

    AppConfig config;
    AreaManager manager;
    AppState state;
};

}  // namespace

TEST_CASE("The hint bar names the commands of the screen it stands under [hintbar]") {
    // The key, and beside it the word the menu writes on a button for the same
    // command: one list of commands answers for both.
    CHECK(hint_bar::text(Fixture(ScreenId::AreaList).state) ==
          "/ next unread  ctrl-u toggle unread  ctrl-r rescan");
    CHECK(hint_bar::text(Fixture(ScreenId::MessageRead).state) ==
          "q reply  n reply elsewhere  e new  l list  w export  ctrl-n nodelist");
    CHECK(hint_bar::text(Fixture(ScreenId::Compose).state) ==
          "ctrl-s save  ctrl-y delete line  ctrl-o import");
    // Every key on the message list moves the cursor, so its list starts empty.
    CHECK(hint_bar::text(Fixture(ScreenId::MessageList).state).empty());
}

TEST_CASE("Each screen's row is the list the config gives it [hintbar]") {
    // Every row is the user's: a screen has the hints that were asked for, in
    // the order they were written, and a screen whose list is empty has none.
    Fixture fixture(ScreenId::MessageRead);
    fixture.config.readerHints = {Command::ReaderInfo, Command::ReaderChange,
                                  Command::AppQuit};
    CHECK(hint_bar::text(fixture.state) == "i info  c change  ctrl-q quit");

    fixture.config.readerHints.clear();
    CHECK(hint_bar::text(fixture.state).empty());

    // The message list included, which has nothing to say only until it is
    // given something.
    Fixture list(ScreenId::MessageList);
    list.config.msglistHints = {Command::AppQuit};
    CHECK(hint_bar::text(list.state) == "ctrl-q quit");
}

TEST_CASE(
    "hint_bar_capitalize is the case the whole row is written in "
    "[hintbar]") {
    Fixture fixture(ScreenId::MessageRead);
    fixture.config.readerHints = {Command::ReaderReply, Command::ReaderFind,
                                  Command::ReaderExport};

    // Off unless the config says otherwise: the row is a quiet one, and a
    // capital on every word of it shouts.
    CHECK_FALSE(fixture.config.hintBarCapitalize);
    CHECK(hint_bar::text(fixture.state) == "q reply  ctrl-f find  w export");

    // On, the key and the word both — the same words the menu writes on its
    // buttons, and the keys as a `keys` file spells them.
    fixture.config.hintBarCapitalize = true;
    CHECK(hint_bar::text(fixture.state) == "Q Reply  Ctrl-F Find  W Export");
}

TEST_CASE(
    "hint_bar_capitalize off lower-cases a word that is not in ASCII "
    "[hintbar][i18n]") {
    // The words are the interface's, and the interface has a language. Lowering
    // a Russian one a byte at a time leaves it exactly as it was — every byte of
    // it is above ASCII — so this is `encoding::loweredCodePoint()`'s to do and
    // the row would otherwise be capitals whatever the config said.
    Fixture fixture(ScreenId::MessageRead);
    fixture.config.readerHints = {Command::ReaderReply};
    fixture.config.hintBarCapitalize = false;

    amberedit::sys::setEnvironment("LANGUAGE", "ru");
    for (const char* locale : {"", "C.UTF-8", "C.utf8", "en_US.UTF-8", "UTF-8"}) {
        if (locale[0] == '\0') {
            amberedit::sys::unsetEnvironment("LC_ALL");
        } else {
            amberedit::sys::setEnvironment("LC_ALL", locale);
        }
        static_cast<void>(amberedit::i18n::start());
        if (amberedit::i18n::translating()) break;
    }

    if (amberedit::i18n::translating()) {
        CHECK(hint_bar::text(fixture.state) == "q ответ");
    }

    amberedit::sys::unsetEnvironment("LC_ALL");
    amberedit::i18n::clear();
}

TEST_CASE(
    "The hint bar shows the layout's keys and skips what it leaves out "
    "[hintbar][keys]") {
    Fixture fixture(ScreenId::MessageRead);
    fixture.state.keys =
        amberedit::test::valueOf(KeyMap::parse("F4 reader.reply\n"
                                               "Ctrl-E reader.new\n"
                                               "x reader.list\n",
                                               "keys"));

    // Three of the six are bound, and the row is the three of them: a label
    // with no key in front of it would be a label saying to press nothing.
    CHECK(hint_bar::text(fixture.state) == "f4 reply  ctrl-e new  x list");
}

TEST_CASE(
    "Where a command has several keys the shortest one is shown "
    "[hintbar][keys]") {
    Fixture fixture(ScreenId::MessageRead);

    // A bare key beats a chord, a chord beats a function key, and Ctrl beats
    // Alt — whichever order the layout wrote them in.
    fixture.state.keys =
        amberedit::test::valueOf(KeyMap::parse("F4 reader.reply\n"
                                               "Ctrl-J reader.reply\n"
                                               "q reader.reply\n"
                                               "F6 reader.new\n"
                                               "Alt-N reader.new\n"
                                               "F7 reader.list\n"
                                               "Alt-L reader.list\n"
                                               "Ctrl-L reader.list\n",
                                               "keys"));

    CHECK(hint_bar::text(fixture.state) == "q reply  alt-n new  ctrl-l list");
}

TEST_CASE("A utility is written under the title its config line gave it [hintbar]") {
    // The word beside the key is the utility's own: nothing in the command
    // table could say what `extern_util0` is called, and a row saying "utility"
    // three times would name none of the three.
    Fixture fixture(ScreenId::MessageRead);
    fixture.config.externUtils[0] = {"Files", {"/usr/bin/mc"}};
    fixture.config.externUtils[1] = {"Disk Free", {"df", "-h"}};
    fixture.config.readerHints = {Command::ReaderExternUtil0, Command::ReaderExternUtil1};
    fixture.state.keys =
        amberedit::test::valueOf(KeyMap::parse("Alt-F1 reader.extern_util0\n"
                                               "Alt-F2 reader.extern_util1\n",
                                               "keys"));

    CHECK(hint_bar::text(fixture.state) == "alt-f1 files  alt-f2 disk free");

    // The case is the row's, as it is for every other word in it.
    fixture.config.hintBarCapitalize = true;
    CHECK(hint_bar::text(fixture.state) == "Alt-F1 Files  Alt-F2 Disk Free");
}

TEST_CASE("hint_bar decides whether the row is there at all [hintbar]") {
    Fixture fixture(ScreenId::AreaList);
    fixture.config.adaptiveUiThreshold = 80;

    // On by default, in a window of any width: there is no help screen, and the
    // narrow window that has least room for the row is the one whose buttons
    // have gone and whose keys are all that is left.
    fixture.state.width = 100;
    CHECK(fixture.state.hintBarShown());
    fixture.state.width = 60;
    CHECK(fixture.state.hintBarShown());

    fixture.config.hintBar = Visibility::On;
    CHECK(fixture.state.hintBarShown());
    fixture.config.hintBar = Visibility::Off;
    CHECK_FALSE(fixture.state.hintBarShown());

    // The same threshold every other window-led setting is read against.
    fixture.config.hintBar = Visibility::WhenWide;
    fixture.state.width = 100;
    CHECK(fixture.state.hintBarShown());
    fixture.state.width = 60;
    CHECK_FALSE(fixture.state.hintBarShown());

    fixture.config.hintBar = Visibility::WhenNarrow;
    CHECK(fixture.state.hintBarShown());
    fixture.state.width = 100;
    CHECK_FALSE(fixture.state.hintBarShown());
}

TEST_CASE("The row is the hints set into a rule [hintbar]") {
    Fixture fixture(ScreenId::AreaList);
    // The list is the test's own rather than the screen's defaults: what is
    // being measured is the rule either side of the hints, and a row that
    // changed length whenever a command was added to the area list would be
    // measuring that instead.
    fixture.config.arealistHints = {Command::AreaListNextUnread, Command::AreaListRescan};
    fixture.state.width = 40;

    term::Screen screen(fixture.state.width, 1);
    term::render(screen, hint_bar::render(fixture.state));

    std::string row;
    for (int x = 0; x < fixture.state.width; ++x) row += screen.at(x, 0).glyph;
    // A space either side of the hints, as every other label set into a rule in
    // this interface carries, and the rule running to both edges of the screen.
    CHECK(row == "───── / next unread  ctrl-r rescan ─────");

    // The hints in the bar's own color — both of them, the second as much as
    // the first — and the rule in the one every other rule is drawn in. No fill
    // of its own either way: the row stands on the theme's background like
    // everything else.
    const auto& palette = amberedit::ui::theme::palette;
    for (const int x : {6, 18, 21, 33}) {
        INFO("column " << x);
        CHECK(screen.at(x, 0).fg == palette.hintBar);
    }
    for (const int x : {0, 4, 35, 39}) {
        INFO("column " << x);
        CHECK(screen.at(x, 0).fg == palette.separator);
    }
    CHECK(screen.at(0, 0).bg == amberedit::ui::theme::Color{});
    CHECK(screen.at(39, 0).bg == amberedit::ui::theme::Color{});
}

TEST_CASE(
    "hint_bar_align is which side of the hints the rule runs along "
    "[hintbar]") {
    using amberedit::config::HintAlign;

    Fixture fixture(ScreenId::AreaList);
    // Stated here for the reason the row above states it: the alignment is what
    // is being read, not how many hints the screen offers today.
    fixture.config.arealistHints = {Command::AreaListNextUnread, Command::AreaListRescan};
    fixture.state.width = 40;
    const auto rowOf = [&fixture]() {
        term::Screen screen(fixture.state.width, 1);
        term::render(screen, hint_bar::render(fixture.state));
        std::string row;
        for (int x = 0; x < fixture.state.width; ++x) row += screen.at(x, 0).glyph;
        return row;
    };

    // In the middle unless the config says otherwise, and the odd column of an
    // uneven rest goes to the right-hand side.
    CHECK(fixture.config.hintBarAlign == HintAlign::Center);
    fixture.state.width = 39;
    CHECK(rowOf() == "──── / next unread  ctrl-r rescan ─────");

    fixture.state.width = 40;
    fixture.config.hintBarAlign = HintAlign::Left;
    CHECK(rowOf() == " / next unread  ctrl-r rescan ──────────");

    fixture.config.hintBarAlign = HintAlign::Right;
    CHECK(rowOf() == "────────── / next unread  ctrl-r rescan ");

    // The space either side of the hints is theirs wherever they stand, so a
    // hint is never flush against the rule.
    fixture.config.hintBarAlign = HintAlign::Center;
    CHECK(rowOf() == "───── / next unread  ctrl-r rescan ─────");
}

TEST_CASE(
    "A window too narrow for the whole row carries what fits of it "
    "[hintbar]") {
    // The reader names six commands, which is more than a narrow window holds.
    Fixture fixture(ScreenId::MessageRead);
    fixture.state.width = 35;

    const auto rowOf = [&fixture]() {
        term::Screen screen(fixture.state.width, 1);
        term::render(screen, hint_bar::render(fixture.state));
        std::string row;
        for (int x = 0; x < fixture.state.width; ++x) row += screen.at(x, 0).glyph;
        return row;
    };

    // Whole hints and no fragments: a squeezed row would be `q re n rep e n`,
    // which names neither a key nor a command. What is left off is left off the
    // end. Three of them fill this window to the column, which leaves no rule
    // to draw and the space either side of the hints still theirs.
    CHECK(rowOf() == " q reply  n reply elsewhere  e new ");
    // And nothing is clickable that is not drawn.
    CHECK(fixture.state.hintSpots.size() == 3);

    // One column narrower than the hint needs takes that hint off the row
    // rather than the last letters of every one of them.
    fixture.state.width = 34;
    CHECK(rowOf() == "─── q reply  n reply elsewhere ───");
    CHECK(fixture.state.hintSpots.size() == 2);

    // Too narrow even for the first: the rule alone, as under a screen with no
    // commands of its own.
    fixture.state.width = 8;
    CHECK(rowOf() == "────────");
    CHECK(fixture.state.hintSpots.empty());
}

TEST_CASE("A click on a hint asks for the key it is written under [hintbar]") {
    Fixture fixture(ScreenId::MessageRead);
    fixture.state.width = 80;

    // Where each hint landed comes off the frame, so one is drawn to find them.
    term::Screen screen(fixture.state.width, 1);
    term::render(screen, hint_bar::render(fixture.state));
    REQUIRE(fixture.state.hintSpots.size() == 6);

    // The second hint is `n reply elsewhere`, and clicking it asks for `n` — the
    // key the row says runs it, so the click and the row cannot disagree.
    const auto& spot = fixture.state.hintSpots[1];
    REQUIRE(spot.command == Command::ReaderReplyElsewhere);
    const auto asked = hint_bar::clicked(fixture.state, clickAt(spot.box.x_min, 0));
    REQUIRE(asked);
    CHECK(*asked == term::Event::Character('n'));

    // A layout that has moved the command moves what the click asks for with
    // it: the hint is written under the key it presses, whichever that is.
    fixture.state.keys =
        amberedit::test::valueOf(KeyMap::parse("F5 reader.reply_elsewhere\n", "keys"));
    term::render(screen, hint_bar::render(fixture.state));
    REQUIRE(fixture.state.hintSpots.size() == 1);
    const auto moved = hint_bar::clicked(
        fixture.state, clickAt(fixture.state.hintSpots[0].box.x_min, 0));
    REQUIRE(moved);
    CHECK(*moved == term::Event::F5);

    // Beside a hint, past the end of the row, and anything that is not a left
    // click at all: none of them is a hint being pressed.
    CHECK_FALSE(hint_bar::clicked(fixture.state, clickAt(0, 0)));
    CHECK_FALSE(hint_bar::clicked(fixture.state, clickAt(79, 0)));
    CHECK_FALSE(hint_bar::clicked(fixture.state, term::Event::Character('n')));
}

TEST_CASE("A screen with nothing to say leaves the rule whole [hintbar]") {
    Fixture fixture(ScreenId::MessageList);
    fixture.state.width = 12;

    term::Screen screen(fixture.state.width, 1);
    term::render(screen, hint_bar::render(fixture.state));

    std::string row;
    for (int x = 0; x < fixture.state.width; ++x) row += screen.at(x, 0).glyph;
    CHECK(row == "────────────");
    CHECK(screen.at(0, 0).fg == amberedit::ui::theme::palette.separator);
}
