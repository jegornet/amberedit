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

TEST_CASE("a modal casts a shadow on what it covers [dialog]") {
    // Two columns to the right of the box and one row below it, so that a box
    // reads as laid over the screen rather than cut into it. The shadow takes
    // no room of its own: it falls on cells the screen has already drawn, which
    // is what keeps every dialog the size and in the place it had without one.
    Screen screen(9, 4);
    render(screen,
           dbox({vbox({text("........."), text("........."), text("........."),
                       text(".........")}) |
                     bgcolor(theme::palette.background) | color(theme::palette.text),
                 dialog::surface(text("xxx")) | center}));

    // Found rather than worked out a second time: where centring puts the box
    // is the centring's business, and the shadow is read off it.
    int left = -1;
    int row = -1;
    for (int y = 0; y < 4 && row < 0; ++y) {
        for (int x = 0; x < 9; ++x) {
            if (screen.at(x, y).glyph == "x") {
                left = x;
                row = y;
                break;
            }
        }
    }
    REQUIRE(left > 0);
    REQUIRE(row >= 0);
    const int right = left + 2;

    // Down the right-hand side, one row lower than the box.
    CHECK(screen.at(right + 1, row + 1).bg == theme::palette.dialogShadow);
    CHECK(screen.at(right + 2, row + 1).bg == theme::palette.dialogShadow);
    // And along the bottom, two columns further right.
    CHECK(screen.at(left + 2, row + 1).bg == theme::palette.dialogShadow);
    // The corner the shadow does not reach keeps the screen: a shadow is the
    // box moved, not the box grown.
    CHECK(screen.at(left, row + 1).bg == theme::palette.background);
    CHECK(screen.at(right + 1, row).bg == theme::palette.background);
    // What it falls on is blanked rather than tinted, so the screen underneath
    // does not read as text lit from behind.
    CHECK(screen.at(right + 1, row + 1).glyph == " ");
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
