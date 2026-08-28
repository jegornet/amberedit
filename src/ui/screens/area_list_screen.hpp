#pragma once

#include "ui/term/event.hpp"
#include "ui/term/element.hpp"

#include "ui/app_state.hpp"

/// The area list screen: an "area / total / unread" table. Both counts are the
/// ones AreaManager worked out when the list was built, and Ctrl-R is what has
/// them worked out again. `/` reads the unread count the other way about: it
/// moves the cursor to the next area that has one.
namespace amberedit::ui::screens::area_list {

/// Draws it. The state is not const because every screen's render() takes it
/// that way: the ones with something to remember about where they drew it write
/// that back, and this one has nothing of the kind.
term::Element render(AppState& state);

/// Puts the list's context menu up — what the button in the top-right corner
/// opens. What it holds is `arealist_menu`, and whether each command can be run
/// is settled here, as it opens, on the list as it stands then.
void openMenu(AppState& state);

/// Runs what was picked in that menu, the box having been put away first: the
/// rescan puts a modal of its own up, and two at once is not something the
/// shell can mean.
void runMenuCommand(AppState& state, Command command);

/// Handles a key. true means the screen consumed the event.
bool handleEvent(AppState& state, const term::Event& event);

/// Reads the tosser config and every base again, which is what brings the
/// "total" and "unread" columns up to date. What Ctrl-R asks for.
///
/// It blocks for as long as opening every base takes, so the caller is expected
/// to have `AppState::rescanning` on the screen first — the flag is the ask and
/// this is the work, and they are deliberately not the same step.
void rescan(AppState& state);

}  // namespace amberedit::ui::screens::area_list
