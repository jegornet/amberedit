#pragma once

#include "ui/app_state.hpp"
#include "ui/term/element.hpp"

namespace amberedit::ui::progress_dialog {

/// The modal counting a long run over the marked messages — what is being done
/// to them, which of them is being done now, and how many there are.
///
/// It answers no key, which is what it shares with the rescan box: whatever
/// asked for the run is blocked inside that call for as long as the box is on
/// the screen, and nothing is polled until it comes down. The one key that
/// reaches it does not come through here at all — Escape is read off the
/// terminal from inside the run, `ui/progress_run.hpp`, and breaks it off.
term::Element render(const AppState& state, term::Element background);

}  // namespace amberedit::ui::progress_dialog
