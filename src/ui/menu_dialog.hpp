#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "config/app_config.hpp"
#include "ui/app_state.hpp"
#include "ui/term/element.hpp"
#include "ui/term/event.hpp"

/// The context menu the button in the top-right corner opens: a column of
/// framed buttons, one command each, standing over whatever screen asked for it.
///
/// A menu costs a corner, is there only while it is being read, and holds as
/// many commands as the user cares to write down. Which those are is
/// `reader_menu` and `compose_menu` — every one of them a thing a key does as
/// well, so the menu says what a screen offers rather than offering anything
/// else.
///
/// What each command is called and which glyph marks it is `config::Commands`,
/// the one list the keyboard and the hint bars read too; a menu lays those out
/// and decides nothing about them.
///
/// The buttons are all one width, the widest label deciding, and stand a blank
/// row apart: a menu is read down, and a column of boxes of different widths
/// reads as a list of things of different weights.
namespace amberedit::ui::menu_dialog {

/// What a key or a click did while the menu was up.
enum class Outcome {
    Ignored,    ///< moved about inside the menu, or meant nothing here
    Picked,     ///< a command was chosen; `current()` names it
    Dismissed,  ///< the menu is gone and nothing was chosen
};

/// The glyph column of a menu: the width of the widest glyph among `items`, so
/// that every word in the column starts in the same place.
///
/// A glyph is one column or two, and which of them any one glyph is comes from
/// the platform's `wcwidth()` — so a column that simply put each word after its
/// own glyph would start the words in different places on some platforms and not
/// on others. Measured over the menu that is up rather than over every command
/// there is: what a user reads is the column in front of them.
[[nodiscard]] int iconWidth(const std::vector<AppState::MenuView::Item>& items);

/// A label as one line `columns` wide at the most: the glyph in a column
/// `iconColumns` wide, a blank, and the word after it.
///
/// The two are passed apart rather than as one string because only the word is
/// the language's — the glyph says the same thing in every one of them — and
/// because the glyph cannot be measured by counting: see `Commands::Info::icon`.
///
/// The word is what gives way when the room runs out, cut with the ellipsis
/// every other label in the interface is cut with. The glyph stays: it is what
/// the eye picks a button out of the column by, and it is the part that does not
/// grow when the interface is translated — the room a Russian `Ответить` wants
/// over `Reply` has to come from somewhere, and taking it from the glyph would
/// leave a column of ellipses that say nothing.
///
/// `iconColumns` is a floor, not a cut: a glyph wider than it is drawn whole and
/// takes the width it takes. `iconWidth()` is what a menu passes.
[[nodiscard]] std::string labelLine(std::string_view icon, std::string_view word,
                                    int columns, int iconColumns = 0);

/// Puts the menu up on `items`, with the cursor on the first command that can
/// actually be run: a menu opening on a button whose click it would swallow is
/// a menu that answers Enter with nothing.
void open(AppState& state, std::vector<AppState::MenuView::Item> items);

/// The command under the cursor — what the shell runs once the menu answers
/// `Picked`, on the screen the menu was opened from.
[[nodiscard]] Command current(const AppState& state);

/// Draws it over whatever the screen was showing. Not const: where each button
/// landed is written back as they are laid out, so that a click is tested
/// against what was drawn.
[[nodiscard]] term::Element render(AppState& state, term::Element background);

/// Answers a key or a click while the menu is up. It is modal, as every other
/// box is — but a click outside it closes it rather than being swallowed: that
/// is what a menu one has thought better of is dismissed with everywhere else.
Outcome handleEvent(AppState& state, const term::Event& event);

}  // namespace amberedit::ui::menu_dialog
