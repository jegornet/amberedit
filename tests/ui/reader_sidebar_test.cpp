#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "temp_squish_base.hpp"
#include "ui/area_fixture.hpp"
#include "ui/reader_sidebar.hpp"
#include "ui/screens/message_list_screen.hpp"
#include "ui/screens/message_read_screen.hpp"
#include "ui/term/element.hpp"
#include "ui/term/event.hpp"
#include "ui/term/screen.hpp"
#include "ui/text_layout.hpp"
#include "ui/theme.hpp"

using amberedit::config::SidebarPosition;
using amberedit::config::Visibility;
using amberedit::test::AreaFixture;
using amberedit::test::TempSquishBase;
using amberedit::ui::AppState;
using amberedit::ui::term::Event;

namespace message_list = amberedit::ui::screens::message_list;
namespace message_read = amberedit::ui::screens::message_read;
namespace reader_sidebar = amberedit::ui::reader_sidebar;
namespace term = amberedit::ui::term;

namespace {

/// The threshold these tests ask for, and a window comfortably over it. The
/// setting is zero unless a config states one — a panel is not a thing to appear
/// unasked — so every fixture here states the width `amberedit.cfg.example`
/// recommends rather than leaning on a default that means "no panel".
constexpr int kThreshold = 120;
constexpr int kWide = 130;

/// The reader as it reaches the terminal, row by row, so that a layout can be
/// read back the way it is seen.
std::vector<std::string> rowsOf(AreaFixture& fixture) {
    term::Screen screen(fixture.state.width, fixture.state.height);
    term::render(screen, message_read::render(fixture.state));

    std::vector<std::string> rows;
    for (int y = 0; y < fixture.state.height; ++y) {
        std::string row;
        for (int x = 0; x < fixture.state.width; ++x) row += screen.at(x, y).glyph;
        rows.push_back(row);
    }
    return rows;
}

/// The column the rule between the panel and the reader stands in, as it was
/// drawn: read off the frame rather than worked out again, so that a test
/// noticing the two have drifted apart is possible at all.
///
/// The column carrying it on every row, rather than the first row's first bar:
/// with the panel on the right the reader's own corner comes first, and a box
/// two rows tall is not the rule that runs the whole height of the screen.
int ruleColumn(const std::vector<std::string>& rows) {
    for (size_t x = 0; x < rows.front().size(); ++x) {
        const auto column = static_cast<int>(x);
        const bool whole = std::all_of(rows.begin(), rows.end(), [&](const auto& row) {
            return amberedit::ui::substrByWidth(row, column, 1) == "│";
        });
        if (whole) return column;
    }
    return -1;
}

/// Where that rule belongs, worked out from the setting: beyond the panel where
/// it stands on the left, and in front of it where it stands on the right.
int ruleFor(const AppState& state) {
    return state.readerSidebarOnLeft() ? state.readerSidebarWidth()
                                       : state.readerSidebarLeft() - 1;
}

/// A column inside the panel, whichever side it is on — what a click meant for
/// a row is aimed at.
int inPanel(const AppState& state) {
    return state.readerSidebarLeft() + 1;
}

Event pressAt(int x, int y) {
    term::MouseEvent mouse;
    mouse.x = x;
    mouse.y = y;
    mouse.button = term::MouseEvent::Button::Left;
    mouse.motion = term::MouseEvent::Motion::Pressed;
    return Event::Mouse(mouse);
}

Event wheelAt(int x, bool down) {
    term::MouseEvent mouse;
    mouse.x = x;
    mouse.y = 0;
    mouse.button =
        down ? term::MouseEvent::Button::WheelDown : term::MouseEvent::Button::WheelUp;
    mouse.motion = term::MouseEvent::Motion::Pressed;
    return Event::Mouse(mouse);
}

/// Which message the reader is showing, counted from one, and zero on none.
uint32_t reading(const AreaFixture& fixture) {
    return fixture.state.readHeader ? fixture.state.readHeader->number : 0;
}

/// Opens the area in the reader, in a window of the given width — a panel up
/// unless a test asks for one too narrow for it — on the side asked for, which
/// is the side a config saying nothing gets.
void openWide(AreaFixture& fixture, int width = kWide,
              SidebarPosition side = SidebarPosition::Right) {
    fixture.config.readerSidebarThreshold = kThreshold;
    fixture.config.readerSidebarPosition = side;
    fixture.state.width = width;
    fixture.state.height = 24;
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());
}

}  // namespace

TEST_CASE(
    "The sidebar comes up at reader_sidebar_threshold and not a column under it "
    "[sidebar][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    const int threshold = kThreshold;
    fixture.config.readerSidebarThreshold = threshold;

    fixture.state.width = threshold;
    CHECK(fixture.state.readerSidebarShown());
    fixture.state.width = threshold - 1;
    CHECK_FALSE(fixture.state.readerSidebarShown());
    // The line is the panel's own and not `adaptive_ui_threshold`'s: a window
    // that is merely wide is still one the message has to itself.
    fixture.state.width = fixture.config.adaptiveUiThreshold;
    CHECK_FALSE(fixture.state.readerSidebarShown());
}

TEST_CASE(
    "reader_sidebar_threshold off keeps the panel off at any width "
    "[sidebar][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    // Which is also what a config saying nothing gets: the panel is there for
    // whoever asks for it and does not appear the first time a terminal is
    // dragged wide.
    CHECK(fixture.config.readerSidebarThreshold == 0);
    fixture.config.readerSidebarThreshold = 0;
    for (const int width : {80, 130, 400}) {
        fixture.state.width = width;
        CHECK_FALSE(fixture.state.readerSidebarShown());
    }
}

TEST_CASE("The panel is reader_sidebar_width wide and the message has the rest "
          "[sidebar][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());

    // The default width leaves the message its eighty columns in a window of
    // the threshold worth turning the panel on at, and every column of a wider
    // one goes to the message: the panel is a strip and does not grow with the
    // terminal.
    CHECK(fixture.config.readerSidebarWidth == 39);
    fixture.config.readerSidebarThreshold = kThreshold;
    fixture.state.width = kThreshold;
    REQUIRE(fixture.state.readerSidebarShown());
    CHECK(fixture.state.readerSidebarWidth() == 39);
    CHECK(fixture.state.readerPaneWidth() == 80);
    for (const int width : {121, 200, 400}) {
        fixture.state.width = width;
        CHECK(fixture.state.readerSidebarWidth() == 39);
        // The panel, the rule beside it and the message are the whole window.
        CHECK(fixture.state.readerPaneWidth() == width - 40);
        // On the right unless a config says otherwise, so the message begins in
        // the window's own first column and stops where the rule does.
        CHECK(fixture.state.readerSidebarLeft() == width - 39);
        CHECK(fixture.state.readerPaneLeft() == 0);
        CHECK(fixture.state.readerPaneRight() == width - 40);
    }

    // Moved to the other side, the same three widths change hands: the panel
    // starts the window and the message runs to its edge.
    fixture.config.readerSidebarPosition = SidebarPosition::Left;
    for (const int width : {121, 200, 400}) {
        fixture.state.width = width;
        CHECK(fixture.state.readerPaneWidth() == width - 40);
        CHECK(fixture.state.readerSidebarLeft() == 0);
        CHECK(fixture.state.readerPaneLeft() == 40);
        CHECK(fixture.state.readerPaneRight() == width);
    }

    // What the setting says is what it stands at.
    fixture.config.readerSidebarWidth = 25;
    fixture.state.width = 130;
    CHECK(fixture.state.readerSidebarWidth() == 25);
    CHECK(fixture.state.readerPaneWidth() == 104);
}

TEST_CASE("A panel the window has no room beside is not up at all [sidebar][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    fixture.config.readerSidebarThreshold = kThreshold;
    fixture.state.width = 130;

    // What would be left is not a message but a strip, so in a window that
    // narrow the panel is simply not there — and the reader has the whole of it.
    fixture.config.readerSidebarWidth = 120;
    CHECK_FALSE(fixture.state.readerSidebarShown());
    CHECK(fixture.state.readerPaneWidth() == 130);
    CHECK(fixture.state.readerPaneLeft() == 0);
    // Dragged out far enough, it is.
    fixture.state.width = 200;
    CHECK(fixture.state.readerSidebarShown());
    CHECK(fixture.state.readerPaneWidth() == 79);
}

TEST_CASE(
    "The panel stands beside a rule, on the side reader_sidebar_position names "
    "[sidebar][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    SidebarPosition side = SidebarPosition::Right;
    SUBCASE("right, which is what a config saying nothing gets") {
        side = SidebarPosition::Right;
    }
    SUBCASE("left") { side = SidebarPosition::Left; }
    openWide(fixture, kWide, side);

    const std::vector<std::string> rows = rowsOf(fixture);
    const int rule = ruleColumn(rows);
    CHECK(rule == ruleFor(fixture.state));
    // Every row carries it: the panel runs the whole height of the screen.
    for (const std::string& row : rows) {
        CHECK(amberedit::ui::substrByWidth(row, rule, 1) == "│");
    }
    // The reader's title begins in the reader's own first column — the window's
    // where the panel is beyond the message, and the one after the rule where
    // it is in front of it.
    const std::string pane = amberedit::ui::substrByWidth(
        rows[0], fixture.state.readerPaneLeft(), fixture.state.readerPaneWidth());
    CHECK(pane.find(" " + fixture.state.currentArea.tag) == 0);
}

TEST_CASE(
    "A row of the panel holds what reader_sidebar_msglist_format says "
    "[sidebar][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    openWide(fixture);

    const std::vector<std::string> rows = rowsOf(fixture);
    const int left = fixture.state.readerSidebarLeft();
    const int width = fixture.state.readerSidebarWidth();
    const std::string first = amberedit::ui::substrByWidth(rows[0], left, width);
    const std::string second = amberedit::ui::substrByWidth(rows[1], left, width);

    // The default is two lines: the names and the stamp above, the subject
    // across the whole of the line under them.
    REQUIRE(fixture.state.readerSidebarRowHeight() == 2);
    CHECK(first.find(fixture.state.readHeader->from) != std::string::npos);
    CHECK(first.find(fixture.state.readHeader->to) != std::string::npos);
    CHECK(second.find(fixture.state.readHeader->subject) != std::string::npos);
    // And no number column, which is what the reader's own title carries.
    CHECK(first.find(std::to_string(fixture.state.messageCount)) == std::string::npos);
}

TEST_CASE("A click on the panel opens that message in the reader [sidebar][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    SidebarPosition side = SidebarPosition::Right;
    SUBCASE("right, which is what a config saying nothing gets") {
        side = SidebarPosition::Right;
    }
    SUBCASE("left") { side = SidebarPosition::Left; }
    openWide(fixture, kWide, side);
    REQUIRE(reading(fixture) == 1);

    // The fourth row of the panel, which stands two lines tall.
    const int row = 3;
    CHECK(message_read::handleEvent(
        fixture.state, pressAt(inPanel(fixture.state),
                               row * fixture.state.readerSidebarRowHeight())));
    CHECK(reading(fixture) == static_cast<uint32_t>(row) + 1);
    // The list's cursor came with it, so going to the list lands on it.
    CHECK(fixture.state.messageCursor == row);
}

TEST_CASE(
    "The second line of a row is the same message as the first "
    "[sidebar][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    openWide(fixture);

    CHECK(message_read::handleEvent(fixture.state, pressAt(inPanel(fixture.state), 5)));
    CHECK(reading(fixture) == 3);
}

TEST_CASE(
    "A click on the rule, and past the end of the area, picks nothing "
    "[sidebar][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    openWide(fixture);
    const int rule = ruleFor(fixture.state);

    CHECK(reader_sidebar::clickedMessage(fixture.state, pressAt(rule, 4)) == 0);
    // Nor does anything the other side of the panel: with it against the right
    // edge, the column past its last is off the window altogether.
    CHECK(reader_sidebar::clickedMessage(
              fixture.state,
              pressAt(fixture.state.readerSidebarLeft() +
                          fixture.state.readerSidebarWidth(),
                      4)) == 0);
    // Fewer messages than rows: what is drawn under them is blank and points at
    // nothing.
    fixture.state.messageCount = 2;
    CHECK(reader_sidebar::clickedMessage(fixture.state,
                                         pressAt(inPanel(fixture.state), 20)) == 0);
}

TEST_CASE("Nothing on the panel takes the keyboard [sidebar][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    openWide(fixture);

    // The keys are the reader's, every one of them: the panel has no cursor of
    // its own for them to move and never takes one.
    const int offset = fixture.state.readerSidebarOffset;
    for (const Event& event : {Event::ArrowDown, Event::ArrowUp, Event::PageDown,
                               Event::PageUp, Event::Home, Event::End}) {
        CHECK(message_read::handleEvent(fixture.state, event));
        CHECK(reading(fixture) == 1);
        CHECK(fixture.state.readerSidebarOffset == offset);
    }
    // → is what moves between messages, here as everywhere else on this screen.
    CHECK(message_read::handleEvent(fixture.state, Event::ArrowRight));
    CHECK(reading(fixture) == 2);
}

TEST_CASE("The panel marks the message the reader is showing [sidebar][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    openWide(fixture);

    message_read::goToMessage(fixture.state, 5);
    static_cast<void>(rowsOf(fixture));
    const int rule = ruleFor(fixture.state);
    const int left = fixture.state.readerSidebarLeft();
    // The marked row is drawn in the selection's colors, every line of it, and
    // no other row is.
    const int marked = 4 * fixture.state.readerSidebarRowHeight();

    term::Screen screen(fixture.state.width, fixture.state.height);
    term::render(screen, message_read::render(fixture.state));
    const auto selected = [&](int y) {
        return screen.at(left, y).bg == screen.at(left, marked).bg;
    };
    // The panel marks rather than chooses — the keyboard is in the reader — so
    // the bar is `reader_sidebar_msglist_selected` and not the one the lists
    // pick a row out with.
    CHECK(screen.at(left, marked).bg ==
          amberedit::ui::theme::palette.readerSidebarMsglistSelected);
    CHECK_FALSE(screen.at(left, marked).bg == amberedit::ui::theme::palette.selection);
    CHECK(selected(marked));
    CHECK(selected(marked + 1));
    CHECK_FALSE(selected(marked - 1));
    CHECK_FALSE(selected(marked + 2));
    // The rule is no part of a row and keeps its own color.
    CHECK(screen.at(rule, marked).glyph == "│");
}

TEST_CASE(
    "Walking on scrolls the panel a row at a time, and only at its edge "
    "[sidebar][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    openWide(fixture);
    const int rows = fixture.state.readerSidebarItems();
    REQUIRE(static_cast<int>(fixture.state.messageCount) > rows + 2);

    // Down the panel: nothing moves until the reading reaches the bottom row.
    for (int i = 1; i < rows; ++i) {
        REQUIRE(message_read::handleEvent(fixture.state, Event::ArrowRight));
        CHECK(fixture.state.readerSidebarOffset == 0);
    }
    REQUIRE(message_read::handleEvent(fixture.state, Event::ArrowRight));
    CHECK(fixture.state.readerSidebarOffset == 1);
    REQUIRE(message_read::handleEvent(fixture.state, Event::ArrowRight));
    CHECK(fixture.state.readerSidebarOffset == 2);
    // And back up the same way.
    REQUIRE(message_read::handleEvent(fixture.state, Event::ArrowLeft));
    CHECK(fixture.state.readerSidebarOffset == 2);
}

TEST_CASE("A jump opens the panel around where it landed [sidebar][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    openWide(fixture);
    const int rows = fixture.state.readerSidebarItems();
    const uint32_t last = fixture.state.messageCount;
    REQUIRE(static_cast<int>(last) > rows * 2);

    const uint32_t target = last / 2;
    message_read::goToMessage(fixture.state, target);
    // Centred rather than pinned to the edge it came in over, with nothing
    // beyond it.
    const int index = static_cast<int>(target) - 1;
    CHECK(fixture.state.readerSidebarOffset == index - (rows / 2));
    CHECK(fixture.state.readerSidebarOffset + rows <= static_cast<int>(last));
}

TEST_CASE("The panel never scrolls past either end of the area [sidebar][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    openWide(fixture);
    const int rows = fixture.state.readerSidebarItems();
    const uint32_t last = fixture.state.messageCount;

    message_read::goToMessage(fixture.state, 1);
    CHECK(fixture.state.readerSidebarOffset == 0);
    message_read::goToMessage(fixture.state, last);
    CHECK(fixture.state.readerSidebarOffset ==
          static_cast<int>(fixture.state.messageCount) - rows);
}

TEST_CASE(
    "The wheel over the panel scrolls the panel and leaves the reader alone "
    "[sidebar][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    openWide(fixture);
    fixture.config.listWheelThrottle = false;

    REQUIRE(message_read::handleEvent(
        fixture.state, wheelAt(inPanel(fixture.state), /*down=*/true)));
    CHECK(fixture.state.readerSidebarOffset == 1);
    // Scrolling the panel is not choosing off it: the reader is still on the
    // message it was, and the mark is still on that message.
    CHECK(reading(fixture) == 1);

    REQUIRE(message_read::handleEvent(
        fixture.state, wheelAt(inPanel(fixture.state), /*down=*/false)));
    CHECK(fixture.state.readerSidebarOffset == 0);

    // Over the text it is the text the wheel reaches, as it always was — the
    // pointer says which of the two was meant.
    REQUIRE(message_read::handleEvent(
        fixture.state, wheelAt(fixture.state.readerPaneLeft() + 4, /*down=*/true)));
    CHECK(fixture.state.readerSidebarOffset == 0);
}

TEST_CASE(
    "Scrolling the panel away from the message being read leaves it there "
    "[sidebar][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    openWide(fixture);
    fixture.config.listWheelThrottle = false;
    static_cast<void>(rowsOf(fixture));

    for (int i = 0; i < 4; ++i) {
        REQUIRE(message_read::handleEvent(
            fixture.state, wheelAt(inPanel(fixture.state), /*down=*/true)));
    }
    CHECK(fixture.state.readerSidebarOffset == 4);
    // Drawing it does not drag it back: the offset is the user's until the
    // reader moves to another message.
    static_cast<void>(rowsOf(fixture));
    CHECK(fixture.state.readerSidebarOffset == 4);
}

TEST_CASE(
    "A window resized under the panel puts the message being read back on it "
    "[sidebar][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    openWide(fixture);
    fixture.config.listWheelThrottle = false;

    message_read::goToMessage(fixture.state, fixture.state.messageCount);
    static_cast<void>(rowsOf(fixture));
    const auto marked = static_cast<int>(fixture.state.messageCount) - 1;
    REQUIRE(fixture.state.readerSidebarOffset <= marked);

    fixture.state.height = 10;
    static_cast<void>(rowsOf(fixture));
    const int rows = fixture.state.readerSidebarItems();
    CHECK(marked >= fixture.state.readerSidebarOffset);
    CHECK(marked < fixture.state.readerSidebarOffset + rows);
}

TEST_CASE(
    "The panel tracks the reader in a window too narrow to show it "
    "[sidebar][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    openWide(fixture, /*width=*/80);
    REQUIRE_FALSE(fixture.state.readerSidebarShown());

    message_read::goToMessage(fixture.state, fixture.state.messageCount);
    // Dragging the window out to where the panel fits opens it on the message
    // being read rather than at the top of the area.
    fixture.state.width = kWide;
    REQUIRE(fixture.state.readerSidebarShown());
    const int rows = fixture.state.readerSidebarItems();
    const auto marked = static_cast<int>(fixture.state.messageCount) - 1;
    CHECK(marked >= fixture.state.readerSidebarOffset);
    CHECK(marked < fixture.state.readerSidebarOffset + rows);
}

TEST_CASE("An area holding nothing draws the panel empty [sidebar][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    fixture.config.readerSidebarThreshold = kThreshold;
    fixture.config.readerSidebarPosition = SidebarPosition::Right;
    fixture.state.width = kWide;
    fixture.state.height = 24;
    fixture.state.messageCount = 0;
    message_read::showEmptyArea(fixture.state);

    const std::vector<std::string> rows = rowsOf(fixture);
    const int rule = ruleColumn(rows);
    CHECK(rule == ruleFor(fixture.state));
    const int left = fixture.state.readerSidebarLeft();
    const int width = fixture.state.readerSidebarWidth();
    for (const std::string& row : rows) {
        CHECK(amberedit::ui::substrByWidth(row, left, width) ==
              std::string(static_cast<size_t>(width), ' '));
    }
}

TEST_CASE(
    "The reader's corners are its own, and the panel does not swallow them "
    "[sidebar][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    // Both boxes are `when_narrow` by default and a window wide enough for the
    // panel is not narrow, so a test about the corners asks for them outright.
    fixture.config.backButton = Visibility::On;
    fixture.config.menuButton = Visibility::On;
    openWide(fixture);
    REQUIRE(fixture.state.readerMenuShown());

    // With the panel beyond the message, the reader's right-hand corner is the
    // rule and not the window's last column: the menu button hangs from that.
    REQUIRE(message_read::handleEvent(
        fixture.state, pressAt(fixture.state.readerPaneRight() - 1, 0)));
    CHECK(fixture.state.menuView);
    fixture.state.menuView.reset();

    // The window's own last column is the panel, which opens the message it
    // marks rather than a menu.
    REQUIRE(
        message_read::handleEvent(fixture.state, pressAt(fixture.state.width - 1, 0)));
    CHECK_FALSE(fixture.state.menuView);
    CHECK(reading(fixture) == 1);

    // And the way back hangs from the reader's left-hand corner, which on this
    // side of the message is the window's own. It goes out of the area, the way
    // Esc does here.
    CHECK(fixture.state.readerPaneLeft() == 0);
    REQUIRE(message_read::handleEvent(fixture.state, pressAt(0, 0)));
    CHECK(fixture.state.navigator.current() == amberedit::app::ScreenId::AreaList);
}

TEST_CASE("The message list itself has no panel [sidebar][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    openWide(fixture);

    term::Screen screen(fixture.state.width, fixture.state.height);
    term::render(screen, message_list::render(fixture.state));
    // The table's own title runs from the first column of the window: the panel
    // is the reader's and the list is a screen of messages already.
    std::string title;
    for (int x = 0; x < fixture.state.width; ++x) title += screen.at(x, 0).glyph;
    CHECK(title.find(" " + fixture.state.currentArea.tag) == 0);
}
