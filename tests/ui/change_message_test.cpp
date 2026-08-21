#include <catch2/catch.hpp>

#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "app/message_builder.hpp"
#include "domain/message.hpp"
#include "temp_squish_base.hpp"
#include "ui/area_fixture.hpp"
#include "ui/confirm_dialog.hpp"
#include "ui/screens/compose_screen.hpp"
#include "ui/screens/message_list_screen.hpp"
#include "ui/screens/message_read_screen.hpp"
#include "ui/term/element.hpp"
#include "ui/term/event.hpp"
#include "ui/term/screen.hpp"

using amberedit::app::ScreenId;
using amberedit::config::AppConfig;
using amberedit::test::AreaFixture;
using amberedit::test::TempSquishBase;
using amberedit::ui::AppState;
using amberedit::ui::term::Event;

namespace compose = amberedit::ui::screens::compose;
namespace confirm_dialog = amberedit::ui::confirm_dialog;
namespace message_list = amberedit::ui::screens::message_list;
namespace message_read = amberedit::ui::screens::message_read;
namespace term = amberedit::ui::term;

namespace {

/// A template of nothing but the @Changed lines: what the notice at the head of
/// somebody else's message is taken from, and nothing else that could reach the
/// editor — a message being changed is never a template's.
class TempTemplate {
public:
    TempTemplate() {
        path_ = std::filesystem::temp_directory_path() /
                ("amberedit-change-" + std::to_string(::getpid()) + ".tpl");
        std::ofstream out(path_);
        out << "@Changed\n"
               "@Changed*** Changed by @CName (@CAddr)\n"
               "A greeting for @pseudo!\n"
               "@Position\n";
    }
    ~TempTemplate() {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }
    TempTemplate(const TempTemplate&) = delete;
    TempTemplate& operator=(const TempTemplate&) = delete;

    [[nodiscard]] std::string path() const { return path_.string(); }

private:
    std::filesystem::path path_;
};

/// The area opened on its first message, which is the one every test here
/// changes.
void openFirstMessage(AreaFixture& fixture) {
    REQUIRE(message_list::enterArea(fixture.state, fixture.area));
    message_read::goToMessage(fixture.state, 1);
    REQUIRE(fixture.state.readHeader.has_value());
}

/// Answers the confirmation the way the shell does: yes, and then whatever was
/// asked about is acted on.
void answerYes(AppState& state) {
    const AppState::Confirm asked = state.confirm;
    REQUIRE(confirm_dialog::handleEvent(state, Event::Character('y')) ==
            confirm_dialog::Outcome::Confirmed);
    state.confirm = AppState::Confirm::None;
    if (asked == AppState::Confirm::ChangeForeignMessage) {
        compose::startChange(state, /*notice=*/true);
    } else if (asked == AppState::Confirm::ChangeSentMessage) {
        compose::startChange(state, /*notice=*/false);
    } else if (asked == AppState::Confirm::SaveMessage) {
        compose::saveMessage(state);
    }
}

/// The clock here, written the way the Date row writes it.
std::string stampNow(const AppConfig& config) {
    return amberedit::app::localStamp(std::time(nullptr))
        .format(config.readerDateTimeFormat);
}

/// What TZUTC says of this machine, which is what a message stamped here
/// carries. Read off the clock rather than written down: the tests run wherever
/// they run.
std::string tzutcNow() {
    const std::time_t now = std::time(nullptr);
    std::tm broken{};
    localtime_r(&now, &broken);
    return amberedit::app::tzutcOffset(static_cast<int>(broken.tm_gmtoff / 60));
}

/// Whether any line of the message being written holds `what`.
bool textHas(const AppState& state, const std::string& what) {
    for (const auto& line : state.edit.lines) {
        if (line.find(what) != std::string::npos) return true;
    }
    return false;
}

/// The message's visible text, one line per entry.
std::vector<std::string> visibleLines(const amberedit::domain::MessageBody& body) {
    std::vector<std::string> out;
    for (const auto& line : body.lines) {
        if (!line.kludge) out.push_back(line.text);
    }
    return out;
}

}  // namespace

TEST_CASE("c opens the editor on the message itself", "[change][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    openFirstMessage(fixture);

    const auto header = *fixture.state.readHeader;
    const auto body = *fixture.state.readBody;
    // The message is ours and has not gone anywhere, so nothing is asked.
    fixture.config.userAddress = header.origAddr;
    fixture.state.readHeader->attributes &= ~amberedit::domain::attr::kSent;

    REQUIRE(message_read::handleEvent(fixture.state, Event::Character('c')));
    CHECK(fixture.state.confirm == AppState::Confirm::None);
    REQUIRE(fixture.state.navigator.current() == ScreenId::Compose);

    // The editor opens on the message, not on a template: its own text, and its
    // own header in the block over it.
    CHECK(fixture.state.compose.changing);
    CHECK(fixture.state.changeNumber == 1);
    CHECK(fixture.state.compose.fromName == header.from);
    CHECK(fixture.state.compose.toName == header.to);
    CHECK(fixture.state.compose.subject == header.subject);
    CHECK(fixture.state.edit.lines == visibleLines(body));
    CHECK_FALSE(textHas(fixture.state, "*** Changed by"));
    // The service lines are kept apart rather than shown: they are not what a
    // person edits, and they go back around the text when it is stored.
    CHECK_FALSE(fixture.state.changeKept.kludges.empty());
    CHECK_FALSE(textHas(fixture.state, "MSGID"));
}

TEST_CASE("F2 asks before changing a message that is not yours", "[change][squish]") {
    TempSquishBase base;
    TempTemplate tpl;
    AppConfig config;
    config.templatePath = tpl.path();
    config.userName = "Yegor Gluhov";
    config.userAddress = amberedit::domain::FtnAddress::parse("192:168/9999");
    AreaFixture fixture(base.path(), config);
    openFirstMessage(fixture);

    // The sender is not one of ours, so the question is asked — and the message
    // is not opened until it is answered.
    REQUIRE_FALSE(fixture.config.isOwnAddress(fixture.state.readHeader->origAddr));
    REQUIRE(message_read::handleEvent(fixture.state, Event::F2));
    CHECK(fixture.state.confirm == AppState::Confirm::ChangeForeignMessage);
    CHECK(fixture.state.navigator.current() == ScreenId::MessageRead);

    answerYes(fixture.state);
    REQUIRE(fixture.state.navigator.current() == ScreenId::Compose);
    // And the message says whose hand it has been in, from the template.
    CHECK(fixture.state.edit.lines.front().empty());
    CHECK(fixture.state.edit.lines[1] == "*** Changed by Yegor Gluhov (192:168/9999)");
    // The rest of the template has nothing to do with a message that is already
    // written: no greeting, no tearline of ours.
    CHECK_FALSE(textHas(fixture.state, "A greeting for"));
}

TEST_CASE("The editor's Date row is the clock over a message being changed",
          "[change][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    openFirstMessage(fixture);
    const std::string was =
        fixture.state.readHeader->date.format(fixture.config.readerDateTimeFormat);

    REQUIRE(message_read::handleEvent(fixture.state, Event::Character('c')));
    answerYes(fixture.state);
    REQUIRE(fixture.state.navigator.current() == ScreenId::Compose);

    // The row says what the message will be dated by, not what it was dated by
    // when the editor opened on it: storing it stamps it afresh.
    term::Screen screen(fixture.state.width, fixture.state.height);
    term::render(screen, compose::render(fixture.state));
    std::string dateRow;
    for (int y = 0; y < fixture.state.height; ++y) {
        std::string row;
        for (int x = 0; x < fixture.state.width; ++x) row += screen.at(x, y).glyph;
        if (row.find(" Date : ") != std::string::npos) dateRow = row;
    }
    REQUIRE_FALSE(dateRow.empty());
    CHECK(dateRow.find(stampNow(fixture.config)) != std::string::npos);
    CHECK(dateRow.find(was) == std::string::npos);
}

TEST_CASE("Esc puts the question away and changes nothing", "[change][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    openFirstMessage(fixture);

    REQUIRE(message_read::handleEvent(fixture.state, Event::Character('c')));
    REQUIRE(fixture.state.confirm != AppState::Confirm::None);
    CHECK(confirm_dialog::handleEvent(fixture.state, Event::Escape) ==
          confirm_dialog::Outcome::Dismissed);
    CHECK(fixture.state.confirm == AppState::Confirm::None);
    CHECK(fixture.state.navigator.current() == ScreenId::MessageRead);
    CHECK_FALSE(fixture.state.compose.changing);
}

TEST_CASE("A message of yours that has gone out is asked about too", "[change][squish]") {
    TempSquishBase base;
    AppConfig config;
    config.userName = "Yegor Gluhov";
    AreaFixture fixture(base.path(), config);
    openFirstMessage(fixture);

    fixture.config.userAddress = fixture.state.readHeader->origAddr;
    fixture.state.readHeader->attributes |= amberedit::domain::attr::kSent;

    REQUIRE(message_read::handleEvent(fixture.state, Event::Character('c')));
    CHECK(fixture.state.confirm == AppState::Confirm::ChangeSentMessage);

    answerYes(fixture.state);
    REQUIRE(fixture.state.navigator.current() == ScreenId::Compose);
    // Nothing is added to a message of one's own, whatever became of it: the
    // notice is about whose message it is, not about where it has been.
    CHECK_FALSE(textHas(fixture.state, "*** Changed by"));

    // What went out is not what is being written now, so the message stops
    // being sent — on the header screen at once, and in the base when it is
    // stored. Every other attribute it carries stays as it was.
    const uint32_t was = fixture.state.readHeader->attributes;
    CHECK((fixture.state.compose.attributes & amberedit::domain::attr::kSent) == 0);
    CHECK(fixture.state.compose.attributes == (was & ~amberedit::domain::attr::kSent));

    REQUIRE(compose::handleEvent(fixture.state, Event::F2));
    answerYes(fixture.state);
    REQUIRE(fixture.state.navigator.current() == ScreenId::MessageRead);
    CHECK((fixture.state.base->header(1).attributes & amberedit::domain::attr::kSent) ==
          0);
    CHECK(fixture.state.base->header(1).attributes ==
          (was & ~amberedit::domain::attr::kSent));
}

TEST_CASE("Saving a change writes over the message it came from", "[change][squish]") {
    TempSquishBase base;
    AppConfig config;
    config.userName = "Yegor Gluhov";
    // An address of our own, which the new MSGID is made of: the area the
    // fixture opens is presented under none.
    config.userAddress = amberedit::domain::FtnAddress::parse("192:168/9999");
    AreaFixture fixture(base.path(), config);
    openFirstMessage(fixture);

    const uint32_t total = fixture.state.messageCount;
    const uint32_t uid = fixture.state.base->uidOf(1);
    const auto second = fixture.state.base->header(2).subject;
    const auto was = *fixture.state.readHeader;
    const auto first = visibleLines(*fixture.state.readBody).front();

    REQUIRE(message_read::handleEvent(fixture.state, Event::Character('c')));
    answerYes(fixture.state);  // it is not ours, so it was asked about
    REQUIRE(fixture.state.navigator.current() == ScreenId::Compose);
    // What the message carries that the editor does not show, kept while the
    // editor holds it — storing it is what puts these back around the text.
    const auto kept = fixture.state.changeKept;
    REQUIRE_FALSE(kept.kludges.empty());
    REQUIRE_FALSE(kept.trailing.empty());

    // A line typed into the message, and the subject changed over it.
    fixture.state.edit.lines.insert(fixture.state.edit.lines.begin(), "Edited here.");
    fixture.state.compose.subject = "Changed subject";

    REQUIRE(compose::handleEvent(fixture.state, Event::F2));
    REQUIRE(fixture.state.confirm == AppState::Confirm::SaveMessage);
    answerYes(fixture.state);

    // Back on the message, which is the same message: same number, same place,
    // and the area no longer than it was. Its date is not the same — it has
    // just been written, and that is the hour it is dated by — while the stamp
    // it arrived here under, which no rewriting changes, is.
    CHECK(fixture.state.navigator.current() == ScreenId::MessageRead);
    CHECK(fixture.state.messageCount == total);
    REQUIRE(fixture.state.readHeader.has_value());
    CHECK(fixture.state.readHeader->number == 1);
    CHECK(fixture.state.readHeader->subject == "Changed subject");
    CHECK(fixture.state.readHeader->date.format(fixture.config.readerDateTimeFormat) ==
          stampNow(fixture.config));
    CHECK(fixture.state.readHeader->arrivalDate.format("%Y-%m-%d %H:%M") ==
          was.arrivalDate.format("%Y-%m-%d %H:%M"));
    CHECK(fixture.state.base->uidOf(1) == uid);
    CHECK(fixture.state.base->header(2).subject == second);

    REQUIRE(fixture.state.readBody.has_value());
    const auto lines = visibleLines(*fixture.state.readBody);
    REQUIRE(lines.size() > 1);
    CHECK(lines[0] == "Edited here.");
    CHECK(lines[1] == first);

    // The MSGID is a new one, naming this system and this moment: what went out
    // under the old one is not what the message now says.
    std::vector<std::string> kludges;
    for (const auto& line : fixture.state.readBody->lines) {
        if (line.kludge) kludges.push_back(line.text);
    }
    const auto msgid = std::find_if(kludges.begin(), kludges.end(), [](const auto& line) {
        return line.compare(0, 7, "@MSGID:") == 0;
    });
    REQUIRE(msgid != kludges.end());
    const std::string prefix = "@MSGID: 192:168/9999 ";
    CHECK(msgid->compare(0, prefix.size(), prefix) == 0);
    // FTS-0009 wants eight hexadecimal digits of serial, and nothing else.
    CHECK(msgid->size() == prefix.size() + 8);
    const auto wasMsgid =
        std::find_if(kept.kludges.begin(), kept.kludges.end(),
                     [](const auto& line) { return line.compare(0, 6, "MSGID:") == 0; });
    REQUIRE(wasMsgid != kept.kludges.end());
    CHECK(msgid->substr(1) != *wasMsgid);

    // Everything else it carried it carries still, and the routing is back
    // after the text where it stood rather than in front of the message. TZUTC
    // moves with the stamp it describes, which is now the clock here.
    std::string shown;
    for (const auto& line : kludges) shown += line + "|";
    std::string expected;
    for (const auto& kludge : kept.kludges) {
        std::string line = kludge;
        if (line.compare(0, 6, "MSGID:") == 0) line = msgid->substr(1);
        if (line.compare(0, 6, "TZUTC:") == 0) line = "TZUTC: " + tzutcNow();
        expected += "@" + line + "|";
    }
    for (const auto& line : kept.trailing) {
        expected += (line.front() == '\x01' ? "@" + line.substr(1) : line) + "|";
    }
    CHECK(shown == expected);
}

TEST_CASE("Dropping a change leaves the message as it was", "[change][squish]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    openFirstMessage(fixture);

    const std::string subject = fixture.state.readHeader->subject;
    const auto lines = visibleLines(*fixture.state.readBody);

    REQUIRE(message_read::handleEvent(fixture.state, Event::Character('c')));
    answerYes(fixture.state);
    fixture.state.edit.lines.insert(fixture.state.edit.lines.begin(), "Not to be kept.");

    REQUIRE(compose::handleEvent(fixture.state, Event::Escape));
    REQUIRE(fixture.state.confirm == AppState::Confirm::DropMessage);
    fixture.state.confirm = AppState::Confirm::None;
    compose::dropMessage(fixture.state);

    CHECK(fixture.state.navigator.current() == ScreenId::MessageRead);
    CHECK_FALSE(fixture.state.compose.changing);
    CHECK(fixture.state.changeNumber == 0);
    CHECK(fixture.state.base->header(1).subject == subject);
    CHECK(visibleLines(fixture.state.base->body(1)) == lines);
}

TEST_CASE("The header of a message being changed rewrites nothing under it",
          "[change][squish]") {
    TempSquishBase base;
    TempTemplate tpl;
    AppConfig config;
    config.templatePath = tpl.path();
    AreaFixture fixture(base.path(), config);
    openFirstMessage(fixture);

    REQUIRE(message_read::handleEvent(fixture.state, Event::Character('c')));
    answerYes(fixture.state);
    const auto opened = fixture.state.edit.lines;

    // Up into the header, a subject typed over, and back down — Enter off the
    // subject is what hands the typing to the text. For a message being written
    // that is what expands the template again; here there is no template to
    // expand, and the message must survive the trip untouched.
    compose::editHeader(fixture.state);
    fixture.state.compose.subject = "Something else";
    REQUIRE(compose::handleEvent(fixture.state, Event::Return));
    REQUIRE_FALSE(fixture.state.composeInHeader);

    CHECK(fixture.state.edit.lines == opened);
    CHECK_FALSE(textHas(fixture.state, "A greeting for"));
}
