#include <doctest/doctest.h>

#include <string>
#include <utility>

#include "ui/dialog_frame.hpp"
#include "ui/term/element.hpp"
#include "ui/term/screen.hpp"
#include "ui/theme.hpp"

using namespace amberedit::ui::term;

namespace dialog = amberedit::ui::dialog;
namespace theme = amberedit::ui::theme;

TEST_CASE("a modal is drawn in colors of its own, never the terminal's [dialog]") {
    // What the screens do: the palette painted across the whole window, a box
    // laid over it by `dbox`. Every cell of the box has to name a color — one
    // left as the terminal's own would be black on white in the middle of a
    // dark screen on a light profile, which is the bug this guards.
    Screen screen(5, 3);
    render(screen, dbox({text("....."), dialog::surface(text("x")) | center}) |
                       bgcolor(theme::palette.background) | color(theme::palette.text));

    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 5; ++x) {
            CHECK_FALSE(screen.at(x, y).fg.defaulted);
            CHECK_FALSE(screen.at(x, y).bg.defaulted);
        }
    }
    CHECK(screen.at(2, 1).bg == theme::palette.dialogBackground);
    CHECK(screen.at(2, 1).fg == theme::palette.dialogText);
    // The wipe is bounded by the centring outside it: the screen keeps its own
    // fill everywhere the box does not stand.
    CHECK(screen.at(0, 1).bg == theme::palette.background);
    CHECK(screen.at(4, 1).bg == theme::palette.background);
}

TEST_CASE("what a modal says for itself is drawn over its fill [dialog]") {
    // The fill is the outermost thing the box does, so a selected row, a field
    // being typed into and a button lit for a click all still land on top.
    Screen screen(3, 1);
    render(screen, dialog::surface(hbox({
                       text("a") | color(theme::palette.selectionText) |
                           bgcolor(theme::palette.selection),
                       text("bc"),
                   })));

    CHECK(screen.at(0, 0).fg == theme::palette.selectionText);
    CHECK(screen.at(0, 0).bg == theme::palette.selection);
    CHECK(screen.at(1, 0).bg == theme::palette.dialogBackground);
}

TEST_CASE("a modal wipes the boldness under it along with the color [dialog]") {
    // Not the color alone: a row left bold under the box would put a word of
    // the screen underneath into the middle of the dialog's own text.
    Screen screen(3, 1);
    render(screen,
           dbox({text("...") | bold | inverted, dialog::surface(text("x")) | center}));

    CHECK(screen.at(1, 0).attrs == 0);
    CHECK(screen.at(0, 0).attrs != 0);
}
