#include <catch2/catch.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include "domain/message.hpp"
#include "nodelist/nodelist_writer.hpp"
#include "temp_dir.hpp"
#include "temp_squish_base.hpp"
#include "ui/area_fixture.hpp"
#include "ui/menu_button.hpp"
#include "ui/menu_dialog.hpp"
#include "ui/screens/message_list_screen.hpp"
#include "ui/screens/message_read_screen.hpp"
#include "ui/term/element.hpp"
#include "ui/term/event.hpp"
#include "ui/term/screen.hpp"
#include "ui/theme.hpp"

using amberedit::app::ScreenId;
using amberedit::config::MenuCommand;
using amberedit::config::Visibility;
using amberedit::test::AreaFixture;
using amberedit::test::TempSquishBase;
using amberedit::test::uidAt;
using amberedit::ui::AppState;
using amberedit::ui::term::Event;

namespace message_list = amberedit::ui::screens::message_list;
namespace menu_dialog = amberedit::ui::menu_dialog;
namespace message_read = amberedit::ui::screens::message_read;
namespace term = amberedit::ui::term;

namespace {

/// Takes every message out of the area, which is how the reader is opened on
/// nothing at all — the screen a first message is written from.
void emptyTheArea(AreaFixture& fixture) {
    amberedit::ports::IMsgBase* base = fixture.manager.openArea(fixture.area);
    REQUIRE(base != nullptr);
    while (base->count() > 0) REQUIRE(base->remove(1));
    fixture.manager.closeCurrentArea();
    fixture.manager.reload();
}

/// Draws the reader into a buffer of its own size, which is what fills the
/// thread markers in: they are built while the frame is laid out, and where
/// each landed is what a click is tested against.
void drawFrame(AreaFixture& fixture) {
    term::Screen screen(fixture.state.width, fixture.state.height);
    term::render(screen, message_read::render(fixture.state));
}

/// Puts the reader's menu up and lays it out over the screen it opened from,
/// which is what fills each button's box in.
void openMenu(AreaFixture& fixture) {
    message_read::openMenu(fixture.state);
    REQUIRE(fixture.state.menuView);
    term::Screen screen(fixture.state.width, fixture.state.height);
    term::render(screen,
                 menu_dialog::render(fixture.state, message_read::render(fixture.state)));
}

/// The rows of a frame, as text, so that a layout can be read back the way it
/// reaches the terminal.
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

/// Whether `row` begins with `prefix`. C++17, so no starts_with.
bool startsWith(const std::string& row, const std::string& prefix) {
    return row.compare(0, prefix.size(), prefix) == 0;
}

/// Where the button running `command` was drawn, or nothing where the menu does
/// not hold it.
const AppState::MenuView::Item* buttonFor(const AreaFixture& fixture,
                                          MenuCommand command) {
    if (!fixture.state.menuView) return nullptr;
    for (const auto& item : fixture.state.menuView->items) {
        if (item.command == command) return &item;
    }
    return nullptr;
}

/// A left-button press in the middle of a button.
Event pressAt(int x, int y) {
    term::MouseEvent mouse;
    mouse.x = x;
    mouse.y = y;
    mouse.button = term::MouseEvent::Button::Left;
    mouse.motion = term::MouseEvent::Motion::Pressed;
    return Event::Mouse(mouse);
}

Event pressOn(const AppState::MenuView::Item& item) {
    return pressAt((item.box.x_min + item.box.x_max) / 2,
                   (item.box.y_min + item.box.y_max) / 2);
}

}  // namespace

TEST_CASE("→ on the last message leaves the area", "[messageread][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());

    const uint32_t total = fixture.total();
    // The UID of the last message, read before the area is entered: it is what
    // the mark should still hold afterwards.
    const uint32_t lastUid = uidAt(fixture, total);
    // A mark on the newest message has nothing after it to move to, so the area
    // opens there — the end the right arrow walks off.
    fixture.lastRead->set(lastUid);
    REQUIRE(message_list::enterArea(fixture.state, fixture.area));
    REQUIRE(fixture.state.messageCursor == static_cast<int>(total) - 1);

    REQUIRE(message_read::handleEvent(fixture.state, Event::ArrowRight));

    CHECK(fixture.state.navigator.current() == ScreenId::AreaList);
    CHECK(fixture.state.base == nullptr);
    CHECK_FALSE(fixture.state.readHeader.has_value());
    // Leaving marks nothing further read: the mark is on the last message the
    // reader actually showed, not past the end of the area.
    CHECK(fixture.lastRead->getLastRead(fixture.area) == lastUid);
    // The key was held down to get here, and on the area list it opens an area.
    CHECK(fixture.state.discardTypeahead);
}

TEST_CASE("← on the first message leaves the area", "[messageread][squish]") {
    TempSquishBase base;
    // A mark on the first message opens the area on the second one by default,
    // so the setting that does that is off here: what is being tested is the
    // key on the first message, which is where the mark then puts the reader.
    amberedit::config::AppConfig config;
    config.lastreadAutoNext = false;
    AreaFixture fixture(base.path(), config);

    fixture.lastRead->set(uidAt(fixture, 1));
    REQUIRE(message_list::enterArea(fixture.state, fixture.area));
    REQUIRE(fixture.state.messageCursor == 0);

    REQUIRE(message_read::handleEvent(fixture.state, Event::ArrowLeft));

    CHECK(fixture.state.navigator.current() == ScreenId::AreaList);
    CHECK(fixture.state.base == nullptr);
}

TEST_CASE("Walking off the front of an area leaves it unread whole",
          "[messageread][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    const uint32_t total = fixture.total();
    REQUIRE(total >= 2);

    // Back through the area a message at a time, from the newest to the first —
    // every one of them read on the way — and then one ← more, off the front.
    // The mark on the newest message is what opens the area at that end.
    fixture.lastRead->set(uidAt(fixture, total));
    REQUIRE(message_list::enterArea(fixture.state, fixture.area));
    REQUIRE(fixture.state.messageCursor == static_cast<int>(total) - 1);
    while (fixture.state.messageCursor > 0) {
        REQUIRE(message_read::handleEvent(fixture.state, Event::ArrowLeft));
    }
    REQUIRE(fixture.state.navigator.current() == ScreenId::MessageRead);
    REQUIRE(message_read::handleEvent(fixture.state, Event::ArrowLeft));

    CHECK(fixture.state.navigator.current() == ScreenId::AreaList);
    // Asking for the message before the first one puts the reader before the
    // area rather than in it, and there is no message there to mark.
    CHECK(fixture.lastRead->getLastRead(fixture.area) == 0);
    // Which is what the area list shows: as many unread as the area holds.
    CHECK(fixture.manager.areas()[0].total == total);
    CHECK(fixture.manager.areas()[0].unread == total);
}

TEST_CASE("Coming back into an area walked off the front of starts it over",
          "[messageread][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    const uint32_t total = fixture.total();
    REQUIRE(total >= 2);

    // Nothing read here, so the area opens at its front to begin with.
    REQUIRE(message_list::enterArea(fixture.state, fixture.area));
    REQUIRE(fixture.state.messageCursor == 0);

    // ← off the front and straight back in: the mark was taken off, and the
    // area opens where the reading stopped rather than at its newest message.
    REQUIRE(message_read::handleEvent(fixture.state, Event::ArrowLeft));
    REQUIRE(fixture.state.navigator.current() == ScreenId::AreaList);
    REQUIRE(message_list::enterArea(fixture.state, fixture.area));

    CHECK(fixture.state.messageCursor == 0);
    REQUIRE(fixture.state.readHeader.has_value());
    CHECK(fixture.state.readHeader->number == 1);
}

TEST_CASE("Esc on the first message leaves the mark on it", "[messageread][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    const uint32_t total = fixture.total();
    REQUIRE(total >= 2);

    // The same walk back through the area as above, and out of the same first
    // message.
    fixture.lastRead->set(uidAt(fixture, total));
    REQUIRE(message_list::enterArea(fixture.state, fixture.area));
    while (fixture.state.messageCursor > 0) {
        REQUIRE(message_read::handleEvent(fixture.state, Event::ArrowLeft));
    }
    // The other way out of it. Esc says nothing about the message
    // before this one, so the reading stands as it was done.
    REQUIRE(message_read::handleEvent(fixture.state, Event::Escape));

    CHECK(fixture.state.navigator.current() == ScreenId::AreaList);
    CHECK(fixture.lastRead->getLastRead(fixture.area) == uidAt(fixture, 1));
    CHECK(fixture.manager.areas()[0].unread == total - 1);
}

TEST_CASE("An area of one message is both its first and its last",
          "[messageread][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    REQUIRE(message_list::enterArea(fixture.state, fixture.area));
    // One message, and the reader on it: either arrow has nowhere to go.
    fixture.state.messageCount = 1;
    fixture.state.messageCursor = 0;

    const Event key = GENERATE(Event::ArrowLeft, Event::ArrowRight);
    REQUIRE(message_read::handleEvent(fixture.state, key));

    CHECK(fixture.state.navigator.current() == ScreenId::AreaList);
}

TEST_CASE("An empty area is left by either arrow too", "[messageread][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    emptyTheArea(fixture);
    REQUIRE(message_list::enterArea(fixture.state, fixture.area));
    REQUIRE(fixture.state.messageCount == 0);
    REQUIRE(fixture.state.navigator.current() == ScreenId::MessageRead);

    // There is no message in either direction, so both keys walk off an end.
    // `e` is still how a first message is written, up until one of them is
    // pressed.
    const Event key = GENERATE(Event::ArrowLeft, Event::ArrowRight);
    REQUIRE(message_read::handleEvent(fixture.state, key));

    CHECK(fixture.state.navigator.current() == ScreenId::AreaList);
}

TEST_CASE("With reader_edge_exit off the ends of an area are a dead end",
          "[messageread][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    fixture.config.edgeExit = false;

    const uint32_t total = fixture.total();
    // The far end of the area, which is where the right arrow has nowhere left
    // to go: the mark on the newest message opens the reader on it.
    fixture.lastRead->set(uidAt(fixture, total));
    REQUIRE(message_list::enterArea(fixture.state, fixture.area));
    REQUIRE(fixture.state.messageCursor == static_cast<int>(total) - 1);

    // The key is still swallowed — it means "next message", and there is none.
    REQUIRE(message_read::handleEvent(fixture.state, Event::ArrowRight));

    CHECK(fixture.state.navigator.current() == ScreenId::MessageRead);
    CHECK(fixture.state.messageCursor == static_cast<int>(total) - 1);
    CHECK(fixture.state.base != nullptr);
    // Nothing moved, so nothing typed after it is aimed anywhere else.
    CHECK_FALSE(fixture.state.discardTypeahead);
    REQUIRE(fixture.state.readHeader.has_value());
    CHECK(fixture.state.readHeader->number == total);
}

TEST_CASE("The header gives each stamp a row of its own",
          "[messageread][header][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    // The title row is not what is under test.
    fixture.config.backButton = Visibility::Off;
    fixture.config.showRecdDate = Visibility::On;
    fixture.state.width = 92;
    REQUIRE(message_list::enterArea(fixture.state, fixture.area));

    const std::vector<std::string> rows = rowsOf(fixture);
    CHECK(startsWith(rows[2], " From : "));
    CHECK(startsWith(rows[3], " To   : "));
    CHECK(startsWith(rows[4], " Subj : "));
    CHECK(startsWith(rows[5], " Date : "));
    CHECK(startsWith(rows[6], " Recd : "));
    // When the message was written on the one row and when it arrived on the
    // other, each in the column the names stand in above, and nothing standing
    // between them: the label is what says which stamp a row carries.
    REQUIRE(fixture.state.readHeader.has_value());
    const std::string written =
        fixture.state.readHeader->date.format(fixture.config.readerDateTimeFormat);
    const std::string arrived =
        fixture.state.readHeader->arrivalDate.format(fixture.config.readerDateTimeFormat);
    REQUIRE_FALSE(arrived.empty());
    // Right after the label column, which is eight wide.
    CHECK(rows[5].find(written) == 8);
    CHECK(rows[6].find(arrived) == 8);
    CHECK(rows[5].find(" | ") == std::string::npos);
}

TEST_CASE("A stamp widens its column rather than being cut at a name's width",
          "[messageread][header][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    fixture.config.backButton = Visibility::Off;
    // A format free enough with its words to outgrow the name field FTS-0001
    // fixes — which is what the column would otherwise stop at, cutting the
    // stamp at every width rather than only in a narrow window.
    fixture.config.readerDateTimeFormat = "%A, the %d of %B %Y, %H:%M %z";
    fixture.state.width = 100;
    REQUIRE(message_list::enterArea(fixture.state, fixture.area));
    REQUIRE(fixture.state.readHeader.has_value());

    fixture.state.readHeader->utcOffset = "+0300";
    const std::string written = fixture.state.readHeader->date.format(
        fixture.config.readerDateTimeFormat, "+0300");
    // Wider than the 36 columns a name may take, which is where the column
    // used to stop.
    REQUIRE(written.size() > 36);

    const std::vector<std::string> rows = rowsOf(fixture);
    CHECK(rows[5].find(written) == 8);
    // The column beside it moves with it rather than being written over: the
    // address on the From row begins past the stamp, so the block still lines
    // up down its two columns.
    const std::string address = fixture.state.readHeader->origAddr.toString();
    REQUIRE_FALSE(address.empty());
    CHECK(rows[2].find(address) >= 8 + written.size());
}

TEST_CASE("%z writes the zone the message states, and only on the row it dates",
          "[messageread][header][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    fixture.config.backButton = Visibility::Off;
    fixture.config.showRecdDate = Visibility::On;
    fixture.state.width = 92;
    fixture.config.readerDateTimeFormat = "%H:%M %z";
    REQUIRE(message_list::enterArea(fixture.state, fixture.area));
    REQUIRE(fixture.state.readHeader.has_value());

    // What the message's own TZUTC says. Whether the message in the base
    // carries one is not what is under test — that the row uses it is.
    fixture.state.readHeader->utcOffset = "+0300";
    const std::string written = fixture.state.readHeader->date.format("%H:%M");
    const std::string arrived = fixture.state.readHeader->arrivalDate.format("%H:%M");
    REQUIRE_FALSE(arrived.empty());

    const std::vector<std::string> rows = rowsOf(fixture);
    // The offset stands on the row of the stamp it dates and on no other: TZUTC
    // says which clock the message was written by, and when it arrived here was
    // read off this system's, which the message has nothing to say about.
    CHECK(rows[5].find(written + " +0300") == 8);
    CHECK(rows[6].find(arrived) == 8);
    CHECK(rows[6].find("+0300") == std::string::npos);
}

TEST_CASE("show_recd_date is what puts the Recd row up, and it is off by default",
          "[messageread][header][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    fixture.config.backButton = Visibility::Off;
    fixture.state.width = 92;
    REQUIRE(message_list::enterArea(fixture.state, fixture.area));

    // Off unless it is asked for: the row is a fifth of the header block and a
    // row off the message under it.
    CHECK(fixture.config.showRecdDate == Visibility::Off);
    std::vector<std::string> rows = rowsOf(fixture);
    CHECK(startsWith(rows[5], " Date : "));
    CHECK_FALSE(startsWith(rows[6], " Recd : "));
    // The rule closing the block off stands where the row would have been, so
    // the body begins a row higher.
    const int bodyRows = fixture.state.readRows();

    fixture.config.showRecdDate = Visibility::On;
    rows = rowsOf(fixture);
    CHECK(startsWith(rows[6], " Recd : "));
    CHECK(fixture.state.readRows() == bodyRows - 1);
}

TEST_CASE("show_recd_date when_wide follows the width of the window",
          "[messageread][header][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    fixture.config.backButton = Visibility::Off;
    fixture.config.showRecdDate = Visibility::WhenWide;
    fixture.config.adaptiveUiThreshold = 80;

    fixture.state.width = 92;
    REQUIRE(message_list::enterArea(fixture.state, fixture.area));
    CHECK(startsWith(rowsOf(fixture)[6], " Recd : "));

    // Dragged under the threshold, the row goes with the width it was asked for
    // — the same line every other window-led setting is read against.
    fixture.state.width = 70;
    CHECK_FALSE(startsWith(rowsOf(fixture)[6], " Recd : "));

    fixture.config.showRecdDate = Visibility::WhenNarrow;
    CHECK(startsWith(rowsOf(fixture)[6], " Recd : "));
}

TEST_CASE("The Recd row is drawn in the color the Date row is",
          "[messageread][header][squish]") {
    namespace theme = amberedit::ui::theme;

    TempSquishBase base;
    AreaFixture fixture(base.path());
    fixture.config.backButton = Visibility::Off;
    fixture.config.showRecdDate = Visibility::On;
    fixture.state.width = 92;
    REQUIRE(message_list::enterArea(fixture.state, fixture.area));

    term::Screen screen(fixture.state.width, fixture.state.height);
    term::render(screen, message_read::render(fixture.state));

    // The two are a pair of stamps and read as one, so the label and what
    // stands beside it are the block's own color on both rows — the arrival
    // stamp wore the kludges' shade while the two shared a row, and the label
    // is what tells them apart now.
    const std::string arrived =
        fixture.state.readHeader->arrivalDate.format(fixture.config.readerDateTimeFormat);
    REQUIRE_FALSE(arrived.empty());
    const int last = 8 + static_cast<int>(arrived.size());
    for (int x = 0; x < last; ++x) {
        CHECK(screen.at(x, 6).fg == theme::palette.header);
        CHECK(screen.at(x, 6).fg == screen.at(x, 5).fg);
    }
}

TEST_CASE("A message that never arrived keeps the Recd row and leaves it blank",
          "[messageread][header][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    fixture.config.backButton = Visibility::Off;
    fixture.config.showRecdDate = Visibility::On;
    fixture.state.width = 92;
    REQUIRE(message_list::enterArea(fixture.state, fixture.area));
    REQUIRE(fixture.state.readHeader.has_value());

    // A message written here arrived from nowhere. The row stays: a block that
    // grew and shrank from one message to the next would walk the body up and
    // down the window while reading through an area.
    fixture.state.readHeader->arrivalDate = {};
    const std::vector<std::string> rows = rowsOf(fixture);
    CHECK(startsWith(rows[6], " Recd : "));
    CHECK(rows[6].find_first_not_of(' ', 8) == std::string::npos);
}

TEST_CASE("The Subj row runs to the edge of the block rather than to the columns",
          "[messageread][header][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    fixture.config.backButton = Visibility::Off;
    fixture.state.width = 92;
    REQUIRE(message_list::enterArea(fixture.state, fixture.area));
    REQUIRE(fixture.state.readHeader.has_value());

    // Longer than the name column the From and To rows stop at, and shorter
    // than what the window has room for beside the label: the subject the base
    // holds at most is 71 characters, and a wide window shows all of it.
    const std::string subject(71, 'x');
    fixture.state.readHeader->subject = subject;

    const std::vector<std::string> rows = rowsOf(fixture);
    REQUIRE(startsWith(rows[4], " Subj : "));
    CHECK(rows[4].find(subject) == 8);
}

TEST_CASE("A window too narrow for both stamps keeps the one that dates the message",
          "[messageread][header][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    fixture.config.backButton = Visibility::Off;
    fixture.config.menuButton = Visibility::Off;
    // Long enough that the pair cannot fit the name column at any width, so
    // the arrival stamp is what has to go.
    fixture.config.readerDateTimeFormat = "%A %d %B %Y %H:%M:%S";
    REQUIRE(message_list::enterArea(fixture.state, fixture.area));

    REQUIRE(fixture.state.readHeader.has_value());
    const std::string written =
        fixture.state.readHeader->date.format(fixture.config.readerDateTimeFormat);
    const std::string arrived =
        fixture.state.readHeader->arrivalDate.format(fixture.config.readerDateTimeFormat);
    REQUIRE_FALSE(arrived.empty());

    const std::vector<std::string> rows = rowsOf(fixture);
    CHECK(startsWith(rows[5], " Date : "));
    CHECK(rows[5].find(" | ") == std::string::npos);
    // Truncated to the column rather than dropped: the date is what the row is
    // for, and it is the arrival stamp that gives way.
    CHECK(rows[5].find(written.substr(0, 8)) != std::string::npos);
}

TEST_CASE("The subject runs the width of the header block and the attributes close it",
          "[messageread][header][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    fixture.config.backButton = Visibility::Off;
    fixture.state.width = 92;
    REQUIRE(message_list::enterArea(fixture.state, fixture.area));

    REQUIRE(fixture.state.readHeader.has_value());
    REQUIRE(fixture.state.readHeader->origAddr.isValid());
    // Longer than the name column, which stops at 36: a subject has nothing
    // lining up under it, so it runs on into the column the addresses hold.
    const std::string subject(40, 'x');
    fixture.state.readHeader->subject = subject;
    fixture.state.readHeader->attributes = amberedit::domain::attr::kPrivate;

    const std::vector<std::string> rows = rowsOf(fixture);
    CHECK(rows[4].find(subject) == 8);
    // The attributes stand where the addresses do, on the Date row.
    const size_t address = rows[2].find(fixture.state.readHeader->origAddr.toString());
    REQUIRE(address != std::string::npos);
    CHECK(rows[5].find("[Pvt]") == address);
}

TEST_CASE("The header block keeps its four rows at any width",
          "[messageread][header][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    fixture.config.backButton = Visibility::Off;
    fixture.config.menuButton = Visibility::Off;
    REQUIRE(message_list::enterArea(fixture.state, fixture.area));

    // Eighty columns is the line the rest of the interface adapts on, and the
    // header block does not: the stamps keep their own row either side of it.
    const int wide = fixture.config.adaptiveUiThreshold;
    for (const int width : {wide + 40, wide, wide - 1, 40}) {
        fixture.state.width = width;
        CHECK(startsWith(rowsOf(fixture)[5], " Date : "));
        CHECK(fixture.state.readRows() == fixture.state.height - 7);
    }
}

TEST_CASE("An empty area shows none of the thread the area before it had",
          "[messageread][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    emptyTheArea(fixture);

    // What the message last read left behind: its markers name messages of the
    // area it was in, and none of them is in this one.
    fixture.state.readThread.replyTo = 19;
    fixture.state.readThread.replies = {21};

    REQUIRE(message_list::enterArea(fixture.state, fixture.area));
    REQUIRE(fixture.state.messageCount == 0);

    CHECK(fixture.state.readThread.empty());
    const std::string title = rowsOf(fixture)[0];
    CHECK(title.find("empty") != std::string::npos);
    CHECK(title.find("-19") == std::string::npos);
    CHECK(title.find("+21") == std::string::npos);
}

TEST_CASE("Deleting the last message of an area takes its thread with it",
          "[messageread][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    REQUIRE(message_list::enterArea(fixture.state, fixture.area));

    // Down to one message, itself an answer to something, and then that one is
    // deleted too: nothing of the thread is left to draw beside the title.
    while (fixture.state.messageCount > 1) message_read::deleteMessage(fixture.state);
    fixture.state.readThread.replyTo = 19;
    message_read::deleteMessage(fixture.state);
    REQUIRE(fixture.state.messageCount == 0);

    CHECK(fixture.state.readThread.empty());
    CHECK(rowsOf(fixture)[0].find("-19") == std::string::npos);
}

TEST_CASE("An empty area leaves the reader's menu with only New and Nodelist live",
          "[messageread][menu][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    emptyTheArea(fixture);
    REQUIRE(message_list::enterArea(fixture.state, fixture.area));
    REQUIRE(fixture.state.messageCount == 0);
    openMenu(fixture);

    // There is no message to answer or to list, and writing the first one is
    // the whole of what the screen is for. The nodelist is about somebody
    // else's system rather than about the message, so it opens either way.
    CHECK_FALSE(buttonFor(fixture, MenuCommand::Reply)->enabled);
    CHECK_FALSE(buttonFor(fixture, MenuCommand::List)->enabled);
    CHECK(buttonFor(fixture, MenuCommand::New)->enabled);
    CHECK(buttonFor(fixture, MenuCommand::Nodelist)->enabled);

    // And the cursor opens on the first of them that can be run rather than on
    // the dead Reply the menu happens to begin with.
    CHECK(menu_dialog::current(fixture.state) == MenuCommand::New);
}

TEST_CASE("The corner opens the menu and a click in it runs the command",
          "[messageread][menu][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    // The window the tests draw into is eighty columns wide, where
    // `when_narrow` leaves the corner to the title; these tests are about the
    // menu, so they ask for it outright.
    fixture.config.menuButton = Visibility::On;
    REQUIRE(message_list::enterArea(fixture.state, fixture.area));
    drawFrame(fixture);

    // The button hangs from the top-right corner, and clicking it is the whole
    // of how the menu is opened with a pointer.
    REQUIRE(
        message_read::handleEvent(fixture.state, pressAt(fixture.state.width - 1, 0)));
    REQUIRE(fixture.state.menuView);

    // What the shell then does with the answer: the box is put away and the
    // command run on the screen it was opened from.
    openMenu(fixture);
    const auto* list = buttonFor(fixture, MenuCommand::List);
    REQUIRE(list != nullptr);
    REQUIRE(menu_dialog::handleEvent(fixture.state, pressOn(*list)) ==
            menu_dialog::Outcome::Picked);
    const MenuCommand picked = menu_dialog::current(fixture.state);
    fixture.state.menuView.reset();
    message_read::runMenuCommand(fixture.state, picked);
    CHECK(fixture.state.navigator.current() == ScreenId::MessageList);
}

TEST_CASE("A click on a dimmed menu button does nothing at all",
          "[messageread][menu][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    emptyTheArea(fixture);
    REQUIRE(message_list::enterArea(fixture.state, fixture.area));
    openMenu(fixture);

    const auto* reply = buttonFor(fixture, MenuCommand::Reply);
    REQUIRE(reply != nullptr);
    REQUIRE_FALSE(reply->enabled);

    // Swallowed rather than passed on, and the menu stays up: it is still a
    // click on the menu, and letting it through to the reader underneath would
    // be worse than doing nothing.
    CHECK(menu_dialog::handleEvent(fixture.state, pressOn(*reply)) ==
          menu_dialog::Outcome::Ignored);
    CHECK(fixture.state.menuView);
}

TEST_CASE("menu_button off leaves the corner to the title",
          "[messageread][menu][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    fixture.config.menuButton = Visibility::Off;
    REQUIRE(message_list::enterArea(fixture.state, fixture.area));

    // The button costs no row either way — it stands in the two the title and
    // the rule under it already take — so what turning it off gives back is the
    // five columns at the end of those two rows.
    const int rows = fixture.state.readRows();
    CHECK_FALSE(fixture.state.readerMenuShown());
    CHECK(rowsOf(fixture)[0].find("≡") == std::string::npos);

    fixture.config.menuButton = Visibility::On;
    CHECK(fixture.state.readerMenuShown());
    CHECK(fixture.state.readRows() == rows);
    CHECK(rowsOf(fixture)[0].find("│ ≡ │") != std::string::npos);

    // A menu with nothing in it is no menu either: there would be nothing in
    // the box the corner opens.
    fixture.config.readerMenu.clear();
    CHECK_FALSE(fixture.state.readerMenuShown());
}

TEST_CASE("menu_button when_narrow follows the width of the window",
          "[messageread][menu][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    REQUIRE(fixture.config.menuButton == Visibility::WhenNarrow);
    REQUIRE(message_list::enterArea(fixture.state, fixture.area));

    // Eighty columns is the line, and the window is measured again on every
    // frame: a wide one has the room to say what a key does elsewhere, and a
    // narrow one is where the corner stays.
    fixture.state.width = fixture.config.adaptiveUiThreshold;
    CHECK_FALSE(fixture.state.readerMenuShown());
    const int wideRows = fixture.state.readRows();

    fixture.state.width = fixture.config.adaptiveUiThreshold - 1;
    CHECK(fixture.state.readerMenuShown());
    // And it costs the body nothing on either side of the line.
    CHECK(fixture.state.readRows() == wideRows);
}

TEST_CASE("when_wide is the same line read from the other side",
          "[messageread][menu][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    fixture.config.menuButton = Visibility::WhenWide;
    fixture.config.backButton = Visibility::WhenWide;
    REQUIRE(message_list::enterArea(fixture.state, fixture.area));

    // At the threshold and over the window is a wide one, which is exactly
    // where `when_narrow` has nothing on the screen.
    fixture.state.width = fixture.config.adaptiveUiThreshold;
    CHECK(fixture.state.readerMenuShown());
    CHECK(fixture.state.backButtonShown());

    fixture.state.width = fixture.config.adaptiveUiThreshold - 1;
    CHECK_FALSE(fixture.state.readerMenuShown());
    CHECK_FALSE(fixture.state.backButtonShown());
}

TEST_CASE("adaptive_ui_threshold moves the width the two cross at",
          "[messageread][menu][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    REQUIRE(fixture.config.menuButton == Visibility::WhenNarrow);
    REQUIRE(fixture.config.backButton == Visibility::WhenNarrow);
    REQUIRE(message_list::enterArea(fixture.state, fixture.area));

    // A hundred columns is a wide window by default, so nothing `when_narrow`
    // is on the screen there...
    fixture.state.width = 100;
    CHECK_FALSE(fixture.state.readerMenuShown());
    CHECK_FALSE(fixture.state.backButtonShown());

    // ...and stating a wider threshold makes the same window a narrow one, for
    // both corners alike: they cross at the one line.
    fixture.config.adaptiveUiThreshold = 120;
    CHECK(fixture.state.readerMenuShown());
    CHECK(fixture.state.backButtonShown());

    // The threshold itself is the first wide width, as eighty is by default.
    fixture.state.width = 120;
    CHECK_FALSE(fixture.state.readerMenuShown());
    CHECK_FALSE(fixture.state.backButtonShown());
}

TEST_CASE("back_button when_narrow follows the width of the window too",
          "[messageread][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    REQUIRE(fixture.config.backButton == Visibility::WhenNarrow);
    REQUIRE(message_list::enterArea(fixture.state, fixture.area));

    // The button stands in the top-left corner, so the first row is where it is
    // there to be read off — and where the title starts instead when it is not.
    fixture.state.width = fixture.config.adaptiveUiThreshold;
    CHECK_FALSE(fixture.state.backButtonShown());
    CHECK_FALSE(startsWith(rowsOf(fixture)[0], "│ ← │"));

    fixture.state.width = fixture.config.adaptiveUiThreshold - 1;
    CHECK(fixture.state.backButtonShown());
    CHECK(startsWith(rowsOf(fixture)[0], "│ ← │"));

    // And a stated setting ignores the window either way round.
    fixture.config.backButton = Visibility::Off;
    CHECK_FALSE(fixture.state.backButtonShown());
    fixture.config.backButton = Visibility::On;
    fixture.state.width = fixture.config.adaptiveUiThreshold;
    CHECK(fixture.state.backButtonShown());
}

TEST_CASE("The arrow keys still move between messages", "[messageread][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());

    // The mark is on the first message, so the area opens on the second.
    fixture.lastRead->set(uidAt(fixture, 1));
    REQUIRE(message_list::enterArea(fixture.state, fixture.area));
    REQUIRE(fixture.state.messageCursor == 1);

    REQUIRE(message_read::handleEvent(fixture.state, Event::ArrowLeft));
    CHECK(fixture.state.messageCursor == 0);
    CHECK(fixture.state.navigator.current() == ScreenId::MessageRead);
    // Moving within the area keeps whatever else was typed: that is how a held
    // arrow walks through several messages at once.
    CHECK_FALSE(fixture.state.discardTypeahead);

    REQUIRE(message_read::handleEvent(fixture.state, Event::ArrowRight));
    CHECK(fixture.state.messageCursor == 1);
    CHECK(fixture.state.navigator.current() == ScreenId::MessageRead);
}

TEST_CASE("w opens the export dialog", "[messageread][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    REQUIRE(message_list::enterArea(fixture.state, fixture.area));

    REQUIRE(message_read::handleEvent(fixture.state, Event::Character('w')));
    REQUIRE(fixture.state.exportPicker);
    // Where it writes and under what name are the dialog's to ask; the reader
    // answers for the message being in it, which is what it opens on.
    CHECK(fixture.state.exportPicker->focus ==
          amberedit::ui::AppState::ExportPicker::Focus::Name);
}

TEST_CASE("The export button is offered but not given", "[messageread][menu][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    REQUIRE(message_list::enterArea(fixture.state, fixture.area));
    openMenu(fixture);
    // Not in the default menu: writing a message out to a file is a thing done
    // now and then, and `w` does it without a button.
    CHECK(buttonFor(fixture, MenuCommand::Export) == nullptr);

    fixture.state.menuView.reset();
    fixture.config.readerMenu = {MenuCommand::Export};
    openMenu(fixture);
    const auto* button = buttonFor(fixture, MenuCommand::Export);
    REQUIRE(button != nullptr);
    REQUIRE(button->enabled);

    REQUIRE(menu_dialog::handleEvent(fixture.state, pressOn(*button)) ==
            menu_dialog::Outcome::Picked);
    fixture.state.menuView.reset();
    message_read::runMenuCommand(fixture.state, MenuCommand::Export);
    CHECK(fixture.state.exportPicker);
}

TEST_CASE("An empty area has nothing to export", "[messageread][menu][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    fixture.config.readerMenu = {MenuCommand::Export};
    emptyTheArea(fixture);

    REQUIRE(message_list::enterArea(fixture.state, fixture.area));
    openMenu(fixture);
    // The button is drawn dimmed, and the key does nothing at all: there is no
    // message on the screen to write out.
    const auto* button = buttonFor(fixture, MenuCommand::Export);
    REQUIRE(button != nullptr);
    CHECK_FALSE(button->enabled);

    fixture.state.menuView.reset();
    REQUIRE(message_read::handleEvent(fixture.state, Event::Character('w')));
    CHECK_FALSE(fixture.state.exportPicker);
}

namespace {

/// Puts a body of our own on the reader's screen and lays it out. The area's
/// own messages say nothing about pipe codes, and what is being tested is what
/// the reader makes of a line that does.
void showBody(AreaFixture& fixture, const std::vector<std::string>& lines) {
    REQUIRE(message_list::enterArea(fixture.state, fixture.area));
    REQUIRE(message_read::loadMessage(fixture.state, 1));

    amberedit::domain::MessageBody body;
    for (const auto& line : lines)
        body.lines.push_back(amberedit::domain::MessageLine{line, false, false});
    fixture.state.readBody = body;
    // The width has not changed, so relayout() would leave the old wrapping
    // standing over the new body.
    fixture.state.readLayoutWidth = 0;
    message_read::relayout(fixture.state);
}

/// The row of the frame the body's first line was drawn on, found by its text
/// so that the header block above it may grow without moving the test.
int rowOf(const AreaFixture& fixture, term::Screen& screen, const std::string& text) {
    for (int y = 0; y < fixture.state.height; ++y) {
        std::string row;
        for (int x = 0; x < fixture.state.width; ++x) row += screen.at(x, y).glyph;
        if (row.find(text) != std::string::npos) return y;
    }
    return -1;
}

}  // namespace

TEST_CASE("The reader draws the pipe codes rather than showing them",
          "[messageread][bbs][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    // On the config rather than on `state.areaConfig`: entering the area
    // resolves the one from the other, and would put back what was written here.
    fixture.config.bbsCodesRenegade = true;
    // High intensity white on a blue background, then bright red — the example
    // out of the format's own documentation, with a color change after it.
    showBody(fixture, {"|15|17white |12red"});

    term::Screen screen(fixture.state.width, fixture.state.height);
    term::render(screen, message_read::render(fixture.state));

    const int row = rowOf(fixture, screen, "white red");
    REQUIRE(row >= 0);
    // The codes themselves are markup and are not on the screen — the text
    // begins in the first column, where the line does.
    CHECK(screen.at(0, row).glyph == "w");
    CHECK(screen.at(0, row).fg == term::Color{15});
    CHECK(screen.at(0, row).bg == term::Color{4});
    // The background runs on under the second code, which named a foreground.
    CHECK(screen.at(6, row).glyph == "r");
    CHECK(screen.at(6, row).fg == term::Color{9});
    CHECK(screen.at(6, row).bg == term::Color{4});
}

TEST_CASE("A color the message set stops at the end of its line",
          "[messageread][bbs][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    fixture.config.bbsCodesRenegade = true;
    showBody(fixture, {"|10green", "plain again", "> and a quote"});

    term::Screen screen(fixture.state.width, fixture.state.height);
    term::render(screen, message_read::render(fixture.state));

    const int green = rowOf(fixture, screen, "green");
    REQUIRE(green >= 0);
    CHECK(screen.at(0, green).fg == term::Color{10});
    // The line the message ended is where the color ended: what follows is the
    // theme's again, quote colors and all.
    const int plain = rowOf(fixture, screen, "plain again");
    REQUIRE(plain >= 0);
    CHECK(screen.at(0, plain).fg == amberedit::ui::theme::palette.text);
    const int quote = rowOf(fixture, screen, "> and a quote");
    REQUIRE(quote >= 0);
    CHECK(screen.at(0, quote).fg == amberedit::ui::theme::palette.quoteOdd);
}

TEST_CASE("A color does cross a wrap the window made", "[messageread][bbs][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    fixture.config.bbsCodesRenegade = true;
    // One line of the message, too wide for the window: the rows under the
    // first are the same line, and a break the window chose must not change
    // what the message looks like.
    fixture.state.width = 20;
    showBody(fixture, {"|10aaaa bbbb cccc dddd eeee"});

    term::Screen screen(fixture.state.width, fixture.state.height);
    term::render(screen, message_read::render(fixture.state));

    const int second = rowOf(fixture, screen, "eeee");
    REQUIRE(second >= 0);
    CHECK(screen.at(0, second).fg == term::Color{10});
}

TEST_CASE("The pipe codes are left as text where the area did not ask for them",
          "[messageread][bbs][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    REQUIRE_FALSE(fixture.state.areaConfig.bbsCodesRenegade);
    showBody(fixture, {"|15white"});

    term::Screen screen(fixture.state.width, fixture.state.height);
    term::render(screen, message_read::render(fixture.state));

    const int row = rowOf(fixture, screen, "|15white");
    REQUIRE(row >= 0);
    CHECK(screen.at(0, row).glyph == "|");
    CHECK(screen.at(0, row).fg == amberedit::ui::theme::palette.text);
}

namespace {

/// A compiled nodelist holding the sender of the message the reader is on, and
/// the config pointed at it.
void giveNodelistWithSender(AreaFixture& fixture, const amberedit::test::TempDir& dir) {
    REQUIRE(fixture.state.readHeader);

    amberedit::nodelist::NodeEntry entry;
    entry.address = fixture.state.readHeader->origAddr;
    entry.system = "A BBS";
    entry.sysop = "Some Sysop";
    entry.location = "Belgrade";
    entry.phone = "-Unpublished-";
    entry.speed = 300;

    amberedit::nodelist::DbSource source;
    source.state.spec = "nodelist";
    source.entries = {entry};
    amberedit::nodelist::writeNodelistDb(dir.path("nodelist.db"), {source}, 0);
    fixture.config.nodelistDbPath = dir.path("nodelist.db");
}

/// Which row of a frame holds `what`, or -1.
int rowHolding(const std::vector<std::string>& rows, const std::string& what) {
    for (size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].find(what) != std::string::npos) return static_cast<int>(i);
    }
    return -1;
}

/// The column `what` starts in on a row of the frame, or -1. Counted in columns
/// and not in bytes: a rule is box characters, and a byte offset into one says
/// nothing about where on the screen it is.
int columnOf(const AreaFixture& fixture, term::Screen& screen, int row,
             const std::string& what) {
    for (int x = 0; x < fixture.state.width; ++x) {
        std::string run;
        for (int i = x; i < fixture.state.width && run.size() < what.size(); ++i) {
            run += screen.at(i, row).glyph;
        }
        if (run.size() >= what.size() && run.compare(0, what.size(), what) == 0) return x;
    }
    return -1;
}

/// The rule closing the header block, which is the row under the Date row —
/// or under the Recd row where the config asks for one.
std::string closingRule(AreaFixture& fixture) {
    const std::vector<std::string> rows = rowsOf(fixture);
    const int date = rowHolding(rows, "Date :");
    REQUIRE(date >= 0);
    const auto after =
        static_cast<size_t>(date) + 1 + (fixture.state.recdRowShown() ? 1 : 0);
    REQUIRE(after < rows.size());
    return rows[after];
}

}  // namespace

TEST_CASE("The rule under the header says where the message was written",
          "[messageread][nodelist][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    amberedit::test::TempDir dir;
    REQUIRE(message_list::enterArea(fixture.state, fixture.area));
    giveNodelistWithSender(fixture, dir);

    // The location stands in the rule that closes the block.
    REQUIRE(closingRule(fixture).find("Belgrade") != std::string::npos);

    const std::vector<std::string> rows = rowsOf(fixture);
    const int from = rowHolding(rows, "From :");
    const int rule = rowHolding(rows, "Belgrade");
    REQUIRE(from >= 0);
    REQUIRE(rule >= 0);

    term::Screen screen(fixture.state.width, fixture.state.height);
    term::render(screen, message_read::render(fixture.state));

    // In the column the addresses and the attributes stand in above it, which is
    // where the eye is already looking for something about the sender.
    const std::string sender = fixture.state.readHeader->origAddr.toString();
    const int address = columnOf(fixture, screen, from, sender);
    const int at = columnOf(fixture, screen, rule, "Belgrade");
    REQUIRE(address > 0);
    CHECK(at == address);

    // And it is drawn in the kludges' color: it is there to be glanced at, and
    // it is not the message.
    CHECK(screen.at(at, rule).fg == amberedit::ui::theme::palette.kludge);
}

TEST_CASE("A point nobody lists is placed where its boss is",
          "[messageread][nodelist][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    amberedit::test::TempDir dir;
    REQUIRE(message_list::enterArea(fixture.state, fixture.area));
    REQUIRE(fixture.state.readHeader);

    // The message is from a point of the node the nodelist holds, and no
    // pointlist here lists the point — which is what most systems have.
    amberedit::domain::FtnAddress boss = fixture.state.readHeader->origAddr;
    boss.point = 0;
    amberedit::domain::FtnAddress point = boss;
    point.point = 7;

    amberedit::nodelist::NodeEntry entry;
    entry.address = boss;
    entry.location = "Belgrade";
    amberedit::nodelist::DbSource source;
    source.state.spec = "nodelist";
    source.entries = {entry};
    amberedit::nodelist::writeNodelistDb(dir.path("nodelist.db"), {source}, 0);
    fixture.config.nodelistDbPath = dir.path("nodelist.db");

    fixture.state.readHeader->origAddr = point;
    CHECK(closingRule(fixture).find("Belgrade") != std::string::npos);
}

TEST_CASE("With show_location off the rule is a rule",
          "[messageread][nodelist][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    amberedit::test::TempDir dir;
    REQUIRE(message_list::enterArea(fixture.state, fixture.area));
    giveNodelistWithSender(fixture, dir);
    REQUIRE(closingRule(fixture).find("Belgrade") != std::string::npos);

    fixture.config.showLocation = false;
    CHECK(closingRule(fixture).find("Belgrade") == std::string::npos);
}

TEST_CASE("The rule under the header says how much message there is",
          "[messageread][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    REQUIRE(message_list::enterArea(fixture.state, fixture.area));
    REQUIRE(fixture.state.readBody);
    fixture.config.readerShowMessageSize = true;

    // The whole message, service lines included, each line with the newline
    // that ends it — which is what the reader is asked to say in as few
    // characters as it can.
    size_t bytes = 0;
    for (const auto& line : fixture.state.readBody->lines) bytes += line.text.size() + 1;
    REQUIRE(bytes < 1024);
    // A count of bytes is written bare, with no unit after it.
    const std::string size = std::to_string(bytes);

    // It stands at the left end of the rule closing the block, in the column
    // the header rows begin in, and it takes no row of its own.
    const std::string rule = closingRule(fixture);
    CHECK(startsWith(rule, " " + size + " "));

    const std::vector<std::string> rows = rowsOf(fixture);
    const int at = rowHolding(rows, "Date :") + 1;
    REQUIRE(at > 0);
    REQUIRE(rows[static_cast<size_t>(at)] == rule);

    term::Screen screen(fixture.state.width, fixture.state.height);
    term::render(screen, message_read::render(fixture.state));
    // In the kludges' color, like the location in the other end of it.
    CHECK(screen.at(1, at).fg == amberedit::ui::theme::palette.kludge);
}

TEST_CASE("Without reader_show_message_size the rule keeps its left end",
          "[messageread][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    REQUIRE(message_list::enterArea(fixture.state, fixture.area));

    // Which is what a config saying nothing gets: the setting is off until it
    // is asked for.
    CHECK_FALSE(fixture.config.readerShowMessageSize);
    CHECK_FALSE(startsWith(closingRule(fixture), " "));
}

TEST_CASE("The size and the location share the closing rule",
          "[messageread][nodelist][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    amberedit::test::TempDir dir;
    REQUIRE(message_list::enterArea(fixture.state, fixture.area));
    giveNodelistWithSender(fixture, dir);
    fixture.config.readerShowMessageSize = true;

    // The size at one end and the location in the addresses' column, which the
    // size does not push out of place: it is a column of the window and not one
    // of the rule.
    const std::vector<std::string> rows = rowsOf(fixture);
    const int from = rowHolding(rows, "From :");
    const int rule = rowHolding(rows, "Belgrade");
    REQUIRE(from >= 0);
    REQUIRE(rule >= 0);
    CHECK(startsWith(rows[rule], " "));

    term::Screen screen(fixture.state.width, fixture.state.height);
    term::render(screen, message_read::render(fixture.state));
    const std::string sender = fixture.state.readHeader->origAddr.toString();
    CHECK(columnOf(fixture, screen, rule, "Belgrade") ==
          columnOf(fixture, screen, from, sender));
}

TEST_CASE("A sender no nodelist holds is said nothing about",
          "[messageread][nodelist][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    amberedit::test::TempDir dir;
    REQUIRE(message_list::enterArea(fixture.state, fixture.area));

    // A config with no nodelist at all, which is what most configs are: the
    // rule is the rule it always was — nothing was looked for, and nothing is
    // said about not having found it.
    CHECK(closingRule(fixture).find(" ") == std::string::npos);

    // And a nodelist that holds somebody else says nothing about this message.
    amberedit::nodelist::NodeEntry other;
    other.address = *amberedit::domain::FtnAddress::parse("2:240/1120");
    other.location = "Heringen";
    amberedit::nodelist::DbSource source;
    source.state.spec = "nodelist";
    source.entries = {other};
    amberedit::nodelist::writeNodelistDb(dir.path("nodelist.db"), {source}, 0);
    fixture.config.nodelistDbPath = dir.path("nodelist.db");
    fixture.state.nodelistOpened = false;

    CHECK(closingRule(fixture).find("Heringen") == std::string::npos);
}

TEST_CASE("w asks first where the message carries a file", "[messageread][uue][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    showBody(fixture, {"Here it is.", "begin 644 report.zip", "#0V%T", "`", "end"});

    // A message carrying a uuencoded file is two things at once, and which of
    // them the export is to be is not something to guess at: the question comes
    // up in the export dialog's place, and the dialog follows the answer.
    REQUIRE(message_read::handleEvent(fixture.state, Event::Character('w')));
    REQUIRE(fixture.state.exportModePicker);
    CHECK_FALSE(fixture.state.exportPicker);

    const auto& picker = *fixture.state.exportModePicker;
    REQUIRE(picker.files.size() == 1);
    CHECK(picker.files[0].name == "report.zip");
    CHECK(picker.files[0].bytes == "Cat");
    // It opens on the files: a message carrying one is a message somebody sent a
    // file in, and taking it out is what the question was raised for.
    CHECK(picker.mode == amberedit::ui::AppState::ExportPicker::Mode::Uue);
}

TEST_CASE("The function keys answer beside the letters they double",
          "[messageread][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    REQUIRE(message_list::enterArea(fixture.state, fixture.area));

    // F9 is `l`: the list of messages in the area.
    REQUIRE(message_read::handleEvent(fixture.state, Event::F9));
    CHECK(fixture.state.navigator.current() == ScreenId::MessageList);
    fixture.state.navigator.pop();
    REQUIRE(fixture.state.navigator.current() == ScreenId::MessageRead);

    // F5 is `n`: the reply goes into whichever area the dialog is answered with.
    REQUIRE(message_read::handleEvent(fixture.state, Event::F5));
    REQUIRE(fixture.state.areaPicker);
    CHECK(fixture.state.areaPicker->purpose == AppState::AreaPicker::For::Reply);
    fixture.state.areaPicker.reset();

    // F10 is Ctrl-N: the nodelist, which opens whether or not there is one to
    // show — the box is the only place there is to say why there is nothing.
    REQUIRE(message_read::handleEvent(fixture.state, Event::F10));
    REQUIRE(fixture.state.nodelistView);
}

TEST_CASE("The reader answers the layout it was given", "[messageread][keys]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    REQUIRE(message_list::enterArea(fixture.state, fixture.area));

    fixture.state.keys = amberedit::ui::KeyMap::parse(
        "F3 reader.find\n"
        "x  reader.list\n",
        "keys");

    // What the layout names, on the key it names it on.
    REQUIRE(message_read::handleEvent(fixture.state, Event::F3));
    REQUIRE(fixture.state.findPicker);
    fixture.state.findPicker.reset();

    REQUIRE(message_read::handleEvent(fixture.state, Event::Character('x')));
    CHECK(fixture.state.navigator.current() == ScreenId::MessageList);
    fixture.state.navigator.pop();

    // And nothing else: a file is the layout entire, so the keys it leaves out
    // are keys this screen no longer knows.
    CHECK_FALSE(message_read::handleEvent(fixture.state, Event::Character('f')));
    CHECK_FALSE(fixture.state.findPicker);
    CHECK_FALSE(message_read::handleEvent(fixture.state, Event::Character('l')));
    CHECK_FALSE(message_read::handleEvent(fixture.state, Event::Character('q')));
    CHECK_FALSE(message_read::handleEvent(fixture.state, Event::F4));
    // Moving about is not the layout's to take away.
    CHECK(message_read::handleEvent(fixture.state, Event::PageDown));
    CHECK(message_read::handleEvent(fixture.state, Event::Escape));
}
