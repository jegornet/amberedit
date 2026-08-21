#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "temp_squish_base.hpp"
#include "ui/area_fixture.hpp"
#include "ui/info_dialog.hpp"
#include "ui/menu_dialog.hpp"
#include "ui/screens/message_list_screen.hpp"
#include "ui/screens/message_read_screen.hpp"
#include "ui/term/element.hpp"
#include "ui/term/event.hpp"
#include "ui/term/screen.hpp"

using amberedit::config::MenuCommand;
using amberedit::test::AreaFixture;
using amberedit::test::TempSquishBase;
using amberedit::ui::term::Event;

namespace info_dialog = amberedit::ui::info_dialog;
namespace menu_dialog = amberedit::ui::menu_dialog;
namespace message_list = amberedit::ui::screens::message_list;
namespace message_read = amberedit::ui::screens::message_read;
namespace term = amberedit::ui::term;

namespace {

/// The box drawn over the reader, as the rows a terminal would show.
std::vector<std::string> rowsOf(AreaFixture& fixture) {
    term::Screen screen(fixture.state.width, fixture.state.height);
    term::render(screen,
                 info_dialog::render(fixture.state, message_read::render(fixture.state)));

    std::vector<std::string> rows;
    for (int y = 0; y < fixture.state.height; ++y) {
        std::string row;
        for (int x = 0; x < fixture.state.width; ++x) row += screen.at(x, y).glyph;
        rows.push_back(row);
    }
    return rows;
}

/// Whether any row holds `what`.
bool anyRowHas(const std::vector<std::string>& rows, const std::string& what) {
    for (const auto& row : rows) {
        if (row.find(what) != std::string::npos) return true;
    }
    return false;
}

/// How wide the box drawn into `rows` is, counted off its top side.
int boxWidth(const std::vector<std::string>& rows) {
    for (const auto& row : rows) {
        const size_t left = row.find("╭");
        if (left == std::string::npos) continue;
        // The row is UTF-8 and the frame is drawn in box characters, so the
        // width is counted in glyphs rather than in bytes.
        int width = 0;
        for (size_t at = left; at < row.size();) {
            const auto lead = static_cast<unsigned char>(row[at]);
            const size_t length = lead < 0x80 ? 1 : lead < 0xe0 ? 2 : lead < 0xf0 ? 3 : 4;
            ++width;
            if (row.compare(at, 3, "╮") == 0) return width;
            at += length;
        }
    }
    return 0;
}

/// The reader, opened on the newest message of the test base.
void enterArea(AreaFixture& fixture) {
    REQUIRE(message_list::enterArea(fixture.state, fixture.area).has_value());
}

}  // namespace

TEST_CASE("i opens the info box over the reader [info][messageread]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    enterArea(fixture);

    REQUIRE(message_read::handleEvent(fixture.state, Event::Character('i')));
    REQUIRE(fixture.state.infoView.has_value());
    CHECK_FALSE(fixture.state.infoView->report.blocks.empty());

    const std::vector<std::string> rows = rowsOf(fixture);
    CHECK(anyRowHas(rows, "Message info"));
    CHECK(anyRowHas(rows, "Msgbase"));
    CHECK(anyRowHas(rows, "Umsgid"));

    // A hexdump is the half of it that no other screen shows: sixteen bytes to
    // the row, the offset down the left and the printable ASCII beside them.
    // It stands at the end of the report, so this is what the End key is for.
    info_dialog::handleEvent(fixture.state, Event::End);
    const std::vector<std::string> end = rowsOf(fixture);
    CHECK(anyRowHas(end, "0000   "));
    CHECK(anyRowHas(end, "Message text, "));

    // Esc puts it away and leaves the reader where it was.
    info_dialog::handleEvent(fixture.state, Event::Escape);
    CHECK_FALSE(fixture.state.infoView.has_value());
    CHECK(fixture.state.readHeader.has_value());
}

TEST_CASE("The info box is eighty columns at most [info][messageread]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    enterArea(fixture);

    // A window with room to spare: the box stops at eighty, which is what a
    // dump of sixteen bytes to the row takes.
    fixture.state.width = 140;
    fixture.state.height = 40;
    info_dialog::open(fixture.state);
    REQUIRE(fixture.state.infoView.has_value());
    CHECK(boxWidth(rowsOf(fixture)) == 80);

    // Sixteen bytes to the row is what eighty columns are for: the second row
    // of a dump starts at 0010 there.
    info_dialog::handleEvent(fixture.state, Event::End);
    CHECK(anyRowHas(rowsOf(fixture), "0010   "));

    // A narrower one: the box is the window, and the dump gives up bytes to
    // the row rather than running off the edge — eight to the row, so the
    // second one starts at 0008.
    fixture.state.width = 60;
    // Drawn first, then the End key: how many rows the report has is what the
    // width has just changed, and the box works that out while it draws.
    rowsOf(fixture);
    info_dialog::handleEvent(fixture.state, Event::End);
    const std::vector<std::string> narrow = rowsOf(fixture);
    CHECK(boxWidth(narrow) == 60);
    CHECK(anyRowHas(narrow, "0008   "));
}

TEST_CASE("The info box scrolls through the report [info][messageread]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    enterArea(fixture);

    fixture.state.height = 12;
    info_dialog::open(fixture.state);
    REQUIRE(fixture.state.infoView.has_value());
    // Laying it out is the render's, and the rows it shows with it: the keys
    // move by what is on the screen.
    const std::vector<std::string> first = rowsOf(fixture);
    const int rows = fixture.state.infoView->rows;
    REQUIRE(rows > 0);
    REQUIRE(static_cast<int>(fixture.state.infoView->lines.size()) > rows);

    info_dialog::handleEvent(fixture.state, Event::PageDown);
    CHECK(fixture.state.infoView->scroll == rows);
    info_dialog::handleEvent(fixture.state, Event::ArrowUp);
    CHECK(fixture.state.infoView->scroll == rows - 1);
    info_dialog::handleEvent(fixture.state, Event::Home);
    CHECK(fixture.state.infoView->scroll == 0);
    CHECK(rowsOf(fixture) == first);

    // The end is the end: neither key walks past it.
    info_dialog::handleEvent(fixture.state, Event::End);
    const int last = fixture.state.infoView->scroll;
    CHECK(last > 0);
    info_dialog::handleEvent(fixture.state, Event::PageDown);
    CHECK(fixture.state.infoView->scroll == last);
    info_dialog::handleEvent(fixture.state, Event::ArrowUp);
    CHECK(fixture.state.infoView->scroll == last - 1);
}

TEST_CASE("The info button in the menu opens the same box [info][messageread][menu]") {
    TempSquishBase base;
    AreaFixture fixture(base.path());
    // The button is not in the default menu — the config is what puts it there.
    fixture.config.readerMenu = {MenuCommand::Info};
    enterArea(fixture);

    message_read::openMenu(fixture.state);
    REQUIRE(fixture.state.menuView);
    REQUIRE(fixture.state.menuView->items.size() == 1);
    REQUIRE(fixture.state.menuView->items[0].command == MenuCommand::Info);
    REQUIRE(fixture.state.menuView->items[0].enabled);

    // Drawn first, since where a button landed is what a click is tested
    // against.
    term::Screen screen(fixture.state.width, fixture.state.height);
    term::render(screen,
                 menu_dialog::render(fixture.state, message_read::render(fixture.state)));

    term::MouseEvent mouse;
    const amberedit::ui::term::Box& box = fixture.state.menuView->items[0].box;
    mouse.x = (box.x_min + box.x_max) / 2;
    mouse.y = (box.y_min + box.y_max) / 2;
    mouse.button = term::MouseEvent::Button::Left;
    mouse.motion = term::MouseEvent::Motion::Pressed;
    REQUIRE(menu_dialog::handleEvent(fixture.state, Event::Mouse(mouse)) ==
            menu_dialog::Outcome::Picked);
    // What the shell does with the answer: the box goes and the command runs on
    // the screen it was opened from.
    fixture.state.menuView.reset();
    message_read::runMenuCommand(fixture.state, MenuCommand::Info);
    CHECK(fixture.state.infoView.has_value());
}
