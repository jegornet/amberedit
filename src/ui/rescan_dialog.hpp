#pragma once

#include "ui/app_state.hpp"
#include "ui/term/element.hpp"

namespace amberedit::ui::rescan_dialog {

/// The modal shown while the areas are being read again, naming the one being
/// read as each is reached.
///
/// It answers no key, which is what tells it apart from the other two dialogs:
/// it is up for exactly as long as the rescan takes, and the shell is inside
/// that call the whole time it is on the screen. Nothing underneath can be
/// reached meanwhile — not the quick search, not opening an area — because
/// nothing is polled until it comes down.
term::Element render(const AppState& state, term::Element background);

}  // namespace amberedit::ui::rescan_dialog
