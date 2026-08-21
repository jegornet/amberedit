#pragma once

#include "ui/term/element.hpp"
#include "ui/term/event.hpp"

#include "ui/app_state.hpp"

namespace amberedit::ui::error_dialog {

/// Draws `AppState::errorMessage` over whatever the screen was showing.
///
/// The state is not const for the same reason the confirmation's render() is
/// not: the box is centred, and where its button landed is recorded as it is
/// laid out so that a click can be matched against what was actually drawn.
term::Element render(AppState& state, term::Element background);

/// Handles a key while the box is up. It is modal and there is only one thing
/// to say to it, so every key is consumed and Enter, Esc, Space, `o` and a
/// click on the button all acknowledge it alike.
///
/// Acknowledging clears the message and puts the user back on the area list:
/// the box comes up in place of a screen that was being opened, so there is
/// nothing else left to come back to.
void handleEvent(AppState& state, const term::Event& event);

}  // namespace amberedit::ui::error_dialog
