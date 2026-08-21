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
                                     theme::Color tint = theme::palette.tableHeader);

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

/// One row of the box: the sides, and `content` between them.
[[nodiscard]] term::Element framed(term::Element content);

/// A rule across the box, marking one part of it off from the next.
[[nodiscard]] term::Element divider(int width);

/// One line of the box, `width` columns wide whatever it says.
[[nodiscard]] term::Element line(const std::string& content, int width,
                                 theme::Color tint);

}  // namespace amberedit::ui::dialog
