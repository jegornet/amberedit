#pragma once

#include <string>
#include <vector>

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
/// utility the way `reader.shell` asks for a shell — by leaving the command in
/// `AppState::externUtilRequested` for `runApp()` to answer on the next pass.
/// The whole command and not the slot alone, the screen in front of the name
/// being what decides what `$msg` is handed: nothing on the area list, the
/// message on screen in the reader, the message being written in the editor.
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

/// Whether the utility that command runs is handed the message as well.
///
/// Two things have to be true of it. Its `extern_utilN` line has to write `$msg`
/// down — a utility is a program that had the terminal to itself, and only the
/// line naming it can say whether it is also to be given what is on the screen.
/// And the screen it was run from has to have a message: the area list has none,
/// so `$msg` there stands for nothing and comes out of the command line
/// altogether.
[[nodiscard]] bool handsOverMessage(const AppState& state, Command command);

/// The message it is handed, for the screen it was run from.
///
/// The editor's is the message being written, lines and all — the very text the
/// screen shows, which is what a utility run from there is run over. The
/// reader's is the text of the message on screen with the service lines left
/// out, exactly as an export leaves them out — and left out whether or not
/// `reader.kludges` has them on the screen, since what a pager or a spell
/// checker is pointed at is what somebody wrote and not what the network wrote
/// about it. The area list's is nothing at all.
///
/// **What comes back out of the file afterwards is another question**, and the
/// two screens answer it differently: the reader drops it — the message it shows
/// is one the base holds — and the editor takes it as the message. `runApp()` is
/// where that stands.
[[nodiscard]] std::vector<std::string> messageFor(const AppState& state, Command command);

}  // namespace amberedit::ui::extern_util
