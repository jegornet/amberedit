#pragma once

#include "ui/app_state.hpp"
#include "ui/term/element.hpp"

namespace amberedit::ui::running_dialog {

/// The modal shown while an `extern_utilN -noterm` utility runs, naming it by
/// its title.
///
/// It answers no key, as the rescan modal answers none and for the same
/// reason: the program is waited for inside the call that started it, so the
/// loop is blocked for exactly as long as this is on the screen, and nothing is
/// polled until it comes down. Without it the screen would stand there looking
/// as though AmberEdit had stopped.
term::Element render(const AppState& state, term::Element background);

}  // namespace amberedit::ui::running_dialog
