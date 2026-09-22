#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "temp_squish_base.hpp"
#include "ui/app_state.hpp"
#include "ui/area_fixture.hpp"
#include "ui/mark_dialog.hpp"
#include "ui/message_marks.hpp"
#include "ui/progress_dialog.hpp"
#include "ui/progress_run.hpp"
#include "ui/screens/message_list_screen.hpp"
#include "ui/screens/message_read_screen.hpp"
#include "ui/term/element.hpp"
#include "ui/term/screen.hpp"

using amberedit::test::AreaFixture;
using amberedit::test::TempSquishBase;
using amberedit::ui::AppState;
using amberedit::ui::Millis;

namespace marks = amberedit::ui::marks;
namespace message_list = amberedit::ui::screens::message_list;
namespace message_read = amberedit::ui::screens::message_read;
namespace progress_dialog = amberedit::ui::progress_dialog;
namespace term = amberedit::ui::term;

namespace {

using Doing = AppState::Progress::Doing;

/// The area open, with the reader on its first message.
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

/// The shell's three, so that a run can be watched and broken off without a
/// terminal: the clock the box times itself by, the frame it draws, and the key
/// that stops it.
///
/// The clock jumps a second at every reading, which makes every message of a run
/// a frame of the box — what a run long enough to be worth counting does anyway,
/// and what lets a test say which message Escape landed on.
struct Watch {
    /// Every frame the run drew, as what the box was saying at the time.
    std::vector<AppState::Progress> frames;
    /// Which of them Escape lands on, counted from one. Zero is nobody pressing
    /// anything, which is every other test in the tree.
    int escapeAt{0};
    Millis now{0};
};

void watch(AppState& state, Watch& seen) {
    state.monotonicMs = [&seen] {
        seen.now += 1000;
        return seen.now;
    };
    state.drawFrame = [&state, &seen] {
        REQUIRE(state.progress);
        seen.frames.push_back(*state.progress);
    };
    state.escapePressed = [&seen] {
        return seen.escapeAt != 0 &&
               static_cast<int>(seen.frames.size()) >= seen.escapeAt;
    };
}

/// Every row of the box as it reaches the terminal.
std::vector<std::string> boxRows(AppState& state) {
    term::Screen screen(state.width, state.height);
    term::render(screen, progress_dialog::render(state, term::text("")));

    std::vector<std::string> rows;
    for (int y = 0; y < state.height; ++y) {
        std::string row;
        for (int x = 0; x < state.width; ++x) row += screen.at(x, y).glyph;
        rows.push_back(row);
    }
    return rows;
}

bool saysSomewhere(AppState& state, const std::string& words) {
    for (const std::string& row : boxRows(state)) {
        if (row.find(words) != std::string::npos) return true;
    }
    return false;
}

}  // namespace

TEST_CASE("A run over the marked messages counts them on the box [progress][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    enter(fixture);
    REQUIRE(fixture.state.messageCount > 5);
    Watch seen;
    watch(fixture.state, seen);

    marks::toggle(fixture.state, 2);
    marks::toggle(fixture.state, 3);
    marks::toggle(fixture.state, 5);

    message_read::deleteMarked(fixture.state);

    REQUIRE(seen.frames.size() == 3);
    for (size_t at = 0; at < seen.frames.size(); ++at) {
        CHECK(seen.frames[at].doing == Doing::Delete);
        CHECK(seen.frames[at].done == at + 1);
        CHECK(seen.frames[at].total == 3);
    }
    // And the box is gone the moment the run is over: the frame the shell draws
    // next is the screen with nothing over it.
    CHECK_FALSE(fixture.state.progress);
}

TEST_CASE("A run too short to read never puts the box up [progress][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    enter(fixture);
    Watch seen;
    watch(fixture.state, seen);
    // A clock that does not move is an operation that takes no time at all.
    fixture.state.monotonicMs = [] { return Millis{0}; };

    marks::toggle(fixture.state, 1);
    marks::toggle(fixture.state, 2);
    message_read::deleteMarked(fixture.state);

    CHECK(seen.frames.empty());
    CHECK(fixture.state.marks.empty());
}

TEST_CASE("Escape stops a delete and leaves the rest marked [progress][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    enter(fixture);
    const uint32_t total = fixture.state.messageCount;
    REQUIRE(total > 5);
    Watch seen;
    watch(fixture.state, seen);
    seen.escapeAt = 2;

    marks::toggle(fixture.state, 2);
    marks::toggle(fixture.state, 3);
    marks::toggle(fixture.state, 5);

    message_read::deleteMarked(fixture.state);

    // The sweep runs backwards, so the message it reached first is the last of
    // the run — and it is the only one gone.
    CHECK(fixture.state.messageCount == total - 1);
    // The two it never reached are still here, and still marked: the stars are
    // what the user would gather them by again.
    CHECK(markedNumbers(fixture) == std::vector<uint32_t>{2, 3});
}

TEST_CASE("The box says what is being done and to which message [progress][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    enter(fixture);

    AppState::Progress progress;
    progress.doing = Doing::Delete;
    progress.done = 13423;
    progress.total = 112344;
    fixture.state.progress = progress;

    CHECK(saysSomewhere(fixture.state, "Deleting messages..."));
    CHECK(saysSomewhere(fixture.state, "message 13423 of 112344"));
    CHECK(saysSomewhere(fixture.state, "Esc cancel"));

    // The word is the pass's own: a copy reads the messages off this base before
    // it writes any of them into the other area, and says so while it does.
    fixture.state.progress->doing = Doing::Read;
    CHECK(saysSomewhere(fixture.state, "Reading messages..."));
    fixture.state.progress->doing = Doing::Copy;
    CHECK(saysSomewhere(fixture.state, "Copying messages..."));
    fixture.state.progress->doing = Doing::Move;
    CHECK(saysSomewhere(fixture.state, "Moving messages..."));

    // And the key is offered only where it does something. The half of a move
    // that takes the messages out of this area, once the other one holds them,
    // goes through whatever is pressed.
    fixture.state.progress->doing = Doing::Delete;
    fixture.state.progress->breakable = false;
    CHECK_FALSE(saysSomewhere(fixture.state, "Esc cancel"));
    CHECK(saysSomewhere(fixture.state, "message 13423 of 112344"));
}
