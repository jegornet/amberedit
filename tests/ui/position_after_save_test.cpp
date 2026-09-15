#include <doctest/doctest.h>

#include <cstdint>
#include <string>

#include "config/app_config.hpp"
#include "domain/ftn_address.hpp"
#include "temp_squish_base.hpp"
#include "ui/area_fixture.hpp"
#include "ui/screens/compose_screen.hpp"
#include "ui/screens/message_list_screen.hpp"
#include "ui/screens/message_read_screen.hpp"

using amberedit::config::AppConfig;
using amberedit::config::PositionAfterSave;
using amberedit::test::AreaFixture;
using amberedit::test::TempSquishBase;

namespace compose = amberedit::ui::screens::compose;
namespace message_list = amberedit::ui::screens::message_list;
namespace message_read = amberedit::ui::screens::message_read;

namespace {

/// A config a message can actually be written under: without a sender's name
/// and address the header block keeps hold of the message, and rightly so.
AppConfig writable(PositionAfterSave where) {
    AppConfig cfg;
    cfg.userName = "Yegor Gluhov";
    cfg.userAddress = amberedit::domain::FtnAddress::parse("2:382/736");
    cfg.positionAfterSave = where;
    return cfg;
}

/// The area open with the reader on message `number`.
void readMessage(AreaFixture& fixture, uint32_t number) {
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());
    message_read::goToMessage(fixture.state, number);
    REQUIRE(fixture.state.readHeader.has_value());
    REQUIRE(fixture.state.readHeader->number == number);
}

/// An answer to whatever is on the screen, written and stored.
void replyAndSave(AreaFixture& fixture) {
    compose::startReply(fixture.state);
    fixture.state.compose.subject = "answer";
    compose::saveMessage(fixture.state);
}

/// Which message the reader came back on.
uint32_t showing(const AreaFixture& fixture) {
    REQUIRE(fixture.state.readHeader.has_value());
    return fixture.state.readHeader->number;
}

/// Empties the base under the fixture, which is the one case with no message to
/// come back to.
void emptyTheArea(AreaFixture& fixture) {
    amberedit::ports::IMsgBase* base =
        amberedit::test::valueOf(fixture.manager.openArea(fixture.area));
    REQUIRE(base != nullptr);
    while (base->count() > 0) REQUIRE(base->remove(1).has_value());
    fixture.manager.closeCurrentArea();
    static_cast<void>(fixture.manager.reload());
}

}  // namespace

TEST_CASE("The reader comes back on the message that was being read "
          "[aftersave][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path(), writable(PositionAfterSave::Current));
    readMessage(fixture, 3);
    const uint32_t before = fixture.state.messageCount;

    replyAndSave(fixture);

    CHECK(fixture.state.messageCount == before + 1);
    CHECK(showing(fixture) == 3);
    // And the list's cursor with it: going back to the list lands on the
    // message the reader is showing, whichever message that is.
    CHECK(fixture.state.messageCursor == 2);
}

TEST_CASE("It is where it was in the message, too [aftersave][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path(), writable(PositionAfterSave::Current));
    readMessage(fixture, 3);
    // Somewhere down the message rather than at the top of it: the reading was
    // interrupted, and what `current` means is that it was not lost.
    fixture.state.readScroll = 1;

    replyAndSave(fixture);

    CHECK(showing(fixture) == 3);
    CHECK(fixture.state.readScroll == 1);
}

TEST_CASE("reader_position_after_save new opens the reader on the answer "
          "[aftersave][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path(), writable(PositionAfterSave::New));
    readMessage(fixture, 3);

    replyAndSave(fixture);

    CHECK(showing(fixture) == fixture.state.messageCount);
    CHECK(fixture.state.messageCursor ==
          static_cast<int>(fixture.state.messageCount) - 1);
}

TEST_CASE("reader_position_after_save next reads on past the message answered "
          "[aftersave][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path(), writable(PositionAfterSave::Next));
    readMessage(fixture, 3);

    replyAndSave(fixture);

    CHECK(showing(fixture) == 4);
    CHECK(fixture.state.messageCursor == 3);
}

TEST_CASE("next off the end of the area is the answer itself [aftersave][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path(), writable(PositionAfterSave::Next));
    const uint32_t last = fixture.total();
    readMessage(fixture, last);

    replyAndSave(fixture);

    // Nothing stood after the message answered, and the answer does now: it was
    // written onto the end of the area. The reader is not sent out of the area
    // the way → off the last message would send it — `reader_edge` answers for
    // that key, and this is not that key.
    CHECK(fixture.state.messageCount == last + 1);
    CHECK(showing(fixture) == last + 1);
}

TEST_CASE("An empty area opens on the message written into it [aftersave][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path(), writable(PositionAfterSave::Current));
    emptyTheArea(fixture);
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());
    REQUIRE(fixture.state.messageCount == 0);

    // There is no message to come back to and none to move on to, so every
    // answer comes to the same thing: the message just written is the only one
    // there is.
    compose::startNew(fixture.state);
    fixture.state.compose.toName = "All";
    fixture.state.compose.subject = "first";
    compose::saveMessage(fixture.state);

    REQUIRE(fixture.state.messageCount == 1);
    CHECK(showing(fixture) == 1);
    CHECK(fixture.state.messageCursor == 0);
}

TEST_CASE("A message changed comes back under the editor whatever this says "
          "[aftersave][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path(), writable(PositionAfterSave::Next));
    readMessage(fixture, 3);

    // Changing a message is not writing a new one: there is one message in it
    // either way, and the reader comes back to it.
    compose::startChange(fixture.state, /*notice=*/false);
    compose::saveMessage(fixture.state);

    CHECK(showing(fixture) == 3);
}
