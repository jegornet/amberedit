#include "ui/hint_bar.hpp"

#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <vector>

#include "msgbase/null_lastread_store.hpp"
#include "ui/app_state.hpp"
#include "ui/keys.hpp"
#include "ui/term/element.hpp"
#include "ui/term/screen.hpp"
#include "ui/theme.hpp"

using amberedit::app::AreaManager;
using amberedit::app::ScreenId;
using amberedit::config::AppConfig;
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
    std::vector<AreaConfig> loadAreas() override { return {}; }
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
    CHECK(hint_bar::text(Fixture(ScreenId::AreaList).state) ==
          "/ next-unread  ctrl-r rescan");
    CHECK(hint_bar::text(Fixture(ScreenId::MessageRead).state) ==
          "q reply  n reply-to  e new  l list  w export  ctrl-n nodelist");
    CHECK(hint_bar::text(Fixture(ScreenId::Compose).state) ==
          "ctrl-s save  ctrl-y delete-line  ctrl-o import");
    // Every key on the message list moves the cursor, so it has nothing to say.
    CHECK(hint_bar::text(Fixture(ScreenId::MessageList).state).empty());
}

TEST_CASE("The hint bar shows the layout's keys and skips what it leaves out "
          "[hintbar][keys]") {
    Fixture fixture(ScreenId::MessageRead);
    fixture.state.keys = KeyMap::parse(
        "F4 reader.reply\n"
        "Ctrl-E reader.new\n"
        "x reader.list\n",
        "keys");

    // Three of the six are bound, and the row is the three of them: a label
    // with no key in front of it would be a label saying to press nothing.
    CHECK(hint_bar::text(fixture.state) == "F4 reply  ctrl-e new  x list");
}

TEST_CASE("Where a command has several keys the shortest one is shown "
          "[hintbar][keys]") {
    Fixture fixture(ScreenId::MessageRead);

    // A bare key beats a chord, a chord beats a function key, and Ctrl beats
    // Alt — whichever order the layout wrote them in.
    fixture.state.keys = KeyMap::parse(
        "F4 reader.reply\n"
        "Ctrl-J reader.reply\n"
        "q reader.reply\n"
        "F6 reader.new\n"
        "Alt-N reader.new\n"
        "F7 reader.list\n"
        "Alt-L reader.list\n"
        "Ctrl-L reader.list\n",
        "keys");

    CHECK(hint_bar::text(fixture.state) == "q reply  alt-n new  ctrl-l list");
}

TEST_CASE("hint_bar decides whether the row is there at all [hintbar]") {
    Fixture fixture(ScreenId::AreaList);
    fixture.config.adaptiveUiThreshold = 80;

    // Wide by default: the row is not cut short to fit, so a narrow window does
    // without it rather than showing the first two hints and a rule.
    fixture.state.width = 100;
    CHECK(fixture.state.hintBarShown());
    fixture.state.width = 60;
    CHECK_FALSE(fixture.state.hintBarShown());

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
    fixture.state.width = 40;

    term::Screen screen(fixture.state.width, 1);
    term::render(screen, hint_bar::render(fixture.state));

    std::string row;
    for (int x = 0; x < fixture.state.width; ++x) row += screen.at(x, 0).glyph;
    // A space either side of the hints, as every other label set into a rule in
    // this interface carries, and the rule to the edge of the screen.
    CHECK(row == " / next-unread  ctrl-r rescan ──────────");

    // The hints in the bar's own color — both of them, the second as much as
    // the first — and the rule in the one every other rule is drawn in. No fill
    // of its own either way: the row stands on the theme's background like
    // everything else.
    const auto& palette = amberedit::ui::theme::palette;
    for (const int x : {1, 13, 16, 28}) {
        INFO("column " << x);
        CHECK(screen.at(x, 0).fg == palette.hintBar);
    }
    for (const int x : {30, 39}) {
        INFO("column " << x);
        CHECK(screen.at(x, 0).fg == palette.separator);
    }
    CHECK(screen.at(0, 0).bg == amberedit::ui::theme::Color{});
    CHECK(screen.at(39, 0).bg == amberedit::ui::theme::Color{});
}

TEST_CASE("A click on a hint asks for the key it is written under [hintbar]") {
    Fixture fixture(ScreenId::MessageRead);
    fixture.state.width = 80;

    // Where each hint landed comes off the frame, so one is drawn to find them.
    term::Screen screen(fixture.state.width, 1);
    term::render(screen, hint_bar::render(fixture.state));
    REQUIRE(fixture.state.hintSpots.size() == 6);

    // The second hint is `n reply-to`, and clicking it asks for `n` — the key
    // the row says runs it, so that the click and the row cannot disagree.
    const auto& spot = fixture.state.hintSpots[1];
    REQUIRE(spot.command == amberedit::ui::KeyCommand::ReaderReplyTo);
    const auto asked = hint_bar::clicked(fixture.state, clickAt(spot.box.x_min, 0));
    REQUIRE(asked);
    CHECK(*asked == term::Event::Character('n'));

    // A layout that has moved the command moves what the click asks for with
    // it: the hint is written under the key it presses, whichever that is.
    fixture.state.keys = KeyMap::parse("F5 reader.reply-to\n", "keys");
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
