#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "config/app_config.hpp"
#include "domain/ftn_address.hpp"
#include "domain/message.hpp"
#include "temp_squish_base.hpp"
#include "test_strings.hpp"
#include "ui/area_fixture.hpp"
#include "ui/charset_dialog.hpp"
#include "ui/keys.hpp"
#include "ui/screens/message_list_screen.hpp"
#include "ui/screens/message_read_screen.hpp"
#include "ui/term/element.hpp"
#include "ui/term/screen.hpp"

using amberedit::config::AppConfig;
using amberedit::config::Command;
using amberedit::domain::FtnAddress;
using amberedit::domain::MessageDraft;
using amberedit::test::AreaFixture;
using amberedit::test::TempSquishBase;
using amberedit::test::valueOf;
using amberedit::ui::AppState;
using amberedit::ui::term::Event;

namespace charset_dialog = amberedit::ui::charset_dialog;
namespace message_list = amberedit::ui::screens::message_list;
namespace message_read = amberedit::ui::screens::message_read;

namespace {

using From = AppState::CharsetPicker::From;

AppConfig plain() {
    AppConfig config;
    config.userName = "Vasya Pupkin";
    config.defaultCharset = "CP866";
    config.composeCharset = "CP866";
    return config;
}

/// One message, written in `charset` and saying so where `declares` — the
/// message that declares nothing is what falls back on the area's default.
void putMessage(AreaFixture& fixture, const std::string& charset, bool declares,
                const std::string& line) {
    amberedit::ports::IMsgBase* base = valueOf(fixture.manager.openArea(fixture.area));
    REQUIRE(base != nullptr);
    while (base->count() > 0) REQUIRE(base->remove(1).has_value());

    MessageDraft draft;
    draft.from = "Petr Petrov";
    draft.to = "All";
    draft.subject = "Привет";
    draft.origAddr = *FtnAddress::parse("2:382/736");
    draft.lines = {line};
    draft.charset = charset;
    if (declares) draft.kludges = {"CHRS: " + charset + " 2"};
    REQUIRE(valueOf(base->write(draft)) != 0);

    fixture.manager.closeCurrentArea();
    static_cast<void>(fixture.manager.reload());
}

/// Two messages, each written and declared in its own charset: what says an
/// answer given for one of them is not carried to the other.
void putTwo(AreaFixture& fixture) {
    amberedit::ports::IMsgBase* base = valueOf(fixture.manager.openArea(fixture.area));
    REQUIRE(base != nullptr);
    while (base->count() > 0) REQUIRE(base->remove(1).has_value());

    for (const char* charset : {"KOI8-R", "CP866"}) {
        MessageDraft draft;
        draft.from = "Petr Petrov";
        draft.to = "All";
        draft.subject = "Привет";
        draft.origAddr = *FtnAddress::parse("2:382/736");
        draft.lines = {"Привет, мир"};
        draft.charset = charset;
        draft.kludges = {std::string("CHRS: ") + charset + " 2"};
        REQUIRE(valueOf(base->write(draft)) != 0);
    }

    fixture.manager.closeCurrentArea();
    static_cast<void>(fixture.manager.reload());
}

/// Types a name into the box, a character at a time, over whatever it opened
/// holding.
void typeCharset(AppState& state, const std::string& name) {
    REQUIRE(state.charsetPicker);
    state.charsetPicker->charset.clear();
    state.charsetPicker->cursor = 0;
    for (const char c : name) {
        CHECK(charset_dialog::handleEvent(state, Event::Character(c)) ==
              charset_dialog::Outcome::Ignored);
    }
}

/// Answers the box the way the shell does: Enter, and then the reader reads the
/// message again in what the box was left holding.
charset_dialog::Outcome answer(AppState& state) {
    const auto outcome = charset_dialog::handleEvent(state, Event::Return);
    if (outcome == charset_dialog::Outcome::Read) {
        const std::string charset = state.charsetPicker->charset;
        state.charsetPicker.reset();
        message_read::readInCharset(state, charset);
    }
    return outcome;
}

/// Every row of the box as it reaches the terminal, so that what it says can be
/// read back the way somebody looking at it would.
std::vector<std::string> boxRows(AppState& state) {
    namespace term = amberedit::ui::term;
    term::Screen screen(state.width, state.height);
    term::render(screen, charset_dialog::render(state, term::text("")));

    std::vector<std::string> rows;
    for (int y = 0; y < state.height; ++y) {
        std::string row;
        for (int x = 0; x < state.width; ++x) row += screen.at(x, y).glyph;
        rows.push_back(row);
    }
    return rows;
}

/// Whether any row of the box holds the words.
bool saysSomewhere(AppState& state, const std::string& words) {
    for (const std::string& row : boxRows(state)) {
        if (row.find(words) != std::string::npos) return true;
    }
    return false;
}

}  // namespace

TEST_CASE("The charset box draws what it is saying [charset][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path(), plain());
    putMessage(fixture, "KOI8-R", /*declares=*/true, "Привет, мир");
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());

    charset_dialog::open(fixture.state);
    REQUIRE(fixture.state.charsetPicker);
    // The title, the line about the message, the field holding the same name,
    // and the button: what is on the state has to reach the screen.
    CHECK(saysSomewhere(fixture.state, "Charset"));
    CHECK(saysSomewhere(fixture.state, "Read as KOI8-R, which its CHRS kludge names."));
    CHECK(saysSomewhere(fixture.state, "Read"));

    // And what an Enter it cannot act on says, in the rule along the bottom.
    typeCharset(fixture.state, "NONEXISTENT");
    REQUIRE(answer(fixture.state) == charset_dialog::Outcome::Ignored);
    CHECK(saysSomewhere(fixture.state, "NONEXISTENT"));
}

TEST_CASE("The charset box says what the message is being read in [charset][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path(), plain());
    putMessage(fixture, "KOI8-R", /*declares=*/true, "Привет, мир");
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());

    // The default key, and the one the user asked for.
    CHECK(fixture.state.keys.is(Event::Character("e", false, /*alt=*/true),
                                Command::ReaderCharset));

    charset_dialog::open(fixture.state);
    REQUIRE(fixture.state.charsetPicker);
    // The message says what it is written in, and the box says so back.
    CHECK(fixture.state.charsetPicker->current == "KOI8-R");
    CHECK(fixture.state.charsetPicker->from == From::Kludge);
    // And it opens holding that name, there being nothing else worth editing.
    CHECK(fixture.state.charsetPicker->charset == "KOI8-R");
}

TEST_CASE("A message declaring nothing is read under the area's default "
          "[charset][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path(), plain());
    putMessage(fixture, "CP866", /*declares=*/false, "Привет, мир");
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());

    charset_dialog::open(fixture.state);
    REQUIRE(fixture.state.charsetPicker);
    CHECK(fixture.state.charsetPicker->current == "CP866");
    CHECK(fixture.state.charsetPicker->from == From::Default);
}

TEST_CASE("The charset answered for is what the message is read in "
          "[charset][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path(), plain());
    // Written in KOI8-R and declaring CP866: the kludge is wrong, which is the
    // whole reason this box exists.
    putMessage(fixture, "KOI8-R", /*declares=*/false, "Привет, мир");
    {
        amberedit::ports::IMsgBase* opened =
            valueOf(fixture.manager.openArea(fixture.area));
        REQUIRE(opened != nullptr);
        // The area reads CP866, so the KOI8-R bytes come out as something else.
        CHECK(opened->body(1).text() != "Привет, мир");
        fixture.manager.closeCurrentArea();
        static_cast<void>(fixture.manager.reload());
    }
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());
    REQUIRE(fixture.state.readBody);
    REQUIRE(fixture.state.readBody->text() != "Привет, мир");

    charset_dialog::open(fixture.state);
    typeCharset(fixture.state, "KOI8-R");
    CHECK(answer(fixture.state) == charset_dialog::Outcome::Read);

    // The box is gone and the message is on the screen in the charset asked
    // for — the text, and the subject out of the header beside it.
    CHECK_FALSE(fixture.state.charsetPicker);
    CHECK(fixture.state.readCharset == "KOI8-R");
    CHECK(fixture.state.readBody->charset == "KOI8-R");
    CHECK(fixture.state.readBody->text() == "Привет, мир");
    CHECK(fixture.state.readHeader->subject == "Привет");
    // And the message still declares what it declared: the box overruled the
    // reading, not the message.
    CHECK_FALSE(fixture.state.readBody->charsetDeclared);

    // Opened again, the box says the answer is the user's own.
    charset_dialog::open(fixture.state);
    REQUIRE(fixture.state.charsetPicker);
    CHECK(fixture.state.charsetPicker->current == "KOI8-R");
    CHECK(fixture.state.charsetPicker->from == From::Override);
}

TEST_CASE("The box takes the Fidonet spelling of a charset [charset][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path(), plain());
    putMessage(fixture, "CP866", /*declares=*/true, "Привет, мир");
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());

    charset_dialog::open(fixture.state);
    typeCharset(fixture.state, "+7_FIDO");
    CHECK(answer(fixture.state) == charset_dialog::Outcome::Read);
    // Resolved to iconv's name for it where the line was read, exactly as a
    // `default_charset` line is.
    CHECK(fixture.state.readCharset == "CP866");
    CHECK(fixture.state.readBody->text() == "Привет, мир");
}

TEST_CASE("A charset nothing can convert from is refused in the box "
          "[charset][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path(), plain());
    putMessage(fixture, "CP866", /*declares=*/true, "Привет, мир");
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());

    charset_dialog::open(fixture.state);

    // A name iconv has never heard of, and one that names no encoding in
    // particular: neither is a charset a message can be read in, and both leave
    // the box standing with the reason in it.
    for (const char* refused : {"NONEXISTENT", "IBMPC", "   "}) {
        INFO("typed = " << std::string(refused));
        typeCharset(fixture.state, refused);
        CHECK(answer(fixture.state) == charset_dialog::Outcome::Ignored);
        REQUIRE(fixture.state.charsetPicker);
        CHECK_FALSE(fixture.state.charsetPicker->error.empty());
        // And nothing was read again: the message is as the area reads it.
        CHECK(fixture.state.readCharset.empty());
        CHECK(fixture.state.readBody->charset == "CP866");
    }
}

TEST_CASE("The charset asked for goes no further than the message "
          "[charset][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path(), plain());
    putTwo(fixture);
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());
    REQUIRE(fixture.state.readHeader->number == 1);

    charset_dialog::open(fixture.state);
    typeCharset(fixture.state, "CP1251");
    REQUIRE(answer(fixture.state) == charset_dialog::Outcome::Read);
    REQUIRE(fixture.state.readCharset == "CP1251");

    // The next message is somebody else's, and so is the answer given for this
    // one: it is read as it declares itself.
    message_read::goToMessage(fixture.state, 2);
    CHECK(fixture.state.readCharset.empty());
    CHECK(fixture.state.readBody->charset == "CP866");

    // And coming back undoes it: the message is read as it declares itself
    // again, the box having changed nothing on disk.
    message_read::goToMessage(fixture.state, 1);
    CHECK(fixture.state.readCharset.empty());
    CHECK(fixture.state.readBody->charset == "KOI8-R");
    CHECK(fixture.state.readBody->text() == "Привет, мир");
}

TEST_CASE("The charset asked for does not outlive the area [charset][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path(), plain());
    putMessage(fixture, "KOI8-R", /*declares=*/true, "Привет, мир");
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());

    charset_dialog::open(fixture.state);
    typeCharset(fixture.state, "CP1251");
    REQUIRE(answer(fixture.state) == charset_dialog::Outcome::Read);
    REQUIRE(fixture.state.readCharset == "CP1251");

    // Entering an area is opening its first message, which goes through
    // loadMessage() like every other way to one.
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());
    CHECK(fixture.state.readCharset.empty());
    CHECK(fixture.state.readBody->charset == "KOI8-R");
}

TEST_CASE("Esc leaves the message being read as it was [charset][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path(), plain());
    putMessage(fixture, "CP866", /*declares=*/true, "Привет, мир");
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());

    charset_dialog::open(fixture.state);
    typeCharset(fixture.state, "KOI8-R");
    CHECK(charset_dialog::handleEvent(fixture.state, Event::Escape) ==
          charset_dialog::Outcome::Dismissed);
    CHECK_FALSE(fixture.state.charsetPicker);
    CHECK(fixture.state.readCharset.empty());
    CHECK(fixture.state.readBody->charset == "CP866");
    CHECK(fixture.state.readBody->text() == "Привет, мир");
}
