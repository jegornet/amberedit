#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "config/app_config.hpp"
#include "domain/ftn_address.hpp"
#include "domain/message.hpp"
#include "temp_squish_base.hpp"
#include "ui/area_fixture.hpp"
#include "ui/screens/message_list_screen.hpp"
#include "ui/screens/message_read_screen.hpp"
#include "ui/term/element.hpp"
#include "ui/term/screen.hpp"

using amberedit::app::ScreenId;
using amberedit::config::AppConfig;
using amberedit::config::TwitMode;
using amberedit::domain::FtnAddress;
using amberedit::domain::MessageDraft;
using amberedit::test::AreaFixture;
using amberedit::test::TempSquishBase;
using amberedit::ui::term::Event;

namespace message_list = amberedit::ui::screens::message_list;
namespace message_read = amberedit::ui::screens::message_read;

namespace {

/// The notice standing in place of a twit's text, word for word as the reader
/// writes it.
const std::string kNotice = "This is a twit message. Press Space to view it";

/// One message to put into the area. Who wrote it, who it is to and what it is
/// about — which is the whole of what a twit rule looks at.
struct Letter {
    std::string from;
    std::string to{"All"};
    std::string subject{"Nothing in particular"};
    std::string address{"2:5020/1042"};
};

/// Puts exactly these messages into the area, in place of the mail the fixture
/// copies. The twits are about who wrote a message, so a base whose senders the
/// test chose is the only one a rule can be written against — and the numbers
/// then say which message is which, which is what the reader is checked on.
void putMessages(AreaFixture& fixture, const std::vector<Letter>& letters) {
    amberedit::ports::IMsgBase* base = fixture.manager.openArea(fixture.area);
    REQUIRE(base != nullptr);
    while (base->count() > 0) REQUIRE(base->remove(1));

    for (size_t i = 0; i < letters.size(); ++i) {
        const Letter& letter = letters[i];
        MessageDraft draft;
        draft.from = letter.from;
        draft.to = letter.to;
        draft.subject = letter.subject;
        draft.origAddr = *FtnAddress::parse(letter.address);
        draft.lines = {"Message number " + std::to_string(i + 1) + "."};
        REQUIRE(base->write(draft) != 0);
    }

    fixture.manager.closeCurrentArea();
    fixture.manager.reload();
}

/// A config with a twit or two in it and nothing else to speak of. The user has
/// a name because `skip` spares what is addressed to them, and that is the one
/// thing it has to be able to recognise.
AppConfig twitting(TwitMode mode, const std::vector<std::string>& twits) {
    AppConfig config;
    config.userName = "Vasya Pupkin";
    config.twitMode = mode;
    for (const std::string& who : twits) {
        amberedit::config::TwitRule rule;
        rule.name = who;
        config.twits.push_back(rule);
    }
    return config;
}

/// The body as it stands on the screen, joined into one string — what says
/// whether the message is behind the notice or in front of it.
std::string bodyOf(const AreaFixture& fixture) {
    std::string text;
    for (const auto& line : fixture.state.readLines) text += line.text;
    return text;
}

/// Which message the reader is showing.
uint32_t showing(const AreaFixture& fixture) {
    REQUIRE(fixture.state.readHeader.has_value());
    return fixture.state.readHeader->number;
}

/// The reader drawn into a buffer of its own size, as one string — what says
/// that the notice reaches the screen and not merely the state behind it.
std::string frameOf(AreaFixture& fixture) {
    amberedit::ui::term::Screen screen(fixture.state.width, fixture.state.height);
    amberedit::ui::term::render(screen, message_read::render(fixture.state));

    std::string text;
    for (int y = 0; y < fixture.state.height; ++y) {
        for (int x = 0; x < fixture.state.width; ++x) text += screen.at(x, y).glyph;
        text += '\n';
    }
    return text;
}

}  // namespace

TEST_CASE("blank puts a notice in place of a twit's text [twit][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path(), twitting(TwitMode::Blank, {"Ivan Ivanov"}));
    putMessages(fixture, {{"Ivan Ivanov"}, {"Petr Petrov"}});

    REQUIRE(message_list::enterArea(fixture.state, fixture.area));

    // Blank does not move the reader: the message is opened, and it is its text
    // that is not on the screen.
    CHECK(showing(fixture) == 1);
    CHECK(bodyOf(fixture) == kNotice);
    // The header block is drawn as it always is — whose message is being passed
    // over is exactly what the user is entitled to see.
    CHECK(fixture.state.readHeader->from == "Ivan Ivanov");
    // And nothing to scroll, so nothing for the scrollbar to say.
    CHECK_FALSE(fixture.state.scrollbarShown);

    // On the screen, and the sender with it.
    const std::string frame = frameOf(fixture);
    CHECK(frame.find(kNotice) != std::string::npos);
    CHECK(frame.find("Ivan Ivanov") != std::string::npos);
    CHECK(frame.find("Message number 1.") == std::string::npos);
}

TEST_CASE("Space shows a blanked twit after all [twit][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path(), twitting(TwitMode::Blank, {"Ivan Ivanov"}));
    putMessages(fixture, {{"Ivan Ivanov"}, {"Petr Petrov"}});
    REQUIRE(message_list::enterArea(fixture.state, fixture.area));
    REQUIRE(bodyOf(fixture) == kNotice);

    REQUIRE(message_read::handleEvent(fixture.state, Event::Character(' ')));

    CHECK(bodyOf(fixture).find("Message number 1.") != std::string::npos);
    // And Space is the page key again, now that there is something to page.
    REQUIRE(message_read::handleEvent(fixture.state, Event::Character(' ')));
    CHECK(bodyOf(fixture).find("Message number 1.") != std::string::npos);
}

TEST_CASE("A twit shown once is hidden again on the next message [twit][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path(), twitting(TwitMode::Blank, {"Ivan Ivanov"}));
    putMessages(fixture, {{"Ivan Ivanov"}, {"Petr Petrov"}, {"Ivan Ivanov"}});
    REQUIRE(message_list::enterArea(fixture.state, fixture.area));
    REQUIRE(message_read::handleEvent(fixture.state, Event::Character(' ')));
    REQUIRE(bodyOf(fixture) != kNotice);

    // Off to the ordinary message and on to the next twit: having asked for one
    // is no reason to be shown the next unasked.
    REQUIRE(message_read::handleEvent(fixture.state, Event::ArrowRight));
    CHECK(showing(fixture) == 2);
    REQUIRE(message_read::handleEvent(fixture.state, Event::ArrowRight));
    CHECK(showing(fixture) == 3);
    CHECK(bodyOf(fixture) == kNotice);
}

TEST_CASE("show leaves the twits alone [twit][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path(), twitting(TwitMode::Show, {"Ivan Ivanov"}));
    putMessages(fixture, {{"Ivan Ivanov"}, {"Petr Petrov"}});

    REQUIRE(message_list::enterArea(fixture.state, fixture.area));

    CHECK(showing(fixture) == 1);
    CHECK(bodyOf(fixture).find("Message number 1.") != std::string::npos);
}

TEST_CASE("skip walks forward past a run of twits [twit][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path(), twitting(TwitMode::Skip, {"Ivan Ivanov"}));
    putMessages(fixture,
                {{"Petr Petrov"}, {"Ivan Ivanov"}, {"Ivan Ivanov"}, {"Petr Petrov"}});
    REQUIRE(message_list::enterArea(fixture.state, fixture.area));
    REQUIRE(showing(fixture) == 1);

    REQUIRE(message_read::handleEvent(fixture.state, Event::ArrowRight));

    CHECK(showing(fixture) == 4);
    // The list's cursor follows, so going there lands on what is being read.
    CHECK(fixture.state.messageCursor == 3);
}

TEST_CASE("skip walks back past a run of twits [twit][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path(), twitting(TwitMode::Skip, {"Ivan Ivanov"}));
    putMessages(fixture,
                {{"Petr Petrov"}, {"Ivan Ivanov"}, {"Ivan Ivanov"}, {"Petr Petrov"}});
    REQUIRE(message_list::enterArea(fixture.state, fixture.area));
    REQUIRE(message_read::handleEvent(fixture.state, Event::ArrowRight));
    REQUIRE(showing(fixture) == 4);

    REQUIRE(message_read::handleEvent(fixture.state, Event::ArrowLeft));

    CHECK(showing(fixture) == 1);
}

TEST_CASE("skip spares a twit written to the user themselves [twit][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path(), twitting(TwitMode::Skip, {"Ivan Ivanov"}));
    putMessages(fixture,
                {{"Petr Petrov"}, {"Ivan Ivanov", "Vasya Pupkin"}, {"Petr Petrov"}});
    REQUIRE(message_list::enterArea(fixture.state, fixture.area));

    REQUIRE(message_read::handleEvent(fixture.state, Event::ArrowRight));

    // And it is read like any other message: a twit writing to you is the one
    // message of theirs you asked for, and being made to press a key for it
    // would be answering the wrong question.
    CHECK(showing(fixture) == 2);
    CHECK(bodyOf(fixture).find("Message number 2.") != std::string::npos);
}

TEST_CASE("ignore walks past a twit written to the user [twit][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path(), twitting(TwitMode::Ignore, {"Ivan Ivanov"}));
    putMessages(fixture,
                {{"Petr Petrov"}, {"Ivan Ivanov", "Vasya Pupkin"}, {"Petr Petrov"}});
    REQUIRE(message_list::enterArea(fixture.state, fixture.area));

    REQUIRE(message_read::handleEvent(fixture.state, Event::ArrowRight));

    CHECK(showing(fixture) == 3);
}

TEST_CASE("Entering an area lands past the twits standing at its front "
          "[twit][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path(), twitting(TwitMode::Skip, {"Ivan Ivanov"}));
    putMessages(fixture, {{"Ivan Ivanov"}, {"Ivan Ivanov"}, {"Petr Petrov"}});

    REQUIRE(message_list::enterArea(fixture.state, fixture.area));

    CHECK(showing(fixture) == 3);
    CHECK(fixture.state.messageCursor == 2);
}

TEST_CASE("An area of nothing but twits is read as blank reads [twit][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path(), twitting(TwitMode::Skip, {"Ivan Ivanov"}));
    putMessages(fixture, {{"Ivan Ivanov"}, {"Ivan Ivanov"}});

    REQUIRE(message_list::enterArea(fixture.state, fixture.area));

    // There is nowhere to skip to, and an area is not left unopened for it: the
    // message asked for is the one shown, its text behind the notice.
    CHECK(fixture.state.navigator.current() == ScreenId::MessageRead);
    CHECK(showing(fixture) == 1);
    CHECK(bodyOf(fixture) == kNotice);
    // And Space still shows it, which is the whole of what blank offers.
    REQUIRE(message_read::handleEvent(fixture.state, Event::Character(' ')));
    CHECK(bodyOf(fixture).find("Message number 1.") != std::string::npos);
}

TEST_CASE("A twit picked out of the list opens the message after it [twit][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path(), twitting(TwitMode::Skip, {"Ivan Ivanov"}));
    putMessages(fixture,
                {{"Petr Petrov"}, {"Ivan Ivanov"}, {"Ivan Ivanov"}, {"Petr Petrov"}});
    REQUIRE(message_list::enterArea(fixture.state, fixture.area));

    fixture.state.navigator.push(ScreenId::MessageList);
    fixture.state.messageCursor = 1;  // the first of the two twits
    REQUIRE(message_list::handleEvent(fixture.state, Event::Return));

    CHECK(fixture.state.navigator.current() == ScreenId::MessageRead);
    CHECK(showing(fixture) == 4);
}

TEST_CASE("A run of twits at the end of an area is the end of it [twit][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path(), twitting(TwitMode::Skip, {"Ivan Ivanov"}));
    putMessages(fixture, {{"Petr Petrov"}, {"Ivan Ivanov"}, {"Ivan Ivanov"}});
    REQUIRE(message_list::enterArea(fixture.state, fixture.area));
    REQUIRE(showing(fixture) == 1);

    // There is nothing further to read here, which is what walking off the end
    // means; `reader_edge_exit` is on by default and leaves the area.
    REQUIRE(message_read::handleEvent(fixture.state, Event::ArrowRight));

    CHECK(fixture.state.navigator.current() == ScreenId::AreaList);
}

TEST_CASE("kill takes the twits out of the area as it opens [twit][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path(), twitting(TwitMode::Kill, {"Ivan Ivanov"}));
    putMessages(fixture,
                {{"Petr Petrov"}, {"Ivan Ivanov"}, {"Ivan Ivanov"}, {"Petr Petrov"}});

    REQUIRE(message_list::enterArea(fixture.state, fixture.area));

    CHECK(fixture.state.messageCount == 2);
    CHECK(fixture.state.base->header(1).from == "Petr Petrov");
    CHECK(fixture.state.base->header(2).from == "Petr Petrov");
    // The area list is counting them too, and it was counting four a moment ago.
    CHECK(fixture.manager.areas()[0].total == 2);
}

TEST_CASE("kill on an area of nothing but twits empties it [twit][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path(), twitting(TwitMode::Kill, {"Ivan Ivanov"}));
    putMessages(fixture, {{"Ivan Ivanov"}, {"Ivan Ivanov"}});

    REQUIRE(message_list::enterArea(fixture.state, fixture.area));

    // Which is an empty area, and the reader opens on nothing at all — the
    // screen a first message is written from.
    CHECK(fixture.state.messageCount == 0);
    CHECK_FALSE(fixture.state.readHeader.has_value());
    CHECK(fixture.state.navigator.current() == ScreenId::MessageRead);
}

TEST_CASE("A twit is one by its subject as well [twit][squish]") {
    TempSquishBase base;
    AppConfig config = twitting(TwitMode::Skip, {});
    config.twitSubjects.emplace_back("*sale*");
    AreaFixture fixture(base.path(), config);
    putMessages(fixture, {{"Petr Petrov", "All", "Buy my things"},
                          {"Petr Petrov", "All", "Everything on SALE today"},
                          {"Petr Petrov", "All", "Nothing to sell"}});
    REQUIRE(message_list::enterArea(fixture.state, fixture.area));
    REQUIRE(showing(fixture) == 1);

    REQUIRE(message_read::handleEvent(fixture.state, Event::ArrowRight));

    CHECK(showing(fixture) == 3);
}

TEST_CASE("A twit is one by the address it was written from [twit][squish]") {
    TempSquishBase base;
    AppConfig config = twitting(TwitMode::Skip, {});
    amberedit::config::TwitRule rule;
    rule.address = amberedit::domain::AddressPattern::parse("2:5030/*");
    config.twits.push_back(rule);
    AreaFixture fixture(base.path(), config);
    putMessages(fixture, {{"Petr Petrov", "All", "One", "2:5020/1042"},
                          {"Ivan Ivanov", "All", "Two", "2:5030/1042"},
                          {"Semen Semenov", "All", "Three", "2:5020/9999"}});
    REQUIRE(message_list::enterArea(fixture.state, fixture.area));

    REQUIRE(message_read::handleEvent(fixture.state, Event::ArrowRight));

    CHECK(showing(fixture) == 3);
}
