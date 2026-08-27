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

namespace {

/// One row of a screen as it was drawn, for the assertions that are about the
/// glyphs rather than about the colors.
std::string rowOf(const Screen& screen, int y) {
    std::string row;
    for (int x = 0; x < screen.width(); ++x) row += screen.at(x, y).glyph;
    return row;
}

}  // namespace

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

TEST_CASE("a dialog button is one row, or three in a frame [dialog]") {
    // `dialog_tall_buttons` is the whole of the difference, and the width is not
    // part of it: the frame takes the two columns the wider padding takes
    // without it, so a box that centres a button by measuring puts it on the
    // same column whichever shape it is drawn in.
    CHECK(dialog::buttonRows(false) == 1);
    CHECK(dialog::buttonRows(true) == 3);
    CHECK(dialog::buttonWidth("Yes", false) == dialog::buttonWidth("Yes", true));
    CHECK(dialog::buttonWidth("Yes", true) == 7);

    // In a vbox with room to spare under it, which is where every dialog puts
    // its buttons: a vbox hands a child the rows it asked for and no more, so
    // what comes back is the button's own height rather than the screen's.
    Box where = Box::Nowhere();
    const auto draw = [&where](Screen& screen, bool tall) {
        render(screen, vbox({dialog::button("Yes", /*selected=*/false,
                                            /*pressed=*/false, tall) |
                                 reflect(where),
                             text("")}));
    };

    Screen flat(7, 4);
    draw(flat, /*tall=*/false);
    CHECK(rowOf(flat, 0) == "  Yes  ");
    // One row, and the box a click is measured against says so.
    CHECK(where.y_max - where.y_min + 1 == 1);

    Screen framed(7, 4);
    draw(framed, /*tall=*/true);
    CHECK(rowOf(framed, 0) == "┌─────┐");
    CHECK(rowOf(framed, 1) == "│ Yes │");
    CHECK(rowOf(framed, 2) == "└─────┘");
    // Three rows, and nothing had to be told so: the button is reflect()ed, so
    // the box the click is hit-tested against is the box it was drawn in.
    CHECK(where.y_max - where.y_min + 1 == 3);
}

TEST_CASE("a framed button takes the sides down all of its rows [dialog]") {
    // A text() paints its top row and no other, so a lone │ beside a button
    // three rows tall would leave the frame open on the two rows under it.
    Screen screen(5, 3);
    render(screen, dialog::framed(vbox({text("abc"), text("def"), text("ghi")}), 3));

    CHECK(rowOf(screen, 0) == "│abc│");
    CHECK(rowOf(screen, 1) == "│def│");
    CHECK(rowOf(screen, 2) == "│ghi│");

    // One row is what every other row of every box asks for, and stays the
    // default so that the twenty-odd callers that never grew a button are
    // untouched.
    Screen one(5, 1);
    render(one, dialog::framed(text("abc")));
    CHECK(rowOf(one, 0) == "│abc│");
}


