#include <doctest/doctest.h>

#include <algorithm>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

#include "app/area_manager.hpp"
#include "app/message_builder.hpp"
#include "config/app_config.hpp"
#include "domain/area.hpp"
#include "domain/message.hpp"
#include "msgbase/null_lastread_store.hpp"
#include "nodelist/nodelist_writer.hpp"
#include "ports/i_area_source.hpp"
#include "temp_dir.hpp"
#include "test_strings.hpp"
#include "ui/app_state.hpp"
#include "ui/attributes_dialog.hpp"
#include "ui/confirm_dialog.hpp"
#include "ui/menu_dialog.hpp"
#include "ui/nodelist_dialog.hpp"
#include "ui/screens/compose_screen.hpp"
#include "ui/term/element.hpp"
#include "ui/term/event.hpp"
#include "ui/term/screen.hpp"
#include "ui/theme.hpp"

using amberedit::app::ScreenId;
using amberedit::config::AppConfig;
using amberedit::config::MenuCommand;
using amberedit::domain::AreaConfig;
using amberedit::domain::AreaKind;
using amberedit::domain::FtnAddress;
using amberedit::ui::term::Event;

namespace compose = amberedit::ui::screens::compose;
namespace confirm_dialog = amberedit::ui::confirm_dialog;
namespace attributes_dialog = amberedit::ui::attributes_dialog;
namespace menu_dialog = amberedit::ui::menu_dialog;
namespace term = amberedit::ui::term;
namespace theme = amberedit::ui::theme;

namespace {

/// Hands back nothing: the compose screen works on the area the state is in and
/// asks the manager for nothing at all.
class EmptyAreaSource final : public amberedit::ports::IAreaConfigSource {
public:
    amberedit::Result<std::vector<AreaConfig>> loadAreas() override { return {}; }
};

/// A state in one area, with no base under it — writing a message touches
/// neither, and the text it opens on is built from the template and the fields
/// alone.
struct ComposeFixture {
    explicit ComposeFixture(AreaKind kind, const std::string& areaAddress)
        : config(configWith(areaAddress)),
          manager(std::make_unique<EmptyAreaSource>(),
                  std::make_unique<amberedit::msgbase::NullLastReadStore>(), config),
          state(manager, config) {
        state.currentArea.tag = "test.area";
        state.currentArea.kind = kind;
        if (const auto address = FtnAddress::parse(areaAddress)) {
            state.currentArea.address = *address;
        }
    }

    static AppConfig configWith(const std::string& userAddress) {
        AppConfig cfg;
        cfg.userName = "Yegor Gluhov";
        cfg.userAddress = FtnAddress::parse(userAddress);
        return cfg;
    }

    /// Enter as many times as it takes to walk the header out of its last
    /// field, which is what hands the typing down to the text.
    void walkToText() {
        for (int i = 0; i <= compose::kFieldCount; ++i) {
            if (!state.composeInHeader) break;
            compose::handleEvent(state, Event::Return);
        }
    }

    AppConfig config;
    amberedit::app::AreaManager manager;
    amberedit::ui::AppState state;
};

/// Ctrl held with a letter, as the input layer hands it over however the
/// terminal happened to spell it.
Event ctrl(char letter) {
    return Event::Character(std::string(1, letter), true, false, false);
}

/// Alt held with a letter, as the input layer hands it over however the
/// terminal happened to spell it.
Event alt(char letter) {
    return Event::Character(std::string(1, letter), false, true, false);
}

/// A left-button press at a cell of the screen.
Event clickAt(int x, int y) {
    term::MouseEvent mouse;
    mouse.button = term::MouseEvent::Button::Left;
    mouse.motion = term::MouseEvent::Motion::Pressed;
    mouse.x = x;
    mouse.y = y;
    return Event::Mouse(mouse);
}

/// The rows of a frame, as text, so that a layout can be read back the way it
/// reaches the terminal. Drawing is also what fills the buttons' boxes in — the
/// frame is what decides where they landed — so a click is tested against a
/// frame that has been drawn, as it is in the shell.
std::vector<std::string> rowsOf(const amberedit::ui::AppState& state,
                                const term::Element& document) {
    term::Screen screen(state.width, state.height);
    term::render(screen, document);

    std::vector<std::string> rows;
    for (int y = 0; y < state.height; ++y) {
        std::string row;
        for (int x = 0; x < state.width; ++x) row += screen.at(x, y).glyph;
        rows.push_back(row);
    }
    return rows;
}

std::vector<std::string> screenRowsOf(amberedit::ui::AppState& state) {
    return rowsOf(state, compose::render(state));
}

/// The dialog over the compose screen, drawn as the shell draws it.
std::vector<std::string> dialogRowsOf(amberedit::ui::AppState& state) {
    return rowsOf(state, attributes_dialog::render(state, compose::render(state)));
}

/// The confirmation over the compose screen, drawn the same way.
std::vector<std::string> confirmRowsOf(amberedit::ui::AppState& state) {
    return rowsOf(state, confirm_dialog::render(state, compose::render(state)));
}

/// Whether any row of a frame holds `what`.
bool shows(const std::vector<std::string>& rows, const std::string& what) {
    for (const auto& row : rows) {
        if (row.find(what) != std::string::npos) return true;
    }
    return false;
}

/// The clock here, written the way the Date row writes it.
std::string stampNow(const AppConfig& config) {
    return amberedit::app::localStamp(std::time(nullptr))
        .format(config.readerDateTimeFormat);
}

/// Which column `what` stands in, or -1 where no row holds it.
int columnOf(const std::vector<std::string>& rows, const std::string& what) {
    for (const auto& row : rows) {
        const size_t at = row.find(what);
        if (at != std::string::npos) return static_cast<int>(at);
    }
    return -1;
}

/// A notch of the wheel, up or down. Where the pointer stands is not asked
/// after: the editor scrolls the message wherever it is pointed at, as the
/// reader scrolls the body it is showing.
Event wheel(bool down) {
    term::MouseEvent mouse;
    mouse.button =
        down ? term::MouseEvent::Button::WheelDown : term::MouseEvent::Button::WheelUp;
    mouse.motion = term::MouseEvent::Motion::Pressed;
    mouse.x = 0;
    mouse.y = 0;
    return Event::Mouse(mouse);
}

/// The rightmost column of a frame, top to bottom — where the scrollbar stands
/// when there is one. Read off the screen rather than off the rows, which are
/// runs of glyphs and cannot be indexed by column.
std::vector<std::string> rightColumnOf(amberedit::ui::AppState& state) {
    term::Screen screen(state.width, state.height);
    term::render(screen, compose::render(state));

    std::vector<std::string> column;
    column.reserve(static_cast<size_t>(state.height));
    for (int y = 0; y < state.height; ++y) {
        column.push_back(screen.at(state.width - 1, y).glyph);
    }
    return column;
}

/// Whether a column holds `glyph` anywhere down it.
bool holds(const std::vector<std::string>& column, const std::string& glyph) {
    return std::find(column.begin(), column.end(), glyph) != column.end();
}

/// A message of `count` lines, each naming itself, in the editor.
void fillText(amberedit::ui::AppState& state, int count) {
    state.edit.lines.clear();
    for (int i = 0; i < count; ++i) {
        state.edit.lines.push_back("line " + std::to_string(i));
    }
    state.edit.row = 0;
    state.edit.col = 0;
    state.editScroll = 0;
}

/// Whether any line of the message being written holds `what`.
bool textHas(const amberedit::ui::AppState& state, const std::string& what) {
    for (const auto& line : state.edit.lines) {
        if (line.find(what) != std::string::npos) return true;
    }
    return false;
}

}  // namespace

TEST_CASE("A new message opens in the header, over the text it will be [compose]") {
    ComposeFixture fixture(AreaKind::Echo, "2:5020/1");
    auto& state = fixture.state;

    compose::startNew(state);
    CHECK(state.navigator.current() == ScreenId::Compose);
    // Who the message is for is the one thing a new one cannot be written
    // without, so that is where the typing goes first.
    CHECK(state.composeInHeader);
    CHECK(state.composeField == compose::kToName);
    // And the message is already there under it: the tearline and the origin
    // close it from the moment it is begun.
    CHECK(state.edit.lines.size() >= 2);
    CHECK(textHas(state, " * Origin:"));
}

TEST_CASE("Enter off the subject hands the typing down to the text [compose]") {
    ComposeFixture fixture(AreaKind::Echo, "2:5020/1");
    auto& state = fixture.state;

    compose::startNew(state);
    fixture.walkToText();
    CHECK_FALSE(state.composeInHeader);
    CHECK(state.navigator.current() == ScreenId::Compose);

    // What is typed now goes into the message, not into the subject.
    compose::handleEvent(state, Event::Character('x'));
    CHECK(state.compose.subject.empty());
    CHECK(textHas(state, "x"));
}

TEST_CASE("Alt-H goes back up into the header, onto the field left behind "
          "[compose]") {
    ComposeFixture fixture(AreaKind::Echo, "2:5020/1");
    auto& state = fixture.state;

    compose::startNew(state);
    fixture.walkToText();
    REQUIRE_FALSE(state.composeInHeader);

    compose::handleEvent(state, alt('h'));
    CHECK(state.composeInHeader);
    // On the field the cursor was last in, which is the last one the header was
    // walked out of — "put me back where I was", not "next".
    CHECK(state.composeField == compose::kSubject);
    // And in the header, Tab is the next stop rather than the way out of it.
    compose::handleEvent(state, Event::Tab);
    CHECK(state.composeField == compose::kAttributes);
    compose::handleEvent(state, Event::Tab);
    CHECK_FALSE(state.composeInHeader);

    // Tab out of the text is the ring rather than the way back to the field
    // left behind: it comes up on the first field rather than the last, which
    // is what "next" means with the text standing after the last field.
    compose::handleEvent(state, Event::Tab);
    CHECK(state.composeInHeader);
    CHECK(state.composeField == compose::kFromName);
}

TEST_CASE("Tab walks the whole ring: every field of the header, then the text "
          "[compose]") {
    ComposeFixture fixture(AreaKind::Netmail, "2:5020/1");
    auto& state = fixture.state;

    compose::startNew(state);
    REQUIRE(state.composeInHeader);
    REQUIRE(state.composeField == compose::kToName);

    compose::handleEvent(state, Event::Tab);
    CHECK(state.composeField == compose::kToAddr);
    compose::handleEvent(state, Event::Tab);
    CHECK(state.composeField == compose::kSubject);
    // The attributes button is a stop of its own, between the subject and the text.
    compose::handleEvent(state, Event::Tab);
    CHECK(state.composeField == compose::kAttributes);
    // Off the last stop is the text, which stands in the ring where a field
    // would...
    compose::handleEvent(state, Event::Tab);
    CHECK_FALSE(state.composeInHeader);
    // ...and out of the text is the top of the block again, not the stop it
    // was left from.
    compose::handleEvent(state, Event::Tab);
    CHECK(state.composeInHeader);
    CHECK(state.composeField == compose::kFromName);
    compose::handleEvent(state, Event::Tab);
    CHECK(state.composeField == compose::kFromAddr);
}

TEST_CASE("Shift-Tab walks the same ring the other way round [compose]") {
    ComposeFixture fixture(AreaKind::Echo, "2:5020/1");
    auto& state = fixture.state;

    compose::startNew(state);
    REQUIRE(state.composeField == compose::kToName);

    compose::handleEvent(state, Event::TabReverse);
    CHECK(state.composeField == compose::kFromAddr);
    compose::handleEvent(state, Event::TabReverse);
    CHECK(state.composeField == compose::kFromName);
    // Off the first field is down into the text, as off the last one is going
    // forwards: the ring closes at both ends.
    compose::handleEvent(state, Event::TabReverse);
    CHECK_FALSE(state.composeInHeader);
    // And back up onto the last stop, which is the attributes button, and off it
    // onto the subject.
    compose::handleEvent(state, Event::TabReverse);
    CHECK(state.composeInHeader);
    CHECK(state.composeField == compose::kAttributes);
    compose::handleEvent(state, Event::TabReverse);
    CHECK(state.composeField == compose::kSubject);
    // The To address is skipped in echomail here as it is everywhere else.
    compose::handleEvent(state, Event::TabReverse);
    CHECK(state.composeField == compose::kToName);
}

TEST_CASE("Enter walks the fields and goes past the button into the message "
          "[compose]") {
    ComposeFixture fixture(AreaKind::Echo, "2:5020/1");
    auto& state = fixture.state;

    compose::startNew(state);
    REQUIRE(state.composeField == compose::kToName);
    compose::handleEvent(state, Event::Return);
    CHECK(state.composeField == compose::kSubject);
    // Off the subject is the message, which is what the user came to write —
    // Enter does not stop at a button on the way to it.
    compose::handleEvent(state, Event::Return);
    CHECK_FALSE(state.composeInHeader);

    // And on the button itself Enter is what a button answers to, as is Space.
    compose::handleEvent(state, Event::TabReverse);
    REQUIRE(state.composeField == compose::kAttributes);
    compose::handleEvent(state, Event::Return);
    CHECK(state.attributePicker);
    attributes_dialog::handleEvent(state, Event::Escape);
    REQUIRE_FALSE(state.attributePicker);
    compose::handleEvent(state, Event::Character(' '));
    CHECK(state.attributePicker);
}

TEST_CASE("Leaving the To address picks the AKA whichever way the cursor goes "
          "[compose]") {
    ComposeFixture fixture(AreaKind::Netmail, "2:5020/1");
    auto& state = fixture.state;
    // A second AKA, chosen for the zone the message below is addressed into.
    amberedit::config::AkaMatch aka;
    aka.aka = *FtnAddress::parse("3:633/280");
    aka.patterns = {*amberedit::domain::AddressPattern::parse("3:*")};
    fixture.config.akaMatches = {aka};

    compose::startNew(state);
    compose::handleEvent(state, Event::Tab);
    REQUIRE(state.composeField == compose::kToAddr);
    for (const char letter : std::string("3:633/281")) {
        compose::handleEvent(state, Event::Character(letter));
    }

    // Backwards off it, where Enter would have gone forwards.
    compose::handleEvent(state, Event::TabReverse);
    CHECK(state.composeField == compose::kToName);
    CHECK(state.compose.fromAddr == "3:633/280");
}

TEST_CASE("The arrows walk the block and stop at its top [compose]") {
    ComposeFixture fixture(AreaKind::Echo, "2:5020/1");
    auto& state = fixture.state;

    compose::startNew(state);
    compose::handleEvent(state, Event::ArrowUp);
    CHECK(state.composeField == compose::kFromAddr);
    compose::handleEvent(state, Event::ArrowUp);
    CHECK(state.composeField == compose::kFromName);
    // There is nothing above the first field for ↑ to reach — the text is
    // below the block, and Shift-Tab is what wraps round to it.
    compose::handleEvent(state, Event::ArrowUp);
    CHECK(state.composeInHeader);
    CHECK(state.composeField == compose::kFromName);
}

TEST_CASE("A reply opens on its quote, the header already filled in [compose]") {
    ComposeFixture fixture(AreaKind::Echo, "2:5020/1");
    auto& state = fixture.state;

    amberedit::domain::MessageHeader answered;
    answered.number = 1;
    answered.from = "Vasya Pupkin";
    answered.to = "Yegor Gluhov";
    answered.subject = "a thread";
    state.readHeader = answered;

    compose::startReply(state);
    REQUIRE(state.navigator.current() == ScreenId::Compose);
    // Straight into the text: everything the header wanted came off the message
    // being answered.
    CHECK_FALSE(state.composeInHeader);
    CHECK(state.compose.toName == "Vasya Pupkin");
    CHECK(state.compose.subject == "a thread");
    // And Alt-H lands on the subject, which is the field worth a second look.
    compose::handleEvent(state, alt('h'));
    CHECK(state.composeField == compose::kSubject);
}

TEST_CASE("A comment answers the recipient and is a reply otherwise [compose]") {
    ComposeFixture fixture(AreaKind::Echo, "2:5020/1");
    auto& state = fixture.state;

    amberedit::domain::MessageHeader answered;
    answered.number = 1;
    answered.from = "Vasya Pupkin";
    answered.to = "Petya Ivanov";
    answered.subject = "a thread";
    state.readHeader = answered;

    compose::startCommentReply(state);
    REQUIRE(state.navigator.current() == ScreenId::Compose);
    // The one field that differs from what `q` would have left: the message was
    // written to Petya, and this is what is being said back to him.
    CHECK(state.compose.toName == "Petya Ivanov");
    // Everything else is the reply — the quote it opens on, the subject carried
    // over, and the flag the template and the REPLY kludge are built off.
    CHECK_FALSE(state.composeInHeader);
    CHECK(state.compose.reply);
    CHECK(state.compose.subject == "a thread");
    compose::handleEvent(state, alt('h'));
    CHECK(state.composeField == compose::kSubject);
}

TEST_CASE("A message is not stored without a sender address [compose]") {
    // Neither the area nor the config names an address, so prefill has nothing
    // to put in the From row.
    ComposeFixture fixture(AreaKind::Echo, "");
    auto& state = fixture.state;

    compose::startNew(state);
    REQUIRE(state.compose.fromAddr.empty());
    fixture.walkToText();
    REQUIRE_FALSE(state.composeInHeader);

    // Saving is not even asked about: the cursor goes up to the field at fault,
    // there being no line left to say which it is in.
    compose::handleEvent(state, ctrl('s'));
    CHECK(state.confirm == amberedit::ui::AppState::Confirm::None);
    CHECK(state.composeInHeader);
    CHECK(state.composeField == compose::kFromAddr);

    // An address that does not parse is the same case: it is not one.
    state.compose.fromAddr = "2:5020";
    compose::handleEvent(state, ctrl('s'));
    CHECK(state.confirm == amberedit::ui::AppState::Confirm::None);
    CHECK(state.composeField == compose::kFromAddr);

    // And with one that does, the question is asked.
    state.compose.fromAddr = "2:5020/1.1";
    compose::handleEvent(state, ctrl('s'));
    CHECK(state.confirm == amberedit::ui::AppState::Confirm::SaveMessage);
}

TEST_CASE("In netmail the recipient's address is wanted as well [compose]") {
    ComposeFixture fixture(AreaKind::Netmail, "2:5020/1");
    auto& state = fixture.state;

    compose::startNew(state);
    REQUIRE(state.compose.netmail);
    REQUIRE(state.compose.fromAddr == "2:5020/1");
    REQUIRE(state.compose.toAddr.empty());

    state.compose.toName = "Vasya Pupkin";
    fixture.walkToText();

    compose::handleEvent(state, ctrl('s'));
    CHECK(state.confirm == amberedit::ui::AppState::Confirm::None);
    CHECK(state.composeInHeader);
    CHECK(state.composeField == compose::kToAddr);

    state.compose.toAddr = "2:5015/46.120";
    compose::handleEvent(state, ctrl('s'));
    CHECK(state.confirm == amberedit::ui::AppState::Confirm::SaveMessage);
}

TEST_CASE("Echomail is addressed to the area, so it needs no address for it "
          "[compose]") {
    ComposeFixture fixture(AreaKind::Echo, "2:5020/1");
    auto& state = fixture.state;

    compose::startNew(state);
    REQUIRE_FALSE(state.compose.netmail);
    REQUIRE(state.compose.toAddr.empty());  // and the row is not even shown

    fixture.walkToText();
    compose::handleEvent(state, ctrl('s'));
    CHECK(state.confirm == amberedit::ui::AppState::Confirm::SaveMessage);
}

TEST_CASE("The template is expanded again for a header still being filled in "
          "[compose]") {
    ComposeFixture fixture(AreaKind::Echo, "2:5020/1");
    auto& state = fixture.state;

    compose::startNew(state);
    REQUIRE(textHas(state, "(2:5020/1)"));  // the origin carries the sender

    // The sender's address is changed after the text was built. Leaving the
    // header is where that is known, so the template's words are chosen again.
    state.composeField = compose::kFromAddr;
    state.compose.fromAddr = "2:5020/2";
    fixture.walkToText();
    CHECK(textHas(state, "(2:5020/2)"));
    CHECK_FALSE(textHas(state, "(2:5020/1)"));

    // But once the message is the user's, it is left alone: a single character
    // typed into it is enough.
    compose::handleEvent(state, Event::Character('!'));
    compose::handleEvent(state, alt('h'));
    state.compose.fromAddr = "2:5020/3";
    fixture.walkToText();
    CHECK(textHas(state, "(2:5020/2)"));
    CHECK(textHas(state, "!"));
}

TEST_CASE("A trip through an unchanged header leaves the cursor where it was "
          "[compose]") {
    ComposeFixture fixture(AreaKind::Echo, "2:5020/1");
    auto& state = fixture.state;

    compose::startNew(state);
    fixture.walkToText();
    // Somewhere other than where the template put it — the tearline, which
    // stands below the line typing starts on.
    compose::handleEvent(state, Event::ArrowDown);
    const int row = state.edit.row;
    REQUIRE(row > 0);

    compose::handleEvent(state, alt('h'));
    fixture.walkToText();
    // Nothing in the header changed, so nothing was built again and the cursor
    // is where it was left.
    CHECK(state.edit.row == row);
}

TEST_CASE("A message begins with the attributes its area asks for [compose]") {
    namespace attr = amberedit::domain::attr;

    ComposeFixture echo(AreaKind::Echo, "2:5020/1");
    compose::startNew(echo.state);
    // Written here, and nothing else: an echo is broadcast and private means
    // nothing in one.
    CHECK(echo.state.compose.attributes == attr::kLocal);

    ComposeFixture netmail(AreaKind::Netmail, "2:5020/1");
    compose::startNew(netmail.state);
    CHECK(netmail.state.compose.attributes == (attr::kLocal | attr::kPrivate));
}

TEST_CASE("The header shows the message's attributes under the addresses "
          "[compose]") {
    namespace attr = amberedit::domain::attr;

    ComposeFixture echo(AreaKind::Echo, "2:5020/1");
    compose::startNew(echo.state);
    const auto echoRows = screenRowsOf(echo.state);
    // Uns among them, as the reader shows it: a message being written is local
    // and has not gone anywhere, and it is drawn here the way it will read
    // there. Nothing stands beside them — the attributes are the button.
    CHECK(shows(echoRows, "[Uns Loc]"));
    CHECK_FALSE(shows(echoRows, "Change"));

    // And it goes when the author says the message has gone out, the rule
    // being the reader's own and not a second copy of it.
    echo.state.compose.attributes |= attr::kSent;
    CHECK(shows(screenRowsOf(echo.state), "[Snt Loc]"));

    ComposeFixture netmail(AreaKind::Netmail, "2:5020/1");
    compose::startNew(netmail.state);
    const auto rows = screenRowsOf(netmail.state);
    CHECK(shows(rows, "[Uns Pvt Loc]"));

    // The attributes stand in the column the addresses are in, on the Date row —
    // the subject takes a row to itself and runs the width of the block.
    CHECK(columnOf(rows, "[Uns Pvt Loc]") == columnOf(rows, "2:5020/1"));
    for (const auto& row : rows) {
        if (row.rfind(" Date : ", 0) == 0)
            CHECK(row.find("[Uns Pvt Loc]") != std::string::npos);
    }
}

TEST_CASE("The editor's header block closes with the reader's Date row [compose]") {
    ComposeFixture fixture(AreaKind::Netmail, "2:5020/1");
    auto& state = fixture.state;
    compose::startNew(state);

    // Read either side of the frame: the row is stamped as it is drawn, and a
    // minute that turns between the two would make one of them the wrong one.
    const std::string before = stampNow(state.config);
    const auto rows = screenRowsOf(state);
    const std::string after = stampNow(state.config);

    const int row = [&] {
        for (size_t i = 0; i < rows.size(); ++i) {
            if (rows[i].rfind(" Date : ", 0) == 0) return static_cast<int>(i);
        }
        return -1;
    }();
    // Under From, To and Subj — the fourth row of the block, where the reader
    // draws it over a message being read.
    REQUIRE(row > 0);
    CHECK(rows[static_cast<size_t>(row) - 1].rfind(" Subj : ", 0) == 0);

    const std::string& date = rows[static_cast<size_t>(row)];
    const std::string stamp = date.find(before) != std::string::npos ? before : after;
    // One stamp, the clock: a message being written is written now, and the
    // arrival stamp the reader draws beside it has nothing to say over one
    // that has not arrived from anywhere.
    const size_t written = date.find(stamp);
    REQUIRE(written != std::string::npos);
    CHECK(date.find(stamp, written + stamp.size()) == std::string::npos);
    // In the column the names stand in above.
    CHECK(static_cast<int>(written) == columnOf(rows, "Yegor Gluhov"));

    // Shown rather than typed into: the fields the cursor walks are the five
    // above it, and the header is still left by walking off the subject.
    fixture.walkToText();
    CHECK_FALSE(state.composeInHeader);
    CHECK(shows(screenRowsOf(state), stamp));
}

TEST_CASE("The editor's Recd row is the clock too, where one is asked for [compose]") {
    ComposeFixture fixture(AreaKind::Netmail, "2:5020/1");
    auto& state = fixture.state;
    compose::startNew(state);

    const auto rowOf = [](const std::vector<std::string>& rows, const char* label) {
        for (size_t i = 0; i < rows.size(); ++i) {
            if (rows[i].rfind(label, 0) == 0) return static_cast<int>(i);
        }
        return -1;
    };

    // Off unless the config asks for it, in the editor as in the reader.
    CHECK(fixture.config.showRecdDate == amberedit::config::Visibility::Off);
    auto rows = screenRowsOf(state);
    const int date = rowOf(rows, " Date : ");
    REQUIRE(date > 0);
    CHECK(rowOf(rows, " Recd : ") == -1);
    // The rule closing the block off stands right under the Date row.
    CHECK(rows[static_cast<size_t>(date) + 1].rfind("─", 0) == 0);

    fixture.config.showRecdDate = amberedit::config::Visibility::On;
    const std::string before = stampNow(state.config);
    rows = screenRowsOf(state);
    const std::string after = stampNow(state.config);

    // Under the Date row and above the rule, where the reader draws it.
    CHECK(rowOf(rows, " Date : ") == date);
    REQUIRE(rowOf(rows, " Recd : ") == date + 1);
    CHECK(rows[static_cast<size_t>(date) + 2].rfind("─", 0) == 0);

    // This moment, as the Date row above it is: a message being written arrives
    // here as it is stored. Read either side of the frame, since a minute that
    // turns while it is drawn would make one of the two the wrong one.
    const std::string& recd = rows[static_cast<size_t>(date) + 1];
    CHECK((recd.find(before) != std::string::npos ||
           recd.find(after) != std::string::npos));
    // In the column the names stand in above.
    const std::string stamp = recd.find(before) != std::string::npos ? before : after;
    CHECK(static_cast<int>(recd.find(stamp)) == columnOf(rows, "Yegor Gluhov"));
}

TEST_CASE("The Recd row is no field, and the typing walks past where it stands "
          "[compose]") {
    ComposeFixture fixture(AreaKind::Netmail, "2:5020/1");
    auto& state = fixture.state;
    fixture.config.showRecdDate = amberedit::config::Visibility::On;
    compose::startNew(state);

    // The same ring as without it: the five fields and the attributes button. The
    // row is shown rather than typed into, so it is no stop on the way down.
    fixture.walkToText();
    CHECK_FALSE(state.composeInHeader);
    CHECK(shows(screenRowsOf(state), " Recd : "));
}

TEST_CASE("The editor's Date row answers %z with the clock here [compose]") {
    ComposeFixture fixture(AreaKind::Netmail, "2:5020/1");
    auto& state = fixture.state;
    fixture.config.readerDateTimeFormat = "%H:%M %z";
    compose::startNew(state);

    // The offset the TZUTC of this message will state — the message is being
    // written here, and the reader shows a stored one by the zone it says it
    // was written in. Spelled the way %z spells one, with the plus FTS-4008
    // leaves off a positive offset.
    const std::time_t now = std::time(nullptr);
    std::tm broken{};
    localtime_r(&now, &broken);
    const auto minutes = static_cast<int>(broken.tm_gmtoff / 60);
    const std::string zone =
        (minutes < 0 ? "" : "+") + amberedit::app::tzutcOffset(minutes);

    const auto rows = screenRowsOf(state);
    const auto date = std::find_if(rows.begin(), rows.end(), [](const std::string& row) {
        return row.rfind(" Date : ", 0) == 0;
    });
    REQUIRE(date != rows.end());
    CHECK(date->find(zone) != std::string::npos);
}

TEST_CASE("The attributes stay on offer once the typing has gone into the text "
          "[compose]") {
    ComposeFixture fixture(AreaKind::Netmail, "2:5020/1");
    auto& state = fixture.state;
    compose::startNew(state);
    state.compose.toName = "Vasya Pupkin";
    state.compose.toAddr = "2:5015/46.120";
    fixture.walkToText();
    REQUIRE_FALSE(state.composeInHeader);

    // One screen, so the block over the text is the same block: the attributes are
    // where they were, and are still the button that sets them.
    const auto rows = screenRowsOf(state);
    CHECK(shows(rows, "[Uns Pvt Loc]"));
    CHECK_FALSE(state.changeAttributesBox.IsEmpty());
    compose::handleEvent(state,
                         clickAt(state.changeAttributesBox.x_min,
                                 state.changeAttributesBox.y_min));
    CHECK(state.attributePicker);
    // Pointing at a stop of the header is what puts the typing on it, the same
    // as pointing at a field, so the button is where the cursor now is.
    CHECK(state.composeInHeader);
    CHECK(state.composeField == compose::kAttributes);
}

TEST_CASE("The attributes button is lit when the typing is on it, and plain when not "
          "[compose]") {
    ComposeFixture fixture(AreaKind::Echo, "2:5020/1");
    auto& state = fixture.state;
    compose::startNew(state);

    // The cells the button was drawn on, taken off the frame that drew it.
    const auto buttonCells = [&] {
        term::Screen screen(state.width, state.height);
        term::render(screen, compose::render(state));
        std::vector<term::Cell> cells;
        const term::Box& box = state.changeAttributesBox;
        for (int x = box.x_min; x <= box.x_max; ++x) {
            cells.push_back(screen.at(x, box.y_min));
        }
        REQUIRE_FALSE(cells.empty());
        return cells;
    };

    // Out of focus it wears the fill and the color the header's own boxes wear,
    // so the row reads as one of the stops of the block rather than as a value
    // beside them: what the attributes say is this stop's value, and a value
    // here is written as the addresses over it are.
    REQUIRE(state.composeField != compose::kAttributes);
    for (const auto& cell : buttonCells()) {
        CHECK(cell.fg == theme::palette.inputText);
        CHECK(cell.bg == theme::palette.inputField);
    }

    // In focus it takes the fields' own fill, being a stop in their ring.
    compose::handleEvent(state, Event::Tab);
    compose::handleEvent(state, Event::Tab);
    REQUIRE(state.composeField == compose::kAttributes);
    for (const auto& cell : buttonCells()) {
        CHECK(cell.fg == theme::palette.focusedText);
        CHECK(cell.bg == theme::palette.focusedField);
    }

    // And nothing of it is left lit once the typing has gone back to a field:
    // back to the idle fill the boxes around it carry.
    compose::handleEvent(state, Event::TabReverse);
    REQUIRE(state.composeField == compose::kSubject);
    for (const auto& cell : buttonCells()) CHECK(cell.bg == theme::palette.inputField);
}

TEST_CASE("The fields that are typed into are drawn as boxes that take typing "
          "[compose]") {
    ComposeFixture fixture(AreaKind::Netmail, "2:5020/1");
    auto& state = fixture.state;
    compose::startNew(state);
    REQUIRE(state.composeField == compose::kToName);

    term::Screen screen(state.width, state.height);
    term::render(screen, compose::render(state));

    // Every column of a field carries the fill, right across it, so the box is
    // there to be seen before anything is typed into it.
    const auto fillOf = [&](int which) {
        const term::Box& box = state.composeFieldSpots[static_cast<size_t>(which)].box;
        REQUIRE_FALSE(box.IsEmpty());
        std::vector<term::Color> fill;
        for (int x = box.x_min; x <= box.x_max; ++x) {
            fill.push_back(screen.at(x, box.y_min).bg);
        }
        return fill;
    };

    for (const int which :
         {compose::kFromName, compose::kFromAddr, compose::kToAddr, compose::kSubject}) {
        for (const term::Color& bg : fillOf(which)) {
            CHECK(bg == theme::palette.inputField);
        }
    }
    // And the one the typing is in takes the focused fill, its text written
    // in the color that goes on it.
    for (const term::Color& bg : fillOf(compose::kToName)) {
        CHECK(bg == theme::palette.focusedField);
    }
    const term::Box& focused = state.composeFieldSpots[compose::kToName].box;

    // The label beside a field is not part of it: it says what the box is for
    // and is not typed into, so the fill stops where the box starts.
    CHECK(screen.at(1, focused.y_min).bg.defaulted);
}

/// Turns the underscores on for as long as it stands: the built-in palette
/// leaves them off, and the test that is about them has to ask for them as a
/// theme does.
struct FillerShown {
    FillerShown() { theme::palette.inputFillerShown = true; }
    ~FillerShown() { theme::palette.inputFillerShown = was; }
    bool was{theme::palette.inputFillerShown};
};

TEST_CASE("The room a field has left is underscored [compose]") {
    const FillerShown shown;
    ComposeFixture fixture(AreaKind::Netmail, "2:5020/1");
    auto& state = fixture.state;
    compose::startNew(state);
    REQUIRE(state.composeField == compose::kToName);
    for (const char letter : std::string("Bob")) {
        compose::handleEvent(state, Event::Character(letter));
    }

    term::Screen screen(state.width, state.height);
    term::render(screen, compose::render(state));

    const auto cellsOf = [&](int which) {
        const term::Box& box = state.composeFieldSpots[static_cast<size_t>(which)].box;
        REQUIRE_FALSE(box.IsEmpty());
        std::vector<term::Cell> cells;
        for (int x = box.x_min; x <= box.x_max; ++x) {
            cells.push_back(screen.at(x, box.y_min));
        }
        return cells;
    };

    // A field nothing has been typed into is underscores from end to end: it is
    // a box asking for something, and on a theme whose idle fields carry no fill
    // of their own this is the only thing that says one is there.
    for (const auto& cell : cellsOf(compose::kSubject)) {
        CHECK(cell.glyph == "_");
        CHECK(cell.fg == theme::palette.inputFiller);
    }

    // What was typed is what was typed, and the underscores stand in the room
    // after it — past the cursor, which is the blank the field is scrolled to.
    const auto typed = cellsOf(compose::kToName);
    REQUIRE(typed.size() > 4);
    CHECK(typed[0].glyph == "B");
    CHECK(typed[0].fg == theme::palette.focusedText);
    CHECK(typed[2].glyph == "b");
    CHECK(typed[3].glyph == " ");  // the cursor, drawn inverted on a stand-in blank
    for (size_t i = 4; i < typed.size(); ++i) {
        CHECK(typed[i].glyph == "_");
        CHECK(typed[i].fg == theme::palette.inputFiller);
    }
}

TEST_CASE("The date is shown like the rest of the block, on no fill [compose]") {
    ComposeFixture fixture(AreaKind::Echo, "2:5020/1");
    auto& state = fixture.state;
    compose::startNew(state);

    // Read either side of the frame: the row is stamped as it is drawn, and a
    // minute that turns between the two would make one of them the wrong one.
    const std::string before = stampNow(state.config);
    term::Screen screen(state.width, state.height);
    term::render(screen, compose::render(state));
    const std::string after = stampNow(state.config);

    const auto rowText = [&](int y) {
        std::string out;
        for (int x = 0; x < state.width; ++x) out += screen.at(x, y).glyph;
        return out;
    };

    int row = -1;
    for (int y = 0; y < state.height; ++y) {
        if (rowText(y).rfind(" Date : ", 0) == 0) row = y;
    }
    REQUIRE(row >= 0);

    const std::string line = rowText(row);
    const std::string stamp = line.find(before) != std::string::npos ? before : after;
    const size_t at = line.find(stamp);
    REQUIRE(at != std::string::npos);

    // The stamp in the block's own color and on no fill of its own: it is the
    // one value here that is shown rather than typed into, and the fills on the
    // rows above are what say which of them the typing may go to.
    for (size_t i = 0; i < stamp.size(); ++i) {
        const term::Cell& cell = screen.at(static_cast<int>(at + i), row);
        CHECK(cell.fg == theme::palette.header);
        CHECK(cell.bg.defaulted);
    }
}

TEST_CASE("A message carrying no attributes says so on the button [compose]") {
    ComposeFixture fixture(AreaKind::Echo, "2:5020/1");
    auto& state = fixture.state;
    compose::startNew(state);
    // Nothing at all, which no message opens with — every new one is local —
    // but which the dialog can leave it at.
    state.compose.attributes = 0;

    const auto rows = screenRowsOf(state);
    CHECK(shows(rows, "Attrs..."));
    CHECK_FALSE(shows(rows, "[]"));
    // Still the button it was, with the dialog behind it.
    REQUIRE_FALSE(state.changeAttributesBox.IsEmpty());
    compose::handleEvent(state,
                         clickAt(state.changeAttributesBox.x_min,
                                 state.changeAttributesBox.y_min));
    CHECK(state.attributePicker);
}

TEST_CASE("Every confirmation has two answers and no third way out "
          "[compose][confirm]") {
    using amberedit::ui::AppState;

    ComposeFixture fixture(AreaKind::Echo, "2:5020/1");
    auto& state = fixture.state;
    compose::startNew(state);
    state.compose.toName = "Vasya Pupkin";
    fixture.walkToText();

    compose::handleEvent(state, ctrl('s'));
    REQUIRE(state.confirm == AppState::Confirm::SaveMessage);
    REQUIRE(state.confirmChoice == AppState::ConfirmChoice::Yes);

    // Nothing beside Yes and No, not even on the question asked with the
    // fields still there to be corrected: going back up to them is Tab's.
    const auto rows = confirmRowsOf(state);
    CHECK_FALSE(shows(rows, "Edit Hdr"));
    CHECK(shows(rows, "y/n ·"));
    CHECK(confirm_dialog::handleEvent(state, Event::Character('e')) ==
          confirm_dialog::Outcome::Ignored);

    // ←→ walk the two, round the end and back, and Tab walks them as well.
    confirm_dialog::handleEvent(state, Event::ArrowRight);
    CHECK(state.confirmChoice == AppState::ConfirmChoice::No);
    confirm_dialog::handleEvent(state, Event::ArrowRight);
    CHECK(state.confirmChoice == AppState::ConfirmChoice::Yes);
    confirm_dialog::handleEvent(state, Event::ArrowLeft);
    CHECK(state.confirmChoice == AppState::ConfirmChoice::No);
    confirm_dialog::handleEvent(state, Event::Tab);
    CHECK(state.confirmChoice == AppState::ConfirmChoice::Yes);

    // Enter on No says what Esc says, and the editor is still there to go on
    // writing in: neither stored nor dropped.
    confirm_dialog::handleEvent(state, Event::ArrowRight);
    CHECK(confirm_dialog::handleEvent(state, Event::Return) ==
          confirm_dialog::Outcome::Dismissed);
    CHECK(state.confirm == AppState::Confirm::None);
    CHECK(state.navigator.current() == ScreenId::Compose);

    // And so does the question asked when the editor is left.
    state.confirm = AppState::Confirm::DropMessage;
    const auto dropRows = confirmRowsOf(state);
    CHECK_FALSE(shows(dropRows, "Edit Hdr"));
    CHECK(shows(dropRows, "y/n ·"));
}

TEST_CASE("The attributes dialog opens from the compose screen, by key and by click "
          "[compose]") {
    ComposeFixture fixture(AreaKind::Echo, "2:5020/1");
    auto& state = fixture.state;
    compose::startNew(state);

    compose::handleEvent(state, ctrl('f'));
    REQUIRE(state.attributePicker);
    attributes_dialog::handleEvent(state, Event::Escape);
    CHECK_FALSE(state.attributePicker);

    // And by pointing at the button, once a frame has said where it is.
    screenRowsOf(state);
    REQUIRE_FALSE(state.changeAttributesBox.IsEmpty());
    compose::handleEvent(state,
                         clickAt(state.changeAttributesBox.x_min,
                                 state.changeAttributesBox.y_min));
    CHECK(state.attributePicker);
}

TEST_CASE("Ctrl-W takes the word before the cursor out of the message "
          "[compose][keys]") {
    ComposeFixture fixture(AreaKind::Echo, "2:5020/1");
    auto& state = fixture.state;
    compose::startNew(state);
    // Into the text, which is where the typing goes once the header is done.
    state.composeInHeader = false;
    state.edit.lines = {"one two three"};
    state.edit.row = 0;
    state.edit.col = state.edit.lines[0].size();

    REQUIRE(compose::handleEvent(state, ctrl('w')));
    CHECK(state.edit.lines[0] == "one two ");
    CHECK(state.edit.col == 8);

    // Alt with Backspace does the same, and the bare key still takes out the
    // one character: the modifier is what tells the two apart.
    REQUIRE(
        compose::handleEvent(state, Event::Named(Event::Name::Backspace, false, true)));
    CHECK(state.edit.lines[0] == "one ");
    REQUIRE(compose::handleEvent(state, Event::Backspace));
    CHECK(state.edit.lines[0] == "one");
    state.edit.lines = {"one two three"};
    state.edit.col = state.edit.lines[0].size();

    // On the layout rather than on the chord: a file that has moved it moves it
    // here too, and the key it was on is a key this screen no longer knows.
    state.keys = amberedit::test::valueOf(
        amberedit::ui::KeyMap::parse("F6 compose.delete-word\n", "keys"));
    REQUIRE(compose::handleEvent(state, Event::F6));
    CHECK(state.edit.lines[0] == "one two ");
    CHECK_FALSE(compose::handleEvent(state, ctrl('w')));
    CHECK_FALSE(
        compose::handleEvent(state, Event::Named(Event::Name::Backspace, false, true)));
    CHECK(state.edit.lines[0] == "one two ");
}

TEST_CASE("The dialog turns attributes over by chord, by Space and by click "
          "[compose]") {
    namespace attr = amberedit::domain::attr;

    ComposeFixture fixture(AreaKind::Netmail, "2:5020/1");
    auto& state = fixture.state;
    compose::startNew(state);
    compose::handleEvent(state, ctrl('f'));
    REQUIRE(state.attributePicker);

    // The chord beside the name, which is the one the panel prints.
    attributes_dialog::handleEvent(state, ctrl('c'));
    CHECK((state.compose.attributes & attr::kCrash) != 0);
    attributes_dialog::handleEvent(state, ctrl('c'));
    CHECK((state.compose.attributes & attr::kCrash) == 0);

    // Space turns over whatever the cursor is on, and the cursor opens on the
    // first attribute of the list.
    state.attributePicker->cursor = 0;
    attributes_dialog::handleEvent(state, Event::Character(' '));
    CHECK((state.compose.attributes & attr::kPrivate) == 0);

    // A click lands on the checkbox the pointer is over, without selecting it
    // first — the boxes come off the frame, so one is drawn to find them.
    dialogRowsOf(state);
    const term::Box box = state.attributePicker->boxes.front();
    REQUIRE_FALSE(box.IsEmpty());
    attributes_dialog::handleEvent(state, clickAt(box.x_min + 1, box.y_min));
    CHECK((state.compose.attributes & attr::kPrivate) != 0);

    // Audit Request is Ctrl-T, and the chord that was once its is now nothing
    // here — every chord is still the dialog's while it is up, so Ctrl-Q does
    // not quit out from under a message being addressed.
    attributes_dialog::handleEvent(state, ctrl('t'));
    CHECK((state.compose.attributes & attr::kAuditRequest) != 0);
    attributes_dialog::handleEvent(state, ctrl('q'));
    CHECK((state.compose.attributes & attr::kAuditRequest) != 0);
    REQUIRE(state.attributePicker);

    // Ctrl-Z clears the lot, whatever is on.
    attributes_dialog::handleEvent(state, ctrl('d'));
    REQUIRE(state.compose.attributes != 0);
    attributes_dialog::handleEvent(state, ctrl('z'));
    CHECK(state.compose.attributes == 0);
}

TEST_CASE("Enter keeps what the dialog did and Esc puts it back [compose]") {
    namespace attr = amberedit::domain::attr;

    ComposeFixture fixture(AreaKind::Netmail, "2:5020/1");
    auto& state = fixture.state;
    compose::startNew(state);
    const uint32_t started = state.compose.attributes;

    compose::handleEvent(state, ctrl('f'));
    attributes_dialog::handleEvent(state, ctrl('k'));
    REQUIRE(state.compose.attributes == (started | attr::kKillSent));
    attributes_dialog::handleEvent(state, Event::Return);
    CHECK_FALSE(state.attributePicker);
    CHECK(state.compose.attributes == (started | attr::kKillSent));

    // Every toggle lands on the message as it is made, so Esc has to put back
    // what the dialog opened with rather than merely close.
    compose::handleEvent(state, ctrl('f'));
    attributes_dialog::handleEvent(state, ctrl('c'));
    attributes_dialog::handleEvent(state, ctrl('z'));
    attributes_dialog::handleEvent(state, Event::Escape);
    CHECK_FALSE(state.attributePicker);
    CHECK(state.compose.attributes == (started | attr::kKillSent));
}

TEST_CASE("The dialog lists every attribute with the chord that sets it "
          "[compose]") {
    ComposeFixture fixture(AreaKind::Netmail, "2:5020/1");
    auto& state = fixture.state;
    compose::startNew(state);
    compose::handleEvent(state, ctrl('f'));

    const auto rows = dialogRowsOf(state);
    CHECK(shows(rows, "Message attributes"));
    CHECK(shows(rows, "[x] Private              Ctrl-P"));
    CHECK(shows(rows, "[ ] Crash                Ctrl-C"));
    CHECK(shows(rows, "[x] Local                Ctrl-L"));
    CHECK(shows(rows, "Done"));
}

TEST_CASE("A Ctrl chord is not typed into the field it was pressed in [compose]") {
    ComposeFixture fixture(AreaKind::Echo, "2:5020/1");
    auto& state = fixture.state;

    compose::startNew(state);
    state.composeField = compose::kSubject;
    state.composeCursor = 0;

    compose::handleEvent(state, ctrl('x'));  // a chord nothing binds
    CHECK(state.compose.subject.empty());

    // An ordinary letter still goes in.
    compose::handleEvent(state, Event::Character('x'));
    CHECK(state.compose.subject == "x");
}

TEST_CASE("A name stops at 35 characters and the subject at 71 [compose]") {
    ComposeFixture fixture(AreaKind::Netmail, "2:5020/1");
    auto& state = fixture.state;
    compose::startNew(state);

    // What XMSG keeps room for: 36 bytes of a name and 72 of a subject, the
    // terminating zero among them. A character past that is not typed at all
    // rather than cut off by the base without a word.
    const auto typeInto = [&](int field, const std::string& character, int times) {
        state.composeField = field;
        state.composeCursor = 0;
        // Over an empty field: the prefill has put a name in two of these, and
        // what is being counted here is the whole of what the field holds.
        state.compose.fromName.clear();
        state.compose.toName.clear();
        state.compose.subject.clear();
        for (int i = 0; i < times; ++i)
            compose::handleEvent(state, Event::Character(character));
    };

    typeInto(compose::kFromName, "a", 40);
    CHECK(state.compose.fromName == std::string(35, 'a'));

    typeInto(compose::kToName, "b", 40);
    CHECK(state.compose.toName == std::string(35, 'b'));

    typeInto(compose::kSubject, "c", 80);
    CHECK(state.compose.subject == std::string(71, 'c'));

    // The keystroke that did not fit is the field's all the same: it was aimed
    // there, and nothing else on the screen may act on it.
    state.composeField = compose::kSubject;
    CHECK(compose::handleEvent(state, Event::Character('c')));
    CHECK(state.compose.subject.size() == 71);

    // Room comes back the moment something is deleted.
    state.composeCursor = state.compose.subject.size();
    compose::handleEvent(state, Event::Backspace);
    compose::handleEvent(state, Event::Character('z'));
    CHECK(state.compose.subject == std::string(70, 'c') + "z");
}

TEST_CASE("The limit is in characters, not in bytes [compose]") {
    ComposeFixture fixture(AreaKind::Echo, "2:5020/1");
    auto& state = fixture.state;
    compose::startNew(state);

    // Two bytes to the letter here and one in CP866, which is what the base
    // stores: a limit counted in bytes would refuse a name at seventeen
    // letters — half of what fits.
    state.composeField = compose::kToName;
    state.composeCursor = 0;
    state.compose.toName.clear();  // an echo opens addressed to "All"
    for (int i = 0; i < 40; ++i) compose::handleEvent(state, Event::Character("Я"));

    std::string expected;
    for (int i = 0; i < 35; ++i) expected += "Я";
    CHECK(state.compose.toName == expected);
}

TEST_CASE("The subject box is no wider than what it will hold [compose]") {
    ComposeFixture fixture(AreaKind::Echo, "2:5020/1");
    auto& state = fixture.state;
    state.width = 140;
    compose::startNew(state);
    screenRowsOf(state);

    // A box across a wide window would offer room the base has none for. One
    // column past the limit, for the cursor to stand in past the last
    // character.
    const term::Box& subject = state.composeFieldSpots[compose::kSubject].box;
    REQUIRE_FALSE(subject.IsEmpty());
    CHECK(subject.x_max - subject.x_min + 1 == 72);
}

TEST_CASE("Ctrl-U puts back the line Ctrl-Y took, and only while the message is "
          "being written [compose]") {
    ComposeFixture fixture(AreaKind::Echo, "2:5020/1");
    auto& state = fixture.state;

    compose::startNew(state);
    fixture.walkToText();
    fillText(state, 4);
    state.edit.row = 1;

    compose::handleEvent(state, ctrl('y'));
    compose::handleEvent(state, ctrl('y'));
    CHECK_FALSE(textHas(state, "line 1"));
    CHECK_FALSE(textHas(state, "line 2"));

    // The stack empties in the order it filled: the line that went last is the
    // one that comes back first.
    compose::handleEvent(state, ctrl('u'));
    CHECK(textHas(state, "line 2"));
    CHECK_FALSE(textHas(state, "line 1"));
    compose::handleEvent(state, ctrl('u'));
    const std::vector<std::string> whole{"line 0", "line 1", "line 2", "line 3"};
    CHECK(state.edit.lines == whole);

    // The stack goes with the message: what was deleted out of one is nothing
    // the next message may be handed.
    compose::handleEvent(state, ctrl('y'));
    compose::dropMessage(state);
    compose::startNew(state);
    fixture.walkToText();
    const auto started = state.edit.lines;
    compose::handleEvent(state, ctrl('u'));
    CHECK(state.edit.lines == started);
}

TEST_CASE("Esc asks before dropping the message, wherever the cursor is [compose]") {
    ComposeFixture fixture(AreaKind::Echo, "2:5020/1");
    auto& state = fixture.state;

    // In the header: the text under it is already written, so there is
    // something to lose here too.
    compose::startNew(state);
    REQUIRE(state.composeInHeader);
    compose::handleEvent(state, Event::Escape);
    CHECK(state.confirm == amberedit::ui::AppState::Confirm::DropMessage);
    CHECK(state.navigator.current() == ScreenId::Compose);

    state.confirm = amberedit::ui::AppState::Confirm::None;
    fixture.walkToText();
    compose::handleEvent(state, Event::Escape);
    CHECK(state.confirm == amberedit::ui::AppState::Confirm::DropMessage);
}

TEST_CASE("The editor's menu offers Save and Import, wherever the typing is "
          "[compose][menu]") {
    ComposeFixture fixture(AreaKind::Echo, "2:5020/1");
    auto& state = fixture.state;
    // The window the tests draw into is eighty columns wide, where
    // `when_narrow` leaves the corner to the title.
    fixture.config.menuButton = amberedit::config::Visibility::On;

    compose::startNew(state);
    REQUIRE(state.composeInHeader);
    // The corner is what opens the menu, and it stands in the title row.
    const auto rows = screenRowsOf(state);
    CHECK(shows(rows, "│ ≡ │"));
    REQUIRE(compose::handleEvent(state, clickAt(state.width - 1, 0)));
    REQUIRE(state.menuView);

    const auto find = [&](MenuCommand command) {
        for (const auto& item : state.menuView->items) {
            if (item.command == command) return item;
        }
        FAIL("the command is not in the menu");
        return state.menuView->items.front();
    };

    // Both are about the message rather than about the line the cursor is on,
    // so neither goes quiet while the typing is up in the header: what is read
    // goes into the text, which is the only place a file could go.
    CHECK(find(MenuCommand::Save).enabled);
    REQUIRE(find(MenuCommand::Import).enabled);

    // Drawn first, since where a button landed is what a click is tested
    // against — and then answered the way the shell answers it.
    term::Screen screen(state.width, state.height);
    term::render(screen, menu_dialog::render(state, compose::render(state)));
    const term::Box box = find(MenuCommand::Import).box;
    REQUIRE(menu_dialog::handleEvent(state, clickAt(box.x_min + 1, box.y_min + 1)) ==
            menu_dialog::Outcome::Picked);
    const MenuCommand picked = menu_dialog::current(state);
    state.menuView.reset();
    compose::runMenuCommand(state, picked);
    CHECK(state.importPicker);

    // And Ctrl-O opens the same dialog — from the text as from the header.
    state.importPicker.reset();
    fixture.walkToText();
    compose::handleEvent(state, ctrl('o'));
    CHECK(state.importPicker);
}

TEST_CASE("An imported file goes into the message from the header too [compose]") {
    ComposeFixture fixture(AreaKind::Echo, "2:5020/1");
    auto& state = fixture.state;

    compose::startNew(state);
    REQUIRE(state.composeInHeader);
    const std::vector<std::string> before = state.edit.lines;

    compose::insertImported(state, {"=== Cut ===", "a file", "=== Cut ==="});
    // The typing comes down with it: there is nowhere in the header block for
    // a file to go, and the block is done with once one has been read.
    CHECK_FALSE(state.composeInHeader);
    REQUIRE(state.edit.lines.size() == before.size() + 3);
    CHECK(state.edit.lines[0] == "=== Cut ===");
    CHECK(state.edit.lines[1] == "a file");
}

TEST_CASE("A click on a header field takes the typing up to it [compose][mouse]") {
    ComposeFixture fixture(AreaKind::Echo, "2:5020/1");
    auto& state = fixture.state;

    compose::startNew(state);
    fixture.walkToText();
    state.compose.subject = "Hello there";
    REQUIRE_FALSE(state.composeInHeader);

    // The frame is what says where the field landed, as it is for every other
    // click on the screen.
    screenRowsOf(state);
    const auto& subject = state.composeFieldSpots[compose::kSubject];
    REQUIRE_FALSE(subject.box.IsEmpty());

    compose::handleEvent(state, clickAt(subject.box.x_min + 6, subject.box.y_min));
    CHECK(state.composeInHeader);
    CHECK(state.composeField == compose::kSubject);
    // On the character pointed at, and that is where what is typed next goes.
    CHECK(state.composeCursor == 6);
    compose::handleEvent(state, Event::Character('X'));
    CHECK(state.compose.subject == "Hello Xthere");

    // And from one field to another, without walking the rows in between.
    screenRowsOf(state);
    const auto& from = state.composeFieldSpots[compose::kFromName];
    REQUIRE_FALSE(from.box.IsEmpty());
    compose::handleEvent(state, clickAt(from.box.x_min, from.box.y_min));
    CHECK(state.composeField == compose::kFromName);
    CHECK(state.composeCursor == 0);

    // Pointing past the end of what is written puts the cursor at the end of
    // it: there is nowhere else in the blank part of a field to stand.
    screenRowsOf(state);
    compose::handleEvent(state, clickAt(from.box.x_max, from.box.y_min));
    CHECK(state.composeField == compose::kFromName);
    CHECK(state.composeCursor == state.compose.fromName.size());
}

TEST_CASE("Ctrl-A and Ctrl-E stand for Home and End [compose]") {
    ComposeFixture fixture(AreaKind::Echo, "2:5020/1");
    auto& state = fixture.state;

    compose::startNew(state);
    REQUIRE(state.composeInHeader);

    // In a field of the header, the two ends of what is written there.
    state.composeField = compose::kSubject;
    state.compose.subject = "a thread";
    state.composeCursor = 3;
    compose::handleEvent(state, ctrl('e'));
    CHECK(state.composeCursor == state.compose.subject.size());
    compose::handleEvent(state, ctrl('a'));
    CHECK(state.composeCursor == 0);

    // And in the text, the two ends of the line the cursor stands on — the
    // line, not the message.
    fixture.walkToText();
    state.edit.lines = {"first line", "second line"};
    state.edit.row = 1;
    state.edit.col = 3;
    compose::handleEvent(state, ctrl('e'));
    CHECK(state.edit.row == 1);
    CHECK(state.edit.col == state.edit.lines[1].size());
    compose::handleEvent(state, ctrl('a'));
    CHECK(state.edit.row == 1);
    CHECK(state.edit.col == 0);
}

TEST_CASE("A click in the text brings the typing back down to it [compose][mouse]") {
    ComposeFixture fixture(AreaKind::Echo, "2:5020/1");
    auto& state = fixture.state;

    compose::startNew(state);
    REQUIRE(state.composeInHeader);
    // A message of the user's own rather than the template's, so that leaving
    // the header leaves the lines exactly as they are here.
    state.edit.lines = {"first line", "second line"};
    state.edit.row = 0;
    state.edit.col = 0;
    state.editScroll = 0;

    screenRowsOf(state);
    const auto& second = state.composeTextRows[1];
    REQUIRE_FALSE(second.box.IsEmpty());

    compose::handleEvent(state, clickAt(second.box.x_min + 3, second.box.y_min));
    CHECK_FALSE(state.composeInHeader);
    CHECK(state.edit.row == 1);
    CHECK(state.edit.col == 3);
    compose::handleEvent(state, Event::Character('X'));
    CHECK(state.edit.lines[1] == "secXond line");

    // Past the end of a line is the end of it, and a blank row under the
    // message is its last line — there is no text beyond either.
    screenRowsOf(state);
    compose::handleEvent(state, clickAt(60, second.box.y_min));
    CHECK(state.edit.row == 1);
    CHECK(state.edit.col == state.edit.lines[1].size());

    screenRowsOf(state);
    const auto& blank = state.composeTextRows.back();
    compose::handleEvent(state, clickAt(blank.box.x_min, blank.box.y_min));
    CHECK(state.edit.row == 1);
}

TEST_CASE("A line too wide for the window is shown wrapped and stays one line "
          "[compose]") {
    ComposeFixture fixture(AreaKind::Echo, "2:5020/1");
    auto& state = fixture.state;

    compose::startNew(state);
    fixture.walkToText();
    state.edit.lines = {std::string{}};
    state.edit.row = 0;
    state.edit.col = 0;

    // Typed rather than assigned: what the user writes is never broken for
    // them, and a carriage return the editor put in would go out in the message
    // and come back as a line the user did not write.
    for (int i = 0; i < 20; ++i) {
        for (const char letter : std::string("typed ")) {
            compose::handleEvent(state, Event::Character(std::string(1, letter)));
        }
    }
    REQUIRE(state.edit.lines.size() == 1);
    CHECK(state.edit.lines[0].size() == 120);

    // On the screen it is two rows, the second carrying what did not fit on the
    // first — the window's doing, and the window's alone.
    const auto rows = screenRowsOf(state);
    int carrying = 0;
    for (const auto& row : rows) {
        if (row.find("typed") != std::string::npos) ++carrying;
    }
    CHECK(carrying == 2);
}

TEST_CASE("A line filling the window sends its last word down, not the line sideways "
          "[compose]") {
    ComposeFixture fixture(AreaKind::Echo, "2:5020/1");
    auto& state = fixture.state;

    compose::startNew(state);
    fixture.walkToText();
    // Seventy-nine columns in a window of eighty, with the cursor at the end of
    // them: the line and the cursor together are exactly what the window holds.
    const std::string head(74, 'a');
    state.edit.lines = {head + " test"};
    state.edit.row = 0;
    state.edit.col = state.edit.lines[0].size();

    auto rows = screenRowsOf(state);
    int at = -1;
    for (size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].find(head) != std::string::npos) at = static_cast<int>(i);
    }
    REQUIRE(at >= 0);
    CHECK(rows[static_cast<size_t>(at)].find("test") != std::string::npos);

    // One character more and there is no column left for the cursor, so the
    // word it is at the end of comes down onto the row below. The line keeps
    // its first column — sliding the text sideways to make room for the cursor
    // is what the window has a row to spare for.
    compose::handleEvent(state, Event::Character('s'));
    REQUIRE(state.edit.lines.size() == 1);
    rows = screenRowsOf(state);
    CHECK(rows[static_cast<size_t>(at)].substr(0, head.size()) == head);
    CHECK(rows[static_cast<size_t>(at)].find("test") == std::string::npos);
    CHECK(rows[static_cast<size_t>(at) + 1].substr(0, 5) == "tests");
}

TEST_CASE("A click on a wrapped row lands in the line it continues [compose][mouse]") {
    ComposeFixture fixture(AreaKind::Echo, "2:5020/1");
    auto& state = fixture.state;

    compose::startNew(state);
    // One line, wider than the eighty columns the state is drawn in, and a
    // short one under it.
    std::string wide;
    for (int i = 0; i < 20; ++i) wide += "typed ";
    state.edit.lines = {wide, "under it"};
    state.edit.row = 0;
    state.edit.col = 0;
    state.editScroll = 0;

    screenRowsOf(state);
    const auto second = state.composeTextRows[1];
    REQUIRE_FALSE(second.box.IsEmpty());
    // The row shows the line from where the first row left off, which is where
    // a column pointed at on it is counted from.
    REQUIRE(second.origin > 0);

    // The second row on screen is the rest of the first line, not the line
    // below it: a click there stays in the line it points at.
    compose::handleEvent(state, clickAt(second.box.x_min + 2, second.box.y_min));
    CHECK(state.edit.row == 0);
    CHECK(state.edit.col == second.origin + 2);
    compose::handleEvent(state, Event::Character('X'));
    CHECK(state.edit.lines[0].size() == wide.size() + 1);
    CHECK(state.edit.lines[1] == "under it");

    // Past the end of a row the line goes on past, the cursor stops on that
    // row rather than dropping onto the one below: the click pointed here.
    screenRowsOf(state);
    const auto first = state.composeTextRows[0];
    compose::handleEvent(state, clickAt(state.width - 1, first.box.y_min));
    CHECK(state.edit.row == 0);
    CHECK(state.edit.col < second.origin);

    // And the line under it is a row further down, where the wrapping put it.
    screenRowsOf(state);
    const auto& third = state.composeTextRows[2];
    compose::handleEvent(state, clickAt(third.box.x_min, third.box.y_min));
    CHECK(state.edit.row == 1);
    CHECK(state.edit.col == 0);
}

TEST_CASE("The arrows walk the rows of the screen, not the lines [compose]") {
    ComposeFixture fixture(AreaKind::Echo, "2:5020/1");
    auto& state = fixture.state;

    compose::startNew(state);
    fixture.walkToText();
    std::string wide;
    for (int i = 0; i < 20; ++i) wide += "typed ";
    state.edit.lines = {wide, "under it"};
    state.edit.row = 0;
    state.edit.col = 0;

    // Down off the first row of a wrapped line is the rest of that same line,
    // which is the row under it on the screen.
    compose::handleEvent(state, Event::ArrowDown);
    CHECK(state.edit.row == 0);
    CHECK(state.edit.col > 0);
    compose::handleEvent(state, Event::ArrowDown);
    CHECK(state.edit.row == 1);

    compose::handleEvent(state, Event::ArrowUp);
    CHECK(state.edit.row == 0);
    CHECK(state.edit.col > 0);
    compose::handleEvent(state, Event::ArrowUp);
    CHECK(state.edit.row == 0);
    CHECK(state.edit.col == 0);
}

TEST_CASE("A click lands on a character, not on a byte [compose][mouse]") {
    ComposeFixture fixture(AreaKind::Echo, "2:5020/1");
    auto& state = fixture.state;

    compose::startNew(state);
    fixture.walkToText();
    // Two bytes to the letter, so a column counted as a byte would land inside
    // one of them and leave the field invalid the moment it was typed into.
    state.compose.subject = "Привет мир";

    screenRowsOf(state);
    const auto& subject = state.composeFieldSpots[compose::kSubject];
    REQUIRE_FALSE(subject.box.IsEmpty());
    compose::handleEvent(state, clickAt(subject.box.x_min + 3, subject.box.y_min));
    CHECK(state.composeField == compose::kSubject);
    CHECK(state.composeCursor == 6);

    compose::handleEvent(state, Event::Character("Ъ"));
    CHECK(state.compose.subject == "ПриЪвет мир");
}

TEST_CASE("A field the row does not carry answers no click [compose][mouse]") {
    ComposeFixture echo(AreaKind::Echo, "2:5020/1");
    compose::startNew(echo.state);
    screenRowsOf(echo.state);
    // Echomail is addressed to the area, so the To row carries a name and
    // nothing else — and nothing that was not drawn can be pointed at.
    CHECK(echo.state.composeFieldSpots[compose::kToAddr].box.IsEmpty());

    ComposeFixture netmail(AreaKind::Netmail, "2:5020/1");
    auto& state = netmail.state;
    compose::startNew(state);
    netmail.walkToText();
    screenRowsOf(state);
    const auto& address = state.composeFieldSpots[compose::kToAddr];
    REQUIRE_FALSE(address.box.IsEmpty());
    compose::handleEvent(state, clickAt(address.box.x_min, address.box.y_min));
    CHECK(state.composeInHeader);
    CHECK(state.composeField == compose::kToAddr);
}

TEST_CASE("Clicking off the To address still picks the AKA to write from "
          "[compose][mouse]") {
    ComposeFixture fixture(AreaKind::Netmail, "2:5020/1");
    auto& state = fixture.state;
    // A second AKA, chosen for the zone the message below is addressed into.
    amberedit::config::AkaMatch aka;
    aka.aka = *FtnAddress::parse("3:633/280");
    aka.patterns = {*amberedit::domain::AddressPattern::parse("3:*")};
    fixture.config.akaMatches = {aka};

    compose::startNew(state);
    screenRowsOf(state);
    const term::Box address = state.composeFieldSpots[compose::kToAddr].box;
    REQUIRE_FALSE(address.IsEmpty());
    compose::handleEvent(state, clickAt(address.x_min, address.y_min));
    REQUIRE(state.composeField == compose::kToAddr);
    for (const char letter : std::string("3:633/281")) {
        compose::handleEvent(state, Event::Character(letter));
    }

    // Pointing at the subject is leaving the address, and leaving it is what
    // chooses the sender — the same as Enter off it.
    screenRowsOf(state);
    const term::Box subject = state.composeFieldSpots[compose::kSubject].box;
    compose::handleEvent(state, clickAt(subject.x_min, subject.y_min));
    CHECK(state.composeField == compose::kSubject);
    CHECK(state.compose.fromAddr == "3:633/280");
}

TEST_CASE("The editor's scrollbar is drawn only over a message that overflows "
          "[compose][scrollbar]") {
    ComposeFixture fixture(AreaKind::Echo, "2:5020/1");
    auto& state = fixture.state;
    compose::startNew(state);
    fixture.walkToText();

    const int rows = compose::editorRows(state);
    // What a new message opens on is a few lines of template, well inside one
    // screen: there is nothing to scroll, and so nothing to say it with.
    REQUIRE(static_cast<int>(state.edit.lines.size()) < rows);
    CHECK_FALSE(holds(rightColumnOf(state), "█"));

    // One line more than the window holds, and the bar is there — thumb and
    // track both, as the reader draws them.
    fillText(state, rows + 1);
    const std::vector<std::string> column = rightColumnOf(state);
    CHECK(holds(column, "█"));
    CHECK(holds(column, "│"));
}

TEST_CASE("The reader's scrollbar setting says nothing about the editor's "
          "[compose][scrollbar]") {
    ComposeFixture fixture(AreaKind::Echo, "2:5020/1");
    auto& state = fixture.state;
    // `reader_scrollbar off`, or `b` pressed in the reader: both leave the bar
    // off a message being read, and neither is about one being written.
    state.showScrollbar = false;

    compose::startNew(state);
    fixture.walkToText();
    fillText(state, compose::editorRows(state) + 1);
    CHECK(holds(rightColumnOf(state), "█"));
}

TEST_CASE("The scrollbar costs the text the column it stands in "
          "[compose][scrollbar]") {
    ComposeFixture fixture(AreaKind::Echo, "2:5020/1");
    auto& state = fixture.state;
    compose::startNew(state);
    fixture.walkToText();

    // A line filling the window exactly, in a message long enough to need the
    // bar: the column it takes is one the line no longer has, so the line is
    // broken a character before its end.
    fillText(state, compose::editorRows(state) + 1);
    state.edit.lines[0] = std::string(static_cast<size_t>(state.width), 'a');

    const std::vector<std::string> rows = screenRowsOf(state);
    const std::string toTheBar(static_cast<size_t>(state.width) - 1, 'a');
    CHECK(shows(rows, toTheBar));
    CHECK_FALSE(shows(rows, toTheBar + "a"));
}

TEST_CASE("The wheel scrolls the message being written, the cursor coming along "
          "[compose][mouse]") {
    ComposeFixture fixture(AreaKind::Echo, "2:5020/1");
    auto& state = fixture.state;
    compose::startNew(state);
    fixture.walkToText();

    const int rows = compose::editorRows(state);
    fillText(state, rows * 3);
    state.edit.row = 4;

    // A notch moves the text a row, as it moves the body in the reader, and
    // leaves the cursor where it was: it is still on the screen.
    compose::handleEvent(state, wheel(true));
    CHECK(state.editScroll == 1);
    CHECK(state.edit.row == 4);

    // Once the window has scrolled past it, the cursor comes with it rather
    // than being left behind — the frame scrolls to the cursor, and one off the
    // screen would only put the text straight back where it was.
    for (int i = 0; i < 8; ++i) compose::handleEvent(state, wheel(true));
    CHECK(state.editScroll == 9);
    CHECK(state.edit.row == 9);

    // And back up the same way, the cursor keeping its place until the top of
    // the window reaches it again.
    for (int i = 0; i < 20; ++i) compose::handleEvent(state, wheel(false));
    CHECK(state.editScroll == 0);
    CHECK(state.edit.row == std::min(9, rows - 1));
}

TEST_CASE("The wheel stops at the ends of the message [compose][mouse]") {
    ComposeFixture fixture(AreaKind::Echo, "2:5020/1");
    auto& state = fixture.state;
    compose::startNew(state);
    fixture.walkToText();

    const int rows = compose::editorRows(state);
    fillText(state, rows + 5);

    for (int i = 0; i < 50; ++i) compose::handleEvent(state, wheel(true));
    // The last row of the message stands at the bottom of the window and the
    // scrolling stops: there is nothing under it to bring on screen.
    CHECK(state.editScroll == 5);

    for (int i = 0; i < 50; ++i) compose::handleEvent(state, wheel(false));
    CHECK(state.editScroll == 0);
    // The cursor was carried down to the last row on the way there, and coming
    // back up leaves it standing where it was put: it is on the screen, and
    // scrolling is not a way of moving about the message.
    CHECK(state.edit.row == 5);
}

namespace {

namespace nodelist = amberedit::nodelist;
namespace nodelist_dialog = amberedit::ui::nodelist_dialog;
using Purpose = amberedit::ui::AppState::NodelistView::Purpose;

nodelist::NodeEntry nodeOf(const std::string& address, const std::string& system,
                           const std::string& sysop) {
    nodelist::NodeEntry entry;
    entry.address = *FtnAddress::parse(address);
    entry.system = system;
    entry.sysop = sysop;
    entry.location = "Somewhere";
    entry.phone = "-Unpublished-";
    entry.speed = 300;
    return entry;
}

/// A compiled nodelist beside the fixture, and the config pointed at it.
void giveNodelist(ComposeFixture& fixture, const amberedit::test::TempDir& dir) {
    nodelist::DbSource source;
    source.state.spec = "nodelist";
    source.entries = {
        nodeOf("2:240/0", "Host Nordnetz", "Torsten Bamberg"),
        nodeOf("2:240/1120", "ambrosia60.goip.de", "Ulrich Schroeter"),
        nodeOf("2:240/1200", "Hub Sued", "Ulrich Schroeter jr"),
        nodeOf("2:240/2188", "Kruemel Boks!", "Christian von Busse"),
    };
    REQUIRE(nodelist::writeNodelistDb(dir.path("nodelist.db"), {source}, 0).has_value());
    fixture.config.nodelistDbPath = dir.path("nodelist.db");
}

/// What the shell does with a node picked out of the box: take it off before
/// the box goes, then hand it to the header.
void pickNode(amberedit::ui::AppState& state) {
    REQUIRE(nodelist_dialog::handleEvent(state, Event::Return) ==
            nodelist_dialog::Outcome::Picked);
    const auto purpose = state.nodelistView->purpose;
    const auto node = nodelist_dialog::currentNode(state);
    REQUIRE(node);
    state.nodelistView.reset();
    compose::useNode(state, purpose, *node);
}

}  // namespace

TEST_CASE("Enter on a named recipient with no address asks the nodelist for one "
          "[compose][nodelist]") {
    amberedit::test::TempDir dir;
    ComposeFixture fixture(AreaKind::Netmail, "2:5020/1");
    giveNodelist(fixture, dir);
    auto& state = fixture.state;

    compose::startNew(state);
    // Part of a name is enough to look one up, which is the whole point of
    // being able to.
    state.compose.toName = "Schroeter";
    state.composeField = compose::kToName;

    compose::handleEvent(state, Event::Return);
    REQUIRE(state.nodelistView);
    CHECK(state.nodelistView->purpose == Purpose::PickAddress);
    CHECK(state.nodelistView->lookup == "Schroeter");
    // The cursor stayed on the field being asked about rather than walking to
    // the very one the box is there to fill in.
    CHECK(state.composeField == compose::kToName);

    // The box shows what the name found and nothing else, closest first: the
    // whole name before the name it begins.
    REQUIRE(state.nodelistView->listMatches);
    REQUIRE(state.nodelistView->matches.size() == 2);
    REQUIRE(nodelist_dialog::currentNode(state));
    CHECK(nodelist_dialog::currentNode(state)->sysop == "Ulrich Schroeter");

    // Enter on a row fills in both halves from the node: the address, and the
    // name as the nodelist spells it rather than as it was typed.
    pickNode(state);
    CHECK_FALSE(state.nodelistView);
    CHECK(state.compose.toAddr == "2:240/1120");
    CHECK(state.compose.toName == "Ulrich Schroeter");
    // And the cursor is on the subject: the To row is whole, and there is
    // nothing left on it to stand on.
    CHECK(state.composeField == compose::kSubject);
}

TEST_CASE("Enter on an address with no name asks the nodelist whose it is "
          "[compose][nodelist]") {
    amberedit::test::TempDir dir;
    ComposeFixture fixture(AreaKind::Netmail, "2:5020/1");
    giveNodelist(fixture, dir);
    auto& state = fixture.state;

    compose::startNew(state);
    state.compose.toAddr = "2:240/2188";
    state.composeField = compose::kToAddr;

    compose::handleEvent(state, Event::Return);
    REQUIRE(state.nodelistView);
    CHECK(state.nodelistView->purpose == Purpose::PickName);
    CHECK(state.nodelistView->lookup == "2:240/2188");
    CHECK(state.composeField == compose::kToAddr);

    // The whole nodelist, at that address — the ordinary order, as Ctrl-N shows
    // it: an address is asked about with its neighbours around it.
    CHECK_FALSE(state.nodelistView->listMatches);
    REQUIRE(nodelist_dialog::currentNode(state));
    CHECK(nodelist_dialog::currentNode(state)->address.toString() == "2:240/2188");

    pickNode(state);
    CHECK(state.compose.toName == "Christian von Busse");
    CHECK(state.compose.toAddr == "2:240/2188");
    CHECK(state.composeField == compose::kSubject);
}

TEST_CASE("The node picked is what the To row ends up addressed to "
          "[compose][nodelist]") {
    amberedit::test::TempDir dir;
    ComposeFixture fixture(AreaKind::Netmail, "2:5020/1");
    giveNodelist(fixture, dir);
    auto& state = fixture.state;

    compose::startNew(state);
    // As much of an address as the user knows, to look around at: the box opens
    // on the net, and the node they then pick is a different address from the
    // one they typed.
    state.compose.toAddr = "2:240";
    state.composeField = compose::kToAddr;
    compose::handleEvent(state, Event::Return);
    REQUIRE(state.nodelistView);

    nodelist_dialog::handleEvent(state, Event::ArrowDown);
    pickNode(state);
    CHECK(state.compose.toAddr == "2:240/1120");
    CHECK(state.compose.toName == "Ulrich Schroeter");
}

TEST_CASE("A To row with nothing to look up walks on as Enter always did "
          "[compose][nodelist]") {
    amberedit::test::TempDir dir;
    ComposeFixture fixture(AreaKind::Netmail, "2:5020/1");
    giveNodelist(fixture, dir);
    auto& state = fixture.state;
    compose::startNew(state);

    // Both halves filled in: there is nothing to ask about.
    state.compose.toName = "Ulrich Schroeter";
    state.compose.toAddr = "2:240/1120";
    state.composeField = compose::kToName;
    compose::handleEvent(state, Event::Return);
    CHECK_FALSE(state.nodelistView);
    CHECK(state.composeField == compose::kToAddr);

    // And an empty one is not a question either.
    state.compose.toName.clear();
    state.compose.toAddr.clear();
    state.composeField = compose::kToName;
    compose::handleEvent(state, Event::Return);
    CHECK_FALSE(state.nodelistView);

    // Nor is a name in echomail, where the header addresses nobody in
    // particular and there is no address field at all.
    ComposeFixture echo(AreaKind::Echo, "2:5020/1");
    giveNodelist(echo, dir);
    compose::startNew(echo.state);
    echo.state.compose.toName = "Ulrich Schroeter";
    echo.state.composeField = compose::kToName;
    compose::handleEvent(echo.state, Event::Return);
    CHECK_FALSE(echo.state.nodelistView);
}

namespace {

/// The macro from the documentation, on a fixture's config:
/// `af,AreaFix,2:382/736,"PASSWORD",k/s`.
void giveAreaFixMacro(ComposeFixture& fixture) {
    amberedit::config::AddressMacro macro;
    macro.macro = "af";
    macro.name = "AreaFix";
    macro.address = *FtnAddress::parse("2:382/736");
    macro.subject = "PASSWORD";
    macro.attributes = amberedit::domain::attr::kKillSent;
    fixture.config.addressMacros.push_back(macro);
}

}  // namespace

TEST_CASE("Enter on an address macro fills the whole recipient in [compose][macro]") {
    ComposeFixture fixture(AreaKind::Netmail, "2:5020/1");
    giveAreaFixMacro(fixture);
    auto& state = fixture.state;

    compose::startNew(state);
    const uint32_t started = state.compose.attributes;
    state.compose.toName = "af";
    state.composeField = compose::kToName;

    compose::handleEvent(state, Event::Return);

    CHECK(state.compose.toName == "AreaFix");
    CHECK(state.compose.toAddr == "2:382/736");
    CHECK(state.compose.subject == "PASSWORD");
    // Added to what a netmail of one's own already carries — Loc and Pvt — and
    // not put in their place.
    CHECK(state.compose.attributes == (started | amberedit::domain::attr::kKillSent));
    // The To row is whole, so the cursor comes to rest on the subject, exactly
    // as it does behind a node picked out of the nodelist.
    CHECK(state.composeField == compose::kSubject);
}

TEST_CASE("An address macro is not looked up in the nodelist [compose][macro]") {
    amberedit::test::TempDir dir;
    ComposeFixture fixture(AreaKind::Netmail, "2:5020/1");
    giveNodelist(fixture, dir);
    giveAreaFixMacro(fixture);
    auto& state = fixture.state;

    compose::startNew(state);
    state.compose.toName = "af";
    state.composeField = compose::kToName;
    compose::handleEvent(state, Event::Return);
    CHECK_FALSE(state.nodelistView);
    CHECK(state.compose.toAddr == "2:382/736");

    // A name that is no macro is the nodelist's, as it always was.
    compose::startNew(state);
    state.compose.toName = "Schroeter";
    state.composeField = compose::kToName;
    compose::handleEvent(state, Event::Return);
    CHECK(state.nodelistView);
}

TEST_CASE("An address macro says nothing it was not written with [compose][macro]") {
    ComposeFixture fixture(AreaKind::Netmail, "2:5020/1");
    amberedit::config::AddressMacro macro;
    macro.macro = "boss";
    macro.name = "Sysop";
    macro.address = *FtnAddress::parse("2:382/736");
    fixture.config.addressMacros.push_back(macro);
    auto& state = fixture.state;

    compose::startNew(state);
    state.compose.subject = "Already typed";
    const uint32_t started = state.compose.attributes;
    state.compose.toName = "BOSS";  // read without regard to case
    state.composeField = compose::kToName;

    compose::handleEvent(state, Event::Return);
    CHECK(state.compose.toName == "Sysop");
    CHECK(state.compose.toAddr == "2:382/736");
    // Neither was stated, so neither is touched.
    CHECK(state.compose.subject == "Already typed");
    CHECK(state.compose.attributes == started);
}

TEST_CASE("An address macro is netmail's alone [compose][macro]") {
    ComposeFixture fixture(AreaKind::Echo, "2:5020/1");
    giveAreaFixMacro(fixture);
    auto& state = fixture.state;

    // An echo addresses nobody in particular and has no address field for a
    // macro to fill in, so the word stands as the name it was typed as.
    compose::startNew(state);
    state.compose.toName = "af";
    state.composeField = compose::kToName;
    compose::handleEvent(state, Event::Return);
    CHECK(state.compose.toName == "af");
    CHECK(state.compose.subject.empty());
}

TEST_CASE("The delete-line button closes round the row the cursor is on "
          "[compose][delete_line]") {
    ComposeFixture fixture(AreaKind::Echo, "2:5020/1");
    fixture.config.composeDeleteLineButton = amberedit::config::Visibility::On;
    auto& state = fixture.state;
    compose::startNew(state);
    fixture.walkToText();
    fillText(state, 6);

    // A row of the message with the button's own cells at the end of it, which
    // is where the three columns it took stand.
    const auto beside = [&state](const std::string& line, const std::string& cells) {
        const auto pad = static_cast<size_t>(state.width) - 3 - line.size();
        return line + std::string(pad, ' ') + cells;
    };

    // In the middle of the message: the top of the box over the row, the cross
    // on it, the bottom under it.
    state.edit.row = 3;
    std::vector<std::string> rows = screenRowsOf(state);
    CHECK(shows(rows, beside("line 2", "\u250c\u2500\u2510")));
    CHECK(shows(rows, beside("line 3", "\u2502\u2613\u2502")));
    CHECK(shows(rows, beside("line 4", "\u2514\u2500\u2518")));

    // On the first line there is no row of the message over it to reach into —
    // what is up there is the rule closing the header block.
    state.edit.row = 0;
    rows = screenRowsOf(state);
    CHECK(shows(rows, beside("line 0", "\u2502\u2613\u2502")));
    CHECK(shows(rows, beside("line 1", "\u2514\u2500\u2518")));
    CHECK_FALSE(shows(rows, "\u250c\u2500\u2510"));

    // And on the last there is no row under it: the message has stopped.
    state.edit.row = 5;
    rows = screenRowsOf(state);
    CHECK(shows(rows, beside("line 4", "\u250c\u2500\u2510")));
    CHECK(shows(rows, beside("line 5", "\u2502\u2613\u2502")));
    CHECK_FALSE(shows(rows, "\u2514\u2500\u2518"));
}

TEST_CASE("The delete-line button costs the text three columns on every row "
          "[compose][delete_line]") {
    ComposeFixture fixture(AreaKind::Echo, "2:5020/1");
    fixture.config.composeDeleteLineButton = amberedit::config::Visibility::On;
    auto& state = fixture.state;
    compose::startNew(state);
    fixture.walkToText();

    // Two lines filling the window exactly, and the cursor on neither of them:
    // both are broken three characters short all the same. The button walks the
    // message with the cursor, and a row laid out to the full width until the
    // button reached it would rewrap under it at every keystroke.
    fillText(state, 3);
    state.edit.lines[1] = std::string(static_cast<size_t>(state.width), 'a');
    state.edit.lines[2] = std::string(static_cast<size_t>(state.width), 'b');
    state.edit.row = 0;

    const std::vector<std::string> rows = screenRowsOf(state);
    const std::string aToTheButton(static_cast<size_t>(state.width) - 3, 'a');
    const std::string bToTheButton(static_cast<size_t>(state.width) - 3, 'b');
    CHECK(shows(rows, aToTheButton));
    CHECK_FALSE(shows(rows, aToTheButton + "a"));
    CHECK(shows(rows, bToTheButton));
    CHECK_FALSE(shows(rows, bToTheButton + "b"));
}

TEST_CASE("The delete-line button stands over the scrollbar rather than beside it "
          "[compose][delete_line][scrollbar]") {
    ComposeFixture fixture(AreaKind::Echo, "2:5020/1");
    fixture.config.composeDeleteLineButton = amberedit::config::Visibility::On;
    auto& state = fixture.state;
    compose::startNew(state);
    fixture.walkToText();

    // A message long enough to want the bar, with a line filling what the
    // button has left of the window: the bar takes the last of the three
    // columns the button already took, so the line is broken no further.
    fillText(state, compose::editorRows(state) + 1);
    state.edit.lines[0] = std::string(static_cast<size_t>(state.width), 'a');
    state.edit.row = 0;

    const std::string toTheButton(static_cast<size_t>(state.width) - 3, 'a');
    CHECK(shows(screenRowsOf(state), toTheButton));
    CHECK_FALSE(shows(screenRowsOf(state), toTheButton + "a"));

    // The bar is there, down the rightmost column, and the button's right-hand
    // side is in that column too — over the bar on the rows it stands on.
    const std::vector<std::string> column = rightColumnOf(state);
    CHECK(holds(column, "\u2588"));
    CHECK(holds(column, "\u2502"));
    CHECK(holds(column, "\u2518"));
}

TEST_CASE("Clicking the delete-line button takes the line out, as Ctrl-Y does "
          "[compose][delete_line][mouse]") {
    ComposeFixture fixture(AreaKind::Echo, "2:5020/1");
    fixture.config.composeDeleteLineButton = amberedit::config::Visibility::On;
    auto& state = fixture.state;
    compose::startNew(state);
    fixture.walkToText();
    fillText(state, 4);
    state.edit.row = 2;

    // Drawing is what says where the button landed, as it is for every other
    // button here.
    screenRowsOf(state);
    const term::Box cross = state.composeDeleteLine.label;
    REQUIRE_FALSE(cross.IsEmpty());
    compose::handleEvent(state, clickAt(cross.x_min, cross.y_min));
    CHECK(state.edit.lines.size() == 3);
    CHECK_FALSE(textHas(state, "line 2"));

    // The top of the box is the same button: what is pointed at is one thing,
    // whichever row of it the pointer landed on.
    screenRowsOf(state);
    const term::Box above = state.composeDeleteLine.top;
    REQUIRE_FALSE(above.IsEmpty());
    compose::handleEvent(state, clickAt(above.x_max, above.y_min));
    CHECK(state.edit.lines.size() == 2);
    CHECK_FALSE(textHas(state, "line 3"));
}

TEST_CASE("The delete-line button off leaves the text the whole window "
          "[compose][delete_line]") {
    ComposeFixture fixture(AreaKind::Echo, "2:5020/1");
    fixture.config.composeDeleteLineButton = amberedit::config::Visibility::Off;
    auto& state = fixture.state;
    compose::startNew(state);
    fixture.walkToText();
    fillText(state, 2);
    state.edit.lines[0] = std::string(static_cast<size_t>(state.width), 'a');
    state.edit.row = 0;

    const std::vector<std::string> rows = screenRowsOf(state);
    CHECK(shows(rows, std::string(static_cast<size_t>(state.width), 'a')));
    CHECK_FALSE(shows(rows, "\u2613"));
    CHECK(state.composeDeleteLine.label.IsEmpty());
}

TEST_CASE("The delete-line button follows the window, as when_narrow says "
          "[compose][delete_line]") {
    ComposeFixture fixture(AreaKind::Echo, "2:5020/1");
    auto& state = fixture.state;
    compose::startNew(state);
    fixture.walkToText();
    fillText(state, 2);

    // The default, read against adaptive_ui_threshold on every frame: eighty
    // columns is a wide window and has no button, a column under it is a narrow
    // one and has.
    REQUIRE(fixture.config.composeDeleteLineButton ==
            amberedit::config::Visibility::WhenNarrow);
    state.width = fixture.config.adaptiveUiThreshold;
    CHECK_FALSE(shows(screenRowsOf(state), "\u2613"));

    state.width = fixture.config.adaptiveUiThreshold - 1;
    CHECK(shows(screenRowsOf(state), "\u2613"));
}
