#pragma once

#include <string>

/// Where the errors the interface swallows are written down.
///
/// Once the screen is up there is nowhere on it to report anything: there is no
/// status line, a frame that cannot be drawn says so in place of the screen, and
/// a keystroke that throws leaves the state it found. `error_log` in the config
/// names a file to keep those in instead, so that a message which breaks the
/// base it is in can be looked into afterwards rather than only reproduced.
///
/// Off unless the config names a file, which is what most configs will say by
/// saying nothing.
namespace amberedit::ui::error_log {

/// Names the file every later `write()` appends to. An empty path — what a
/// config stating no `error_log` leaves — turns the log off.
///
/// A global for the reason `theme::palette` is one: it is written once, before
/// the screen opens, and only read afterwards, and threading it through would
/// put a parameter on every call that might throw.
void open(std::string path);

/// Appends one line: the local time, `where` it happened, and `what` was said
/// about it.
///
/// It says nothing about a log it could not write, and answers no one about it
/// either. This is where an error comes when there is nowhere left to report it,
/// so there is nowhere for a failure here to go — and a box over a broken
/// keystroke saying the log is unwritable would be worse than the silence it
/// replaced.
///
/// Newlines in either become spaces: one error is one line, which is what makes
/// the file worth grepping.
void write(const std::string& where, const std::string& what);

/// The file being written to, empty where the log is off.
[[nodiscard]] const std::string& path();

}  // namespace amberedit::ui::error_log
