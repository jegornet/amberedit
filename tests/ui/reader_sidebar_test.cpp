#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <string>
#include <utility>
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

using amberedit::config::SidebarContent;
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
///
/// The panel is asked for the area rather than the thread, which is the other
/// way round from what a config saying nothing gets: these are the tests of a
/// panel showing the messages of an area, and the ones about the thread state
/// `tree` for the same reason. What each is about is on the line.
void openWide(AreaFixture& fixture, int width = kWide,
              SidebarPosition side = SidebarPosition::Right,
              SidebarContent content = SidebarContent::List) {
    fixture.config.readerSidebarThreshold = kThreshold;
    fixture.config.readerSidebarPosition = side;
    fixture.config.readerSidebarContent = content;
    fixture.state.width = width;
    fixture.state.height = 24;
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());
}

/// One record of a Squish index: where the message's frame stands in the .sqd,
/// and the UMSGID that names it wherever a link does.
struct IndexRecord {
    uint32_t offset{0};
    uint32_t uid{0};
};

uint32_t readU32(const unsigned char* raw) {
    return static_cast<uint32_t>(raw[0]) | (static_cast<uint32_t>(raw[1]) << 8) |
           (static_cast<uint32_t>(raw[2]) << 16) | (static_cast<uint32_t>(raw[3]) << 24);
}

/// The index of the base, a record a message, in message order.
std::vector<IndexRecord> indexOf(const TempSquishBase& base) {
    std::ifstream file(base.path() + ".sqi", std::ios::binary);
    REQUIRE(file.good());
    std::vector<IndexRecord> records;
    unsigned char raw[12];
    while (file.read(reinterpret_cast<char*>(raw), sizeof(raw))) {
        records.push_back({readU32(raw), readU32(raw + 4)});
    }
    return records;
}

/// Threads the base by hand: message `number` is made to answer `replyTo` and to
/// be answered by `replies`, all of them message numbers.
///
/// Written into the frames rather than through the driver, because threading a
/// base is the tosser's work and nothing above `IMsgBase` can ask for it —
/// `MessageDraft` carries no links. The checked-in base holds one pair of them,
/// and a tree wants a shape to be drawn in. The frame header is 28 bytes and
/// the message header follows it, with the message answered 174 bytes in and
/// the nine answers Squish has room for right after it, every one a UMSGID.
void linkThread(const TempSquishBase& base, uint32_t number, uint32_t replyTo,
                const std::vector<uint32_t>& replies) {
    const std::vector<IndexRecord> records = indexOf(base);
    REQUIRE(number >= 1);
    REQUIRE(number <= records.size());

    std::fstream data(base.path() + ".sqd",
                      std::ios::in | std::ios::out | std::ios::binary);
    REQUIRE(data.good());

    const auto uidOf = [&](uint32_t message) -> uint32_t {
        if (message == 0) return 0;
        REQUIRE(message <= records.size());
        return records[message - 1].uid;
    };
    const auto write = [&](std::streamoff at, uint32_t value) {
        const unsigned char raw[4] = {static_cast<unsigned char>(value & 0xffu),
                                      static_cast<unsigned char>((value >> 8) & 0xffu),
                                      static_cast<unsigned char>((value >> 16) & 0xffu),
                                      static_cast<unsigned char>((value >> 24) & 0xffu)};
        data.seekp(at);
        data.write(reinterpret_cast<const char*>(raw), sizeof(raw));
    };

    const auto header = static_cast<std::streamoff>(records[number - 1].offset) + 28;
    write(header + 174, uidOf(replyTo));
    for (size_t i = 0; i < 9; ++i) {
        write(header + 178 + static_cast<std::streamoff>(i * 4),
              i < replies.size() ? uidOf(replies[i]) : 0);
    }
    data.flush();
    REQUIRE(data.good());
}

/// The thread the tests below are drawn around, written into the base:
///
///     10
///     |-11
///     | `-15
///     |-12
///     | |-20
///     | `-21
///     `-13
///
/// One of every part the panel draws: a message answered, answers beside the
/// one being read, and answers under those.
void linkFixtureThread(const TempSquishBase& base) {
    linkThread(base, 10, 0, {11, 12, 13});
    linkThread(base, 11, 10, {15});
    linkThread(base, 12, 10, {20, 21});
    linkThread(base, 13, 10, {});
    linkThread(base, 15, 11, {});
    linkThread(base, 20, 12, {});
    linkThread(base, 21, 12, {});
}

/// A level of thread above the fixture's own, so that a tree may be asked to
/// climb further than one:
///
///     5
///     |-6
///     | `-7
///     `-10, and everything linkFixtureThread() hung under it
///
void linkDeeperThread(const TempSquishBase& base) {
    linkFixtureThread(base);
    linkThread(base, 5, 0, {6, 10});
    linkThread(base, 6, 5, {7});
    linkThread(base, 7, 6, {});
    linkThread(base, 10, 5, {11, 12, 13});
}

/// The messages the tree holds, top to bottom.
std::vector<uint32_t> treeOf(const AreaFixture& fixture) {
    std::vector<uint32_t> numbers;
    numbers.reserve(fixture.state.readerSidebarTree.size());
    for (const auto& row : fixture.state.readerSidebarTree) numbers.push_back(row.number);
    return numbers;
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

TEST_CASE(
    "The tree holds the message, what it answers, and what answers either "
    "[sidebar][squish]") {
    TempSquishBase base;
    linkFixtureThread(base);
    AreaFixture fixture(base.path());
    openWide(fixture, kWide, SidebarPosition::Right, SidebarContent::Tree);

    message_read::goToMessage(fixture.state, 12);
    // The message answered at the top, its answers under it — the one being read
    // among them — and the answers to each of those under those. Nothing else:
    // the thread as far as it bears on where the reading is.
    CHECK(treeOf(fixture) == std::vector<uint32_t>{10, 11, 15, 12, 20, 21, 13});
    CHECK(fixture.state.readerSidebarTreeFor == 12);
    // A thread this size is drawn whole and from its top.
    CHECK(fixture.state.readerSidebarOffset == 0);
}

TEST_CASE("A message answering nothing is the top of its own tree [sidebar][squish]") {
    TempSquishBase base;
    linkFixtureThread(base);
    AreaFixture fixture(base.path());
    openWide(fixture, kWide, SidebarPosition::Right, SidebarContent::Tree);

    message_read::goToMessage(fixture.state, 10);
    // Its answers stand under it and nothing stands above it. What its siblings
    // would be is every other thread of the area, which is the area rather than
    // a thread — so the answers to its answers are not drawn either: they are
    // the tree of whichever of them is read next.
    CHECK(treeOf(fixture) == std::vector<uint32_t>{10, 11, 12, 13});
}

TEST_CASE("A message in no thread is a tree of itself [sidebar][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    openWide(fixture, kWide, SidebarPosition::Right, SidebarContent::Tree);

    // Nothing links message 1 in the base as it is checked in, and a panel with
    // one row on it is the honest answer: there is no thread to draw.
    REQUIRE(reading(fixture) == 1);
    CHECK(treeOf(fixture) == std::vector<uint32_t>{1});
}

TEST_CASE("The tree stands in front of the messages it is drawn for [sidebar][squish]") {
    TempSquishBase base;
    linkFixtureThread(base);
    AreaFixture fixture(base.path());
    openWide(fixture, kWide, SidebarPosition::Right, SidebarContent::Tree);
    message_read::goToMessage(fixture.state, 12);

    const std::vector<std::string> rows = rowsOf(fixture);
    const int left = fixture.state.readerSidebarLeft();
    const int height = fixture.state.readerSidebarRowHeight();
    REQUIRE(height == 2);

    // The corner on the line the message is read from, and under it the bar the
    // messages below it hang from — a row two lines tall is one message hanging
    // from one fork. Every row is drawn out to the width of the deepest, so the
    // messages themselves begin in one column whatever they hang at.
    const std::vector<std::pair<std::string, std::string>> drawn{
        {"     ", "     "},  // 10, which answers nothing
        {" ├─  ", " │   "},  // 11
        {" │ └─", " │   "},  // 15, the answer to it
        {" ├─  ", " │   "},  // 12, the message being read
        {" │ ├─", " │ │ "},  // 20
        {" │ └─", " │   "},  // 21
        {" └─  ", "     "},  // 13, the last answer of the three
    };
    for (size_t i = 0; i < drawn.size(); ++i) {
        const int y = static_cast<int>(i) * height;
        CHECK(amberedit::ui::substrByWidth(rows[static_cast<size_t>(y)], left, 5) ==
              drawn[i].first);
        CHECK(amberedit::ui::substrByWidth(rows[static_cast<size_t>(y) + 1], left, 5) ==
              drawn[i].second);
    }
}

TEST_CASE("A click on a row of the tree opens that message [sidebar][squish]") {
    TempSquishBase base;
    linkFixtureThread(base);
    AreaFixture fixture(base.path());
    openWide(fixture, kWide, SidebarPosition::Right, SidebarContent::Tree);
    message_read::goToMessage(fixture.state, 12);

    // The third row of the panel, which is the answer to the answer beside the
    // one being read: a message the area holds at 15, and a click on the panel
    // is a way to it that no key on this screen is.
    CHECK(message_read::handleEvent(
        fixture.state,
        pressAt(inPanel(fixture.state), 2 * fixture.state.readerSidebarRowHeight())));
    CHECK(reading(fixture) == 15);
    // The list's cursor came with it, as it does from a row of the area.
    CHECK(fixture.state.messageCursor == 14);
    // And the tree is the tree around the message now being read: what it
    // answers, and nothing under it, there being nothing that answers it.
    CHECK(treeOf(fixture) == std::vector<uint32_t>{11, 15});
}

TEST_CASE("The tree is drawn again as the reading moves on [sidebar][squish]") {
    TempSquishBase base;
    linkFixtureThread(base);
    AreaFixture fixture(base.path());
    openWide(fixture, kWide, SidebarPosition::Right, SidebarContent::Tree);
    message_read::goToMessage(fixture.state, 12);
    REQUIRE(fixture.state.readerSidebarTreeFor == 12);

    // → is what walks the area here as everywhere else on this screen, and it
    // walks out of the thread as readily as along it: 13 answers the same
    // message, so the tree is the same one with the bar moved down it.
    REQUIRE(message_read::handleEvent(fixture.state, Event::ArrowRight));
    CHECK(reading(fixture) == 13);
    CHECK(treeOf(fixture) == std::vector<uint32_t>{10, 11, 15, 12, 20, 21, 13});
    CHECK(fixture.state.readerSidebarTreeFor == 13);

    // And 14 is in no thread at all, so the panel holds the one message.
    REQUIRE(message_read::handleEvent(fixture.state, Event::ArrowRight));
    CHECK(reading(fixture) == 14);
    CHECK(treeOf(fixture) == std::vector<uint32_t>{14});
}

TEST_CASE("The panel marks the message the tree was drawn around [sidebar][squish]") {
    TempSquishBase base;
    linkFixtureThread(base);
    AreaFixture fixture(base.path());
    openWide(fixture, kWide, SidebarPosition::Right, SidebarContent::Tree);
    message_read::goToMessage(fixture.state, 12);

    const int left = fixture.state.readerSidebarLeft();
    // The fourth row of the tree, message 12 standing there.
    const int marked = 3 * fixture.state.readerSidebarRowHeight();

    term::Screen screen(fixture.state.width, fixture.state.height);
    term::render(screen, message_read::render(fixture.state));
    const auto selected = [&](int y) {
        return screen.at(left, y).bg == screen.at(left, marked).bg;
    };
    // The panel's own quieter bar, as it is over an area: it says which message
    // is on the screen beside it, and the keyboard is in the reader either way.
    CHECK(screen.at(left, marked).bg ==
          amberedit::ui::theme::palette.readerSidebarMsglistSelected);
    CHECK(selected(marked));
    CHECK(selected(marked + 1));
    CHECK_FALSE(selected(marked - 1));
    CHECK_FALSE(selected(marked + 2));
}

TEST_CASE("A tree taller than the screen opens around the message [sidebar][squish]") {
    TempSquishBase base;
    linkFixtureThread(base);
    AreaFixture fixture(base.path());
    openWide(fixture, kWide, SidebarPosition::Right, SidebarContent::Tree);

    // Three rows of the seven fit.
    fixture.state.height = 6;
    message_read::goToMessage(fixture.state, 12);
    const int rows = fixture.state.readerSidebarItems();
    REQUIRE(rows == 3);

    const int marked = 3;
    CHECK(fixture.state.readerSidebarOffset <= marked);
    CHECK(marked < fixture.state.readerSidebarOffset + rows);
    // Around it rather than pinned to the edge it would otherwise stand against.
    CHECK(fixture.state.readerSidebarOffset == marked - (rows / 2));
}

TEST_CASE("The wheel scrolls the tree and leaves the reader alone [sidebar][squish]") {
    TempSquishBase base;
    linkFixtureThread(base);
    AreaFixture fixture(base.path());
    openWide(fixture, kWide, SidebarPosition::Right, SidebarContent::Tree);
    fixture.config.listWheelThrottle = false;
    fixture.state.height = 6;
    message_read::goToMessage(fixture.state, 10);
    REQUIRE(fixture.state.readerSidebarOffset == 0);

    REQUIRE(message_read::handleEvent(fixture.state,
                                      wheelAt(inPanel(fixture.state), /*down=*/true)));
    CHECK(fixture.state.readerSidebarOffset == 1);
    CHECK(reading(fixture) == 10);
    // And no further than the last row of the thread: four rows, three of which
    // are on the screen.
    for (int i = 0; i < 5; ++i) {
        REQUIRE(message_read::handleEvent(
            fixture.state, wheelAt(inPanel(fixture.state), /*down=*/true)));
    }
    CHECK(fixture.state.readerSidebarOffset == 1);
}

TEST_CASE("Nothing on the tree takes the keyboard either [sidebar][squish]") {
    TempSquishBase base;
    linkFixtureThread(base);
    AreaFixture fixture(base.path());
    openWide(fixture, kWide, SidebarPosition::Right, SidebarContent::Tree);
    message_read::goToMessage(fixture.state, 12);

    // The keys are the reader's, every one of them: a tree is no more a place
    // for the reading to be than a list of the area is, and there is no cursor
    // on it to move.
    const std::vector<uint32_t> tree = treeOf(fixture);
    for (const Event& event : {Event::ArrowDown, Event::ArrowUp, Event::PageDown,
                               Event::PageUp, Event::Home, Event::End}) {
        CHECK(message_read::handleEvent(fixture.state, event));
        CHECK(reading(fixture) == 12);
        CHECK(treeOf(fixture) == tree);
        CHECK(fixture.state.readerSidebarOffset == 0);
    }
}

TEST_CASE("A window dragged out to fit the panel draws the tree then "
          "[sidebar][squish]") {
    TempSquishBase base;
    linkFixtureThread(base);
    AreaFixture fixture(base.path());
    openWide(fixture, /*width=*/80, SidebarPosition::Right, SidebarContent::Tree);
    REQUIRE_FALSE(fixture.state.readerSidebarShown());

    message_read::goToMessage(fixture.state, 12);
    // Nothing was built for a panel nobody could see: a tree is a read of the
    // base for every message in it, and the reading walks on whether or not
    // there is a panel beside it.
    CHECK(fixture.state.readerSidebarTree.empty());

    fixture.state.width = kWide;
    REQUIRE(fixture.state.readerSidebarShown());
    static_cast<void>(rowsOf(fixture));
    CHECK(treeOf(fixture) == std::vector<uint32_t>{10, 11, 15, 12, 20, 21, 13});
}

TEST_CASE("An area holding nothing draws no tree [sidebar][squish]") {
    TempSquishBase base;
    linkFixtureThread(base);
    AreaFixture fixture(base.path());
    openWide(fixture, kWide, SidebarPosition::Right, SidebarContent::Tree);
    message_read::goToMessage(fixture.state, 12);
    REQUIRE_FALSE(fixture.state.readerSidebarTree.empty());

    fixture.state.messageCount = 0;
    message_read::showEmptyArea(fixture.state);
    // The thread named messages of an area that no longer holds one.
    CHECK(fixture.state.readerSidebarTree.empty());

    const std::vector<std::string> rows = rowsOf(fixture);
    const int left = fixture.state.readerSidebarLeft();
    const int width = fixture.state.readerSidebarWidth();
    for (const std::string& row : rows) {
        CHECK(amberedit::ui::substrByWidth(row, left, width) ==
              std::string(static_cast<size_t>(width), ' '));
    }
}

TEST_CASE("The tree reaches as far as the two levels settings say [sidebar][squish]") {
    TempSquishBase base;
    linkFixtureThread(base);
    AreaFixture fixture(base.path());
    openWide(fixture, kWide, SidebarPosition::Right, SidebarContent::Tree);

    // A level each way is the message in its own place, and what a config
    // saying nothing gets.
    REQUIRE(fixture.config.readerSidebarTreeLevelsUp == 1);
    REQUIRE(fixture.config.readerSidebarTreeLevelsDown == 1);
    message_read::goToMessage(fixture.state, 12);
    CHECK(treeOf(fixture) == std::vector<uint32_t>{10, 11, 15, 12, 20, 21, 13});

    // Nothing below: the tree stops on the row the message stands on, so what
    // is left is the message answered and everything answering it.
    fixture.config.readerSidebarTreeLevelsDown = 0;
    message_read::goToMessage(fixture.state, 12);
    CHECK(treeOf(fixture) == std::vector<uint32_t>{10, 11, 12, 13});

    // Nothing above: the tree stands on the message itself, so its siblings and
    // what they answer are gone and its own answers are all that is left.
    fixture.config.readerSidebarTreeLevelsUp = 0;
    fixture.config.readerSidebarTreeLevelsDown = 1;
    message_read::goToMessage(fixture.state, 12);
    CHECK(treeOf(fixture) == std::vector<uint32_t>{12, 20, 21});

    // Neither way: the message alone, which is a panel worth having only beside
    // a list on the other setting — and allowed, a setting that means none
    // saying so rather than being turned off somewhere else.
    fixture.config.readerSidebarTreeLevelsDown = 0;
    message_read::goToMessage(fixture.state, 12);
    CHECK(treeOf(fixture) == std::vector<uint32_t>{12});
    // Which is still the message being read, marked and clickable like any row.
    CHECK(reader_sidebar::clickedMessage(fixture.state,
                                         pressAt(inPanel(fixture.state), 0)) == 12);

    // Two levels below and none above reach what the answers themselves are
    // answered by, the message being read at the top of it.
    fixture.config.readerSidebarTreeLevelsDown = 2;
    message_read::goToMessage(fixture.state, 10);
    CHECK(treeOf(fixture) == std::vector<uint32_t>{10, 11, 15, 12, 20, 21, 13});
}

TEST_CASE("The tree climbs as many levels as it is asked for [sidebar][squish]") {
    TempSquishBase base;
    linkDeeperThread(base);
    AreaFixture fixture(base.path());
    openWide(fixture, kWide, SidebarPosition::Right, SidebarContent::Tree);

    // A level up is the message answered and no further, whatever stands above
    // that.
    message_read::goToMessage(fixture.state, 12);
    CHECK(treeOf(fixture) == std::vector<uint32_t>{10, 11, 15, 12, 20, 21, 13});

    // Two, and the tree is drawn from what that message answers in its turn —
    // every level of it down to one below the message being read, so the
    // messages beside the one answered bring their own answers with them.
    fixture.config.readerSidebarTreeLevelsUp = 2;
    message_read::goToMessage(fixture.state, 12);
    CHECK(treeOf(fixture) == std::vector<uint32_t>{5, 6, 7, 10, 11, 15, 12, 20, 21, 13});

    // The levels below are counted from the message and not from the top, so
    // stopping the drawing at the message's own row leaves everything above it
    // and nothing under it.
    fixture.config.readerSidebarTreeLevelsDown = 0;
    message_read::goToMessage(fixture.state, 12);
    CHECK(treeOf(fixture) == std::vector<uint32_t>{5, 6, 7, 10, 11, 12, 13});

    // A climb longer than the thread ends at the top of it: five answers
    // nothing, so nine levels and two come to the same tree.
    fixture.config.readerSidebarTreeLevelsUp = 9;
    fixture.config.readerSidebarTreeLevelsDown = 1;
    message_read::goToMessage(fixture.state, 12);
    CHECK(treeOf(fixture) == std::vector<uint32_t>{5, 6, 7, 10, 11, 15, 12, 20, 21, 13});
}

TEST_CASE("A tree three levels deep is drawn three levels wide [sidebar][squish]") {
    TempSquishBase base;
    linkDeeperThread(base);
    AreaFixture fixture(base.path());
    openWide(fixture, kWide, SidebarPosition::Right, SidebarContent::Tree);
    fixture.config.readerSidebarTreeLevelsUp = 2;
    message_read::goToMessage(fixture.state, 12);

    const std::vector<std::string> rows = rowsOf(fixture);
    const int left = fixture.state.readerSidebarLeft();
    const int height = fixture.state.readerSidebarRowHeight();
    // Six columns rather than four: the drawing is as wide as the deepest row
    // of the tree, and every row is padded out to it so the messages still
    // begin in one column.
    const std::vector<std::string> drawn{
        "       ",  // 5
        " ├─    ",  // 6
        " │ └─  ",  // 7
        " └─    ",  // 10
        "   ├─  ",  // 11
        "   │ └─",  // 15
        "   ├─  ",  // 12, the message being read
        "   │ ├─",  // 20
        "   │ └─",  // 21
        "   └─  ",  // 13
    };
    for (size_t i = 0; i < drawn.size(); ++i) {
        const size_t y = i * static_cast<size_t>(height);
        CHECK(amberedit::ui::substrByWidth(rows[y], left, 7) == drawn[i]);
    }
}
