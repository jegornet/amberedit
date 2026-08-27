#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <vector>

#include "app/area_manager.hpp"
#include "config/app_config.hpp"
#include "domain/area.hpp"
#include "domain/message.hpp"
#include "msgbase/null_lastread_store.hpp"
#include "ports/i_area_source.hpp"
#include "ui/app_state.hpp"
#include "ui/external_dialog.hpp"
#include "ui/focus.hpp"
#include "ui/hint_bar.hpp"
#include "ui/keys.hpp"
#include "ui/menu_dialog.hpp"
#include "ui/screens/compose_screen.hpp"
#include "ui/term/element.hpp"
#include "ui/term/event.hpp"
#include "ui/term/screen.hpp"

using amberedit::app::ScreenId;
using amberedit::config::AppConfig;
using amberedit::config::Command;
using amberedit::domain::AreaConfig;
using amberedit::domain::AreaKind;
using amberedit::domain::FtnAddress;
using amberedit::ui::AppState;
using amberedit::ui::term::Event;

namespace compose = amberedit::ui::screens::compose;
namespace external_dialog = amberedit::ui::external_dialog;
namespace menu_dialog = amberedit::ui::menu_dialog;
namespace term = amberedit::ui::term;

using Answer = AppState::ExternalReview::Answer;

namespace {

class EmptyAreaSource final : public amberedit::ports::IAreaConfigSource {
public:
    tl::expected<std::vector<AreaConfig>, amberedit::ErrorPtr> loadAreas() override {
        return {};
    }
};

/// A state in one echo, with `external_editor` naming a program — which is the
/// whole of what turns the internal editor off. Nothing here ever runs it: the
/// shell is what hands the terminal over, and what the screen does is ask.
struct ExternalFixture {
    ExternalFixture() : config(configWithEditor()),
                        manager(std::make_unique<EmptyAreaSource>(),
                                std::make_unique<amberedit::msgbase::NullLastReadStore>(),
                                config),
                        state(manager, config) {
        state.currentArea.tag = "test.area";
        state.currentArea.kind = AreaKind::Echo;
        if (const auto address = FtnAddress::parse("2:382/736")) {
            state.currentArea.address = *address;
        }
        state.width = 80;
        state.height = 24;
    }

    static AppConfig configWithEditor() {
        AppConfig cfg;
        cfg.userName = "Yegor Gluhov";
        cfg.userAddress = FtnAddress::parse("2:382/736");
        cfg.externalEditor = {"mcedit", "$msg"};
        return cfg;
    }

    /// The message on the reader's screen, which is what a reply answers.
    void readingAMessage() {
        amberedit::domain::MessageHeader answered;
        answered.number = 1;
        answered.from = "Vasya Pupkin";
        answered.to = "Yegor Gluhov";
        answered.subject = "a thread";
        state.readHeader = answered;
    }

    /// The editor having been run and having come back with `lines`, which is
    /// what `runApp()` does on the pass after the screen asked for it.
    void editorLeft(std::vector<std::string> lines) {
        REQUIRE(state.externalEditRequested);
        state.externalEditRequested = false;
        compose::externalEditReturned(state, /*changed=*/true, std::move(lines));
    }

    AppConfig config;
    amberedit::app::AreaManager manager;
    AppState state;
};

/// The rows of a frame, as text.
std::vector<std::string> rowsOf(const AppState& state, const term::Element& document) {
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

bool shows(const std::vector<std::string>& rows, const std::string& what) {
    for (const auto& row : rows) {
        if (row.find(what) != std::string::npos) return true;
    }
    return false;
}

Event ctrl(char letter) {
    return Event::Character(std::string(1, letter), true, false, false);
}

Event clickAt(int x, int y) {
    term::MouseEvent mouse;
    mouse.button = term::MouseEvent::Button::Left;
    mouse.motion = term::MouseEvent::Motion::Pressed;
    mouse.x = x;
    mouse.y = y;
    return Event::Mouse(mouse);
}

}  // namespace

TEST_CASE("A reply goes straight to the editor the config names [externaleditor]") {
    ExternalFixture fixture;
    auto& state = fixture.state;
    fixture.readingAMessage();

    compose::startReply(state);
    REQUIRE(state.navigator.current() == ScreenId::Compose);
    // Nothing was asked: the header came off the message being answered, and
    // what is left to do is write, which is done elsewhere.
    CHECK(state.externalEditRequested);
    // And the typing is in the header, which is the only half of this screen
    // that takes any.
    CHECK(state.composeInHeader);
}

TEST_CASE("A new message is asked its header first [externaleditor]") {
    ExternalFixture fixture;
    auto& state = fixture.state;

    compose::startNew(state);
    REQUIRE(state.navigator.current() == ScreenId::Compose);
    CHECK_FALSE(state.externalEditRequested);
    CHECK(state.composeInHeader);
    CHECK(state.composeField == compose::kToName);

    // Enter down the block: the name, the subject, and off the subject is the
    // editor rather than a cursor in the text.
    compose::handleEvent(state, Event::Return);  // To name -> Subject
    REQUIRE(state.composeField == compose::kSubject);
    CHECK_FALSE(state.externalEditRequested);
    compose::handleEvent(state, Event::Return);
    CHECK(state.externalEditRequested);
    CHECK(state.composeInHeader);
}

TEST_CASE("A click in the message asks for the editor too [externaleditor]") {
    ExternalFixture fixture;
    auto& state = fixture.state;

    compose::startNew(state);
    // The frame is what decides where the rows of the text landed, so a click
    // is tested against one that has been drawn.
    static_cast<void>(rowsOf(state, compose::render(state)));
    REQUIRE_FALSE(state.composeTextRows.empty());

    const term::Box& row = state.composeTextRows.front().box;
    compose::handleEvent(state, clickAt(row.x_min, row.y_min));
    CHECK(state.externalEditRequested);
    // Still no cursor down there: the click said "write the message", not
    // "put the cursor on this character".
    CHECK(state.composeInHeader);
}

TEST_CASE("A file that came back untouched drops the message [externaleditor]") {
    ExternalFixture fixture;
    auto& state = fixture.state;
    fixture.readingAMessage();

    compose::startReply(state);
    REQUIRE(state.navigator.current() == ScreenId::Compose);
    state.externalEditRequested = false;

    compose::externalEditReturned(state, /*changed=*/false, {"the quote"});
    // Back to the reader with nothing asked and nothing stored: leaving the
    // editor without writing is how the user says they thought better of it.
    CHECK(state.navigator.current() != ScreenId::Compose);
    CHECK_FALSE(state.externalReview);
}

TEST_CASE("A refusal is an untouched file the box never stood over [externaleditor]") {
    // The rule entire, in the two shapes it takes. Nothing about *how* the
    // editor was reached the second time matters — only that the box has been
    // shown since the message was begun.
    SUBCASE("never shown: the message is refused") {
        ExternalFixture fixture;
        auto& state = fixture.state;
        fixture.readingAMessage();
        compose::startReply(state);
        state.externalEditRequested = false;

        compose::externalEditReturned(state, /*changed=*/false, {"the quote"});
        CHECK(state.navigator.current() != ScreenId::Compose);
    }

    SUBCASE("shown, and reached again through the header") {
        ExternalFixture fixture;
        auto& state = fixture.state;

        compose::startNew(state);
        compose::handleEvent(state, Event::Return);  // To name -> Subject
        compose::handleEvent(state, Event::Return);  // off the subject
        fixture.editorLeft({"a message of my own"});
        // The box stood; Header put the typing back in the block, and Enter off
        // the subject went into the editor again.
        state.externalReview.reset();
        compose::editHeader(state);
        state.compose.subject = "changed";
        compose::handleEvent(state, Event::Return);
        REQUIRE(state.externalEditRequested);
        state.externalEditRequested = false;

        compose::externalEditReturned(state, /*changed=*/false,
                                      {"a message of my own"});
        CHECK(state.navigator.current() == ScreenId::Compose);
        REQUIRE(state.externalReview);
        CHECK(state.edit.lines == std::vector<std::string>{"a message of my own"});
    }

    SUBCASE("the count starts afresh with the next message") {
        ExternalFixture fixture;
        auto& state = fixture.state;
        fixture.readingAMessage();

        // One message written, reviewed and dropped.
        compose::startReply(state);
        fixture.editorLeft({"Hello, Vasya"});
        state.externalReview.reset();
        compose::dropMessage(state);
        REQUIRE(state.navigator.current() != ScreenId::Compose);

        // The next one is a message nobody has been shown anything about, so an
        // untouched file refuses it as it always would.
        compose::startReply(state);
        state.externalEditRequested = false;
        compose::externalEditReturned(state, /*changed=*/false, {"the quote"});
        CHECK(state.navigator.current() != ScreenId::Compose);
    }
}

TEST_CASE("Continue that changed nothing keeps the message [externaleditor]") {
    ExternalFixture fixture;
    auto& state = fixture.state;
    fixture.readingAMessage();

    compose::startReply(state);
    fixture.editorLeft({"Hello, Vasya"});
    state.externalReview.reset();
    compose::requestExternalEditor(state);
    state.externalEditRequested = false;

    // Back in and out again with nothing touched. The first time round that
    // would have thrown the message away, there being no other way to say no;
    // here the box is back with Discard on it and the message underneath.
    compose::externalEditReturned(state, /*changed=*/false, {"Hello, Vasya"});
    CHECK(state.navigator.current() == ScreenId::Compose);
    REQUIRE(state.externalReview);
    CHECK(state.edit.lines == std::vector<std::string>{"Hello, Vasya"});
}

TEST_CASE("What the editor wrote is shown under the header [externaleditor]") {
    ExternalFixture fixture;
    auto& state = fixture.state;
    fixture.readingAMessage();

    compose::startReply(state);
    fixture.editorLeft({"Hello, Vasya", "", "Bye"});

    REQUIRE(state.navigator.current() == ScreenId::Compose);
    REQUIRE(state.externalReview);
    const auto rows = rowsOf(state, compose::render(state));
    CHECK(shows(rows, "Hello, Vasya"));
    // The box over it says what can be done next, and every one of the four is
    // a button because there is no cursor on this screen to do it with.
    const auto withBox =
        rowsOf(state, external_dialog::render(state, compose::render(state)));
    CHECK(shows(withBox, "Save"));
    CHECK(shows(withBox, "Discard"));
    CHECK(shows(withBox, "Continue"));
    CHECK(shows(withBox, "Header"));
}

TEST_CASE("The message that came back cannot be typed into [externaleditor]") {
    ExternalFixture fixture;
    auto& state = fixture.state;
    fixture.readingAMessage();

    compose::startReply(state);
    fixture.editorLeft({"Hello, Vasya"});
    state.externalReview.reset();

    const std::vector<std::string> written = state.edit.lines;
    // Every key the screen answers goes to the header block. `x` typed here is
    // a character in whichever field the cursor is in, and never a character in
    // the message.
    compose::handleEvent(state, Event::Character("x"));
    CHECK(state.edit.lines == written);
    CHECK(state.composeInHeader);
    // Nor do the keys that edit lines: they are the internal editor's, and
    // there is none.
    compose::handleEvent(state, ctrl('y'));
    CHECK(state.edit.lines == written);
}

TEST_CASE("Reading a file into the message is dead here [externaleditor]") {
    ExternalFixture fixture;
    auto& state = fixture.state;
    fixture.config.composeMenu = {Command::ComposeImport, Command::ComposeSave};

    compose::startNew(state);
    compose::handleEvent(state, ctrl('o'));
    CHECK_FALSE(state.importPicker);

    // And the button for it is drawn dim rather than left to be pressed.
    compose::openMenu(state);
    REQUIRE(state.menuView);
    REQUIRE(state.menuView->items.size() == 2);
    CHECK_FALSE(state.menuView->items[0].enabled);
    CHECK(state.menuView->items[1].enabled);
}

TEST_CASE("The delete-line button is not drawn over a message shown [externaleditor]") {
    ExternalFixture fixture;
    auto& state = fixture.state;
    // Asked for by the config and still not there: it is a button that edits
    // the text, and the text here is what somebody else's editor left.
    fixture.config.composeDeleteLineButton = amberedit::config::Visibility::On;

    compose::startNew(state);
    CHECK_FALSE(state.composeDeleteLineShown());
}

TEST_CASE("The four answers to what the editor left [externaleditor]") {
    SUBCASE("Header puts the typing into the block") {
        ExternalFixture fixture;
        auto& state = fixture.state;
        fixture.readingAMessage();
        compose::startReply(state);
        fixture.editorLeft({"Hello"});

        REQUIRE(external_dialog::handleEvent(state, Event::Character("h")) ==
                external_dialog::Outcome::Picked);
        CHECK(state.externalReview->answer == Answer::Header);
        state.externalReview.reset();
        compose::editHeader(state);
        CHECK(state.composeInHeader);
        CHECK(state.navigator.current() == ScreenId::Compose);
    }

    SUBCASE("Esc is that same answer and never Discard") {
        ExternalFixture fixture;
        auto& state = fixture.state;
        fixture.readingAMessage();
        compose::startReply(state);
        fixture.editorLeft({"Hello"});

        REQUIRE(external_dialog::handleEvent(state, Event::Escape) ==
                external_dialog::Outcome::Picked);
        CHECK(state.externalReview->answer == Answer::Header);
    }

    SUBCASE("Continue asks for the editor again") {
        ExternalFixture fixture;
        auto& state = fixture.state;
        fixture.readingAMessage();
        compose::startReply(state);
        fixture.editorLeft({"Hello"});

        REQUIRE(external_dialog::handleEvent(state, Event::Character("c")) ==
                external_dialog::Outcome::Picked);
        CHECK(state.externalReview->answer == Answer::Again);
        state.externalReview.reset();
        compose::requestExternalEditor(state);
        CHECK(state.externalEditRequested);
        CHECK(state.navigator.current() == ScreenId::Compose);
    }

    SUBCASE("Discard leaves the editor with nothing stored") {
        ExternalFixture fixture;
        auto& state = fixture.state;
        fixture.readingAMessage();
        compose::startReply(state);
        fixture.editorLeft({"Hello"});

        REQUIRE(external_dialog::handleEvent(state, Event::Character("d")) ==
                external_dialog::Outcome::Picked);
        CHECK(state.externalReview->answer == Answer::Drop);
        state.externalReview.reset();
        compose::dropMessage(state);
        CHECK(state.navigator.current() != ScreenId::Compose);
    }
}

TEST_CASE("The box scrolls the message it is asking about [externaleditor]") {
    ExternalFixture fixture;
    auto& state = fixture.state;
    fixture.readingAMessage();
    compose::startReply(state);

    std::vector<std::string> many;
    many.reserve(200);
    for (int i = 0; i < 200; ++i) many.push_back("line " + std::to_string(i));
    fixture.editorLeft(many);

    REQUIRE(state.editScroll == 0);
    CHECK(external_dialog::handleEvent(state, Event::ArrowDown) ==
          external_dialog::Outcome::Ignored);
    CHECK(state.editScroll == 1);
    // ←→ are the buttons, not the message.
    CHECK(external_dialog::handleEvent(state, Event::ArrowRight) ==
          external_dialog::Outcome::Ignored);
    CHECK(state.externalReview->answer == Answer::Drop);
    CHECK(state.editScroll == 1);
}

TEST_CASE("A template is never expanded over what the editor left [externaleditor]") {
    ExternalFixture fixture;
    auto& state = fixture.state;

    compose::startNew(state);
    compose::handleEvent(state, Event::Return);
    compose::handleEvent(state, Event::Return);
    fixture.editorLeft({"a message of my own"});
    state.externalReview.reset();

    // Back into the header, a field changed, and down again: what comes back
    // is the message as it was written, not the template built afresh round a
    // new subject.
    compose::editHeader(state);
    state.compose.subject = "changed";
    compose::handleEvent(state, Event::Return);
    CHECK(state.edit.lines == std::vector<std::string>{"a message of my own"});
}

TEST_CASE("A dead command is not written in the hint row [externaleditor]") {
    ExternalFixture fixture;
    auto& state = fixture.state;
    state.keys = amberedit::ui::KeyMap::defaults();

    compose::startNew(state);
    const std::string row = amberedit::ui::hint_bar::text(state);
    // Saving is still there; reading a file in and deleting a line are not —
    // a hint is a reminder of a key worth pressing, and neither of those does
    // anything on a screen whose text came out of another program.
    CHECK(row.find("ctrl-s") != std::string::npos);
    CHECK(row.find("ctrl-o") == std::string::npos);
    CHECK(row.find("ctrl-y") == std::string::npos);
}

TEST_CASE("The page keys walk a long message down the window [externaleditor]") {
    ExternalFixture fixture;
    auto& state = fixture.state;
    fixture.readingAMessage();
    compose::startReply(state);

    std::vector<std::string> many;
    many.reserve(200);
    for (int i = 0; i < 200; ++i) many.push_back("line " + std::to_string(i));
    fixture.editorLeft(many);
    state.externalReview.reset();

    REQUIRE(state.editScroll == 0);
    compose::handleEvent(state, Event::PageDown);
    CHECK(state.editScroll == compose::editorRows(state));
    compose::handleEvent(state, Event::PageUp);
    CHECK(state.editScroll == 0);
}

TEST_CASE("The review box is what a wheel flick is aimed at [externaleditor]") {
    ExternalFixture fixture;
    auto& state = fixture.state;
    fixture.readingAMessage();
    compose::startReply(state);
    fixture.editorLeft({"Hello"});

    CHECK(amberedit::ui::addresseeOf(state) == amberedit::ui::Addressee::External);
}
