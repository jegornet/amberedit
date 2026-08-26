#include "ui/after_handover.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <functional>
#include <string>

#include "app/message_builder.hpp"
#include "msgbase/ftn_msgbase.hpp"
#include "temp_squish_base.hpp"
#include "ui/area_fixture.hpp"
#include "ui/screens/message_list_screen.hpp"
#include "ui/screens/message_read_screen.hpp"
#include "ui/term/event.hpp"

using amberedit::app::ScreenId;
using amberedit::test::AreaFixture;
using amberedit::test::TempSquishBase;
using amberedit::ui::term::Event;

namespace after_handover = amberedit::ui::after_handover;
namespace message_list = amberedit::ui::screens::message_list;
namespace message_read = amberedit::ui::screens::message_read;

namespace {

/// The tosser, as far as the reader is concerned: a second handle on the same
/// files, written through while the reader's own base is open and knows nothing
/// of it.
///
/// This is the whole point of the fixture. Every driver reads its index into
/// memory when the area is opened, so the base the reader holds goes on
/// answering from what it saw then — which is exactly the staleness the refresh
/// is there to end, and exactly what a test going through `AreaManager` could
/// not reproduce, `openArea()` closing the reader's base on the way in.
void asAnotherProgram(const amberedit::domain::AreaConfig& area,
                      const std::function<void(amberedit::ports::IMsgBase&)>& work) {
    amberedit::msgbase::FtnMsgBase other("CP866");
    REQUIRE(other.open(area).has_value());
    work(other);
    other.close();
}

/// A message appended to the area, which is what a tosser leaves behind.
void appendMessage(amberedit::ports::IMsgBase& base) {
    const auto header = base.header(1);
    const auto body = base.body(1);
    REQUIRE(
        base.write(amberedit::app::copyOf(header, body, /*netmail=*/false)).has_value());
}

/// The message with the most lines in it, the area being open already: the one
/// message in the base a scroll offset can be tested against.
uint32_t longestMessage(const amberedit::ui::AppState& state) {
    uint32_t longest = 1;
    size_t most = 0;
    for (uint32_t number = 1; number <= state.messageCount; ++number) {
        const size_t lines = state.base->body(number).lines.size();
        if (lines > most) {
            most = lines;
            longest = number;
        }
    }
    return longest;
}

}  // namespace

TEST_CASE(
    "A base nothing touched comes back exactly as it was [after_handover][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    // A window a message can be scrolled in, which is what an offset means at
    // all: one that fits comes back at nought however it was left, the re-wrap
    // clamping it, and would prove nothing either way.
    fixture.state.width = 80;
    fixture.state.height = 12;
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());
    REQUIRE(message_read::loadMessage(fixture.state, longestMessage(fixture.state)));
    REQUIRE(static_cast<int>(fixture.state.readLines.size()) >
            fixture.state.readRows() + 2);

    const uint32_t count = fixture.state.messageCount;
    const uint32_t number = fixture.state.readHeader->number;
    const std::string subject = fixture.state.readHeader->subject;
    fixture.state.readScroll = 2;
    fixture.state.showKludges = true;

    after_handover::refresh(fixture.state);

    CHECK(fixture.state.messageCount == count);
    REQUIRE(fixture.state.readHeader);
    CHECK(fixture.state.readHeader->number == number);
    CHECK(fixture.state.readHeader->subject == subject);
    // What belongs to the reader rather than to the message is where the user
    // left it: coming back from a shell is not coming back to the top.
    CHECK(fixture.state.readScroll == 2);
    CHECK(fixture.state.showKludges);
    CHECK(fixture.state.errorMessage.empty());
}

TEST_CASE(
    "A message added out of band is there on the way back [after_handover][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());
    REQUIRE(message_read::loadMessage(fixture.state, 3));
    const uint32_t before = fixture.state.messageCount;

    asAnotherProgram(fixture.area, appendMessage);

    // Still the old number, the reader's base having read its index once and
    // never again: this is the staleness, asserted rather than assumed.
    CHECK(fixture.state.base->count() == before);

    after_handover::refresh(fixture.state);

    CHECK(fixture.state.messageCount == before + 1);
    // And the area list was told, which only a base opened afresh could say.
    CHECK(fixture.total() == before + 1);
    // The message being read did not move.
    REQUIRE(fixture.state.readHeader);
    CHECK(fixture.state.readHeader->number == 3);
}

TEST_CASE("A message changed out of band is read again [after_handover][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());
    REQUIRE(message_read::loadMessage(fixture.state, 4));
    REQUIRE(fixture.state.readHeader->subject != "Changed underneath");

    asAnotherProgram(fixture.area, [](amberedit::ports::IMsgBase& other) {
        auto draft = amberedit::app::copyOf(other.header(4), other.body(4),
                                            /*netmail=*/false);
        draft.subject = "Changed underneath";
        REQUIRE(other.replace(4, draft).has_value());
    });

    after_handover::refresh(fixture.state);

    REQUIRE(fixture.state.readHeader);
    CHECK(fixture.state.readHeader->number == 4);
    CHECK(fixture.state.readHeader->subject == "Changed underneath");
}

TEST_CASE(
    "A message deleted out of band lands on the nearest survivor before it "
    "[after_handover][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());
    REQUIRE(message_read::loadMessage(fixture.state, 5));

    // The message before the one on screen, read before anything moves. That is
    // where the reader lands: `indexOfUid()` answers with the nearest *earlier*
    // survivor, which is the rule a lastread mark on a deleted message follows
    // — it says how far the reading got, and reading goes on forwards from
    // there.
    const std::string earlier = fixture.state.base->header(4).subject;

    // Two of them, and one *before* the message being read: a refresh that kept
    // the position rather than the UID would land a message off and pass every
    // assertion about the number alone.
    asAnotherProgram(fixture.area, [](amberedit::ports::IMsgBase& other) {
        REQUIRE(other.remove(5).has_value());
        REQUIRE(other.remove(2).has_value());
    });

    after_handover::refresh(fixture.state);

    REQUIRE(fixture.state.readHeader);
    // What was message 4 is message 3 now, message 2 having gone from under it.
    CHECK(fixture.state.readHeader->number == 3);
    CHECK(fixture.state.readHeader->subject == earlier);
    CHECK(fixture.state.messageCursor == 2);
}

TEST_CASE(
    "Everything up to the message going leaves the reader on the first "
    "[after_handover][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());
    REQUIRE(message_read::loadMessage(fixture.state, 3));

    // What message 4 says, which is the first thing left once 1 to 3 have gone.
    const std::string survivor = fixture.state.base->header(4).subject;

    asAnotherProgram(fixture.area, [](amberedit::ports::IMsgBase& other) {
        // Backwards, so each number still names what it named when it was read.
        for (uint32_t number = 3; number >= 1; --number) {
            REQUIRE(other.remove(number).has_value());
        }
    });

    after_handover::refresh(fixture.state);

    // Nothing at or before it survives, so the nearest survivor is the first
    // message — not the last, which is what reading `indexOfUid()`'s zero as
    // "the end of the area" would give.
    REQUIRE(fixture.state.readHeader);
    CHECK(fixture.state.readHeader->number == 1);
    CHECK(fixture.state.readHeader->subject == survivor);
}

TEST_CASE(
    "An area emptied out of band leaves the reader blank [after_handover][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());
    REQUIRE(fixture.state.readHeader);

    asAnotherProgram(fixture.area, [](amberedit::ports::IMsgBase& other) {
        while (other.count() > 0) REQUIRE(other.remove(1).has_value());
    });

    after_handover::refresh(fixture.state);

    CHECK(fixture.state.messageCount == 0);
    CHECK_FALSE(fixture.state.readHeader);
    CHECK(fixture.state.readLines.empty());
    CHECK_FALSE(fixture.state.scrollbarShown);
    CHECK(fixture.total() == 0);
}

TEST_CASE(
    "A message that came back shorter keeps the scroll inside it "
    "[after_handover][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    fixture.state.width = 80;
    fixture.state.height = 25;
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());
    REQUIRE(message_read::loadMessage(fixture.state, 2));
    fixture.state.readScroll = 40;

    asAnotherProgram(fixture.area, [](amberedit::ports::IMsgBase& other) {
        auto draft = amberedit::app::copyOf(other.header(2), other.body(2),
                                            /*netmail=*/false);
        draft.lines = {"One line and no more."};
        REQUIRE(other.replace(2, draft).has_value());
    });

    after_handover::refresh(fixture.state);

    // The re-wrap is what clamps it, so a message that lost its text cannot
    // leave the reader scrolled past the end of what is left.
    const int rows = fixture.state.readRows();
    const auto lines = static_cast<int>(fixture.state.readLines.size());
    CHECK(fixture.state.readScroll <= std::max(0, lines - rows));
}

TEST_CASE(
    "A twit shown after all is not shown on somebody else [after_handover][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());
    REQUIRE(message_read::loadMessage(fixture.state, 5));
    fixture.state.twitRevealed = true;

    // The message it was asked for is still there, so the answer stands.
    after_handover::refresh(fixture.state);
    CHECK(fixture.state.twitRevealed);

    // And once it is gone, what stands in its place is somebody else's message
    // and is hidden again until it is asked for in its turn.
    asAnotherProgram(fixture.area, [&fixture](amberedit::ports::IMsgBase& other) {
        REQUIRE(other.remove(fixture.state.readHeader->number).has_value());
    });
    after_handover::refresh(fixture.state);
    CHECK_FALSE(fixture.state.twitRevealed);
}

TEST_CASE("The editor is left alone entirely [after_handover][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());
    fixture.state.navigator.push(ScreenId::Compose);
    fixture.state.compose.subject = "Half written";

    asAnotherProgram(fixture.area, appendMessage);
    const uint32_t before = fixture.state.messageCount;

    after_handover::refresh(fixture.state);

    // Nothing at all: the draft is the user's own text, and an area that would
    // not open again is not worth losing it over. The reader underneath is read
    // again when the editor is left, which is when it is next looked at.
    CHECK(fixture.state.compose.subject == "Half written");
    CHECK(fixture.state.messageCount == before);
    CHECK(fixture.state.navigator.current() == ScreenId::Compose);
}

TEST_CASE("The area list has no area to reopen [after_handover][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    REQUIRE(fixture.state.base == nullptr);
    REQUIRE(fixture.state.navigator.current() == ScreenId::AreaList);

    // Nothing is open, so there is nothing to do and nothing to fall over.
    after_handover::refresh(fixture.state);

    CHECK(fixture.state.base == nullptr);
    CHECK(fixture.state.errorMessage.empty());
}

TEST_CASE(
    "rescan_on_return is what reads every other area again [after_handover][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());

    asAnotherProgram(fixture.area, appendMessage);
    const uint32_t grown = fixture.state.messageCount + 1;

    // Off, the area being read is still reopened — that half is not optional —
    // and it is the area list's own counts that the setting is about. Here the
    // two are the same area, so what is asserted is that the reopen happened
    // without the rescan: `AreaManager::reload()` would have closed the base.
    REQUIRE_FALSE(fixture.config.rescanOnReturn);
    after_handover::refresh(fixture.state);
    CHECK(fixture.state.messageCount == grown);
    REQUIRE(fixture.state.base != nullptr);

    // On, the whole list is read again and the base being read survives it: the
    // rescan runs before the area is opened afresh, never after, or this pointer
    // would name a base that had been closed underneath it.
    fixture.config.rescanOnReturn = true;
    asAnotherProgram(fixture.area, appendMessage);
    after_handover::refresh(fixture.state);

    CHECK(fixture.state.messageCount == grown + 1);
    CHECK(fixture.total() == grown + 1);
    REQUIRE(fixture.state.base != nullptr);
    // The base is a live one and not the one the rescan closed.
    CHECK(message_read::loadMessage(fixture.state, 1));
}
