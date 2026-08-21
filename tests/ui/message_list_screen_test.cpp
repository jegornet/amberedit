#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "domain/message.hpp"
#include "temp_squish_base.hpp"
#include "ui/area_fixture.hpp"
#include "ui/screens/message_list_screen.hpp"
#include "ui/screens/message_read_screen.hpp"
#include "ui/term/element.hpp"
#include "ui/term/screen.hpp"
#include "ui/text_layout.hpp"
#include "ui/theme.hpp"

using amberedit::test::AreaFixture;
using amberedit::test::TempSquishBase;
using amberedit::test::uidAt;
using amberedit::ui::term::Event;

namespace message_list = amberedit::ui::screens::message_list;
namespace message_read = amberedit::ui::screens::message_read;
namespace term = amberedit::ui::term;
namespace theme = amberedit::ui::theme;

namespace {

/// The table as it reaches the terminal, row by row, so that a layout can be
/// read back the way it is seen.
std::vector<std::string> rowsOf(AreaFixture& fixture) {
    term::Screen screen(fixture.state.width, fixture.state.height);
    term::render(screen, message_list::render(fixture.state));

    std::vector<std::string> rows;
    for (int y = 0; y < fixture.state.height; ++y) {
        std::string row;
        for (int x = 0; x < fixture.state.width; ++x) row += screen.at(x, y).glyph;
        rows.push_back(row);
    }
    return rows;
}

/// A notch of the wheel, up or down. Where the pointer stands is not asked
/// after: the list moves its cursor wherever in it the wheel was turned.
Event wheel(bool down) {
    term::MouseEvent mouse;
    mouse.button =
        down ? term::MouseEvent::Button::WheelDown : term::MouseEvent::Button::WheelUp;
    mouse.motion = term::MouseEvent::Motion::Pressed;
    mouse.x = 0;
    mouse.y = 0;
    return Event::Mouse(mouse);
}

/// The rightmost column of a drawn row, as a glyph rather than as a byte: the
/// scrollbar's own are three bytes each.
std::string lastGlyph(const std::string& row) {
    return amberedit::ui::substrByWidth(row, amberedit::ui::displayWidth(row) - 1, 1);
}

/// The rightmost column over `count` rows starting at `from`, one character to a
/// row: `#` for the scrollbar's thumb, `|` for its track, a space where nothing
/// is drawn, `?` for anything else.
std::string barShape(const std::vector<std::string>& rows, int from, int count) {
    std::string shape;
    for (int i = from; i < from + count; ++i) {
        const std::string glyph = lastGlyph(rows[static_cast<size_t>(i)]);
        shape += glyph == "█" ? '#' : glyph == "│" ? '|' : glyph == " " ? ' ' : '?';
    }
    return shape;
}

/// What column `x` of the row showing message `number` (1-based) is drawn in.
term::Color cellColor(AreaFixture& fixture, int number, int x) {
    term::Screen screen(fixture.state.width, fixture.state.height);
    term::render(screen, message_list::render(fixture.state));
    // The three rows the table stands under: the title, the headings, the rule.
    const int row = 3 + (number - 1 - fixture.state.messageOffset);
    REQUIRE(row >= 3);
    REQUIRE(row < fixture.state.height);
    REQUIRE(x < fixture.state.width);
    return screen.at(x, row).fg;
}

/// What the row is drawn in, read out of its number column — the one cell no
/// per-cell rule ever colors, so what stands there is whatever was painted over
/// the row as a whole.
term::Color rowColor(AreaFixture& fixture, int number) {
    return cellColor(fixture, number, 2);
}

/// Where a column heading stands, in columns rather than in bytes: the rule and
/// the back button beside it are drawn in box characters.
int columnOf(const std::vector<std::string>& rows, const std::string& heading) {
    const size_t at = rows[1].find(heading);
    REQUIRE(at != std::string::npos);
    return amberedit::ui::displayWidth(rows[1].substr(0, at));
}

/// What a row shows in its Date column, the padding out to the right margin
/// taken off again.
std::string dateCell(AreaFixture& fixture, int width) {
    fixture.state.width = width;
    const auto rows = rowsOf(fixture);
    const int at = columnOf(rows, "Date");
    std::string cell = amberedit::ui::substrByWidth(rows[3], at, width - at);
    while (!cell.empty() && cell.back() == ' ') cell.pop_back();
    return cell;
}

}  // namespace

TEST_CASE("centerCursor puts the current message halfway down the list "
          "[messagelist]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    fixture.state.messageCount = 100;
    fixture.state.height = 24;  // 21 rows for the table
    fixture.state.messageCursor = 50;

    message_list::centerCursor(fixture.state);

    CHECK(fixture.state.messageOffset == 40);
    CHECK(fixture.cursorRow() == 10);
}

TEST_CASE("centerCursor does not scroll past either end of the area "
          "[messagelist]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    fixture.state.messageCount = 100;
    fixture.state.height = 24;

    SUBCASE("near the start there is nothing above to scroll to") {
        fixture.state.messageCursor = 3;
        message_list::centerCursor(fixture.state);
        CHECK(fixture.state.messageOffset == 0);
        CHECK(fixture.cursorRow() == 3);
    }
    SUBCASE("at the end the last row is as far as it goes") {
        fixture.state.messageCursor = 99;
        message_list::centerCursor(fixture.state);
        CHECK(fixture.state.messageOffset == 79);
        CHECK(fixture.cursorRow() == 20);
    }
    SUBCASE("an area that fits on one screen does not scroll at all") {
        fixture.state.messageCount = 5;
        fixture.state.messageCursor = 4;
        message_list::centerCursor(fixture.state);
        CHECK(fixture.state.messageOffset == 0);
    }
}

TEST_CASE("Opening an area on its lastread mark shows the messages after it "
          "[messagelist][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    fixture.state.height = 24;

    const uint32_t total = fixture.manager.areas()[0].total;
    REQUIRE(total > 25);
    // A mark in the middle of the area, which is what a reader that left off
    // partway through would have written: the messages after it are the new
    // ones, and they are the reason the list is being opened.
    const uint32_t mark = total / 2;
    fixture.lastRead->set(uidAt(fixture, mark));

    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());

    // The area opens on the first message after the mark, that being the first
    // one not read yet.
    CHECK(fixture.state.messageCursor == static_cast<int>(mark));
    CHECK(fixture.cursorRow() == fixture.state.messageListRows() / 2);
    // Which is the whole point: there are rows left below the cursor for the
    // unread messages, rather than the mark sitting on the bottom one.
    CHECK(fixture.cursorRow() < fixture.state.messageListRows() - 1);
}

TEST_CASE("The list comes up centred on the message being read "
          "[messagelist][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    fixture.state.height = 24;

    const int total = static_cast<int>(fixture.manager.areas()[0].total);
    // Partway down the area, and far enough from its end that the walk below
    // has somewhere to go.
    const uint32_t mark = static_cast<uint32_t>(total) / 2 - 2;
    fixture.lastRead->set(uidAt(fixture, mark));
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());

    // Reading on with → walks past the bottom of the window the area opened
    // with, which is the state the list would otherwise come up in.
    const int rows = fixture.state.messageListRows();
    const int walk = rows / 2 + 2;
    // Where the area opened: the message after the mark.
    const int start = static_cast<int>(mark);
    // Far enough from the end of the area that centring is not cut short by it.
    REQUIRE(start + walk + rows / 2 < total);
    for (int i = 0; i < walk; ++i) {
        REQUIRE(message_read::handleEvent(fixture.state, Event::ArrowRight));
    }

    REQUIRE(message_read::handleEvent(fixture.state, Event::Character('l')));

    CHECK(fixture.state.navigator.current() == amberedit::app::ScreenId::MessageList);
    CHECK(fixture.state.messageCursor == start + walk);
    CHECK(fixture.cursorRow() == rows / 2);
}

TEST_CASE("PageUp goes to the top visible row first and pages only from there "
          "[messagelist]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    fixture.state.messageCount = 58;
    fixture.state.height = 31;  // 28 rows for the table
    // Rows 30..57 on screen, the cursor partway down them.
    fixture.state.messageCursor = 39;
    fixture.state.messageOffset = 30;

    // First stop: the top row of the window, which does not move.
    REQUIRE(message_list::handleEvent(fixture.state, Event::PageUp));
    CHECK(fixture.state.messageCursor == 30);
    CHECK(fixture.state.messageOffset == 30);

    // From the top row the page turns, a row short of the window: the row
    // that was on top is now the bottom one.
    REQUIRE(message_list::handleEvent(fixture.state, Event::PageUp));
    CHECK(fixture.state.messageCursor == 3);
    CHECK(fixture.state.messageOffset == 3);

    // The last page is shorter than the window and stops at the first row.
    REQUIRE(message_list::handleEvent(fixture.state, Event::PageUp));
    CHECK(fixture.state.messageCursor == 0);
    CHECK(fixture.state.messageOffset == 0);

    // And back down the same way: the bottom visible row first...
    REQUIRE(message_list::handleEvent(fixture.state, Event::PageDown));
    CHECK(fixture.state.messageCursor == 27);
    CHECK(fixture.state.messageOffset == 0);

    // ...then a page at a time, the bottom row staying on top of the next one.
    REQUIRE(message_list::handleEvent(fixture.state, Event::PageDown));
    CHECK(fixture.state.messageCursor == 54);
    CHECK(fixture.state.messageOffset == 27);

    REQUIRE(message_list::handleEvent(fixture.state, Event::PageDown));
    CHECK(fixture.state.messageCursor == 57);
    CHECK(fixture.state.messageOffset == 30);
}

TEST_CASE("Moving within the list scrolls a row at a time [messagelist][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    fixture.state.height = 24;

    const uint32_t total = fixture.manager.areas()[0].total;
    fixture.lastRead->set(uidAt(fixture, total / 2));
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());
    REQUIRE(message_read::handleEvent(fixture.state, Event::Character('l')));

    const int offset = fixture.state.messageOffset;
    const int rows = fixture.state.messageListRows();

    // Down to the bottom row: the window stays where it is — re-centring on
    // every keystroke would scroll the whole table under the cursor.
    for (int i = fixture.cursorRow(); i < rows - 1; ++i) {
        REQUIRE(message_list::handleEvent(fixture.state, Event::ArrowDown));
    }
    CHECK(fixture.state.messageOffset == offset);
    CHECK(fixture.cursorRow() == rows - 1);

    // Only past it does the list follow, one row for one message.
    REQUIRE(message_list::handleEvent(fixture.state, Event::ArrowDown));
    CHECK(fixture.state.messageOffset == offset + 1);
    CHECK(fixture.cursorRow() == rows - 1);
}

TEST_CASE("The wheel spends a row's worth of notches on a two-line row "
          "[messagelist][squish]") {
    using amberedit::config::MsgFieldKind;
    TempSquishBase base;
    AreaFixture fixture(base.path());
    fixture.state.height = 24;
    // Two lines to the row, whatever the window: the number and the names on
    // one, the subject under them.
    fixture.config.messageListFormatNarrow = {
        {{MsgFieldKind::Number, 4}, {MsgFieldKind::Space, 1}, {MsgFieldKind::From, 0}},
        {{MsgFieldKind::Subject, 0}}};
    fixture.config.messageListFormatWide = fixture.config.messageListFormatNarrow;
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());
    REQUIRE(message_read::handleEvent(fixture.state, Event::Character('l')));
    REQUIRE(static_cast<int>(fixture.state.messageCount) > 20);

    // A clock the test turns itself: a run of the wheel is what it is by how
    // far apart the notches arrive, and no test can flick a real one.
    amberedit::ui::Millis now = 1000;
    fixture.state.monotonicMs = [&now] { return now; };
    const auto notch = [&fixture, &now](bool down, amberedit::ui::Millis after) {
        now += after;
        REQUIRE(message_list::handleEvent(fixture.state, wheel(down)));
    };

    // A flick — six notches in quick succession — moves three messages, the
    // row being two lines tall. Every one of them is handled: the swallowed
    // ones are the list answering the wheel by standing still.
    const int start = fixture.state.messageCursor;
    for (int i = 0; i < 6; ++i) notch(true, 20);
    CHECK(fixture.state.messageCursor == start + 3);

    // Scrolling by hand is not a flick and is not counted: notches further
    // apart than the window each move a message.
    for (int i = 0; i < 4; ++i) notch(true, 500);
    CHECK(fixture.state.messageCursor == start + 7);

    // Turning back moves at once rather than after the row's worth is paid.
    notch(false, 20);
    CHECK(fixture.state.messageCursor == start + 6);

    // And with the throttle off the wheel is a message a notch again, however
    // tall the row stands.
    fixture.config.listWheelThrottle = false;
    const int before = fixture.state.messageCursor;
    for (int i = 0; i < 6; ++i) notch(true, 20);
    CHECK(fixture.state.messageCursor == before + 6);
}

TEST_CASE("A single-line row moves a message a notch [messagelist][squish]") {
    using amberedit::config::MsgFieldKind;
    TempSquishBase base;
    AreaFixture fixture(base.path());
    fixture.state.height = 24;
    // One line to the row: there is nothing for the throttle to count, and it
    // is on all the same.
    fixture.config.messageListFormatNarrow = {
        {{MsgFieldKind::Number, 4}, {MsgFieldKind::Space, 1}, {MsgFieldKind::Subject, 0}}};
    fixture.config.messageListFormatWide = fixture.config.messageListFormatNarrow;
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());
    REQUIRE(message_read::handleEvent(fixture.state, Event::Character('l')));

    amberedit::ui::Millis now = 1000;
    fixture.state.monotonicMs = [&now] { return now; };
    const int start = fixture.state.messageCursor;
    for (int i = 0; i < 6; ++i) {
        now += 20;
        REQUIRE(message_list::handleEvent(fixture.state, wheel(true)));
    }
    CHECK(fixture.state.messageCursor == start + 6);
}

TEST_CASE("The table draws the columns msglist_format asks for "
          "[messagelist][squish]") {
    using amberedit::config::MsgFieldKind;
    TempSquishBase base;
    AreaFixture fixture(base.path());
    fixture.state.height = 24;
    // The scrollbar and the back button both take columns off the table, and
    // neither is what this is about: the row read back here is the format's.
    fixture.config.messageListScrollbar = false;
    fixture.config.backButton = amberedit::config::Visibility::Off;
    fixture.config.messageListFormatNarrow = {{{MsgFieldKind::Number, 4},
                                               {MsgFieldKind::Space, 1},
                                               {MsgFieldKind::From, 8},
                                               {MsgFieldKind::Space, 1},
                                               {MsgFieldKind::Subject, 0}}};
    fixture.config.messageListFormatWide = fixture.config.messageListFormatNarrow;
    fixture.state.width = 30;
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());
    REQUIRE(message_read::handleEvent(fixture.state, Event::Character('l')));

    // The heading follows the format — no To and no Date asked for, neither
    // drawn — and the subject takes what the fixed columns leave, margins on
    // both sides apart.
    const auto rows = rowsOf(fixture);
    CHECK(rows[1] == "    # From     Subject        ");

    // The number column is as wide as it was asked for, and the row lines up
    // under the heading.
    const amberedit::domain::MessageHeader* msg = message_list::headerAt(
        fixture.state, static_cast<uint32_t>(fixture.state.messageOffset + 1));
    REQUIRE(msg != nullptr);
    const int from = columnOf(rows, "From");
    CHECK(from == 1 + 4 + 1);
    CHECK(amberedit::ui::substrByWidth(rows[3], from, 8) ==
          amberedit::ui::padRight(amberedit::ui::truncateToWidth(msg->from, 8), 8));

    // A number column written with no width of its own is as wide as the
    // highest number that can go in it, and everything after it moves over.
    fixture.config.messageListFormatNarrow[0][0] = {MsgFieldKind::Number,
                                                    amberedit::config::kAutoWidth};
    fixture.config.messageListFormatWide = fixture.config.messageListFormatNarrow;
    fixture.state.messageCount = 100500;
    CHECK(columnOf(rowsOf(fixture), "From") == 1 + 6 + 1);
}

TEST_CASE("A Date column too narrow for the stamp drops its trailing parts "
          "[messagelist][squish]") {
    using amberedit::config::MsgFieldKind;
    TempSquishBase base;
    AreaFixture fixture(base.path());
    fixture.state.height = 24;
    // The Date column is what is read back here, and it is the last on the row:
    // without the scrollbar the cell ends at the right margin and what the
    // stamp was cut to can be read off the row.
    fixture.config.messageListScrollbar = false;
    fixture.config.backButton = amberedit::config::Visibility::Off;
    // The subject is what gives the columns up as the window narrows — the
    // Date takes what the stamps come to out of what is left.
    fixture.config.messageListFormatNarrow = {
        {{MsgFieldKind::Number, 3},
         {MsgFieldKind::Space, 1},
         {MsgFieldKind::Subject, 0},
         {MsgFieldKind::Space, 1},
         {MsgFieldKind::Date, amberedit::config::kAutoWidth}}};
    fixture.config.messageListFormatWide = fixture.config.messageListFormatNarrow;
    // The end of the area: the stamp being cut here is the four-part one the
    // newest messages carry, without a zone of their own.
    fixture.lastRead->set(uidAt(fixture, fixture.total()));
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());
    REQUIRE(message_read::handleEvent(fixture.state, Event::Character('l')));

    const amberedit::domain::MessageHeader* msg = message_list::headerAt(
        fixture.state, static_cast<uint32_t>(fixture.state.messageOffset + 1));
    REQUIRE(msg != nullptr);
    // The default format, day to time: "15 Aug 26 20:28". These messages carry
    // no zone of their own, so `%z` writes nothing and the stamp is four parts
    // rather than five.
    const std::string full =
        msg->date.format(fixture.state.config.readerDateTimeFormat, msg->utcOffset);
    REQUIRE(amberedit::ui::displayWidth(full) == 15);

    // Wide enough for the whole stamp, and then narrower and narrower: the time
    // goes first, then the year, then the month, each at a space and never
    // mid-word — a stamp cut mid-word would read as a different date.
    CHECK(dateCell(fixture, 60) == full);
    CHECK(dateCell(fixture, 20) == full.substr(0, 9));
    CHECK(dateCell(fixture, 14) == full.substr(0, 6));
    CHECK(dateCell(fixture, 12) == full.substr(0, 2));
}

TEST_CASE(
    "The width the window has picks which of the two message formats a row "
    "follows "
    "[messagelist][squish]") {
    using amberedit::config::MsgFieldKind;
    TempSquishBase base;
    AreaFixture fixture(base.path());
    fixture.state.height = 24;
    fixture.config.messageListScrollbar = false;
    fixture.config.backButton = amberedit::config::Visibility::Off;
    fixture.config.adaptiveUiThreshold = 40;
    // Who a message is to is what a narrow window goes without: it is the
    // column that says least — "All" row after row in an echo — and the subject
    // is what the columns are better spent on where they are short. Which is
    // now a thing the format says rather than a thing the screen knows.
    fixture.config.messageListFormatNarrow = {{{MsgFieldKind::Number, 3},
                                               {MsgFieldKind::Space, 1},
                                               {MsgFieldKind::From, 8},
                                               {MsgFieldKind::Space, 1},
                                               {MsgFieldKind::Subject, 0}}};
    fixture.config.messageListFormatWide = {{{MsgFieldKind::Number, 3},
                                             {MsgFieldKind::Space, 1},
                                             {MsgFieldKind::From, 8},
                                             {MsgFieldKind::Space, 1},
                                             {MsgFieldKind::To, 8},
                                             {MsgFieldKind::Space, 1},
                                             {MsgFieldKind::Subject, 0}}};
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());
    REQUIRE(message_read::handleEvent(fixture.state, Event::Character('l')));

    // At the threshold and over, the wide format: all four columns, and the
    // subject starts past the To.
    fixture.state.width = 40;
    auto wide = rowsOf(fixture);
    CHECK(wide[1].find("To") != std::string::npos);
    const int wideSubject = columnOf(wide, "Subject");
    CHECK(wideSubject == 1 + 3 + 1 + 8 + 1 + 8 + 1);

    // A column short of it, the narrow one: the To column goes and the subject
    // takes what it and its gap were spending.
    fixture.state.width = 39;
    auto narrow = rowsOf(fixture);
    CHECK(narrow[1].find("To") == std::string::npos);
    CHECK(narrow[1].find("From") != std::string::npos);
    const int narrowSubject = columnOf(narrow, "Subject");
    CHECK(narrowSubject == 1 + 3 + 1 + 8 + 1);

    // The rows line up under the headings at either width, which is what says
    // the whole column moved and not the heading alone.
    const amberedit::domain::MessageHeader* msg = message_list::headerAt(
        fixture.state, static_cast<uint32_t>(fixture.state.messageOffset + 1));
    REQUIRE(msg != nullptr);
    REQUIRE_FALSE(msg->subject.empty());
    const int width = amberedit::ui::displayWidth(msg->subject);
    CHECK(amberedit::ui::substrByWidth(wide[3], wideSubject, width) == msg->subject);
    CHECK(amberedit::ui::substrByWidth(narrow[3], narrowSubject, width) == msg->subject);

    // And the threshold is what the two cross at, not a literal eighty.
    fixture.config.adaptiveUiThreshold = 120;
    CHECK(rowsOf(fixture)[1].find("To") == std::string::npos);
    fixture.state.width = 120;
    CHECK(rowsOf(fixture)[1].find("To") != std::string::npos);
}

TEST_CASE("A message format written on several lines draws a message on all of them "
          "[messagelist][squish]") {
    using amberedit::config::MsgFieldKind;
    TempSquishBase base;
    AreaFixture fixture(base.path());
    fixture.config.messageListScrollbar = false;
    fixture.config.backButton = amberedit::config::Visibility::Off;
    // The names on one line and the subject under them — what a narrow window
    // has no columns to put side by side.
    fixture.config.messageListFormatNarrow = {
        {{MsgFieldKind::Number, 3}, {MsgFieldKind::Space, 1}, {MsgFieldKind::From, 0}},
        {{MsgFieldKind::Subject, 0}}};
    fixture.config.messageListFormatWide = fixture.config.messageListFormatNarrow;
    fixture.state.width = 30;
    fixture.state.height = 10;  // seven lines: three whole rows, and one over
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());
    REQUIRE(message_read::handleEvent(fixture.state, Event::Character('l')));

    const auto rows = rowsOf(fixture);
    const int first = fixture.state.messageOffset + 1;
    const amberedit::domain::MessageHeader* msg =
        message_list::headerAt(fixture.state, static_cast<uint32_t>(first));
    REQUIRE(msg != nullptr);

    // One heading row however tall a row is, over the line the row is read from
    // first.
    CHECK(rows[1].substr(0, 5) == "   # ");
    // The message's own two lines, in the order the format writes them.
    CHECK(amberedit::ui::substrByWidth(rows[3], 1, 3) ==
          amberedit::ui::padLeft(std::to_string(first), 3));
    CHECK(amberedit::ui::substrByWidth(
              rows[4], 1, amberedit::ui::displayWidth(msg->subject)) == msg->subject);
    // Three whole rows of two lines, and the line under them left blank: a row
    // is drawn whole or not at all.
    CHECK(fixture.state.messageListItems() == 3);
    CHECK(rows[9] == std::string(30, ' '));
}

TEST_CASE("A click on any line of a message row is a click on that message "
          "[messagelist][squish]") {
    using amberedit::config::MsgFieldKind;
    TempSquishBase base;
    AreaFixture fixture(base.path());
    fixture.config.messageListFormatNarrow = {
        {{MsgFieldKind::Number, 3}, {MsgFieldKind::Space, 1}, {MsgFieldKind::From, 0}},
        {{MsgFieldKind::Subject, 0}}};
    fixture.config.messageListFormatWide = fixture.config.messageListFormatNarrow;
    fixture.state.width = 40;
    fixture.state.height = 24;
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());
    REQUIRE(message_read::handleEvent(fixture.state, Event::Character('l')));

    const auto clickAt = [](int y) {
        term::MouseEvent mouse;
        mouse.x = 2;
        mouse.y = y;
        mouse.button = term::MouseEvent::Button::Left;
        mouse.motion = term::MouseEvent::Motion::Pressed;
        return Event::Mouse(mouse);
    };

    // The second line of the second row is the second message on the screen,
    // not the fourth: the subject under a name is the same message as the name.
    const int offset = fixture.state.messageOffset;
    REQUIRE(message_list::handleEvent(fixture.state, clickAt(3 + 3)));
    CHECK(fixture.state.messageCursor == offset + 1);
}

TEST_CASE("The selected message keeps its place when a row changes height "
          "[messagelist][squish]") {
    using amberedit::config::MsgFieldKind;
    TempSquishBase base;
    AreaFixture fixture(base.path());
    fixture.config.messageListScrollbar = false;
    fixture.config.adaptiveUiThreshold = 40;
    // Two lines to a row in a narrow window and one in a wide one, which is
    // what the defaults do: a screen of twelve lines holds six messages or
    // twelve.
    fixture.config.messageListFormatNarrow = {{{MsgFieldKind::From, 0}},
                                              {{MsgFieldKind::Subject, 0}}};
    fixture.config.messageListFormatWide = {{{MsgFieldKind::From, 0}}};
    fixture.state.height = 15;  // twelve lines for the list
    REQUIRE(fixture.total() > 20);
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());
    REQUIRE(message_read::handleEvent(fixture.state, Event::Character('l')));

    // Narrow first, with the cursor on the bottom row of the six.
    fixture.state.width = 39;
    fixture.state.messageCursor = 10;
    fixture.state.messageOffset = 5;
    rowsOf(fixture);
    REQUIRE(fixture.state.messageOffset == 5);

    // Widened: the rows are half as tall, so twelve messages stand where six
    // did. The cursor was on the eleventh line of the list and it stays there —
    // the messages above it are counted again rather than kept.
    fixture.state.width = 40;
    rowsOf(fixture);
    CHECK(fixture.state.messageCursor == 10);
    CHECK(fixture.state.messageOffset == 0);

    // And back: the same line again, which is the offset it started from.
    fixture.state.width = 39;
    rowsOf(fixture);
    CHECK(fixture.state.messageCursor == 10);
    CHECK(fixture.state.messageOffset == 5);
}

TEST_CASE("The message list draws the scrollbar where the area does not fit "
          "[messagelist][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    fixture.state.width = 80;
    fixture.state.height = 24;  // twenty-one rows for more messages than that
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());
    REQUIRE(message_read::handleEvent(fixture.state, Event::Character('l')));
    REQUIRE(static_cast<int>(fixture.state.messageCount) >
            fixture.state.messageListRows());

    // The bar stands beside the rows alone: the title, the headings and the rule
    // above them span the whole width, the rule drawn right across it.
    const int listRows = fixture.state.messageListRows();
    auto rows = rowsOf(fixture);
    CHECK(barShape(rows, 3, listRows).find('?') == std::string::npos);
    CHECK(lastGlyph(rows[2]) == "─");

    // The thumb says where in the area the rows stand. Home puts it at the top
    // of the bar and End at the bottom, the track filling the rest either way.
    REQUIRE(message_list::handleEvent(fixture.state, Event::Home));
    const std::string atTop = barShape(rowsOf(fixture), 3, listRows);
    CHECK(atTop.front() == '#');
    CHECK(atTop.back() == '|');
    REQUIRE(message_list::handleEvent(fixture.state, Event::End));
    const std::string atEnd = barShape(rowsOf(fixture), 3, listRows);
    CHECK(atEnd.front() == '|');
    CHECK(atEnd.back() == '#');

    // The rows have that column less to lay out in, so every column of the table
    // stands one further left than it does with the bar turned off.
    const int withBar = columnOf(rowsOf(fixture), "Date");
    fixture.config.messageListScrollbar = false;
    rows = rowsOf(fixture);
    CHECK(columnOf(rows, "Date") == withBar + 1);
    CHECK(barShape(rows, 3, listRows) == std::string(static_cast<size_t>(listRows), ' '));
}

TEST_CASE("The message list's scrollbar beside tall rows counts messages, not lines "
          "[messagelist][squish]") {
    using amberedit::config::MsgFieldKind;
    TempSquishBase base;
    AreaFixture fixture(base.path());
    fixture.config.messageListFormatNarrow = {{{MsgFieldKind::From, 0}},
                                              {{MsgFieldKind::Subject, 0}}};
    fixture.config.messageListFormatWide = fixture.config.messageListFormatNarrow;
    fixture.state.width = 40;
    fixture.state.height = 9;  // six lines: three messages of two lines each
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());
    REQUIRE(message_read::handleEvent(fixture.state, Event::Character('l')));
    REQUIRE(fixture.state.messageListItems() == 3);
    REQUIRE(static_cast<int>(fixture.state.messageCount) > 3);

    // The thumb points at one of the three messages on the screen and covers
    // both the lines that message is drawn on rather than one of them.
    REQUIRE(message_list::handleEvent(fixture.state, Event::Home));
    CHECK(barShape(rowsOf(fixture), 3, 6) == "##||||");
    REQUIRE(message_list::handleEvent(fixture.state, Event::End));
    CHECK(barShape(rowsOf(fixture), 3, 6) == "||||##");
}

TEST_CASE("The message list draws no scrollbar for an area that fits "
          "[messagelist][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    fixture.state.width = 80;
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());
    REQUIRE(message_read::handleEvent(fixture.state, Event::Character('l')));
    // A window tall enough for every message in the base and then some.
    fixture.state.height = static_cast<int>(fixture.state.messageCount) + 10;
    REQUIRE(static_cast<int>(fixture.state.messageCount) <
            fixture.state.messageListRows());

    // Nothing to scroll, so nothing is drawn and the rows keep the column.
    const int listRows = fixture.state.messageListRows();
    CHECK(barShape(rowsOf(fixture), 3, listRows) ==
          std::string(static_cast<size_t>(listRows), ' '));
}

TEST_CASE("The message list paints a message nobody has read yet "
          "[messagelist][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    fixture.state.height = 24;

    const uint32_t total = fixture.manager.areas()[0].total;
    REQUIRE(total > 6);
    // A base as a tosser leaves one: nothing in it has been read, so every row
    // of the list starts out unread.
    fixture.lastRead->set(uidAt(fixture, total / 2));
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());

    // Entering the area drops into the reader, and that is what marks a message
    // read — whichever one it landed on, the cursor is on it. The cursor is a
    // position and the row is a number, so the message read is `read + 1`.
    const int read = fixture.state.messageCursor + 1;
    REQUIRE(read > 1);
    // The cursor goes off it, so that the row is not the current one: the
    // selection paints the whole row and would answer for whatever is under it.
    fixture.state.messageCursor = read - 2;
    message_list::centerCursor(fixture.state);
    message_list::ensureHeaders(fixture.state);

    CHECK(rowColor(fixture, read) != theme::palette.msglistUnread);
    CHECK(rowColor(fixture, read + 1) == theme::palette.msglistUnread);

    // Reading the next one marks it too, and its row stops standing out — the
    // list's window is a copy of the headers, and it has to be told.
    message_read::loadMessage(fixture.state, static_cast<uint32_t>(read + 1));
    CHECK(rowColor(fixture, read + 1) != theme::palette.msglistUnread);
}

TEST_CASE("highlight_unread off leaves the message list as it was "
          "[messagelist][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    fixture.state.height = 24;
    fixture.config.highlightUnread = false;

    const uint32_t total = fixture.manager.areas()[0].total;
    REQUIRE(total > 6);
    fixture.lastRead->set(uidAt(fixture, total / 2));
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());

    const int read = fixture.state.messageCursor + 1;
    REQUIRE(read > 1);
    fixture.state.messageCursor = read - 2;
    message_list::centerCursor(fixture.state);
    message_list::ensureHeaders(fixture.state);

    // The row after it is unread and drawn no differently for it.
    CHECK(rowColor(fixture, read + 1) != theme::palette.msglistUnread);

    // The mark is still made, though: it belongs to the message rather than to
    // this screen, and another reader goes by it.
    const amberedit::domain::MessageHeader* msg =
        message_list::headerAt(fixture.state, static_cast<uint32_t>(read));
    REQUIRE(msg != nullptr);
    CHECK(msg->seen);
    const amberedit::domain::MessageHeader* next =
        message_list::headerAt(fixture.state, static_cast<uint32_t>(read + 1));
    REQUIRE(next != nullptr);
    CHECK_FALSE(next->seen);
}

TEST_CASE("A name of the user's own keeps its color on an unread row "
          "[messagelist][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    fixture.state.height = 24;
    fixture.state.width = 100;

    const uint32_t total = fixture.manager.areas()[0].total;
    REQUIRE(total > 6);
    fixture.lastRead->set(uidAt(fixture, total / 2));
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());

    // Off the message the reader opened on and off its neighbour, so the row
    // under test is neither read nor the current one.
    const int unread = fixture.state.messageCursor + 2;
    fixture.state.messageCursor = unread - 3;
    message_list::centerCursor(fixture.state);
    message_list::ensureHeaders(fixture.state);

    const amberedit::domain::MessageHeader* msg =
        message_list::headerAt(fixture.state, static_cast<uint32_t>(unread));
    REQUIRE(msg != nullptr);
    REQUIRE_FALSE(msg->seen);
    REQUIRE_FALSE(msg->from.empty());
    // The user is whoever wrote that message, so its From is a name of one's
    // own — set on the area's config, which is what isOwnName() reads.
    fixture.state.areaConfig.userName = msg->from;

    const auto rows = rowsOf(fixture);
    const int from = columnOf(rows, "From");
    CHECK(cellColor(fixture, unread, from) == theme::palette.ownName);
    // And the rest of the row still says the message is unread.
    CHECK(rowColor(fixture, unread) == theme::palette.msglistUnread);
    // The subject is quiet on every row, read or not: it is prose rather than a
    // fact about the message, and it keeps its own color over the row's exactly
    // as a name of the user's own does.
    CHECK(cellColor(fixture, unread, columnOf(rows, "Subject")) == theme::palette.dimmed);

    // With the name no longer the user's, that cell goes back to the row's.
    fixture.state.areaConfig.userName = "Nobody At All";
    CHECK(cellColor(fixture, unread, from) == theme::palette.msglistUnread);
}
