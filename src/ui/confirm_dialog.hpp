#pragma once

#include "ui/term/element.hpp"
#include "ui/term/event.hpp"

#include "ui/app_state.hpp"

namespace amberedit::ui::confirm_dialog {

/// What a keystroke did to the dialog.
enum class Outcome {
    Ignored,    ///< the key means nothing here, but the dialog stays up
    Dismissed,  ///< the user said no
    Confirmed,  ///< the user said yes: the caller should quit
};

/// Draws the confirmation over whatever the screen was showing.
///
/// The state is not const: the box is centred on the screen, and where its
/// buttons ended up is recorded as they are laid out so that a click can be
/// matched against what was actually drawn rather than against a second guess
/// at the same arithmetic.
term::Element render(AppState& state, term::Element background);

/// Handles a key while the dialog is up. The dialog is modal, so every key is
/// consumed — the outcome only says what it meant.
Outcome handleEvent(AppState& state, const term::Event& event);

}  // namespace amberedit::ui::confirm_dialog
