#include "ui/help_dialog.hpp"

#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <vector>

#include "msgbase/null_lastread_store.hpp"
#include "test_strings.hpp"
#include "ui/app_state.hpp"
#include "ui/keys.hpp"
#include "ui/term/element.hpp"
#include "ui/term/event.hpp"
#include "ui/term/screen.hpp"

using amberedit::app::AreaManager;
using amberedit::app::ScreenId;
using amberedit::config::AppConfig;
using amberedit::config::Command;
using amberedit::domain::AreaConfig;
using amberedit::ui::AppState;
using amberedit::ui::KeyMap;
using amberedit::ui::term::Event;

namespace help_dialog = amberedit::ui::help_dialog;
namespace term = amberedit::ui::term;

namespace {

class EmptyAreaSource final : public amberedit::ports::IAreaConfigSource {
public:
    tl::expected<std::vector<AreaConfig>, amberedit::ErrorPtr> loadAreas() override {
        return {};
    }
};

/// A state on whichever screen the test is about, and nothing else: the box is
/// built from the layout and the screen it was opened on, so no message base is
/// needed to open one.
struct Fixture {
    explicit Fixture(ScreenId screen)
        : manager(std::make_unique<EmptyAreaSource>(),
                  std::make_unique<amberedit::msgbase::NullLastReadStore>(), config),
          state(manager, config) {
        state.width = 100;
        state.height = 40;
        while (state.navigator.current() != screen) state.navigator.push(screen);
    }

    AppConfig config;
    AreaManager manager;
    AppState state;
};

/// The box, as the rows a terminal would show it in.
std::vector<std::string> rowsOf(AppState& state) {
    term::Screen screen(state.width, state.height);
    term::render(screen, help_dialog::render(state, term::text("")));

    std::vector<std::string> rows;
    for (int y = 0; y < state.height; ++y) {
        std::string row;
        for (int x = 0; x < state.width; ++x) row += screen.at(x, y).glyph;
        rows.push_back(row);
    }
    return rows;
}

/// The row holding `what`, or an empty string where no row does.
std::string rowWith(const std::vector<std::string>& rows, const std::string& what) {
    for (const auto& row : rows) {
        if (row.find(what) != std::string::npos) return row;
    }
    return {};
}

bool anyRowHas(const std::vector<std::string>& rows, const std::string& what) {
    return !rowWith(rows, what).empty();
}

}  // namespace

TEST_CASE("The help box is every key of the screen it was opened on [help]") {
    Fixture fixture(ScreenId::MessageRead);
    help_dialog::open(fixture.state);
    REQUIRE(fixture.state.helpView.has_value());
    const std::vector<std::string> rows = rowsOf(fixture.state);

    // Which screen's keys these are is what the box is called.
    CHECK(anyRowHas(rows, "Reader keys"));

    // Every key the layout gives a command, in the order it gave them, and the
    // sentence the one command table writes beside it.
    CHECK(rowWith(rows, "Reply in this area").find("q, F4") != std::string::npos);
    CHECK(rowWith(rows, "Look an address or a sysop up").find("Ctrl-N, F10") !=
          std::string::npos);
    CHECK(rowWith(rows, "Delete the message").find("d, Del") != std::string::npos);

    // The keys answered before every screen stand at the end, under a heading:
    // they are the same two rows on every screen, and what F1 was pressed to
    // read is the block above.
    CHECK(anyRowHas(rows, "Everywhere"));
    CHECK(rowWith(rows, "Leave AmberEdit").find("Ctrl-Q") != std::string::npos);
    CHECK(rowWith(rows, "This list of keys").find("F1") != std::string::npos);

    // And no other screen's: the editor's keys are the editor's.
    CHECK_FALSE(anyRowHas(rows, "Save the message"));
}

TEST_CASE("Each screen's box holds that screen's keys [help]") {
    Fixture areas(ScreenId::AreaList);
    help_dialog::open(areas.state);
    const std::vector<std::string> areaRows = rowsOf(areas.state);
    CHECK(anyRowHas(areaRows, "Area list keys"));
    CHECK(rowWith(areaRows, "Go to the next area with unread mail").find("/") !=
          std::string::npos);

    Fixture compose(ScreenId::Compose);
    help_dialog::open(compose.state);
    const std::vector<std::string> composeRows = rowsOf(compose.state);
    CHECK(anyRowHas(composeRows, "Editor keys"));
    CHECK(rowWith(composeRows, "Save the message").find("Ctrl-S, F2") !=
          std::string::npos);

    // The message list binds one command of its own, and the box opens there
    // like anywhere else — with that one and the two answered everywhere.
    Fixture list(ScreenId::MessageList);
    help_dialog::open(list.state);
    const std::vector<std::string> listRows = rowsOf(list.state);
    CHECK(anyRowHas(listRows, "Message list keys"));
    CHECK(rowWith(listRows, "Mark the message, or take the mark off").find("t") !=
          std::string::npos);
    CHECK(anyRowHas(listRows, "Leave AmberEdit"));
}

TEST_CASE("The box is the layout read back, not a page written about it [help]") {
    Fixture fixture(ScreenId::MessageRead);
    fixture.state.keys =
        amberedit::test::valueOf(KeyMap::parse("F8   reader.reply\n"
                                               "Alt-K  reader.kludges\n",
                                               "keys"));
    help_dialog::open(fixture.state);
    const std::vector<std::string> rows = rowsOf(fixture.state);

    // The keys the file gave, and not the ones the defaults would have.
    CHECK(rowWith(rows, "Reply in this area").find("F8") != std::string::npos);
    CHECK(rowWith(rows, "Reply in this area").find("q") == std::string::npos);
    CHECK(rowWith(rows, "Show or hide the service lines").find("Alt-K") !=
          std::string::npos);

    // A command the layout leaves unbound is left out entirely: there is
    // nothing to press, and a sentence on its own would say to press nothing.
    CHECK_FALSE(anyRowHas(rows, "Write a new message"));
    // `keys_mode clear` and a file that names neither of them, so even the
    // block that stands on every screen is gone.
    CHECK_FALSE(anyRowHas(rows, "Everywhere"));
    CHECK_FALSE(anyRowHas(rows, "Leave AmberEdit"));
}

TEST_CASE("A key that does nothing on the screen is not in the box [help]") {
    // `external_editor` takes the internal editor away, and with it every
    // command that edits the text of a message — the same keys the hint bar
    // drops, asked once for both.
    Fixture fixture(ScreenId::Compose);
    fixture.config.externalEditor = {"vi", "$msg"};
    help_dialog::open(fixture.state);
    const std::vector<std::string> rows = rowsOf(fixture.state);

    CHECK_FALSE(anyRowHas(rows, "Delete the line the cursor is on"));
    CHECK_FALSE(anyRowHas(rows, "Read a file into the message"));
    // What is still about the message rather than about the line the cursor is
    // on stays.
    CHECK(anyRowHas(rows, "Save the message"));
    CHECK(anyRowHas(rows, "Change the message attributes"));
}

TEST_CASE("A utility is described by the title its config line gave it [help]") {
    Fixture fixture(ScreenId::MessageRead);
    fixture.config.externUtils[0].title = "Files";
    fixture.config.externUtils[0].command = {"mc"};
    fixture.state.keys =
        amberedit::test::valueOf(KeyMap::parse("Alt-F1  reader.extern_util0\n", "keys"));
    help_dialog::open(fixture.state);
    const std::vector<std::string> rows = rowsOf(fixture.state);

    CHECK(rowWith(rows, "Files").find("Alt-F1") != std::string::npos);
}

TEST_CASE("The box scrolls, and the bar beside it says where [help]") {
    Fixture fixture(ScreenId::MessageRead);
    // A window with no room for the reader's two dozen commands, so the box is
    // shorter than what it holds.
    fixture.state.height = 12;
    help_dialog::open(fixture.state);

    const std::vector<std::string> top = rowsOf(fixture.state);
    CHECK(anyRowHas(top, "Reply in this area"));
    // The reader's own scrollbar, in the rightmost column of the box.
    CHECK(anyRowHas(top, "█"));
    CHECK_FALSE(anyRowHas(top, "Leave AmberEdit"));

    help_dialog::handleEvent(fixture.state, Event::End);
    const std::vector<std::string> bottom = rowsOf(fixture.state);
    CHECK(anyRowHas(bottom, "Leave AmberEdit"));
    CHECK_FALSE(anyRowHas(bottom, "Reply in this area"));

    help_dialog::handleEvent(fixture.state, Event::Home);
    CHECK(anyRowHas(rowsOf(fixture.state), "Reply in this area"));

    // A box tall enough for all of it has nothing to scroll and no bar.
    fixture.state.height = 40;
    CHECK_FALSE(anyRowHas(rowsOf(fixture.state), "█"));
}

TEST_CASE("Whichever key the hand reaches for puts the box away [help]") {
    for (const Event& key : {Event::Escape, Event::Return, Event::Backspace, Event::F1}) {
        Fixture fixture(ScreenId::MessageRead);
        help_dialog::open(fixture.state);
        REQUIRE(fixture.state.helpView.has_value());
        help_dialog::handleEvent(fixture.state, key);
        CHECK_FALSE(fixture.state.helpView.has_value());
    }

    // A click anywhere as well: there is nothing in the box to point at.
    Fixture fixture(ScreenId::MessageRead);
    help_dialog::open(fixture.state);
    term::MouseEvent mouse;
    mouse.button = term::MouseEvent::Button::Left;
    mouse.motion = term::MouseEvent::Motion::Pressed;
    mouse.x = 10;
    mouse.y = 10;
    help_dialog::handleEvent(fixture.state, Event::Mouse(mouse));
    CHECK_FALSE(fixture.state.helpView.has_value());
}
