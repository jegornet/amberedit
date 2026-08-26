#pragma once

#include "ui/app_state.hpp"
#include "ui/term/event.hpp"

/// The external utilities the config names, run from whichever screen offers
/// them.
///
/// Three screens do — the area list, the reader and the editor — and each of
/// them answers the same ten commands under names of its own, so what a
/// keystroke means here is one question asked in one place rather than thirty
/// lines repeated on three screens.
///
/// Nothing runs from here: a screen has no terminal, so this asks for the
/// utility the way `reader.shell` asks for a shell — by leaving the slot in
/// `AppState::externUtilRequested` for `runApp()` to answer on the next pass.
namespace amberedit::ui::extern_util {

/// Whether the keystroke runs one of that screen's utilities, having asked for
/// it where it does.
///
/// A slot the config never set is not run and not claimed: `main()` refuses a
/// layout that binds one, so a key reaching here is a key with a program behind
/// it.
[[nodiscard]] bool handleKey(AppState& state, const term::Event& event,
                             CommandScreen screen);

/// Asks for the utility that command runs, and says whether it was one at all —
/// what a menu button picked from the reader's or the editor's menu does.
bool run(AppState& state, Command command);

}  // namespace amberedit::ui::extern_util
