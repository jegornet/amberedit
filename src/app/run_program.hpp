#pragma once

#include <string>
#include <vector>

#include "support/error.hpp"

/// A program run from inside AmberEdit and waited for.
///
/// Wanted by everything that hands the screen over to somebody else's program —
/// the one a link is opened with, the utilities `extern_util0` and its nine
/// fellows name — and written once rather than in each of them: the fork, the
/// exec, the interrupts put aside and the pipe the child reports a failed exec
/// down are the same either way.
///
/// **Nothing here draws or knows a terminal.** Giving the screen back before the
/// program starts and taking it again afterwards is `ui::term::Terminal`'s,
/// which is what this is called inside of.
///
/// Nothing goes through a shell: the words are the arguments of an `exec`, so
/// an argument is one argument however many spaces it holds and there is no
/// quoting to be got wrong.
namespace amberedit::app {

/// Runs `command` — the program and the arguments after it — and waits for it
/// to end.
///
/// An empty `command` runs nothing and is not a failure: it is how a setting
/// that names no program is made to do nothing at all.
///
/// The failure is the program not starting — no such file, nothing executable
/// by that name anywhere on `$PATH`, no room for another process. A program
/// that ran and exited non-zero is not one: what it did while it had the
/// terminal is between it and the user, and a message about it here would be
/// about that program rather than about AmberEdit.
[[nodiscard]] tl::expected<void, ErrorPtr> runProgram(
    const std::vector<std::string>& command);

}  // namespace amberedit::app
