#include "ui/nodelist_dialog.hpp"

#include <catch2/catch.hpp>

#include <string>
#include <vector>

#include "nodelist/nodelist_writer.hpp"
#include "temp_dir.hpp"
#include "temp_squish_base.hpp"
#include "ui/area_fixture.hpp"
#include "ui/screens/message_list_screen.hpp"
#include "ui/screens/message_read_screen.hpp"
#include "ui/term/element.hpp"
#include "ui/term/event.hpp"
#include "ui/term/screen.hpp"

using amberedit::test::AreaFixture;
using amberedit::test::TempDir;
using amberedit::test::TempSquishBase;
using amberedit::ui::term::Event;

namespace nodelist = amberedit::nodelist;
namespace nodelist_dialog = amberedit::ui::nodelist_dialog;
namespace message_list = amberedit::ui::screens::message_list;
namespace message_read = amberedit::ui::screens::message_read;
namespace term = amberedit::ui::term;

using Purpose = amberedit::ui::AppState::NodelistView::Purpose;

namespace {

/// The chord the reader opens the nodelist with, and the box closes on.
const Event kNodelistKey = Event::Character("n", /*ctrl=*/true);

nodelist::NodeEntry node(const std::string& address, const std::string& system,
                         const std::string& sysop, const std::string& location) {
    nodelist::NodeEntry entry;
    entry.address = *amberedit::domain::FtnAddress::parse(address);
    entry.system = system;
    entry.sysop = sysop;
    entry.location = location;
    entry.phone = "-Unpublished-";
    entry.speed = 300;
    return entry;
}

/// The box drawn over the reader, as the rows a terminal would show.
std::vector<std::string> rowsOf(AreaFixture& fixture) {
    term::Screen screen(fixture.state.width, fixture.state.height);
    term::render(screen, nodelist_dialog::render(fixture.state,
                                                 message_read::render(fixture.state)));
    std::vector<std::string> rows;
    for (int y = 0; y < fixture.state.height; ++y) {
        std::string row;
        for (int x = 0; x < fixture.state.width; ++x) row += screen.at(x, y).glyph;
        rows.push_back(row);
    }
    return rows;
}

bool anyRowHas(const std::vector<std::string>& rows, const std::string& what) {
    for (const auto& row : rows) {
        if (row.find(what) != std::string::npos) return true;
    }
    return false;
}

/// Which row holds `what`, or -1. The box is centred, so where it landed is
/// what the frame decided rather than something a test can count off.
int rowWith(const std::vector<std::string>& rows, const std::string& what) {
    for (size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].find(what) != std::string::npos) return static_cast<int>(i);
    }
    return -1;
}

/// Which rows of the list the scrollbar's thumb covers, counted from the first
/// row under the rule — where the thumb stands is what says the bar works, and
/// where the box itself landed is the frame's business.
std::vector<int> thumbRows(const std::vector<std::string>& rows) {
    const int first = rowWith(rows, "├") + 1;
    const int last = rowWith(rows, "╰");
    std::vector<int> found;
    for (int i = first; i >= 1 && i < last; ++i) {
        if (rows[static_cast<size_t>(i)].find("█") != std::string::npos) {
            found.push_back(i - first);
        }
    }
    return found;
}

/// The address the cursor is on, which is what every lookup here is checked by.
std::string cursorAddress(const AreaFixture& fixture) {
    const auto& view = *fixture.state.nodelistView;
    return fixture.state.nodelistDb->addressAt(static_cast<size_t>(view.cursor))
        .toString();
}

void type(AreaFixture& fixture, const std::string& text) {
    for (char c : text) {
        nodelist_dialog::handleEvent(fixture.state, Event::Character(c));
    }
}

/// The reader open on the first message of the checked-in base, with a compiled
/// nodelist beside it that holds the sender of that message.
///
/// The nodelist is written after the message is loaded, so that the address the
/// dialog opens on is one the test knows is in it: what that address is belongs
/// to the fixture's base, not to this test.
struct Fixture {
    TempSquishBase base;
    TempDir dir;
    AreaFixture area{base.path()};
    std::string sender;

    Fixture() {
        REQUIRE(message_list::enterArea(area.state, area.area));
        REQUIRE(area.state.readHeader);
        sender = area.state.readHeader->origAddr.toString();

        nodelist::DbSource nodes{
            {"~/ftn/nodelist/Z2DAILY.999", "/home/user/ftn/nodelist/Z2DAILY.225", 1, 2},
            {
                node("2:222/0", "Southwest Finland", "Kim Heino", "Turku"),
                node("2:240/0", "Host Nordnetz", "Torsten Bamberg", "Hamburg"),
                node("2:240/1120", "ambrosia60.goip.de", "Ulrich Schroeter",
                     "Heringen (Werra)"),
                node("2:240/1200", "Hub Sued", "Ulrich Schroeter", "Heringen"),
                node("2:240/2188", "Kruemel Boks!", "Christian von Busse", "Boeblingen"),
                node(sender, "The Sender", "Some Sysop", "Nowhere"),
            }};
        nodelist::DbSource points{
            {"~/ftn/nodelist/Z2PNT.Z99", "/home/user/ftn/nodelist/Z2PNT.Z19", 3, 4},
            {node("2:240/1120.8", "Point #8", "Werner Krammer", "Dieburg")}};
        nodelist::writeNodelistDb(dir.path("nodelist.db"), {nodes, points}, 0);
        area.config.nodelistDbPath = dir.path("nodelist.db");
    }
};

}  // namespace

TEST_CASE("Ctrl-N opens the nodelist on whoever wrote the message", "[nodelist][ui]") {
    Fixture fixture;

    REQUIRE(message_read::handleEvent(fixture.area.state, kNodelistKey));
    REQUIRE(fixture.area.state.nodelistView);
    REQUIRE(fixture.area.state.nodelistDb);

    // The Lookup line opens holding the sender's address, and the cursor stands
    // on that node — the question a nodelist is opened to answer.
    CHECK(fixture.area.state.nodelistView->lookup == fixture.sender);

    const auto rows = rowsOf(fixture.area);
    CHECK(anyRowHas(rows, "Lookup: " + fixture.sender));
    CHECK(anyRowHas(rows, "The Sender"));
    // The file the node came from, along the bottom of the frame, by the name
    // of the file rather than by the line the config wrote.
    CHECK(anyRowHas(rows, "Z2DAILY.225"));
    CHECK_FALSE(anyRowHas(rows, "Z2DAILY.999"));
}

TEST_CASE("Ctrl-N on a point nobody lists opens on the node it hangs off",
          "[nodelist][ui]") {
    TempSquishBase base;
    TempDir dir;
    AreaFixture area{base.path()};
    REQUIRE(message_list::enterArea(area.state, area.area));
    REQUIRE(area.state.readHeader);

    // The message is from a point of a node the nodelist has, and the point
    // itself is in no list here — a pointlist nobody compiled, which is the
    // ordinary case.
    amberedit::domain::FtnAddress sender = area.state.readHeader->origAddr;
    sender.point = 7;
    area.state.readHeader->origAddr = sender;

    amberedit::domain::FtnAddress boss = sender;
    boss.point = 0;
    nodelist::DbSource source{
        {"nodelist", "/home/user/ftn/nodelist/NODELIST.225", 1, 2},
        {node(boss.toString(), "The Boss", "Some Sysop", "Nowhere")}};
    nodelist::writeNodelistDb(dir.path("nodelist.db"), {source}, 0);
    area.config.nodelistDbPath = dir.path("nodelist.db");

    nodelist_dialog::open(area.state);
    REQUIRE(area.state.nodelistView);
    // The box is on the boss, and the Lookup line says so rather than naming a
    // point that is not what is on the screen.
    CHECK(area.state.nodelistView->lookup == boss.toString());
    CHECK(area.state.nodelistView->found);
    REQUIRE(nodelist_dialog::currentNode(area.state));
    CHECK(nodelist_dialog::currentNode(area.state)->system == "The Boss");
}

TEST_CASE("the list is the whole nodelist and the lookup is where the cursor goes",
          "[nodelist][ui]") {
    Fixture fixture;
    nodelist_dialog::open(fixture.area.state);
    auto& view = *fixture.area.state.nodelistView;

    // Everything is in the list, whatever the lookup says: the nodes around one
    // are half of what a nodelist is asked about.
    REQUIRE(fixture.area.state.nodelistDb->size() == 7);
    const auto rows = rowsOf(fixture.area);
    CHECK(anyRowHas(rows, "2:222/0"));
    CHECK(anyRowHas(rows, "2:240/1120"));
    CHECK(anyRowHas(rows, "Kim Heino"));
    CHECK(anyRowHas(rows, "Christian von Busse"));

    view.lookup.clear();
    type(fixture.area, "2:240/1120");
    CHECK(view.found);
    CHECK(cursorAddress(fixture.area) == "2:240/1120");

    // Its point is still under it, and the neighbours still around it.
    const auto shown = rowsOf(fixture.area);
    CHECK(anyRowHas(shown, "2:240/1120.8"));
    CHECK(anyRowHas(shown, "2:240/2188"));
}

TEST_CASE("a lookup takes an address whole or in part", "[nodelist][ui]") {
    Fixture fixture;
    nodelist_dialog::open(fixture.area.state);
    auto& view = *fixture.area.state.nodelistView;

    const auto lookFor = [&fixture, &view](const std::string& text) {
        view.lookup.clear();
        type(fixture.area, text);
        return cursorAddress(fixture.area);
    };

    CHECK(lookFor("2:240/1120.8") == "2:240/1120.8");
    CHECK(lookFor("2:240/1120") == "2:240/1120");
    // Part of one goes to the first node under it.
    CHECK(lookFor("2:240") == "2:240/0");
    CHECK(lookFor("2:222") == "2:222/0");
    CHECK(view.found);

    // An address nobody has scrolls to where it would stand, so that the
    // neighbours answer the question the address was asking.
    CHECK(lookFor("2:240/1121") == "2:240/1200");
    CHECK_FALSE(view.found);
    // And the line says so by turning red, which is the box's whole way of
    // saying it — the cursor is where it was left.
    CHECK_FALSE(fixture.area.state.nodelistView->found);
}

TEST_CASE("a lookup takes any part of a sysop's name", "[nodelist][ui]") {
    Fixture fixture;
    nodelist_dialog::open(fixture.area.state);
    auto& view = *fixture.area.state.nodelistView;
    view.lookup.clear();

    // A surname on its own, with a space in the query the lists' quick search
    // would never have taken.
    type(fixture.area, "von Busse");
    CHECK(view.found);
    CHECK(cursorAddress(fixture.area) == "2:240/2188");

    view.lookup.clear();
    type(fixture.area, "krammer");
    CHECK(cursorAddress(fixture.area) == "2:240/1120.8");

    // One letter more and it is nobody's name: the line says so and the cursor
    // stays on what the last letter that did match had found.
    type(fixture.area, "x");
    CHECK_FALSE(view.found);
    CHECK(cursorAddress(fixture.area) == "2:240/1120.8");

    // And backspacing back to a name finds it again — erasing a lookup is not
    // undoing it.
    nodelist_dialog::handleEvent(fixture.area.state, Event::Backspace);
    CHECK(view.lookup == "krammer");
    CHECK(view.found);
    CHECK(cursorAddress(fixture.area) == "2:240/1120.8");
}

TEST_CASE("the first character typed replaces the address the box opened on",
          "[nodelist][ui]") {
    Fixture fixture;
    nodelist_dialog::open(fixture.area.state);
    REQUIRE(fixture.area.state.nodelistView->lookup == fixture.sender);

    // What stood in the line was an answer already given: typing asks about
    // something else, so it takes the whole line rather than adding to the end
    // of an address.
    type(fixture.area, "Kim");
    CHECK(fixture.area.state.nodelistView->lookup == "Kim");
    CHECK(cursorAddress(fixture.area) == "2:222/0");

    // And only that first one — after it the line is an ordinary field.
    type(fixture.area, " Heino");
    CHECK(fixture.area.state.nodelistView->lookup == "Kim Heino");
    CHECK(cursorAddress(fixture.area) == "2:222/0");
}

TEST_CASE("a line already edited is added to rather than replaced", "[nodelist][ui]") {
    Fixture fixture;
    nodelist_dialog::open(fixture.area.state);
    REQUIRE(fixture.area.state.nodelistView->lookup == fixture.sender);
    const std::string shorter = fixture.sender.substr(0, fixture.sender.size() - 1);

    // Erasing a character is editing the address, so what is in the line is the
    // user's from then on — and the next character goes on the end of it.
    nodelist_dialog::handleEvent(fixture.area.state, Event::Backspace);
    REQUIRE(fixture.area.state.nodelistView->lookup == shorter);
    type(fixture.area, "1");
    CHECK(fixture.area.state.nodelistView->lookup == shorter + "1");

    // A box opened again starts over: the address is the answer once more.
    nodelist_dialog::open(fixture.area.state);
    REQUIRE(fixture.area.state.nodelistView->lookup == fixture.sender);
    type(fixture.area, "K");
    CHECK(fixture.area.state.nodelistView->lookup == "K");
}

TEST_CASE("Enter walks through everything the lookup finds", "[nodelist][ui]") {
    Fixture fixture;
    nodelist_dialog::open(fixture.area.state);
    auto& view = *fixture.area.state.nodelistView;
    view.lookup.clear();

    // Two nodes of one sysop: the first, the second, and round again.
    type(fixture.area, "Schroeter");
    CHECK(cursorAddress(fixture.area) == "2:240/1120");
    nodelist_dialog::handleEvent(fixture.area.state, Event::Return);
    CHECK(cursorAddress(fixture.area) == "2:240/1200");
    nodelist_dialog::handleEvent(fixture.area.state, Event::Return);
    CHECK(cursorAddress(fixture.area) == "2:240/1120");

    // A net walks the same way, a node at a time.
    view.lookup.clear();
    type(fixture.area, "2:240");
    CHECK(cursorAddress(fixture.area) == "2:240/0");
    nodelist_dialog::handleEvent(fixture.area.state, Event::Return);
    CHECK(cursorAddress(fixture.area) == "2:240/1120");
}

TEST_CASE("the head of the box is the node the cursor is on", "[nodelist][ui]") {
    Fixture fixture;
    nodelist_dialog::open(fixture.area.state);
    fixture.area.state.nodelistView->lookup.clear();
    type(fixture.area, "2:240/1120");

    // The two lines stand at the head of the box, under the Lookup line and
    // over the rule that marks the list off from them.
    const auto rows = rowsOf(fixture.area);
    const int title = rowWith(rows, "Lookup:");
    REQUIRE(title >= 0);
    CHECK(rows[title + 1].find("Ulrich Schroeter, ambrosia60.goip.de") !=
          std::string::npos);
    // The location stands against the right edge of the first line, and the
    // rest of the node's fields — the line past the sysop, commas and all —
    // against the right edge of the second, each with the left of its line kept
    // clear of it.
    CHECK(rows[title + 1].find("Heringen (Werra)│") != std::string::npos);
    CHECK(rows[title + 2].find("│ 2:240/1120 ") != std::string::npos);
    CHECK(rows[title + 2].find(" -Unpublished-,300│") != std::string::npos);
    CHECK(rows[title + 3].find("├") != std::string::npos);

    // Moving the cursor moves them with it.
    nodelist_dialog::handleEvent(fixture.area.state, Event::ArrowDown);
    const auto moved = rowsOf(fixture.area);
    CHECK(moved[title + 1].find("Werner Krammer, Point #8") != std::string::npos);
    // And the point came out of the other nodelist, which the foot now names.
    CHECK(anyRowHas(moved, "Z2PNT.Z19"));
}

TEST_CASE("the box is a modal and not the whole window", "[nodelist][ui]") {
    Fixture fixture;
    nodelist_dialog::open(fixture.area.state);

    const auto rows = rowsOf(fixture.area);
    const int top = rowWith(rows, "╭");
    const int bottom = rowWith(rows, "╰");
    REQUIRE(top > 0);
    REQUIRE(bottom > top);
    // A row above it and a row below it are the reader's, still on the screen
    // around the box.
    CHECK(bottom < fixture.area.state.height - 1);
    // The box is as wide as it asks to be rather than as wide as the window,
    // and the reader is still there around it — which is the whole of what
    // makes it a modal rather than a screen.
    CHECK(fixture.area.state.nodelistView->inner < fixture.area.state.width - 2);
    CHECK(rows[top - 1].find_first_not_of(' ') != std::string::npos);
}

TEST_CASE("a box with no room for the station name shows none", "[nodelist][ui]") {
    Fixture fixture;
    nodelist_dialog::open(fixture.area.state);
    fixture.area.state.nodelistView->lookup.clear();
    type(fixture.area, "2:222");

    // Wide: all three columns, and every one of them worth reading.
    const auto wide = rowsOf(fixture.area);
    CHECK(anyRowHas(wide, "Christian von Busse"));
    CHECK(anyRowHas(wide, "Kruemel Boks!"));

    // Narrow: the address and the sysop are the two questions a nodelist is
    // opened with, so the station is what goes rather than all three being cut.
    fixture.area.state.width = 50;
    const auto narrow = rowsOf(fixture.area);
    CHECK(anyRowHas(narrow, "Christian von Busse"));
    CHECK(anyRowHas(narrow, "2:240/2188"));
    CHECK_FALSE(anyRowHas(narrow, "Kruemel Boks!"));

    // And it comes back with the room for it.
    fixture.area.state.width = 80;
    CHECK(anyRowHas(rowsOf(fixture.area), "Kruemel Boks!"));
}

TEST_CASE("a list longer than the box carries the reader's scrollbar", "[nodelist][ui]") {
    Fixture fixture;
    nodelist_dialog::open(fixture.area.state);

    // Seven nodes in a box with room for more: nothing to scroll, and no bar —
    // which is what the reader does with a message that fits.
    CHECK_FALSE(anyRowHas(rowsOf(fixture.area), "█"));

    // A window with room for three rows of the list. The bar is the reader's
    // own: a "█" thumb on a "│" track, in the rightmost column inside the frame.
    fixture.area.state.height = 10;
    nodelist_dialog::handleEvent(fixture.area.state, Event::Home);
    const auto top = thumbRows(rowsOf(fixture.area));
    REQUIRE(top.size() == 1);
    CHECK(top.front() == 0);

    // And it walks down the track with the list.
    nodelist_dialog::handleEvent(fixture.area.state, Event::End);
    const auto bottom = thumbRows(rowsOf(fixture.area));
    REQUIRE(bottom.size() == 1);
    CHECK(bottom.front() == 2);
}

TEST_CASE("a box opened on a name shows what the name found and nothing else",
          "[nodelist][ui]") {
    Fixture fixture;
    auto& state = fixture.area.state;
    nodelist_dialog::openFor(state, Purpose::PickAddress, "Ulrich Schroeter");
    REQUIRE(state.nodelistView);

    // Two nodes of that name and nothing around them: the nodes a name did not
    // find say nothing about which node is theirs.
    REQUIRE(state.nodelistView->listMatches);
    CHECK(state.nodelistView->matches.size() == 2);
    const auto rows = rowsOf(fixture.area);
    CHECK(anyRowHas(rows, "2:240/1120"));
    CHECK(anyRowHas(rows, "2:240/1200"));
    CHECK_FALSE(anyRowHas(rows, "Kim Heino"));

    // Typing looks again, and the list is what the new text finds.
    type(fixture.area, "Kim");
    CHECK(state.nodelistView->lookup == "Kim");
    REQUIRE(state.nodelistView->matches.size() == 1);
    CHECK(anyRowHas(rowsOf(fixture.area), "Kim Heino"));

    // A name nobody has is an empty list and a Lookup line that says so.
    type(fixture.area, "zzz");
    CHECK_FALSE(state.nodelistView->found);
    CHECK(state.nodelistView->matches.empty());
    CHECK_FALSE(nodelist_dialog::currentNode(state));

    // And a line erased back to nothing is the whole nodelist: in a box that is
    // there to pick from, no lookup can only mean all of them.
    for (int i = 0; i < 10; ++i) {
        nodelist_dialog::handleEvent(state, Event::Backspace);
    }
    REQUIRE(state.nodelistView->lookup.empty());
    CHECK_FALSE(state.nodelistView->listMatches);
    CHECK(anyRowHas(rowsOf(fixture.area), "Kim Heino"));
    CHECK(anyRowHas(rowsOf(fixture.area), "Christian von Busse"));
}

TEST_CASE("an address typed into a box opened on a name filters by it too",
          "[nodelist][ui]") {
    Fixture fixture;
    auto& state = fixture.area.state;
    nodelist_dialog::openFor(state, Purpose::PickAddress, "Ulrich Schroeter");

    type(fixture.area, "2:240");
    REQUIRE(state.nodelistView->listMatches);
    // Every node of the net and nothing else, in the order a nodelist is in —
    // an address has no closeness beyond being under what was typed.
    for (size_t index : state.nodelistView->matches) {
        CHECK(state.nodelistDb->addressAt(index).net == 240);
    }
    // The four nodes of the net and the one point under one of them.
    CHECK(state.nodelistView->matches.size() == 5);
}

TEST_CASE("Enter picks in a box opened to pick and walks in one opened to browse",
          "[nodelist][ui]") {
    Fixture fixture;
    auto& state = fixture.area.state;

    // Browsing, Enter is "the next one this finds" and picks nothing.
    nodelist_dialog::open(state);
    CHECK(nodelist_dialog::handleEvent(state, Event::Return) ==
          nodelist_dialog::Outcome::Ignored);
    CHECK(state.nodelistView);

    // Asked to pick, it picks — and leaves the box standing, so that whoever
    // asked can take the node off it before putting it away.
    nodelist_dialog::openFor(state, Purpose::PickName, "2:240/2188");
    CHECK(nodelist_dialog::handleEvent(state, Event::Return) ==
          nodelist_dialog::Outcome::Picked);
    REQUIRE(state.nodelistView);
    REQUIRE(nodelist_dialog::currentNode(state));
    CHECK(nodelist_dialog::currentNode(state)->sysop == "Christian von Busse");

    // With nothing under the cursor there is nothing to pick.
    type(fixture.area, "zzz");
    nodelist_dialog::openFor(state, Purpose::PickAddress, "zzz");
    CHECK(nodelist_dialog::handleEvent(state, Event::Return) ==
          nodelist_dialog::Outcome::Ignored);
}

TEST_CASE("the arrows move the cursor and leave the lookup alone", "[nodelist][ui]") {
    Fixture fixture;
    nodelist_dialog::open(fixture.area.state);
    auto& view = *fixture.area.state.nodelistView;
    view.lookup.clear();
    type(fixture.area, "2:222/0");
    REQUIRE(cursorAddress(fixture.area) == "2:222/0");

    nodelist_dialog::handleEvent(fixture.area.state, Event::ArrowDown);
    CHECK(cursorAddress(fixture.area) == "2:240/0");
    CHECK(view.lookup == "2:222/0");

    nodelist_dialog::handleEvent(fixture.area.state, Event::End);
    CHECK(view.cursor == static_cast<int>(fixture.area.state.nodelistDb->size()) - 1);
    nodelist_dialog::handleEvent(fixture.area.state, Event::Home);
    CHECK(view.cursor == 0);
}

TEST_CASE("Esc and the keys that opened it put the nodelist away", "[nodelist][ui]") {
    Fixture fixture;

    nodelist_dialog::open(fixture.area.state);
    nodelist_dialog::handleEvent(fixture.area.state, Event::Escape);
    CHECK_FALSE(fixture.area.state.nodelistView);

    nodelist_dialog::open(fixture.area.state);
    nodelist_dialog::handleEvent(fixture.area.state, kNodelistKey);
    CHECK_FALSE(fixture.area.state.nodelistView);

    // F10 opens it beside Ctrl-N, so it closes it beside Ctrl-N: whichever key
    // the hand reached for is the one it reaches for again.
    REQUIRE(message_read::handleEvent(fixture.area.state, Event::F10));
    REQUIRE(fixture.area.state.nodelistView);
    nodelist_dialog::handleEvent(fixture.area.state, Event::F10);
    CHECK_FALSE(fixture.area.state.nodelistView);

    // Backspace edits the line and never closes the box — not even on an empty
    // one, which is a keystroke away while a lookup is being cleared to type
    // another.
    nodelist_dialog::open(fixture.area.state);
    REQUIRE_FALSE(fixture.area.state.nodelistView->lookup.empty());
    for (int i = 0; i < 40; ++i) {
        nodelist_dialog::handleEvent(fixture.area.state, Event::Backspace);
    }
    REQUIRE(fixture.area.state.nodelistView);
    CHECK(fixture.area.state.nodelistView->lookup.empty());
}

TEST_CASE("with no nodelist the box opens and says so", "[nodelist][ui]") {
    TempSquishBase base;
    AreaFixture area(base.path());
    REQUIRE(message_list::enterArea(area.state, area.area));

    // A config naming no nodelist at all: Ctrl-N is still a key, and the box is
    // the only place there is to say why it has nothing to show.
    REQUIRE(message_read::handleEvent(area.state, kNodelistKey));
    REQUIRE(area.state.nodelistView);
    CHECK_FALSE(area.state.nodelistDb);

    const auto rows = rowsOf(area);
    CHECK(anyRowHas(rows, "no nodelist"));

    // And one that names a file that is not there says what it could not do.
    area.state.nodelistView.reset();
    area.state.nodelistOpened = false;
    area.state.nodelistProblem.clear();
    area.config.nodelistDbPath = "/nonexistent/nodelist.db";
    nodelist_dialog::open(area.state);
    CHECK_FALSE(area.state.nodelistDb);
    CHECK(anyRowHas(rowsOf(area), "nodelist.db"));

    // Neither of them is a box that will not answer a key.
    nodelist_dialog::handleEvent(area.state, Event::ArrowDown);
    nodelist_dialog::handleEvent(area.state, Event::Return);
    nodelist_dialog::handleEvent(area.state, Event::Escape);
    CHECK_FALSE(area.state.nodelistView);
}
