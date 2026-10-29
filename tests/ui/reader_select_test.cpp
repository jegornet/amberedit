#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "domain/message.hpp"
#include "temp_squish_base.hpp"
#include "test_strings.hpp"
#include "ui/area_fixture.hpp"
#include "ui/hint_bar.hpp"
#include "ui/reader_select.hpp"
#include "ui/screens/compose_screen.hpp"
#include "ui/screens/message_list_screen.hpp"
#include "ui/screens/message_read_screen.hpp"
#include "ui/term/element.hpp"
#include "ui/term/event.hpp"
#include "ui/term/screen.hpp"

using amberedit::app::ScreenId;
using amberedit::config::Command;
using amberedit::test::AreaFixture;
using amberedit::test::TempSquishBase;
using amberedit::ui::term::Event;

namespace compose = amberedit::ui::screens::compose;
namespace hint_bar = amberedit::ui::hint_bar;
namespace message_list = amberedit::ui::screens::message_list;
namespace message_read = amberedit::ui::screens::message_read;
namespace reader_select = amberedit::ui::reader_select;
namespace term = amberedit::ui::term;

namespace {

/// The reader on the first message of the test base, with its text replaced by
/// `lines` and laid out again — so that what is selected is known — writing to
/// AmberEdit's own clipboard.
struct Reader {
    TempSquishBase base;
    AreaFixture fixture{base.path()};
    amberedit::ui::AppState& state = fixture.state;

    explicit Reader(const std::vector<std::string>& lines) {
        fixture.config.clipboard = amberedit::config::ClipboardMode::Internal;
        REQUIRE(message_list::enterArea(state, fixture.area).has_value());
        REQUIRE(state.readHeader);
        amberedit::domain::MessageBody body;
        for (const auto& line : lines) body.lines.push_back({line, false, false});
        state.readBody = body;
        state.readLayoutWidth = 0;
        message_read::relayout(state);
        state.readScroll = 0;
    }

    bool key(const Event& event) { return message_read::handleEvent(state, event); }

    /// A frame, which is what fills in where the fields and the rows landed.
    void draw() {
        term::Screen screen(state.width, state.height);
        term::render(screen, message_read::render(state));
    }
};

Event ctrl(char letter) { return Event::Character(std::string(1, letter), true, false, false); }

Event shifted(Event::Name name) { return Event::Named(name, false, false, true); }

Event pointerAt(int x, int y, term::MouseEvent::Motion motion) {
    term::MouseEvent mouse;
    mouse.button = term::MouseEvent::Button::Left;
    mouse.motion = motion;
    mouse.x = x;
    mouse.y = y;
    return Event::Mouse(mouse);
}

}  // namespace

TEST_CASE("v puts a cursor on the first row on screen [messageread][select]") {
    Reader reader({"first line", "second line", "third line"});
    auto& state = reader.state;
    state.readScroll = 1;

    REQUIRE(reader.key(Event::Character('v')));
    REQUIRE(state.readSelect.on);
    CHECK_FALSE(state.readSelect.inHeader);
    CHECK(state.readSelect.text.row == 1);
    CHECK(state.readSelect.text.col == 0);
    CHECK(hint_bar::text(state) == "shift-arrows select  ctrl-c copy  q reply  n reply elsewhere  esc cancel");

    // The arrows move the cursor and not the reader: → is not the next message.
    const uint32_t number = state.readHeader->number;
    reader.key(Event::ArrowRight);
    CHECK(state.readSelect.text.col == 1);
    CHECK(state.readHeader->number == number);

    // The reader's own commands are not answered: `c` does not change the
    // message.
    reader.key(Event::Character('c'));
    CHECK(state.navigator.current() == ScreenId::MessageRead);
    CHECK(state.readSelect.on);

    reader.key(Event::Escape);
    CHECK_FALSE(state.readSelect.on);
    CHECK(state.navigator.current() == ScreenId::MessageRead);
}

TEST_CASE("Enter does not begin the mode [messageread][select]") {
    Reader reader({"text"});
    reader.key(Event::Return);
    CHECK_FALSE(reader.state.readSelect.on);
}

TEST_CASE("Enter and Ctrl-C copy what is selected and leave the mode "
          "[messageread][select][clipboard]") {
    Reader reader({"Hello world", "second line"});
    auto& state = reader.state;

    reader.key(Event::Character('v'));
    // Nothing selected is nothing to copy, and the mode stays on.
    reader.key(Event::Return);
    CHECK(state.readSelect.on);
    CHECK(state.clipboard.empty());

    for (int i = 0; i < 5; ++i) reader.key(shifted(Event::Name::ArrowRight));
    reader.key(Event::Return);
    CHECK(state.clipboard == "Hello");
    CHECK(state.copiedNotice);
    CHECK_FALSE(state.readSelect.on);

    // A break the message wrote is a line feed.
    reader.key(Event::Character('v'));
    reader.key(Event::Named(Event::Name::ArrowRight, false, true, false));  // Alt-→
    reader.key(shifted(Event::Name::ArrowDown));
    reader.key(ctrl('c'));
    CHECK(state.clipboard == " world\nsecon");
    CHECK_FALSE(state.readSelect.on);
}

TEST_CASE("A line the window broke is copied as one line [messageread][select]") {
    Reader reader({"alpha beta gamma delta epsilon zeta eta theta iota kappa"});
    auto& state = reader.state;
    state.width = 20;
    state.readLayoutWidth = 0;
    message_read::relayout(state);
    REQUIRE(state.readLines.size() > 2);
    REQUIRE(state.readLines[0].continues);
    CHECK(state.readLines[0].joiner == " ");
    CHECK_FALSE(state.readLines.back().continues);

    reader.key(Event::Character('v'));
    reader.key(shifted(Event::Name::ArrowDown));
    reader.key(shifted(Event::Name::ArrowDown));
    reader.key(shifted(Event::Name::End));
    reader.key(Event::Return);
    CHECK(state.clipboard ==
          state.readLines[0].text + " " + state.readLines[1].text + " " +
              state.readLines[2].text);
}

TEST_CASE("Up off the first row is the header, walked as the editor walks it "
          "[messageread][select]") {
    Reader reader({"text"});
    auto& state = reader.state;
    reader.draw();

    reader.key(Event::Character('v'));
    reader.key(Event::ArrowUp);
    REQUIRE(state.readSelect.inHeader);
    CHECK(state.readSelect.field == compose::kSubject);
    CHECK(state.readSelect.fieldCursor == state.readHeader->subject.size());

    reader.key(shifted(Event::Name::Home));
    reader.key(ctrl('c'));
    CHECK(state.clipboard == state.readHeader->subject);
    CHECK_FALSE(state.readSelect.on);

    // Tab walks the fields from the From name, the To address left out of an
    // echo, and closes the ring through the text.
    reader.key(Event::Character('v'));
    reader.key(Event::Tab);
    CHECK(state.readSelect.field == compose::kFromName);
    reader.key(Event::Tab);
    CHECK(state.readSelect.field == compose::kFromAddr);
    reader.key(Event::Tab);
    CHECK(state.readSelect.field == compose::kToName);
    reader.key(Event::Tab);
    CHECK(state.readSelect.field == compose::kSubject);
    reader.key(Event::Tab);
    CHECK_FALSE(state.readSelect.inHeader);
    reader.key(Event::TabReverse);
    CHECK(state.readSelect.field == compose::kSubject);

    // The arrows walk it as it is drawn, and down off the subject is the text.
    reader.key(Event::ArrowUp);
    CHECK(state.readSelect.field == compose::kToName);
    reader.key(Event::ArrowUp);
    CHECK(state.readSelect.field == compose::kFromName);
    reader.key(Event::ArrowRight);
    CHECK(state.readSelect.field == compose::kFromAddr);
    CHECK(state.readSelect.fieldCursor == 0);
    reader.key(Event::ArrowLeft);
    CHECK(state.readSelect.field == compose::kFromName);
    reader.key(Event::ArrowDown);
    reader.key(Event::ArrowDown);
    reader.key(Event::ArrowDown);
    CHECK_FALSE(state.readSelect.inHeader);
}

TEST_CASE("A selection copies itself as clipboard_copy_on_select says, and the mode "
          "stays [messageread][select][clipboard]") {
    Reader reader({"first line", "second line"});
    auto& state = reader.state;
    auto& config = reader.fixture.config;

    // The keyboard only under `always`.
    reader.key(Event::Character('v'));
    reader.key(shifted(Event::Name::End));
    CHECK(state.clipboard.empty());
    config.clipboardCopyOnSelect = amberedit::config::CopyOnSelect::Always;
    reader.key(shifted(Event::Name::ArrowLeft));
    CHECK(state.clipboard == "first lin");
    CHECK_FALSE(state.copiedNotice);
    CHECK(state.readSelect.on);

    // A drag under `mouse`, the default, as the button is let go.
    config.clipboardCopyOnSelect = amberedit::config::CopyOnSelect::Mouse;
    reader.draw();
    const auto first = state.readSelect.textRows[0].box;
    const auto second = state.readSelect.textRows[1].box;
    reader.key(pointerAt(first.x_min + 6, first.y_min, term::MouseEvent::Motion::Pressed));
    reader.key(pointerAt(first.x_min + 3, second.y_min, term::MouseEvent::Motion::Moved));
    reader.key(pointerAt(first.x_min + 6, second.y_min, term::MouseEvent::Motion::Released));
    CHECK(state.clipboard == "line\nsecond");
    CHECK_FALSE(state.copiedNotice);
    CHECK(state.readSelect.on);

    // And not at all under `off`.
    config.clipboardCopyOnSelect = amberedit::config::CopyOnSelect::Off;
    state.clipboard.clear();
    reader.draw();
    reader.key(pointerAt(first.x_min, first.y_min, term::MouseEvent::Motion::Pressed));
    reader.key(pointerAt(first.x_min + 5, first.y_min, term::MouseEvent::Motion::Released));
    CHECK(state.clipboard.empty());
    REQUIRE(reader_select::hasSelection(state));
    CHECK(reader_select::selectedText(state) == "first");
}

TEST_CASE("A double click selects a word and a triple one the line "
          "[messageread][select][mouse]") {
    Reader reader({"alpha beta gamma delta epsilon zeta eta theta iota kappa", "next"});
    auto& state = reader.state;
    state.width = 20;
    state.readLayoutWidth = 0;
    message_read::relayout(state);
    REQUIRE(state.readLines.size() > 3);
    amberedit::ui::Millis now = 1000;
    state.monotonicMs = [&now] { return now; };

    reader.key(Event::Character('v'));
    reader.draw();
    const auto second = state.readSelect.textRows[1].box;
    const auto click = [&](const term::Box& box, int dx) {
        reader.key(pointerAt(box.x_min + dx, box.y_min, term::MouseEvent::Motion::Pressed));
        reader.key(pointerAt(box.x_min + dx, box.y_min, term::MouseEvent::Motion::Released));
        now += 150;
    };

    // The second row of a line the window broke: the word, then the whole
    // line, every row of it, joined as it was written.
    const std::string row = state.readLines[1].text;
    const size_t space = row.find(' ');
    REQUIRE(space != std::string::npos);
    click(second, 1);
    click(second, 1);
    CHECK(state.clipboard == row.substr(0, space));
    click(second, 1);
    CHECK(state.clipboard == "alpha beta gamma delta epsilon zeta eta theta iota kappa");
    CHECK(state.readSelect.on);

    // In a field: a word, then the field.
    now += 1000;
    reader.draw();
    const auto from = state.readSelect.fieldSpots[compose::kFromName].box;
    click(from, 1);
    CHECK(state.readSelect.inHeader);
    click(from, 1);
    const std::string name = state.readHeader->from;
    CHECK(state.clipboard == name.substr(0, name.find(' ')));
    click(from, 1);
    CHECK(state.clipboard == name);
}

TEST_CASE("The menu starts and ends the mode, and another message ends it "
          "[messageread][select]") {
    Reader reader({"text"});
    auto& state = reader.state;

    message_read::runMenuCommand(state, Command::ReaderSelect);
    CHECK(state.readSelect.on);
    message_read::runMenuCommand(state, Command::ReaderSelect);
    CHECK_FALSE(state.readSelect.on);

    // Any other button ends it before it does what it does.
    reader.key(Event::Character('v'));
    message_read::runMenuCommand(state, Command::ReaderKludges);
    CHECK_FALSE(state.readSelect.on);

    reader.key(Event::Character('v'));
    REQUIRE(state.readSelect.on);
    message_read::loadMessage(state, state.readHeader->number);
    CHECK_FALSE(state.readSelect.on);
}

TEST_CASE("select closes the reader's default menu [messageread][select][config]") {
    const amberedit::config::AppConfig defaults;
    REQUIRE(!defaults.readerMenu.empty());
    CHECK(defaults.readerMenu.back() == Command::ReaderSelect);
}

TEST_CASE("The corners finish the mode: ✓ copies, ← does not "
          "[messageread][select][clipboard]") {
    Reader reader({"Hello world"});
    auto& state = reader.state;
    reader.fixture.config.menuButton = amberedit::config::Visibility::On;
    reader.fixture.config.backButton = amberedit::config::Visibility::On;

    const auto topRow = [&reader] {
        term::Screen screen(reader.state.width, reader.state.height);
        term::render(screen, message_read::render(reader.state));
        std::string row;
        for (int x = 0; x < reader.state.width; ++x) row += screen.at(x, 0).glyph;
        return row;
    };
    const auto press = [](int x) {
        return pointerAt(x, 0, term::MouseEvent::Motion::Pressed);
    };

    CHECK(topRow().find("│ ≡ │") != std::string::npos);
    reader.key(Event::Character('v'));
    CHECK(topRow().find("│ ✓ │") != std::string::npos);
    CHECK(topRow().find("≡") == std::string::npos);

    // The back button leaves the mode and nothing more: no copy, still reading.
    reader.key(shifted(Event::Name::End));
    reader.key(press(1));
    CHECK_FALSE(state.readSelect.on);
    CHECK(state.clipboard.empty());
    CHECK(state.navigator.current() == ScreenId::MessageRead);
    CHECK(topRow().find("│ ≡ │") != std::string::npos);

    // The ✓ copies what is selected and leaves — no menu put up.
    reader.key(Event::Character('v'));
    reader.key(shifted(Event::Name::End));
    reader.key(press(state.width - 2));
    CHECK_FALSE(state.readSelect.on);
    CHECK(state.clipboard == "Hello world");
    CHECK(state.copiedNotice);
    CHECK_FALSE(state.menuView);
}

TEST_CASE("The sides of the text are text in the mode, not ← and → "
          "[messageread][select][mouse]") {
    Reader reader({"Hello world", "second line"});
    auto& state = reader.state;
    reader.fixture.config.readerSideTaps = amberedit::config::Visibility::On;
    reader.fixture.config.readerSideTapWidth = 6;
    REQUIRE(state.readerSideTapsShown());
    REQUIRE(state.messageCount > 1);
    const uint32_t number = state.readHeader->number;

    reader.key(Event::Character('v'));
    reader.draw();
    const int top = state.readTop();
    // A press in the left strip puts the cursor there; one in the right strip
    // lands at the end of the row it is on. Neither walks to another message.
    reader.key(pointerAt(state.readerPaneLeft() + 2, top, term::MouseEvent::Motion::Pressed));
    CHECK(state.readHeader->number == number);
    CHECK(state.readSelect.on);
    CHECK(state.readSelect.text.row == 0);
    CHECK(state.readSelect.text.col == 2);
    reader.key(pointerAt(state.readerPaneLeft() + 2, top, term::MouseEvent::Motion::Released));

    reader.key(pointerAt(state.readerPaneRight() - 2, top + 1,
                         term::MouseEvent::Motion::Pressed));
    CHECK(state.readHeader->number == number);
    CHECK(state.readSelect.on);
    CHECK(state.readSelect.text.row == 1);
    CHECK(state.readSelect.text.col == std::string("second line").size());

    // And out of the mode the strips are ← and → again.
    reader.key(Event::Escape);
    reader.draw();
    reader.key(pointerAt(state.readerPaneRight() - 2, top, term::MouseEvent::Motion::Pressed));
    CHECK(state.readHeader->number != number);
}

namespace {

/// Whether any line of the editor holds `text`.
bool editorHolds(const amberedit::ui::AppState& state, const std::string& text) {
    for (const auto& line : state.edit.lines) {
        if (amberedit::test::contains(line, text)) return true;
    }
    return false;
}

}  // namespace

TEST_CASE("A reply from the mode quotes what is selected and nothing else "
          "[messageread][select][compose]") {
    Reader reader({"Hello world", "second line", "third line"});
    auto& state = reader.state;

    reader.key(Event::Character('v'));
    reader.key(Event::ArrowDown);
    for (int i = 0; i < 6; ++i) reader.key(shifted(Event::Name::ArrowRight));
    REQUIRE(reader_select::selectedText(state) == "second");

    reader.key(Event::Character('q'));
    REQUIRE(state.navigator.current() == ScreenId::Compose);
    CHECK_FALSE(state.readSelect.on);
    CHECK(editorHolds(state, "> second"));
    CHECK_FALSE(editorHolds(state, "second line"));
    CHECK_FALSE(editorHolds(state, "Hello world"));

    // The quote goes with the reply it was taken for.
    compose::dropMessage(state);
    CHECK_FALSE(state.quoteSelection);
}

TEST_CASE("A reply from the mode with nothing selected quotes the whole message "
          "[messageread][select][compose]") {
    Reader reader({"Hello world", "second line"});
    auto& state = reader.state;

    reader.key(Event::Character('v'));
    reader.key(Event::Character('g'));
    REQUIRE(state.navigator.current() == ScreenId::Compose);
    CHECK_FALSE(state.readSelect.on);
    CHECK(editorHolds(state, "> Hello world"));
    CHECK(editorHolds(state, "> second line"));
}

TEST_CASE("n from the mode keeps the selection for the area picked, and an answer "
          "begun afterwards does not [messageread][select][compose]") {
    Reader reader({"Hello world", "second line"});
    auto& state = reader.state;

    reader.key(Event::Character('v'));
    for (int i = 0; i < 5; ++i) reader.key(shifted(Event::Name::ArrowRight));
    reader.key(Event::Character('n'));
    REQUIRE(state.areaPicker);
    CHECK_FALSE(state.readSelect.on);
    REQUIRE(state.quoteSelection);
    CHECK(*state.quoteSelection == std::vector<std::string>{"Hello"});

    // The picker given up on, a reply begun outside the mode quotes it all.
    state.areaPicker.reset();
    reader.key(Event::Character('q'));
    REQUIRE(state.navigator.current() == ScreenId::Compose);
    CHECK_FALSE(state.quoteSelection);
    CHECK(editorHolds(state, "> second line"));
}

TEST_CASE("The mode's Shift-arrows hint is pressed by no click "
          "[messageread][select][hintbar]") {
    Reader reader({"Hello world"});
    auto& state = reader.state;
    reader.key(Event::Character('v'));

    term::Screen screen(state.width, 1);
    term::render(screen, hint_bar::render(state));
    REQUIRE(state.hintSpots.size() == 5);
    CHECK_FALSE(state.hintSpots[0].key);

    const auto& gesture = state.hintSpots[0].box;
    CHECK_FALSE(hint_bar::clicked(
        state, pointerAt(gesture.x_min, gesture.y_min, term::MouseEvent::Motion::Pressed)));
    const auto& copy = state.hintSpots[1].box;
    const auto pressed = hint_bar::clicked(
        state, pointerAt(copy.x_min, copy.y_min, term::MouseEvent::Motion::Pressed));
    REQUIRE(pressed);
    CHECK(state.keys.is(*pressed, Command::ReaderCopy));
}
