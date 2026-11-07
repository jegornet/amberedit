#pragma once

#include "ui/app_state.hpp"
#include "ui/term/element.hpp"
#include "ui/term/event.hpp"

/// The dialog that asks what to look for in the area being read — what the
/// reader's `f`, and the `find` button in its menu, put up.
///
/// Two things are asked: the words, and how much of a message to read them
/// against — the header and the text, or the header alone, as a pair of radio
/// buttons, since one of the two always holds and neither of them acts. The
/// **Find** button under them is what does, and so does Enter from anywhere in
/// the box. The box asks and does not search: the searching walks the base and moves the
/// reader, which is the reader's own business, so what comes back here is
/// `Outcome::Search` and the shell asks the screen underneath.
///
/// It stays up when nothing was found, saying so in its bottom rule the way the
/// export box says a file would not open: the words are still in the field to be
/// changed, and a box that vanished would leave the user to open it again and
/// type them afresh.
namespace amberedit::ui::find_dialog {

/// What a key or a click did while the dialog was up.
enum class Outcome {
    Ignored,    ///< moved about inside the dialog, or meant nothing here
    Search,     ///< look for what the picker holds
    Dismissed,  ///< the dialog is gone and nothing was asked for
};

/// Puts it up, holding whatever was last searched for.
void open(AppState& state);

/// Draws it over whatever the screen was showing. Not const: where the field and
/// the two answers landed is written back as they are laid out, so that a click
/// is tested against what was drawn.
[[nodiscard]] term::Element render(AppState& state, term::Element background);

/// Answers a key or a click while the dialog is up. Everything else is
/// swallowed: the dialog is modal, as every other one here is.
Outcome handleEvent(AppState& state, const term::Event& event);

}  // namespace amberedit::ui::find_dialog
