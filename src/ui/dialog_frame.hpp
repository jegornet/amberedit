#pragma once

#include <string>

#include "ui/term/element.hpp"
#include "ui/theme.hpp"

/// The frame a modal box is drawn in, and the rows that stand inside it.
///
/// Every dialog in AmberEdit is the same box: a rounded frame with its title in
/// the top rule, sides down each row, and whatever it has to say about the last
/// thing tried in the bottom one. The drawing is here so that the boxes cannot
/// drift apart a corner at a time — and so that a dialog is the rows it puts in
/// the frame rather than the frame again.
namespace amberedit::ui::dialog {

/// The top of the frame, with a label in the middle of it.
[[nodiscard]] term::Element titleBar(const std::string& label, int width,
                                     theme::Color tint = theme::palette.dialogTitle);

/// The bottom of it: the keys the dialog answers to, standing in the rule the
/// way the title stands in the top one — and what went wrong **in their place**
/// when something did, which is the more urgent of the two and says the same
/// thing about where to look.
///
/// Both in the frame rather than on a row of their own. A line of keys is worth
/// having on the screen and not worth a row of the list, and a box that grew a
/// line when something was mistyped would walk itself up the window at the very
/// moment the user is reading why. Empty and empty is the plain closing rule.
[[nodiscard]] term::Element bottomBar(const std::string& hint, const std::string& error,
                                      int width);

/// The bottom of it with a label centred in the rule, the way the title stands
/// in the top one — for a box whose foot names a thing rather than listing the
/// keys it answers to.
[[nodiscard]] term::Element footerBar(const std::string& label, int width);

/// The box over whatever is behind it: the screen underneath wiped out — the
/// colors it was drawn in and the bold or the inversion with them — and the
/// dialog's own fill put down in its place, `dialog_background` with
/// `dialog_text` on it.
///
/// Every modal ends in this rather than in `clear_under` alone. A wipe on its
/// own leaves the cells in whatever the terminal draws with when nothing is
/// asked for, which is a color the theme never chose and, on a light profile,
/// black text on white in the middle of a dark screen. Painting is the
/// outermost thing a dialog does, so every color it asks for inside — a
/// selected row, a field being typed into, a button lit for a click — still
/// lands on top.
[[nodiscard]] term::Element surface(term::Element box);

/// One row of the box: the sides, and `content` between them.
///
/// `rows` is how tall `content` stands, and the sides are drawn down all of it.
/// A side is a `text()`, and a `text()` paints its top row and no other, so a
/// single `│` beside a button three rows tall would leave the frame open on the
/// two under it.
[[nodiscard]] term::Element framed(term::Element content, int rows = 1);

/// How tall a button stands: one row, or three where it is drawn in a frame.
/// What `dialog_tall_buttons` comes to, for the boxes that count their own rows.
[[nodiscard]] int buttonRows(bool tall);

/// How wide a button with `label` on it stands, frame included — for the boxes
/// that centre a button by measuring it rather than with a `filler()`.
[[nodiscard]] int buttonWidth(const std::string& label, bool tall);

/// A button: `label` with a space either side of it, and a frame round the two
/// where `tall` says so — the frame the menu's buttons and the editor's
/// delete-line button already stand in.
///
/// `selected` is whatever Enter would act on, drawn in the fill every list gives
/// its current row so that one color means one thing wherever the user is.
/// `pressed` is a click being shown before it acts, in `dialog_flash`, whether
/// or not the button was the selected one. Both land on the whole of the button,
/// frame and all: what the cursor is on has to be visible from across the box.
[[nodiscard]] term::Element button(const std::string& label, bool selected, bool pressed,
                                   bool tall);

/// A rule across the box, marking one part of it off from the next.
[[nodiscard]] term::Element divider(int width);

/// One line of the box, `width` columns wide whatever it says.
[[nodiscard]] term::Element line(const std::string& content, int width,
                                 theme::Color tint);

}  // namespace amberedit::ui::dialog
