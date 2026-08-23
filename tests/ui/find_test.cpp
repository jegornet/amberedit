#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "app/message_search.hpp"
#include "config/app_config.hpp"
#include "domain/ftn_address.hpp"
#include "domain/message.hpp"
#include "temp_squish_base.hpp"
#include "test_strings.hpp"
#include "ui/area_fixture.hpp"
#include "ui/find_dialog.hpp"
#include "ui/screens/message_list_screen.hpp"
#include "ui/screens/message_read_screen.hpp"
#include "ui/term/element.hpp"
#include "ui/term/event.hpp"
#include "ui/term/screen.hpp"

using amberedit::app::SearchScope;
using amberedit::config::AppConfig;
using amberedit::config::TwitMode;
using amberedit::domain::FtnAddress;
using amberedit::domain::MessageDraft;
using amberedit::test::AreaFixture;
using amberedit::test::TempSquishBase;
using amberedit::ui::AppState;
using amberedit::ui::term::Event;

namespace find_dialog = amberedit::ui::find_dialog;
namespace message_list = amberedit::ui::screens::message_list;
namespace message_read = amberedit::ui::screens::message_read;
namespace term = amberedit::ui::term;

namespace {

/// One message to put into the area: what a search reads, and nothing else.
struct Letter {
    std::string from{"Petr Petrov"};
    std::string subject{"Nothing in particular"};
    std::vector<std::string> lines{"Nothing here."};
    std::string charset{"CP866"};
};

/// A message with a body and nothing else worth saying about it.
Letter says(std::vector<std::string> lines) {
    Letter letter;
    letter.lines = std::move(lines);
    return letter;
}

/// The same, in the charset it names — which is what a search folds it by.
Letter saysIn(const std::string& charset, std::vector<std::string> lines) {
    Letter letter = says(std::move(lines));
    letter.charset = charset;
    return letter;
}

/// A message from somebody in particular.
Letter from(const std::string& who, std::vector<std::string> lines) {
    Letter letter = says(std::move(lines));
    letter.from = who;
    return letter;
}

/// A message about something in particular.
Letter about(const std::string& subject) {
    Letter letter;
    letter.subject = subject;
    return letter;
}

/// Puts exactly these messages into the area, in place of the mail the fixture
/// copies: a search is checked on which message it lands on, so the numbers
/// have to be the test's own.
void putMessages(AreaFixture& fixture, const std::vector<Letter>& letters) {
    amberedit::ports::IMsgBase* base =
        amberedit::test::valueOf(fixture.manager.openArea(fixture.area));
    REQUIRE(base != nullptr);
    while (base->count() > 0) REQUIRE(base->remove(1).has_value());

    for (const Letter& letter : letters) {
        MessageDraft draft;
        draft.from = letter.from;
        draft.to = "All";
        draft.subject = letter.subject;
        draft.origAddr = *FtnAddress::parse("2:5020/1042");
        draft.lines = letter.lines;
        draft.charset = letter.charset;
        draft.kludges = {"CHRS: " + letter.charset + " 2"};
        REQUIRE(base->write(draft) != 0);
    }

    fixture.manager.closeCurrentArea();
    static_cast<void>(fixture.manager.reload());
}

/// Which message the reader is showing.
uint32_t showing(const AreaFixture& fixture) {
    REQUIRE(fixture.state.readHeader.has_value());
    return fixture.state.readHeader->number;
}

/// Every stretch of the body the reader would light up, in the order it draws
/// them — what says the highlight reached the rows and not merely the state.
std::vector<std::string> highlighted(const AreaFixture& fixture) {
    std::vector<std::string> lit;
    for (const auto& line : fixture.state.readLines) {
        for (const auto& match : line.found) {
            lit.push_back(line.text.substr(match.begin, match.end - match.begin));
        }
    }
    return lit;
}

AppConfig plain() {
    AppConfig config;
    config.userName = "Vasya Pupkin";
    return config;
}

}  // namespace

TEST_CASE("A search opens the first message from here that holds the words "
          "[find][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path(), plain());
    putMessages(fixture, {says({"nothing"}), says({"the needle is here"}),
                          says({"the needle again"})});
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());
    REQUIRE(showing(fixture) == 1);

    CHECK(message_read::findMessage(fixture.state, "needle", SearchScope::HeaderAndText));
    CHECK(showing(fixture) == 2);
    CHECK(highlighted(fixture) == std::vector<std::string>{"needle"});
}

TEST_CASE("The search starts on the message in front of the user [find][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path(), plain());
    putMessages(fixture, {says({"needle"}), says({"nothing"}), says({"needle"})});
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());

    // The message on screen is searched too, so a word standing in it is found
    // where it is rather than in the next message that also has it.
    CHECK(message_read::findMessage(fixture.state, "needle", SearchScope::HeaderAndText));
    CHECK(showing(fixture) == 1);
}

TEST_CASE("The same search again goes on from the next message [find][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path(), plain());
    putMessages(fixture, {says({"needle"}), says({"needle"}), says({"needle"})});
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());

    REQUIRE(
        message_read::findMessage(fixture.state, "needle", SearchScope::HeaderAndText));
    CHECK(showing(fixture) == 1);
    REQUIRE(
        message_read::findMessage(fixture.state, "needle", SearchScope::HeaderAndText));
    CHECK(showing(fixture) == 2);
    REQUIRE(
        message_read::findMessage(fixture.state, "needle", SearchScope::HeaderAndText));
    CHECK(showing(fixture) == 3);
    // The end of the area is the end of the search: nothing wraps round, and
    // the reader is left where it stood.
    CHECK_FALSE(
        message_read::findMessage(fixture.state, "needle", SearchScope::HeaderAndText));
    CHECK(showing(fixture) == 3);
}

TEST_CASE("Different words start again from where the reader stands [find][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path(), plain());
    putMessages(fixture, {says({"needle and thread"}), says({"needle"})});
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());

    REQUIRE(
        message_read::findMessage(fixture.state, "needle", SearchScope::HeaderAndText));
    CHECK(showing(fixture) == 1);
    // Another query, so the message on screen is searched again rather than
    // stepped over.
    REQUIRE(
        message_read::findMessage(fixture.state, "thread", SearchScope::HeaderAndText));
    CHECK(showing(fixture) == 1);
}

TEST_CASE("The header is searched, and the text only where it was asked for "
          "[find][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path(), plain());
    putMessages(fixture, {about("About cats"), from("Ivan Ivanov", {"cats everywhere"})});
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());

    // Header only: the subject of the first message answers, and the body of
    // the second is not read at all.
    REQUIRE(message_read::findMessage(fixture.state, "cats", SearchScope::Header));
    CHECK(showing(fixture) == 1);
    CHECK_FALSE(message_read::findMessage(fixture.state, "cats", SearchScope::Header));

    // A name, and an address the message was written from.
    CHECK(message_read::findMessage(fixture.state, "ivanov", SearchScope::Header));
    CHECK(showing(fixture) == 2);
    CHECK(message_read::findMessage(fixture.state, "2:5020/1042", SearchScope::Header));
}

TEST_CASE("The header block lights what a search found in it [find][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path(), plain());
    putMessages(fixture, {about("About cats")});
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());
    REQUIRE(message_read::findMessage(fixture.state, "cats", SearchScope::Header));

    // Nothing in the body, so nothing among the rows — but the frame still
    // draws, which is what says the header block is painted rather than thrown.
    CHECK(highlighted(fixture).empty());
    term::Screen screen(fixture.state.width, fixture.state.height);
    term::render(screen, message_read::render(fixture.state));
    CHECK(fixture.state.findHighlight == "cats");
}

TEST_CASE("Moving to another message takes the highlight off [find][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path(), plain());
    putMessages(fixture, {says({"nothing"}), says({"needle"}), says({"needle"})});
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());
    REQUIRE(
        message_read::findMessage(fixture.state, "needle", SearchScope::HeaderAndText));
    REQUIRE(showing(fixture) == 2);
    REQUIRE_FALSE(highlighted(fixture).empty());

    // → is the next message, and it is nobody's search result even though the
    // same word stands in it.
    REQUIRE(message_read::handleEvent(fixture.state, Event::ArrowRight));
    CHECK(showing(fixture) == 3);
    CHECK(fixture.state.findHighlight.empty());
    CHECK(highlighted(fixture).empty());
}

TEST_CASE("Every occurrence in the message is lit [find][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path(), plain());
    putMessages(fixture, {says({"needle one", "and needle two", "no more"})});
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());

    REQUIRE(
        message_read::findMessage(fixture.state, "NEEDLE", SearchScope::HeaderAndText));
    CHECK(highlighted(fixture) == std::vector<std::string>{"needle", "needle"});
}

TEST_CASE("The reader scrolls to the occurrence [find][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path(), plain());
    std::vector<std::string> lines(80, "filler");
    lines.push_back("the needle at the end");
    putMessages(fixture, {says(lines)});
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());
    REQUIRE(fixture.state.readScroll == 0);

    REQUIRE(
        message_read::findMessage(fixture.state, "needle", SearchScope::HeaderAndText));
    CHECK(fixture.state.readScroll > 0);
    // And what it scrolled to is on the screen.
    const int last = fixture.state.readScroll + fixture.state.readRows();
    bool visible = false;
    for (int i = fixture.state.readScroll; i < last; ++i) {
        visible |= !fixture.state.readLines[static_cast<size_t>(i)].found.empty();
    }
    CHECK(visible);
}

TEST_CASE("A search reads the charset the message declares [find][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path(), plain());
    fixture.config.defaultCharset = "CP866";
    // Two Russian messages, one written in each of the two charsets Russian
    // echoes use. The words typed are the same UTF-8 either way; whether they
    // are found is what says the message was folded by its own charset.
    putMessages(fixture, {says({"nothing"}), saysIn("KOI8-R", {"Привет, мир"}),
                          saysIn("CP866", {"Привет, мир"})});
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());

    REQUIRE(
        message_read::findMessage(fixture.state, "ПРИВЕТ", SearchScope::HeaderAndText));
    CHECK(showing(fixture) == 2);
    CHECK(highlighted(fixture) == std::vector<std::string>{"Привет"});

    // The Russian quirks are CP866's alone: a Latin p for the Cyrillic р finds
    // the CP866 message and steps over the KOI8-R one.
    AreaFixture other(base.path(), plain());
    REQUIRE(message_list::enterArea(other.state, other.area).has_value());
    REQUIRE(message_read::findMessage(other.state, "Пpивет", SearchScope::HeaderAndText));
    CHECK(other.state.readHeader->number == 3);
}

TEST_CASE("A search steps over the twits the reader would step over "
          "[find][twit][squish]") {
    TempSquishBase base;
    AppConfig config = plain();
    config.twitMode = TwitMode::Ignore;
    amberedit::config::TwitRule rule;
    rule.name = "Ivan Ivanov";
    config.twits.push_back(rule);

    TempSquishBase copy;
    AreaFixture fixture(copy.path(), config);
    putMessages(fixture,
                {from("Petr Petrov", {"nothing"}), from("Ivan Ivanov", {"needle"}),
                 from("Petr Petrov", {"needle"})});
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());

    // `ignore` is navigation: the twit holding the word is walked past exactly
    // as → walks past it.
    REQUIRE(
        message_read::findMessage(fixture.state, "needle", SearchScope::HeaderAndText));
    CHECK(showing(fixture) == 3);
}

TEST_CASE("A twit blank is found and opens behind the notice [find][twit][squish]") {
    TempSquishBase base;
    AppConfig config = plain();
    config.twitMode = TwitMode::Blank;
    amberedit::config::TwitRule rule;
    rule.name = "Ivan Ivanov";
    config.twits.push_back(rule);

    AreaFixture fixture(base.path(), config);
    putMessages(fixture,
                {from("Petr Petrov", {"nothing"}), from("Ivan Ivanov", {"needle"})});
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());

    // `blank` is not navigation — the message is found, and what stands in
    // place of its text is the notice.
    REQUIRE(
        message_read::findMessage(fixture.state, "needle", SearchScope::HeaderAndText));
    CHECK(showing(fixture) == 2);
    REQUIRE(fixture.state.readLines.size() == 1);
    CHECK(fixture.state.readLines[0].text ==
          "This is a twit message. Press Space to view it");
    CHECK(highlighted(fixture).empty());

    // Space shows it, and the occurrence is lit in what appears.
    REQUIRE(message_read::handleEvent(fixture.state, Event::Character(' ')));
    CHECK(highlighted(fixture) == std::vector<std::string>{"needle"});
}

TEST_CASE("Words that are nowhere leave the reader as it was [find][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path(), plain());
    putMessages(fixture, {says({"nothing"}), says({"nothing either"})});
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());

    CHECK_FALSE(
        message_read::findMessage(fixture.state, "needle", SearchScope::HeaderAndText));
    CHECK(showing(fixture) == 1);
    CHECK(fixture.state.findHighlight.empty());

    // And a query of nothing at all is not a search.
    CHECK_FALSE(
        message_read::findMessage(fixture.state, "   ", SearchScope::HeaderAndText));
}

TEST_CASE("Ctrl-F opens the find box on what was last looked for [find][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path(), plain());
    putMessages(fixture, {says({"needle"})});
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());

    REQUIRE(message_read::handleEvent(fixture.state, Event::Character("f", true)));
    REQUIRE(fixture.state.findPicker);
    CHECK(fixture.state.findPicker->query.empty());

    // Typed into, answered, and opened again: the words are still there, with
    // the cursor at the end of them.
    for (const char c : std::string("needle")) {
        find_dialog::handleEvent(fixture.state, Event::Character(c));
    }
    CHECK(find_dialog::handleEvent(fixture.state, Event::Return) ==
          find_dialog::Outcome::Search);
    REQUIRE(message_read::findMessage(fixture.state, fixture.state.findPicker->query,
                                      fixture.state.findPicker->scope));
    fixture.state.findPicker.reset();

    REQUIRE(message_read::handleEvent(fixture.state, Event::F6));
    REQUIRE(fixture.state.findPicker);
    CHECK(fixture.state.findPicker->query == "needle");
    CHECK(fixture.state.findPicker->cursor == 6);
}

TEST_CASE("The reader's menu offers Find [find][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path(), plain());
    putMessages(fixture, {says({"needle"})});
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());

    // Between Forward and Nodelist, which is where the default menu puts it.
    message_read::openMenu(fixture.state);
    REQUIRE(fixture.state.menuView);
    const auto& items = fixture.state.menuView->items;
    REQUIRE(items.size() >= 2);
    CHECK(items[items.size() - 2].command == amberedit::config::Command::ReaderFind);
    CHECK(items[items.size() - 2].enabled);

    fixture.state.menuView.reset();
    message_read::runMenuCommand(fixture.state, amberedit::config::Command::ReaderFind);
    CHECK(fixture.state.findPicker);
}

TEST_CASE("The find box refuses an empty query [find]") {
    TempSquishBase base;
    AreaFixture fixture(base.path(), plain());
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());

    find_dialog::open(fixture.state);
    REQUIRE(fixture.state.findPicker);
    CHECK(find_dialog::handleEvent(fixture.state, Event::Return) ==
          find_dialog::Outcome::Ignored);
    CHECK_FALSE(fixture.state.findPicker->error.empty());
    // And the box stays up: the words are still to be typed.
    CHECK(fixture.state.findPicker);
}

TEST_CASE("The find box asks how much of a message to read [find]") {
    TempSquishBase base;
    AreaFixture fixture(base.path(), plain());
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());

    find_dialog::open(fixture.state);
    REQUIRE(fixture.state.findPicker);
    CHECK(fixture.state.findPicker->scope == SearchScope::HeaderAndText);

    // Tab reaches the two radio buttons and the arrows walk them, one under
    // the other.
    find_dialog::handleEvent(fixture.state, Event::Tab);
    CHECK(fixture.state.findPicker->focus == AppState::FindPicker::Focus::Scope);
    find_dialog::handleEvent(fixture.state, Event::ArrowDown);
    CHECK(fixture.state.findPicker->scope == SearchScope::Header);
    find_dialog::handleEvent(fixture.state, Event::ArrowDown);
    CHECK(fixture.state.findPicker->scope == SearchScope::HeaderAndText);

    // And Tab again is the Find button, the ring's third stop, which searches
    // exactly as Enter does.
    find_dialog::handleEvent(fixture.state, Event::Tab);
    CHECK(fixture.state.findPicker->focus == AppState::FindPicker::Focus::Button);
    CHECK(find_dialog::handleEvent(fixture.state, Event::Character(' ')) ==
          find_dialog::Outcome::Ignored);  // nothing typed to look for yet

    // Esc puts it away without searching.
    CHECK(find_dialog::handleEvent(fixture.state, Event::Escape) ==
          find_dialog::Outcome::Dismissed);
    CHECK_FALSE(fixture.state.findPicker);
}
