#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "ui/delete_line_button.hpp"
#include "ui/term/element.hpp"
#include "ui/term/screen.hpp"
#include "ui/term/utf8.hpp"
#include "ui/theme.hpp"

using namespace amberedit::ui::term;
namespace delete_line_button = amberedit::ui::delete_line_button;
namespace theme = amberedit::ui::theme;

namespace {

/// What a row of the button says and what color each of its cells is written
/// in. Rendering into a buffer and reading it back is the only way to check
/// what a click does to it without a terminal — and the buffer is what the
/// terminal is handed.
struct Rendered {
    std::string text;
    std::vector<Color> fg;
};

Rendered rowOf(const Element& element) {
    Screen screen(delete_line_button::kWidth, 1);
    render(screen, element);

    Rendered row;
    for (int x = 0; x < delete_line_button::kWidth; ++x) {
        row.text += screen.at(x, 0).glyph;
        row.fg.push_back(screen.at(x, 0).fg);
    }
    return row;
}

}  // namespace

TEST_CASE("the delete-line button is a box with a cross in it "
          "[delete_line_button]") {
    CHECK(rowOf(delete_line_button::topRow()).text == "┌─┐");
    CHECK(rowOf(delete_line_button::labelRow()).text == "│☓│");
    CHECK(rowOf(delete_line_button::bottomRow()).text == "└─┘");

    for (const Color& color : rowOf(delete_line_button::labelRow()).fg) {
        CHECK(color == theme::palette.screenButtons);
    }
}

TEST_CASE("every row of the delete-line button is exactly its width "
          "[delete_line_button]") {
    // The columns it stands in are columns the text is laid out without, so a
    // cross the terminal drew two cells wide would push the message over by one
    // on the row the cursor happens to be on and nowhere else.
    CHECK(stringWidth("┌─┐") == delete_line_button::kWidth);
    CHECK(stringWidth("│☓│") == delete_line_button::kWidth);
    CHECK(stringWidth("└─┘") == delete_line_button::kWidth);
}

TEST_CASE("a click recolors the whole delete-line button "
          "[delete_line_button]") {
    // Every row of it, though they are laid out a screen apart: three columns
    // of box are a small thing to catch in a tenth of a second. The glyphs are
    // untouched — what a click changes is the color they are written in, so
    // nothing beside the button moves over for it.
    for (const Element& row : {delete_line_button::topRow(true),
                               delete_line_button::labelRow(true),
                               delete_line_button::bottomRow(true)}) {
        const Rendered drawn = rowOf(row);
        for (const Color& color : drawn.fg) {
            CHECK(color == theme::palette.animatedButtonText);
        }
    }

    CHECK(rowOf(delete_line_button::labelRow(true)).text == "│☓│");
}
