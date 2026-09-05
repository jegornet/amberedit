#pragma once

#include "ui/app_state.hpp"
#include "ui/term/element.hpp"
#include "ui/term/event.hpp"

/// The keys of whichever screen is up, with what each of them does beside them —
/// what F1 opens, on the area list, the message list, the reader and the editor
/// alike.
///
/// **It is the layout read back, and not a page written about it.** Every row is
/// a command the layout has given a key to, under all of the keys it gave it, so
/// a `keys` file that moved Reply to `F8` opens a box that says `F8`. A command
/// with no key is not in it: there is nothing to press, and a sentence on its own
/// would say to press nothing. What each command does is `Commands::helpOf()`,
/// out of the one table every menu and hint bar is written from — a utility
/// being described by the `title` its config line gave it, as it is everywhere
/// else.
///
/// The screen's own commands come first and the ones answered everywhere last,
/// under a heading, because the second block is the same two rows on every
/// screen and the first is what the user pressed F1 to read.
///
/// The keys that move about are not here. The arrows, PgUp and PgDn, Home and
/// End, Space, Enter, Esc, Backspace and Tab mean the same thing on every screen
/// and in every dialog, and no layout may bind them — see `config::Command`. A
/// list of them here would be a second copy of a rule stated in four screens'
/// `handleEvent()`, and it would fall out of step with them.
///
/// The box is a modal of a fixed size, as the nodelist's is, and carries the
/// reader's own scrollbar down its rightmost column where the rows do not all
/// fit. Esc closes it, and so does F1 again.
namespace amberedit::ui::help_dialog {

/// Opens it on the screen in front of the user. The rows are built here, once.
void open(AppState& state);

/// Draws it over `background`.
[[nodiscard]] term::Element render(AppState& state, term::Element background);

/// Answers a key or a click. The box closes itself — `state.helpView` is empty
/// afterwards — so there is nothing for the caller to do but stop sending it
/// events.
void handleEvent(AppState& state, const term::Event& event);

}  // namespace amberedit::ui::help_dialog
