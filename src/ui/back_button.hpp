#pragma once

#include "ui/term/event.hpp"
#include "ui/term/element.hpp"

#include <string>

#include "ui/event_util.hpp"
#include "ui/text_layout.hpp"
#include "ui/theme.hpp"

/// The way back, drawn in the top-left corner of the reader and of the message
/// list: a box two rows tall around a left arrow, which clicking goes back the
/// same way Esc does on that screen. `ui.back_button` decides whether it is
/// there at all; the screens check that themselves, since where the two rows
/// come from differs between them.
///
/// Both screens draw it from here rather than each from its own glyphs, so the
/// hit test cannot drift away from what is on the screen.
namespace amberedit::ui::back_button {

/// The label, with the padding around it inside the box. The arrow rather than
/// a word: it is the one the reader's footer already uses, and it needs no
/// translating.
constexpr const char* kLabel = " ← ";
/// The label plus a side on either hand — five columns, the arrow being one
/// column wide whatever it takes in bytes.
constexpr int kWidth = 5;

/// What the button is drawn in: its own quiet color, or — while a click on it
/// is being shown — the theme's `animated_button_text`. The frame goes with the
/// label rather than staying behind: a five-column arrow is a small thing to
/// notice in a tenth of a second, and the whole box changing is what makes it
/// plain that the button was hit.
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

/// The bottom row, which is what the second row of the screen costs: on the
/// reader it lands on the rule under the title, on the list on the column
/// headings. Whatever it lands on carries on a column further along, so the
/// button reads as a thing standing beside it rather than a piece of it.
[[nodiscard]] inline term::Element bottomRow(bool pressed = false) {
    return term::text("└" + horizontalRule(kWidth - 2) + "┘") |
           term::color(colorOf(pressed));
}

/// Whether the event is a click on the button: anywhere in the two rows it
/// occupies. Only the press acts — were the release to act as well, it would
/// arrive at the screen the first click went back to and act on that.
[[nodiscard]] inline bool clicked(const term::Event& event) {
    const auto click = leftClick(event);
    return click && click->x >= 0 && click->x < kWidth && click->y >= 0 &&
           click->y <= 1;
}

}  // namespace amberedit::ui::back_button
