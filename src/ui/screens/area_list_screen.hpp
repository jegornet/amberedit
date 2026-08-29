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

/// Goes on to the area the reader should read next, the area just read having
/// been left already — what `reader_edge next_unread_area` and
/// `next_unread_only` ask for when → walks off the last message of an area.
///
/// The next area with something unread in it, counting down the list from the
/// one just read and round the end of it, exactly as `/` counts. Where there is
/// none, `next_unread_area` takes the next area on the list below the one just
/// read and `next_unread_only` takes neither; either way, an answer of nowhere
/// leaves the reader standing on the area list.
void openNextArea(AppState& state, config::EdgeBehavior behavior);

/// Puts the cursor on the area the reader should read next, the area just read
/// having been left already — what `reader_edge exit_set_to_next_unread` asks
/// for when → walks off the last message of an area.
///
/// The area it names is the one `openNextArea` would have opened under
/// `next_unread_area`: the next one with something unread in it, counting round
/// the end of the list as `/` counts, and failing that the next area on the list
/// below the one just read. What it does with it is only to stand there — the
/// area is not opened, and with nowhere to go the cursor is left where it is.
void cursorToNextArea(AppState& state);

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
