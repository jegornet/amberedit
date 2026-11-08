#pragma once

#include <cstddef>
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

/// `runProgram()` with the terminal left where it is: waited for all the same,
/// but given nothing of the screen or the keyboard — `extern_utilN -noterm`.
/// Called with the screen still up, and nothing on it has to be handed over.
[[nodiscard]] tl::expected<void, ErrorPtr> runProgramWithoutTerminal(
    const std::vector<std::string>& command);

/// What a program run for its output wrote — `extern_utilN -printresult`.
struct ProgramOutput {
    /// Its standard output and its standard error together, as the bytes it
    /// wrote them in and in the order it wrote them.
    std::string bytes;
    /// Whether it wrote more than `kOutputLimit` and only the front of it is
    /// here.
    bool cut{false};
};

/// How much of a program's output is kept for showing. A box to read, not a
/// log to keep: a megabyte is thousands of screens, and a program that writes
/// past it is one whose output nobody is going to scroll through here.
constexpr size_t kOutputLimit = size_t{1} << 20U;

/// `runProgramWithoutTerminal()`, with what the program writes to its standard
/// output and its standard error gathered rather than thrown away. What it
/// exits with is not reported, as for every other way a utility is run.
[[nodiscard]] tl::expected<ProgramOutput, ErrorPtr> runProgramForOutput(
    const std::vector<std::string>& command);

/// The output as lines to be shown: decoded out of `charset` — `msg_file_charset`,
/// the charset the config says programs on this machine are spoken to in —
/// with a byte that charset has no meaning for made U+FFFD rather than costing
/// the rest.
///
/// What a terminal would have done with it rather than shown is done here:
/// a carriage return inside a line starts it over, so a progress counter comes
/// out as its last state rather than every state run together; an escape
/// sequence — the colors a program writes even into a pipe — is taken out
/// whole; and what is left goes through `config::text::messageLine()`, tabs
/// opened out and every other control byte dropped. Blank lines at the end are
/// dropped, every program ending its output with a line feed.
[[nodiscard]] std::vector<std::string> outputLines(const std::string& bytes,
                                                   const std::string& charset);

/// Starts `command` and leaves it running — `extern_utilN -detach`. The one
/// failure is the program not starting; whatever becomes of it afterwards is
/// never known here.
[[nodiscard]] tl::expected<void, ErrorPtr> startProgram(
    const std::vector<std::string>& command);

}  // namespace amberedit::app
