#pragma once

#include <string>
#include <vector>

#include "support/error.hpp"

/// The program a link in a message is opened with, run from inside AmberEdit
/// and waited for.
///
/// What it is for: a message carries addresses, and following one is a
/// browser's work rather than a mail editor's. The config's `urlhandler` names
/// the program; this hands the terminal over to it and takes it back, the same
/// way `app/user_shell` does for the user's own shell — a text browser wants
/// the screen, and one that opens a window elsewhere is done before the screen
/// has been missed.
///
/// **Nothing here draws or knows a terminal.** Giving the screen back before
/// the program starts and taking it again afterwards is `ui::term::Terminal`'s,
/// which is what this is called inside of.
///
/// The link reaches the program as one argument of an `exec`, never through a
/// shell: an address is written by whoever sent the message, and no quoting of
/// it can be got wrong where there is nothing to quote it for.
///
/// Filling the link in is the whole of what is here. Running the command and
/// waiting for it is `app/run_program`, which the external utilities share.
namespace amberedit::app {

/// `handler` with `$url` replaced by `url` wherever it stands, which is the
/// command line that will be run. Every occurrence in every argument, so
/// `--url=$url` says what it looks like it says.
[[nodiscard]] std::vector<std::string> urlHandlerCommand(
    const std::vector<std::string>& handler, const std::string& url);

/// Runs that command and waits for it to end.
///
/// An empty `handler` runs nothing and is not a failure: a config naming no
/// handler is how a click on a link is made to do nothing at all.
///
/// The failure is the program not starting — no such file, nothing executable
/// by that name anywhere on `$PATH`, no room for another process. A program
/// that ran and exited non-zero is not one: what it made of the address is
/// between it and the user, and a message about it here would be about a
/// browser rather than about AmberEdit.
[[nodiscard]] tl::expected<void, ErrorPtr> runUrlHandler(
    const std::vector<std::string>& handler, const std::string& url);

}  // namespace amberedit::app
