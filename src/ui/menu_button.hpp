#pragma once

#include "ui/term/element.hpp"
#include "ui/term/event.hpp"

#include <string>

#include "ui/event_util.hpp"
#include "ui/text_layout.hpp"
#include "ui/theme.hpp"

/// The way in, drawn in the top-right corner of the area list, the reader and
/// the editor: a box two rows tall around the menu glyph, which clicking opens
/// that screen's context menu. `ui.menu_button` decides whether it is there at
/// all; the screens check that themselves, since where the two rows come from
/// differs between them — the title and the rule under it in the reader and the
/// editor, the column headings and the rule under them on the area list.
///
/// It is the back button read from the other side of the window — the same box,
/// the same two rows, the same colors — and both are drawn from one place each
/// rather than from each screen's own glyphs, so the hit test cannot drift away
/// from what is on the screen.
///
/// The message list has no such corner: marking the message under the cursor is
/// its one command, and one button is no menu.
namespace amberedit::ui::menu_button {

/// The label, with the padding around it inside the box. The three bars rather
/// than a word: it is what a menu has been drawn as for as long as there have
/// been menus to draw, and it needs no translating.
constexpr const char* kLabel = " ≡ ";
/// The label plus a side on either hand — five columns, the glyph being one
/// column wide whatever it takes in bytes.
constexpr int kWidth = 5;

/// What the button is drawn in: its own quiet color, or — while a click on it is
/// being shown — the theme's `animated_button_text`. The frame goes with the
/// label rather than staying behind, for the reason the back button's does: five
/// columns are a small thing to notice in a tenth of a second, and the whole box
/// changing is what makes it plain that the button was hit.
[[nodiscard]] inline theme::Color colorOf(bool pressed) {
    return pressed ? theme::palette.animatedButtonText : theme::palette.screenButtons;
}

/// The top row: the sides of the box around the label. There is no top side —
/// the button hangs from the edge of the screen, and the edge closes it.
///
/// `pressed` is passed to both rows, which are laid out a screen apart, so that
/// the button lights up as one thing rather than as the half of it the pointer
/// happened to land on.
[[nodiscard]] inline term::Element topRow(bool pressed = false) {
    return term::text("│" + std::string(kLabel) + "│") | term::color(colorOf(pressed));
}

/// The bottom row, which is what the second row of the screen costs: it lands on
/// the rule under the title, or under the column headings where the list is what
/// is drawn. The rule stops a column short of it, so the button reads as a thing
/// standing beside it rather than a piece of it.
[[nodiscard]] inline term::Element bottomRow(bool pressed = false) {
    return term::text("└" + horizontalRule(kWidth - 2) + "┘") |
           term::color(colorOf(pressed));
}

/// Whether the event is a click on the button: anywhere in the two rows it
/// occupies, which are the last `kWidth` columns of a window `width` wide. Only
/// the press acts — the release would arrive with the menu already up and land
/// on whatever the box had put under the pointer.
[[nodiscard]] inline bool clicked(const term::Event& event, int width) {
    const auto click = leftClick(event);
    return click && click->x >= width - kWidth && click->x < width && click->y >= 0 &&
           click->y <= 1;
}

}  // namespace amberedit::ui::menu_button
