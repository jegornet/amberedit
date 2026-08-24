#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "config/app_config.hpp"
#include "msgbase/null_lastread_store.hpp"
#include "ui/app_state.hpp"
#include "ui/menu_button.hpp"
#include "ui/menu_dialog.hpp"
#include "ui/term/element.hpp"
#include "ui/term/event.hpp"
#include "ui/term/screen.hpp"
#include "ui/text_layout.hpp"
#include "ui/theme.hpp"

using amberedit::config::Command;
using amberedit::config::Commands;
using amberedit::ui::AppState;
using namespace amberedit::ui::term;

namespace menu_button = amberedit::ui::menu_button;
namespace menu_dialog = amberedit::ui::menu_dialog;
namespace theme = amberedit::ui::theme;

using amberedit::ui::displayWidth;

namespace {

/// Hands back nothing: these tests are about the menu, not the areas.
class EmptyAreaSource final : public amberedit::ports::IAreaConfigSource {
public:
    tl::expected<std::vector<amberedit::domain::AreaConfig>, amberedit::ErrorPtr>
    loadAreas() override {
        return {};
    }
};

/// The state the menu stands on, with the config it refers to outliving it.
struct Fixture {
    Fixture()
        : manager(std::make_unique<EmptyAreaSource>(),
                  std::make_unique<amberedit::msgbase::NullLastReadStore>(), config),
          state(manager, config) {
        state.width = kWidth;
        state.height = kHeight;
    }

    static constexpr int kWidth = 40;
    static constexpr int kHeight = 20;

    amberedit::config::AppConfig config;
    amberedit::app::AreaManager manager;
    AppState state;
};

/// What one item looks like as it is written down.
struct Item {
    Command command;
    bool enabled{true};
};

std::vector<AppState::MenuView::Item> itemsOf(const std::vector<Item>& written) {
    std::vector<AppState::MenuView::Item> items;
    items.reserve(written.size());
    for (const Item& item : written) items.push_back({item.command, item.enabled, {}});
    return items;
}

/// The screen as the menu leaves it, so that what the layout did can be read
/// back without a terminal — the buffer is what the terminal is handed.
struct Rendered {
    std::vector<std::vector<std::string>> rows;
    std::vector<std::vector<Color>> fg;
    std::vector<std::vector<Color>> bg;

    /// `columns` cells of a row, from `x`.
    [[nodiscard]] std::string at(int y, int x, int columns) const {
        const auto& row = rows[static_cast<size_t>(y)];
        std::string text;
        for (int i = 0; i < columns; ++i) {
            text += row[static_cast<size_t>(x) + static_cast<size_t>(i)];
        }
        return text;
    }
};

Rendered draw(AppState& state) {
    const Element element = menu_dialog::render(state, text(""));
    Screen screen(Fixture::kWidth, Fixture::kHeight);
    render(screen, element);

    Rendered drawn;
    for (int y = 0; y < Fixture::kHeight; ++y) {
        std::vector<std::string> glyphs;
        std::vector<Color> fg;
        std::vector<Color> bg;
        for (int x = 0; x < Fixture::kWidth; ++x) {
            glyphs.push_back(screen.at(x, y).glyph);
            fg.push_back(screen.at(x, y).fg);
            bg.push_back(screen.at(x, y).bg);
        }
        drawn.rows.push_back(std::move(glyphs));
        drawn.fg.push_back(std::move(fg));
        drawn.bg.push_back(std::move(bg));
    }
    return drawn;
}

/// A left-button press where the pointer landed.
Event pressAt(int x, int y) {
    MouseEvent mouse;
    mouse.x = x;
    mouse.y = y;
    mouse.button = MouseEvent::Button::Left;
    mouse.motion = MouseEvent::Motion::Pressed;
    return Event::Mouse(mouse);
}

}  // namespace

TEST_CASE("the menu button is drawn plainly until it is clicked [menu]") {
    const auto rowOf = [](const Element& element) {
        Screen screen(menu_button::kWidth, 1);
        render(screen, element);
        std::string text;
        for (int x = 0; x < menu_button::kWidth; ++x) text += screen.at(x, 0).glyph;
        return text;
    };

    CHECK(rowOf(menu_button::topRow()) == "│ ≡ │");
    CHECK(rowOf(menu_button::bottomRow()) == "└───┘");
    // The glyphs are untouched by a click: what changes is the color they are
    // written in, so nothing moves under the pointer.
    CHECK(rowOf(menu_button::topRow(true)) == "│ ≡ │");
    CHECK(menu_button::colorOf(true) == theme::palette.animatedButtonText);
    CHECK(menu_button::colorOf(false) == theme::palette.screenButtons);
}

TEST_CASE("the menu button is clicked in the top-right corner [menu]") {
    constexpr int kWidth = 80;
    CHECK(menu_button::clicked(pressAt(kWidth - menu_button::kWidth, 0), kWidth));
    CHECK(menu_button::clicked(pressAt(kWidth - 1, 1), kWidth));

    // A column short of its left-hand side, and the row under it, belong to
    // whatever the button stands beside.
    CHECK_FALSE(
        menu_button::clicked(pressAt(kWidth - menu_button::kWidth - 1, 0), kWidth));
    CHECK_FALSE(menu_button::clicked(pressAt(kWidth - 1, 2), kWidth));
    // And so does the whole of the other corner, which is the back button's.
    CHECK_FALSE(menu_button::clicked(pressAt(0, 0), kWidth));

    // The release is not a click: it would arrive with the menu already up and
    // land on whatever the box had put under the pointer.
    MouseEvent release;
    release.x = kWidth - 1;
    release.button = MouseEvent::Button::Left;
    release.motion = MouseEvent::Motion::Released;
    CHECK_FALSE(menu_button::clicked(Event::Mouse(release), kWidth));
}

TEST_CASE("the menu is a column of buttons of one stated width [menu]") {
    Fixture fixture;
    REQUIRE(fixture.config.menuButtonsWidth == 22);
    menu_dialog::open(fixture.state,
                      itemsOf({{Command::ReaderList}, {Command::ReaderNodelist}}));
    REQUIRE(fixture.state.menuView);

    const Rendered drawn = draw(fixture.state);
    const auto& items = fixture.state.menuView->items;
    const Box first = items[0].box;
    const Box second = items[1].box;

    // Both buttons are as wide as `menu_buttons_width` asks, whatever they say:
    // a column measured against whichever labels happened to be in the menu
    // would stand a different width every time it was opened.
    CHECK(first.x_min == second.x_min);
    CHECK(first.x_max - first.x_min + 1 == 22);
    CHECK(second.x_max - second.x_min + 1 == 22);

    // Three rows each and nothing between them: the frames meet, so the column
    // reads as one list rather than as a handful of boxes.
    CHECK(first.y_max - first.y_min + 1 == 3);
    CHECK(second.y_min == first.y_max + 1);

    // Each label carries a glyph in front of the word, and the words all start
    // in the same column: the glyphs stand in a column of their own, as wide as
    // the widest of them. Both of these are one column wide by the wcwidth()
    // glibc and Apple's libc answer with — were a platform to call one of them
    // two, the glyph column would widen and both words would move together,
    // which is the whole point of measuring rather than counting. See
    // `codepointWidth` in `ui/term/utf8.cpp`.
    CHECK(drawn.at(first.y_min, first.x_min, 22) == "┌────────────────────┐");
    CHECK(drawn.at(first.y_min + 1, first.x_min, 22) == "│ ≔ List             │");
    CHECK(drawn.at(first.y_max, first.x_min, 22) == "└────────────────────┘");
    CHECK(drawn.at(second.y_min + 1, second.x_min, 22) == "│ ⚲ Nodelist         │");
}

TEST_CASE("every label the menus offer fits the default width [menu]") {
    // Nothing in either menu is cut at the default width until the user narrows
    // the buttons or the window, and no word is set against the frame: the
    // longest of them, `Reply elsewhere`, leaves two columns after it.
    const std::vector<Item> all{{Command::ReaderList},
                                {Command::ReaderReply},
                                {Command::ReaderReplyElsewhere},
                                {Command::ReaderCommentReply},
                                {Command::ReaderNew},
                                {Command::ReaderForward},
                                {Command::ReaderChange},
                                {Command::ReaderInfo},
                                {Command::ReaderExport},
                                {Command::ReaderNodelist},
                                {Command::ComposeSave},
                                {Command::ComposeImport}};
    // The glyph column is as wide as the widest glyph in the menu and no wider:
    // a column set to what one platform draws an emoji in would stand a blank
    // column wide on another.
    int widest = 0;
    for (const Item& item : all) {
        widest = std::max(widest, displayWidth(Commands::of(item.command).icon));
    }
    const int icon = menu_dialog::iconWidth(itemsOf(all));
    CHECK(icon == widest);

    for (const Item& item : all) {
        // Measured as it is drawn, glyph column and blank and word together: the
        // glyph in front is not one column on every platform, so the room the
        // word has left cannot be worked out by counting characters.
        const Commands::Info& command = Commands::of(item.command);
        const std::string line =
            menu_dialog::labelLine(command.icon, command.labelId, 99, icon);
        INFO(line);
        CHECK(displayWidth(line) <= 19);  // 22 less the two sides and the indent
    }
}

TEST_CASE("a label is a glyph and a word, kept apart [menu]") {
    // What a translation replaces is the word on its own — the glyph in front
    // says the same thing in every language, and is not part of what is written
    // down for translating. Both come off the one list of commands, which is
    // what the hint bars and the keyboard read as well.
    const Commands::Info& command = Commands::of(Command::ReaderNodelist);
    CHECK(command.icon == "⚲");
    CHECK(std::string_view(command.labelId) == "Nodelist");
    CHECK(menu_dialog::labelLine(command.icon, command.labelId, 99) == "⚲ Nodelist");

    // The word is what gives way when the room runs out; the glyph stays,
    // however many columns the platform draws it in.
    const int icon = displayWidth(command.icon);
    CHECK(menu_dialog::labelLine(command.icon, command.labelId, icon + 4) == "⚲ No…");
    CHECK(menu_dialog::labelLine(command.icon, command.labelId, icon + 1) == "⚲");
    CHECK(menu_dialog::labelLine(command.icon, command.labelId, 0).empty());

    // A glyph narrower than the column it is set in is padded out to it, so that
    // the words below one another line up. The column is stated in columns, not
    // in characters: what a wider glyph elsewhere in the menu asks for is what
    // every other glyph is set in, whatever it is made of.
    CHECK(menu_dialog::labelLine("≔", "List", 99, 1) == "≔ List");
    CHECK(menu_dialog::labelLine("≔", "List", 99, 2) == "≔  List");
    CHECK(menu_dialog::labelLine("≔", "List", 99, 3) == "≔   List");

    // A word longer than `Nodelist` — which is what translating one is — is cut
    // rather than pushing the button wider.
    CHECK(displayWidth(menu_dialog::labelLine(command.icon, "Список узлов", 12)) <= 12);
}

TEST_CASE("menu_buttons_width is what the buttons are cut to [menu]") {
    Fixture fixture;
    fixture.config.menuButtonsWidth = 8;
    menu_dialog::open(fixture.state,
                      itemsOf({{Command::ReaderNew}, {Command::ReaderNodelist}}));
    REQUIRE(fixture.state.menuView);

    const Rendered drawn = draw(fixture.state);
    const auto& items = fixture.state.menuView->items;
    CHECK(items[0].box.x_max - items[0].box.x_min + 1 == 8);
    // A label with no room left for it is cut where every other label in the
    // interface is cut, ellipsis and all, rather than pushing the button out of
    // the column.
    CHECK(drawn.at(items[0].box.y_min + 1, items[0].box.x_min, 8) == "│ ✎ New│");
    // The glyphs' widths are the platform's — see the check above.
    CHECK(drawn.at(items[1].box.y_min + 1, items[1].box.x_min, 8) == "│ ⚲ No…│");

    // A window with nothing to spare cuts it back further: the margins round
    // the column are what the button gives way to.
    fixture.state.width = 10;
    draw(fixture.state);
    CHECK(items[0].box.x_max - items[0].box.x_min + 1 == 6);
}

TEST_CASE("the column stands clear of the edges of the box [menu]") {
    Fixture fixture;
    menu_dialog::open(fixture.state, itemsOf({{Command::ReaderList}}));
    REQUIRE(fixture.state.menuView);

    const Rendered drawn = draw(fixture.state);
    const Box box = fixture.state.menuView->items[0].box;

    // Two columns either hand and a row over and under, all of them wiped clear
    // of whatever the menu stands on: a frame flush against the screen behind it
    // would read as part of that screen.
    CHECK(drawn.at(box.y_min, box.x_min - 2, 2) == "  ");
    CHECK(drawn.at(box.y_min, box.x_max + 1, 2) == "  ");
    CHECK(drawn.at(box.y_min - 1, box.x_min - 2, 19) == std::string(19, ' '));
    CHECK(drawn.at(box.y_max + 1, box.x_min - 2, 19) == std::string(19, ' '));
}

TEST_CASE("the cursor opens on the first command that can be run [menu]") {
    Fixture fixture;
    menu_dialog::open(fixture.state, itemsOf({{Command::ReaderReply, false},
                                              {Command::ReaderList, false},
                                              {Command::ReaderNew, true}}));
    REQUIRE(fixture.state.menuView);
    CHECK(fixture.state.menuView->cursor == 2);
    CHECK(menu_dialog::current(fixture.state) == Command::ReaderNew);

    // And it stays there: the two above it are dead, and a ring with one live
    // command on it comes back round to that one.
    menu_dialog::handleEvent(fixture.state, Event::ArrowDown);
    CHECK(fixture.state.menuView->cursor == 2);
    menu_dialog::handleEvent(fixture.state, Event::ArrowUp);
    CHECK(fixture.state.menuView->cursor == 2);
}

TEST_CASE("the arrows walk the menu as a ring [menu]") {
    Fixture fixture;
    menu_dialog::open(fixture.state, itemsOf({{Command::ReaderList},
                                              {Command::ReaderReply, false},
                                              {Command::ReaderNew}}));
    REQUIRE(fixture.state.menuView);
    REQUIRE(fixture.state.menuView->cursor == 0);

    // The dead command in the middle is stepped over rather than rested on: the
    // cursor is where Enter acts.
    menu_dialog::handleEvent(fixture.state, Event::ArrowDown);
    CHECK(fixture.state.menuView->cursor == 2);
    menu_dialog::handleEvent(fixture.state, Event::ArrowDown);
    CHECK(fixture.state.menuView->cursor == 0);
    menu_dialog::handleEvent(fixture.state, Event::ArrowUp);
    CHECK(fixture.state.menuView->cursor == 2);

    CHECK(menu_dialog::handleEvent(fixture.state, Event::Return) ==
          menu_dialog::Outcome::Picked);
    CHECK(menu_dialog::current(fixture.state) == Command::ReaderNew);
}

TEST_CASE("a disabled button is drawn quietly and the cursor's is filled [menu]") {
    Fixture fixture;
    menu_dialog::open(fixture.state,
                      itemsOf({{Command::ReaderList}, {Command::ReaderReply, false}}));
    REQUIRE(fixture.state.menuView);

    const Rendered drawn = draw(fixture.state);
    const auto& items = fixture.state.menuView->items;
    const auto cell = [&](const Box& box) {
        return std::make_pair(drawn.fg[static_cast<size_t>(box.y_min + 1)]
                                      [static_cast<size_t>(box.x_min + 1)],
                              drawn.bg[static_cast<size_t>(box.y_min + 1)]
                                      [static_cast<size_t>(box.x_min + 1)]);
    };

    // The cursor's button takes the fill the lists give the row Enter would act
    // on, so that what it is on is visible from across the box.
    CHECK(cell(items[0].box).first == theme::palette.selectionText);
    CHECK(cell(items[0].box).second == theme::palette.selection);
    // The dead one is drawn in `dialog_hint`, which is what a box shows but
    // will not act on — the counterpart inside a box of the `dimmed` the lists
    // use, and a separate role because the box carries a fill of its own.
    CHECK(cell(items[1].box).first == theme::palette.dialogHint);
}

TEST_CASE("a click answers with the button it landed on [menu]") {
    Fixture fixture;
    menu_dialog::open(fixture.state, itemsOf({{Command::ReaderList},
                                              {Command::ReaderReply, false},
                                              {Command::ReaderNew}}));
    REQUIRE(fixture.state.menuView);
    draw(fixture.state);

    const Box live = fixture.state.menuView->items[2].box;
    const Box dead = fixture.state.menuView->items[1].box;

    // A dead button swallows the click: it is still a click on the menu, and
    // letting it through to the screen underneath would be worse than nothing.
    CHECK(menu_dialog::handleEvent(fixture.state, pressAt(dead.x_min, dead.y_min)) ==
          menu_dialog::Outcome::Ignored);
    REQUIRE(fixture.state.menuView);
    CHECK(fixture.state.menuView->cursor == 0);

    CHECK(menu_dialog::handleEvent(fixture.state, pressAt(live.x_min, live.y_max)) ==
          menu_dialog::Outcome::Picked);
    CHECK(menu_dialog::current(fixture.state) == Command::ReaderNew);
}

TEST_CASE("Esc and a click outside put the menu away [menu]") {
    Fixture fixture;
    const auto items = itemsOf({{Command::ReaderList}, {Command::ReaderNew}});

    menu_dialog::open(fixture.state, items);
    REQUIRE(fixture.state.menuView);
    CHECK(menu_dialog::handleEvent(fixture.state, Event::Escape) ==
          menu_dialog::Outcome::Dismissed);
    CHECK_FALSE(fixture.state.menuView);

    // Pointing away from a menu is how one thought better of is dismissed
    // everywhere else, and the screen underneath is not acted on.
    menu_dialog::open(fixture.state, items);
    REQUIRE(fixture.state.menuView);
    draw(fixture.state);
    CHECK(menu_dialog::handleEvent(fixture.state, pressAt(0, 0)) ==
          menu_dialog::Outcome::Dismissed);
    CHECK_FALSE(fixture.state.menuView);
}

TEST_CASE("a menu with no commands in it does not open at all [menu]") {
    Fixture fixture;
    menu_dialog::open(fixture.state, {});
    CHECK_FALSE(fixture.state.menuView);
}
