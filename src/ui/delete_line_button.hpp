#pragma once

#include "ui/term/element.hpp"

#include <string>

#include "ui/theme.hpp"

/// The way to be rid of a line, drawn down the three rightmost columns of the
/// editor beside the line the cursor is on: a box around that row with a cross
/// in it, which clicking deletes the line the same way `Ctrl-Y` does.
/// `ui.compose_delete_line_button` decides whether it is there at all; the
/// screen checks that itself, since it is also what the text is laid out
/// against.
///
/// It is the back button's idea in another corner — the same colors, the same
/// press, the same one place for the glyphs so that the hit test cannot drift
/// away from what is on the screen — with one difference: this button moves.
/// It stands beside whichever row the cursor is on, and its shoulders are what
/// say which row that is.
///
/// **The three columns are taken from every row and not only from that one.**
/// The button walks up and down the message as the cursor does, and a message
/// laid out to the full width everywhere else would rewrap under it at every
/// keystroke — words jumping from row to row a screen away from what was being
/// typed. So the width the editor breaks its lines at is three columns short
/// wherever the button is on the screen, and what stands under the button is
/// the scrollbar, which asks for the last of those three columns and no more.
namespace amberedit::ui::delete_line_button {

/// The columns it stands in: the two sides of the box and the cross between
/// them. The cross is one column wide whatever it takes in bytes.
constexpr int kWidth = 3;

/// What the button is drawn in: its own quiet color, or — while a click on it
/// is being shown — the theme's `animated_button_text`. The box goes with the
/// cross rather than staying behind, for the reason the back button's frame
/// does: three columns are a small thing to notice in a tenth of a second, and
/// the whole of it changing is what makes it plain that the button was hit.
[[nodiscard]] inline theme::Color colorOf(bool pressed) {
    return pressed ? theme::palette.animatedButtonText : theme::palette.screenButtons;
}

/// The top of the box, over the row the cursor is on. It is left off where that
/// row is the first of the message: the box closes round a line of the message,
/// and a side reaching past its first row would point at the rule above it.
///
/// `pressed` is passed to every row, which are laid out a screen apart, so that
/// the button lights up as one thing rather than as the row the pointer
/// happened to land on.
[[nodiscard]] inline term::Element topRow(bool pressed = false) {
    return term::text("┌─┐") | term::color(colorOf(pressed));
}

/// The button itself, on the cursor's own row: the sides of the box and the
/// cross between them that is pressed.
[[nodiscard]] inline term::Element labelRow(bool pressed = false) {
    return term::text("│☓│") | term::color(colorOf(pressed));
}

/// The bottom of the box, left off under the last row of the message for the
/// reason the top is left off over the first: what is under it there is the
/// blank the message has stopped at.
[[nodiscard]] inline term::Element bottomRow(bool pressed = false) {
    return term::text("└─┘") | term::color(colorOf(pressed));
}

}  // namespace amberedit::ui::delete_line_button
