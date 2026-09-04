#pragma once

#include <string>
#include <vector>

namespace amberedit::sys {

/// Running another program and waiting for it, with the terminal handed over.

/// Which of the two things that can go wrong did, since the caller says them
/// differently: a program that would not start names the program, and a wait
/// that failed names the waiting.
enum class CommandStage {
    Started,     ///< it ran and it ended; nothing to report
    NotStarted,  ///< it never began — no such program, or no permission
    NotWaited,   ///< it began, and waiting for it failed
};

struct CommandResult {
    CommandStage stage{CommandStage::Started};
    /// The system's own words for it, ready to be put in a message. Empty when
    /// the stage is `Started`.
    std::string reason;
};

/// Runs `command` to completion, looking `command[0]` up on the path the way a
/// user typing it at a prompt would. An empty command does nothing and succeeds.
///
/// What the program exits with is deliberately not reported: whether it liked
/// its arguments is between it and the user.
[[nodiscard]] CommandResult runCommand(const std::vector<std::string>& command);

/// The user's own interactive shell.
///
/// `$SHELL` first, since that is what the user has arranged for themselves.
/// After that the system's idea of it — the password file on POSIX, `%COMSPEC%`
/// on Windows — and then the one shell each platform is certain to have.
[[nodiscard]] std::string userShellPath();

/// Whether `path` names something this user could run. Asked before starting a
/// shell, so that a shell that never started can be told apart from one that
/// ran and exited unhappily — both of which look like 127 afterwards.
[[nodiscard]] bool canExecute(const std::string& path);

}  // namespace amberedit::sys
