#include <catch2/catch.hpp>

#include <string>
#include <vector>

#include "ui/back_button.hpp"
#include "ui/term/element.hpp"
#include "ui/term/event.hpp"
#include "ui/term/screen.hpp"
#include "ui/theme.hpp"

using namespace amberedit::ui::term;
namespace back_button = amberedit::ui::back_button;
namespace theme = amberedit::ui::theme;

namespace {

/// What a row of the frame says, what color each of its cells is written in, and
/// whether any of them is in reverse video. Rendering into a buffer and reading
/// it back is the only way to check what a click does to the button without a
/// terminal — and the buffer is what the terminal is handed.
struct Rendered {
    std::string text;
    std::vector<Color> fg;
    bool anyInverted{false};
};

Rendered rowOf(const Element& element, int width) {
    Screen screen(width, 1);
    render(screen, element);

    Rendered row;
    for (int x = 0; x < width; ++x) {
        row.text += screen.at(x, 0).glyph;
        row.fg.push_back(screen.at(x, 0).fg);
        row.anyInverted |= (screen.at(x, 0).attrs & kInverted) != 0;
    }
    return row;
}

/// A left-button press where the pointer landed.
Event pressAt(int x, int y) {
    MouseEvent mouse;
    mouse.x = x;
    mouse.y = y;
    mouse.button = MouseEvent::Button::Left;
    mouse.motion = MouseEvent::Motion::Pressed;
    return Event::Mouse(mouse);
}

}  // namespace

TEST_CASE("the back button is drawn plainly until it is clicked", "[back_button]") {
    const Rendered top = rowOf(back_button::topRow(), back_button::kWidth);
    const Rendered bottom = rowOf(back_button::bottomRow(), back_button::kWidth);

    CHECK(top.text == "│ ← │");
    CHECK(bottom.text == "└───┘");
    for (const Color& color : top.fg) CHECK(color == theme::palette.footer);
    for (const Color& color : bottom.fg) CHECK(color == theme::palette.footer);
}

TEST_CASE("a click recolors the whole back button", "[back_button]") {
    // The frame with the label, and both rows though they are laid out a screen
    // apart: five columns of arrow are a small thing to catch in a tenth of a
    // second. The glyphs are untouched — what a click changes is the color they
    // are written in, so nothing beside the button moves over for it.
    const Rendered top = rowOf(back_button::topRow(true), back_button::kWidth);
    const Rendered bottom = rowOf(back_button::bottomRow(true), back_button::kWidth);

    CHECK(top.text == "│ ← │");
    CHECK(bottom.text == "└───┘");
    for (const Color& color : top.fg) CHECK(color == theme::palette.animatedButtonText);
    for (const Color& color : bottom.fg) {
        CHECK(color == theme::palette.animatedButtonText);
    }

    // Reverse video is what this replaced; nothing uses it here any more.
    CHECK_FALSE(top.anyInverted);
    CHECK_FALSE(bottom.anyInverted);
}

TEST_CASE("the back button is clicked anywhere in its two rows", "[back_button]") {
    CHECK(back_button::clicked(pressAt(0, 0)));
    CHECK(back_button::clicked(pressAt(back_button::kWidth - 1, 1)));

    // A column past its right-hand side, and the row under it, belong to
    // whatever the button stands beside.
    CHECK_FALSE(back_button::clicked(pressAt(back_button::kWidth, 0)));
    CHECK_FALSE(back_button::clicked(pressAt(0, 2)));

    // The release is not a click: it would arrive at the screen the press went
    // back to and act there.
    MouseEvent release;
    release.button = MouseEvent::Button::Left;
    release.motion = MouseEvent::Motion::Released;
    CHECK_FALSE(back_button::clicked(Event::Mouse(release)));
}
