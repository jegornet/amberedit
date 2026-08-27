#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "config/text_util.hpp"
#include "temp_squish_base.hpp"
#include "test_strings.hpp"
#include "ui/app_state.hpp"
#include "ui/area_fixture.hpp"
#include "ui/export_dialog.hpp"
#include "ui/export_mode_dialog.hpp"
#include "ui/mark_dialog.hpp"
#include "ui/message_marks.hpp"
#include "ui/scope_dialog.hpp"
#include "ui/screens/message_list_screen.hpp"
#include "ui/screens/message_read_screen.hpp"
#include "ui/term/element.hpp"
#include "ui/term/event.hpp"
#include "ui/term/screen.hpp"

using amberedit::test::AreaFixture;
using amberedit::test::contains;
using amberedit::test::TempSquishBase;
using amberedit::ui::term::Event;

namespace scope_dialog = amberedit::ui::scope_dialog;
namespace export_dialog = amberedit::ui::export_dialog;
namespace export_mode_dialog = amberedit::ui::export_mode_dialog;
namespace marks = amberedit::ui::marks;
namespace mark_dialog = amberedit::ui::mark_dialog;
namespace message_list = amberedit::ui::screens::message_list;
namespace message_read = amberedit::ui::screens::message_read;
namespace term = amberedit::ui::term;

namespace {

using Action = amberedit::ui::AppState::MarkPicker::Action;
using ScopeMode = amberedit::ui::AppState::ScopePicker::Mode;
using ForwardMode = amberedit::ui::AppState::ForwardPicker::Mode;

/// The area open, with the reader standing on its first message — what both
/// screens are driven from below.
void enter(AreaFixture& fixture) {
    fixture.state.height = 24;
    fixture.state.width = 100;
    fixture.lastRead->set(0);
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());
}

/// Which messages are marked, by number, in the order they stand in the area.
std::vector<uint32_t> markedNumbers(const AreaFixture& fixture) {
    std::vector<uint32_t> numbers;
    for (uint32_t number = 1; number <= fixture.state.messageCount; ++number) {
        if (marks::isMarked(fixture.state, number)) numbers.push_back(number);
    }
    return numbers;
}

/// One row of the message list as it reaches the terminal — row 3 is the first,
/// the title, the headings and the rule standing over them.
std::string listRow(AreaFixture& fixture, int row) {
    term::Screen screen(fixture.state.width, fixture.state.height);
    term::render(screen, message_list::render(fixture.state));
    std::string text;
    for (int x = 0; x < fixture.state.width; ++x) text += screen.at(x, row).glyph;
    return text;
}

/// The reader's title row, as drawn.
std::string readerTitle(AreaFixture& fixture) {
    term::Screen screen(fixture.state.width, fixture.state.height);
    message_read::relayout(fixture.state);
    term::render(screen, message_read::render(fixture.state));
    std::string text;
    for (int x = 0; x < fixture.state.width; ++x) text += screen.at(x, 0).glyph;
    while (!text.empty() && text.back() == ' ') text.pop_back();
    return text;
}

}  // namespace

TEST_CASE("A mark is toggled on and off one message at a time [marks][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    enter(fixture);
    REQUIRE(fixture.state.messageCount > 3);

    CHECK_FALSE(marks::isMarked(fixture.state, 2));
    marks::toggle(fixture.state, 2);
    CHECK(marks::isMarked(fixture.state, 2));
    CHECK_FALSE(marks::isMarked(fixture.state, 3));
    marks::toggle(fixture.state, 2);
    CHECK_FALSE(marks::isMarked(fixture.state, 2));
}

TEST_CASE("Nothing outside the area can be marked [marks][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    enter(fixture);

    marks::toggle(fixture.state, 0);
    marks::toggle(fixture.state, fixture.state.messageCount + 1);
    CHECK(fixture.state.marks.empty());
    CHECK_FALSE(marks::isMarked(fixture.state, 0));
    CHECK_FALSE(marks::isMarked(fixture.state, fixture.state.messageCount + 1));
}

TEST_CASE("A mark follows its message when the area is renumbered [marks][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    enter(fixture);
    REQUIRE(fixture.state.messageCount > 5);

    // Two marks after the message about to go, so both numbers move.
    marks::toggle(fixture.state, 4);
    marks::toggle(fixture.state, 5);
    REQUIRE(fixture.state.base != nullptr);
    REQUIRE(fixture.state.base->remove(2).has_value());
    fixture.state.messageCount = fixture.state.base->count();
    fixture.state.headers.clear();

    // Everything after the deleted message moved up one, and the marks moved
    // with the messages rather than staying on the numbers.
    CHECK(markedNumbers(fixture) == std::vector<uint32_t>{3, 4});
}

TEST_CASE("The mark box's five answers [marks][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    enter(fixture);
    const auto total = fixture.state.messageCount;
    REQUIRE(total > 5);

    // Everything the box does that counts from somewhere counts from the
    // message the reader is showing.
    message_read::goToMessage(fixture.state, 3);
    REQUIRE(fixture.state.readHeader);
    REQUIRE(fixture.state.readHeader->number == 3);

    SUBCASE("All msgs marks the area entire") {
        mark_dialog::apply(fixture.state, Action::All);
        CHECK(fixture.state.marks.size() == total);
        CHECK(marks::isMarked(fixture.state, 1));
        CHECK(marks::isMarked(fixture.state, total));
    }
    SUBCASE("Unmark all empties the set") {
        mark_dialog::apply(fixture.state, Action::All);
        mark_dialog::apply(fixture.state, Action::UnmarkAll);
        CHECK(fixture.state.marks.empty());
    }
    SUBCASE("Toggle marks turns the set inside out") {
        marks::toggle(fixture.state, 2);
        marks::toggle(fixture.state, 4);
        mark_dialog::apply(fixture.state, Action::Toggle);
        CHECK(fixture.state.marks.size() == total - 2);
        CHECK_FALSE(marks::isMarked(fixture.state, 2));
        CHECK_FALSE(marks::isMarked(fixture.state, 4));
        CHECK(marks::isMarked(fixture.state, 1));
        CHECK(marks::isMarked(fixture.state, 3));
    }
    SUBCASE("New msgs marks everything after the current one, itself excluded") {
        mark_dialog::apply(fixture.state, Action::Newer);
        CHECK_FALSE(marks::isMarked(fixture.state, 3));
        CHECK(marks::isMarked(fixture.state, 4));
        CHECK(marks::isMarked(fixture.state, total));
        CHECK(fixture.state.marks.size() == total - 3);
    }
    SUBCASE("Old msgs marks everything before it, itself excluded") {
        mark_dialog::apply(fixture.state, Action::Older);
        CHECK(marks::isMarked(fixture.state, 1));
        CHECK(marks::isMarked(fixture.state, 2));
        CHECK_FALSE(marks::isMarked(fixture.state, 3));
        CHECK(fixture.state.marks.size() == 2);
    }
    SUBCASE("Marking a run adds to what is already marked") {
        marks::toggle(fixture.state, 1);
        mark_dialog::apply(fixture.state, Action::Newer);
        CHECK(marks::isMarked(fixture.state, 1));
        CHECK(marks::isMarked(fixture.state, 4));
    }
}

TEST_CASE("The mark box does not open on an area with nothing in it [marks][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    enter(fixture);
    message_read::showEmptyArea(fixture.state);

    mark_dialog::open(fixture.state);
    CHECK_FALSE(fixture.state.markPicker);
}

TEST_CASE("The mark box answers its letters and its arrows [marks][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    enter(fixture);
    mark_dialog::open(fixture.state);
    REQUIRE(fixture.state.markPicker);

    SUBCASE("it opens on the first answer") {
        CHECK(fixture.state.markPicker->action == Action::All);
    }
    SUBCASE("an arrow walks the column and wraps round it") {
        CHECK(mark_dialog::handleEvent(fixture.state, Event::ArrowDown) ==
              mark_dialog::Outcome::Ignored);
        CHECK(fixture.state.markPicker->action == Action::UnmarkAll);
        CHECK(mark_dialog::handleEvent(fixture.state, Event::ArrowUp) ==
              mark_dialog::Outcome::Ignored);
        CHECK(mark_dialog::handleEvent(fixture.state, Event::ArrowUp) ==
              mark_dialog::Outcome::Ignored);
        CHECK(fixture.state.markPicker->action == Action::Older);
    }
    SUBCASE("a letter answers outright") {
        CHECK(mark_dialog::handleEvent(fixture.state, Event::Character('n')) ==
              mark_dialog::Outcome::Picked);
        CHECK(fixture.state.markPicker->action == Action::Newer);
    }
    SUBCASE("Esc puts it away and marks nothing") {
        CHECK(mark_dialog::handleEvent(fixture.state, Event::Escape) ==
              mark_dialog::Outcome::Dismissed);
        CHECK_FALSE(fixture.state.markPicker);
        CHECK(fixture.state.marks.empty());
    }
}

TEST_CASE("The message list marks the row under the cursor [marks][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    enter(fixture);
    fixture.state.messageCursor = 1;  // the second message

    SUBCASE("the bound key") {
        CHECK(message_list::handleEvent(fixture.state, Event::Character('t')));
    }
    SUBCASE("and Space, which no layout binds") {
        CHECK(message_list::handleEvent(fixture.state, Event::Character(' ')));
    }
    CHECK(markedNumbers(fixture) == std::vector<uint32_t>{2});
}

TEST_CASE("The reader marks the message it is showing [marks][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    enter(fixture);
    message_read::goToMessage(fixture.state, 3);

    REQUIRE(message_read::handleEvent(fixture.state, Event::Character('t')));
    CHECK(markedNumbers(fixture) == std::vector<uint32_t>{3});
    REQUIRE(message_read::handleEvent(fixture.state, Event::Character('t')));
    CHECK(fixture.state.marks.empty());
}

TEST_CASE("A marked message wears a star in both places it is shown [marks][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    enter(fixture);
    message_read::goToMessage(fixture.state, 1);
    message_list::centerCursor(fixture.state);
    message_list::ensureHeaders(fixture.state);

    // Which message of how many, as the reader's title writes it.
    const std::string pair = "1/" + std::to_string(fixture.state.messageCount);

    const std::string plainRow = listRow(fixture, 3);
    CHECK(plainRow.find('*') == std::string::npos);
    CHECK_FALSE(contains(readerTitle(fixture), pair + "*"));

    marks::toggle(fixture.state, 1);

    // The star stands in the blank column beside the number, so the row is
    // exactly as wide as it was and every field after it is where it was: one
    // character of the row differs and no other.
    const std::string markedRow = listRow(fixture, 3);
    const size_t star = markedRow.find('*');
    REQUIRE(star != std::string::npos);
    REQUIRE(star > 0);
    CHECK(markedRow[star - 1] == '1');
    CHECK(plainRow[star] == ' ');
    std::string blanked = markedRow;
    blanked[star] = ' ';
    CHECK(blanked == plainRow);
    // And in the reader's title, right after the pair naming the message.
    CHECK(contains(readerTitle(fixture), pair + "*"));
}

TEST_CASE("Leaving the area takes its marks with it [marks][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    enter(fixture);
    marks::toggle(fixture.state, 1);
    REQUIRE_FALSE(fixture.state.marks.empty());

    message_list::leaveArea(fixture.state);
    CHECK(fixture.state.marks.empty());
}

TEST_CASE("Delete asks yes or no while nothing is marked [marks][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    enter(fixture);
    message_read::goToMessage(fixture.state, 3);

    REQUIRE(message_read::handleEvent(fixture.state, Event::Character('d')));
    CHECK_FALSE(fixture.state.scopePicker);
    CHECK(fixture.state.confirm == amberedit::ui::AppState::Confirm::DeleteMessage);
}

TEST_CASE("Delete asks which messages once anything is marked [marks][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    enter(fixture);
    message_read::goToMessage(fixture.state, 3);
    marks::toggle(fixture.state, 1);
    marks::toggle(fixture.state, 2);

    REQUIRE(message_read::handleEvent(fixture.state, Event::Character('d')));
    REQUIRE(fixture.state.scopePicker);
    CHECK(fixture.state.confirm == amberedit::ui::AppState::Confirm::None);
    // It opens on the answer the marks raised, and says how many they are.
    CHECK(fixture.state.scopePicker->mode == ScopeMode::Marked);
    CHECK(fixture.state.scopePicker->marked == 2);

    SUBCASE("→ walks the three and wraps round them") {
        CHECK(scope_dialog::handleEvent(fixture.state, Event::ArrowRight) ==
              scope_dialog::Outcome::Ignored);
        CHECK(fixture.state.scopePicker->mode == ScopeMode::Current);
        CHECK(scope_dialog::handleEvent(fixture.state, Event::ArrowRight) ==
              scope_dialog::Outcome::Ignored);
        CHECK(fixture.state.scopePicker->mode == ScopeMode::Cancel);
        CHECK(scope_dialog::handleEvent(fixture.state, Event::ArrowRight) ==
              scope_dialog::Outcome::Ignored);
        CHECK(fixture.state.scopePicker->mode == ScopeMode::Marked);
    }
    SUBCASE("Esc puts it away and deletes nothing") {
        const auto before = fixture.state.messageCount;
        CHECK(scope_dialog::handleEvent(fixture.state, Event::Escape) ==
              scope_dialog::Outcome::Dismissed);
        CHECK_FALSE(fixture.state.scopePicker);
        CHECK(fixture.state.messageCount == before);
        CHECK(fixture.state.marks.size() == 2);
    }
    SUBCASE("Cancel is an answer that does nothing either") {
        CHECK(scope_dialog::handleEvent(fixture.state, Event::ArrowLeft) ==
              scope_dialog::Outcome::Ignored);
        CHECK(fixture.state.scopePicker->mode == ScopeMode::Cancel);
        CHECK(scope_dialog::handleEvent(fixture.state, Event::Return) ==
              scope_dialog::Outcome::Picked);
    }
}

TEST_CASE("Deleting the marked messages takes exactly those [marks][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    enter(fixture);
    const auto total = fixture.state.messageCount;
    REQUIRE(total > 6);

    // The subjects either side of the run, so that what survived can be named
    // rather than counted.
    const std::string kept = fixture.state.base->header(4).subject;
    marks::toggle(fixture.state, 2);
    marks::toggle(fixture.state, 3);
    marks::toggle(fixture.state, 5);
    message_read::goToMessage(fixture.state, 4);

    message_read::deleteMarked(fixture.state);

    CHECK(fixture.state.messageCount == total - 3);
    // The reader is still on the message it was showing, wherever the
    // renumbering put it.
    REQUIRE(fixture.state.readHeader);
    CHECK(fixture.state.readHeader->number == 2);
    CHECK(fixture.state.readHeader->subject == kept);
    // And the marks went with the messages they named.
    CHECK(fixture.state.marks.empty());
}

TEST_CASE("Deleting a run the reader stood in lands on what came before [marks]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    enter(fixture);
    REQUIRE(fixture.state.messageCount > 5);
    const std::string before = fixture.state.base->header(1).subject;

    marks::toggle(fixture.state, 2);
    marks::toggle(fixture.state, 3);
    message_read::goToMessage(fixture.state, 3);

    message_read::deleteMarked(fixture.state);

    REQUIRE(fixture.state.readHeader);
    CHECK(fixture.state.readHeader->number == 1);
    CHECK(fixture.state.readHeader->subject == before);
}

TEST_CASE("Deleting a run off the top lands on the first message left [marks]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    enter(fixture);
    REQUIRE(fixture.state.messageCount > 4);
    const std::string third = fixture.state.base->header(3).subject;

    marks::toggle(fixture.state, 1);
    marks::toggle(fixture.state, 2);
    message_read::goToMessage(fixture.state, 1);

    message_read::deleteMarked(fixture.state);

    REQUIRE(fixture.state.readHeader);
    CHECK(fixture.state.readHeader->number == 1);
    CHECK(fixture.state.readHeader->subject == third);
}

TEST_CASE("Deleting every message leaves the reader on an empty area [marks]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    enter(fixture);
    mark_dialog::apply(fixture.state, Action::All);

    message_read::deleteMarked(fixture.state);

    CHECK(fixture.state.messageCount == 0);
    CHECK_FALSE(fixture.state.readHeader);
    CHECK(fixture.state.marks.empty());
}

TEST_CASE("w asks which messages once anything is marked [marks][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    enter(fixture);
    message_read::goToMessage(fixture.state, 2);

    SUBCASE("with nothing marked it opens the file picker straight away") {
        REQUIRE(message_read::handleEvent(fixture.state, Event::Character('w')));
        CHECK_FALSE(fixture.state.scopePicker);
        REQUIRE(fixture.state.exportPicker);
        CHECK_FALSE(fixture.state.exportPicker->marked);
    }
    SUBCASE("with a set standing it asks which messages first") {
        marks::toggle(fixture.state, 1);
        REQUIRE(message_read::handleEvent(fixture.state, Event::Character('w')));
        REQUIRE(fixture.state.scopePicker);
        CHECK_FALSE(fixture.state.exportPicker);
        CHECK(fixture.state.scopePicker->purpose ==
              amberedit::ui::AppState::ScopePicker::For::Export);
        CHECK(fixture.state.scopePicker->marked == 1);
    }
}

TEST_CASE("The files a message carries are asked about first [marks][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    enter(fixture);
    marks::toggle(fixture.state, 1);

    // What `w` finds in a message carrying a uuencoded file: the question about
    // the files, and no scope question over it.
    export_mode_dialog::open(fixture.state,
                             {amberedit::app::UueFile{"report.zip", "PK"}});
    REQUIRE(fixture.state.exportModePicker);
    CHECK_FALSE(fixture.state.scopePicker);

    // Answered with the files, the file picker follows and is about this one
    // message: the decoding never looked at the marked set.
    export_dialog::open(fixture.state, {amberedit::app::UueFile{"report.zip", "PK"}},
                        /*marked=*/true);
    REQUIRE(fixture.state.exportPicker);
    CHECK(fixture.state.exportPicker->mode ==
          amberedit::ui::AppState::ExportPicker::Mode::Uue);
    CHECK_FALSE(fixture.state.exportPicker->marked);
}

namespace {

/// How many times `what` stands in `text`.
size_t countOf(const std::string& text, const std::string& what) {
    size_t found = 0;
    for (size_t at = text.find(what); at != std::string::npos;
         at = text.find(what, at + what.size())) {
        ++found;
    }
    return found;
}

/// The Subj row a message is written under, whole, so that one subject cannot be
/// found inside another.
std::string subjectRow(const AreaFixture& fixture, uint32_t number) {
    return "Subj : " + fixture.state.base->header(number).subject + "\n";
}

/// What the export left in `name`, in the directory the base is in.
std::string exported(const TempSquishBase& base, const std::string& name) {
    return amberedit::test::valueOf(
        amberedit::config::text::readFile((base.dir() / name).string()));
}

}  // namespace

TEST_CASE("The marked messages are written into the one file [marks][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    enter(fixture);
    REQUIRE(fixture.state.messageCount > 7);

    // Three messages of different subjects: two marked and one the reader is
    // standing on, so that what went into the file can be told apart.
    const std::string fifth = subjectRow(fixture, 5);
    const std::string sixth = subjectRow(fixture, 6);
    const std::string seventh = subjectRow(fixture, 7);
    REQUIRE(fifth != sixth);
    REQUIRE(sixth != seventh);
    marks::toggle(fixture.state, 5);
    marks::toggle(fixture.state, 7);
    message_read::goToMessage(fixture.state, 6);

    // What the scope box's Marked answer leads to, as the shell puts it up.
    export_dialog::open(fixture.state, {}, /*marked=*/true);
    REQUIRE(fixture.state.exportPicker);
    CHECK(fixture.state.exportPicker->marked);
    fixture.state.exportDirectory = base.dir().string();
    fixture.state.exportName = "digest.txt";

    REQUIRE(export_dialog::handleEvent(fixture.state, Event::Return) ==
            export_dialog::Outcome::Written);

    const std::string written = exported(base, "digest.txt");
    // Two messages went in and no more — the one the reader was on is not one of
    // them — and each is a block of its own rather than a file written twice
    // over.
    CHECK(countOf(written, "Area : localnet") == 2);
    CHECK_MESSAGE(contains(written, fifth), written);
    CHECK_MESSAGE(contains(written, seventh), written);
    CHECK_FALSE_MESSAGE(contains(written, sixth), written);
    // In the order they stand in the area.
    CHECK(written.find(fifth) < written.find(seventh));
}

TEST_CASE("The export scope answered Current writes the one message [marks]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    enter(fixture);
    REQUIRE(fixture.state.messageCount > 6);

    const std::string fifth = subjectRow(fixture, 5);
    const std::string sixth = subjectRow(fixture, 6);
    REQUIRE(fifth != sixth);
    marks::toggle(fixture.state, 5);
    message_read::goToMessage(fixture.state, 6);

    export_dialog::open(fixture.state, {}, /*marked=*/false);
    REQUIRE(fixture.state.exportPicker);
    fixture.state.exportDirectory = base.dir().string();
    fixture.state.exportName = "one.txt";

    REQUIRE(export_dialog::handleEvent(fixture.state, Event::Return) ==
            export_dialog::Outcome::Written);

    const std::string written = exported(base, "one.txt");
    CHECK(countOf(written, "Area : localnet") == 1);
    CHECK_MESSAGE(contains(written, sixth), written);
    CHECK_FALSE_MESSAGE(contains(written, fifth), written);
}
