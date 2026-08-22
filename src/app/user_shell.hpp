#pragma once

#include <string>

#include "support/result.hpp"

/// The user's own shell, run from inside AmberEdit and waited for.
///
/// What it is for: the reader is where a user sits while mail is being read,
/// and the things wanted mid-read — a look at what the tosser left, a file
/// copied somewhere — belong to the shell rather than to a mail editor. This is
/// the whole of what AmberEdit does about that: it hands the terminal over and
/// takes it back, and everything between the two is the shell's.
///
/// **Nothing here draws or knows a terminal.** Giving the screen back before the
/// shell starts and taking it again afterwards is `ui::term::Terminal`'s, which
/// is what this is called inside of.
namespace amberedit::app {

/// Which shell that is: `$SHELL` where the environment names one, the shell the
/// password file gives the user where it does not, and `/bin/sh` where neither
/// says anything. The same order every program asking this question uses, and
/// the last of the three is on every Unix there is.
[[nodiscard]] std::string userShellPath();

/// Runs it and waits for it to end.
///
/// The failure is the shell not starting — no such file, nothing executable
/// there, no room for another process. A shell that ran and exited non-zero is
/// not one: what the user did in it is between them and it, and a message about
/// it here would be about the last command they typed rather than about
/// AmberEdit.
[[nodiscard]] Result<void> runUserShell();

}  // namespace amberedit::app
