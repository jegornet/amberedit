#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "support/error.hpp"

/// The two clipboards that are somebody else's program or somebody else's
/// system: `clipboard external` and `clipboard windows`. The other two are not
/// here — `internal` is a string `AppState` keeps, and `terminal` is OSC 52,
/// which only `ui::term::Terminal` can write.
///
/// **The text is UTF-8 both ways**, whatever `msg_file_charset` says. That
/// setting is about a file a program on this machine is handed to work on; a
/// clipboard is the desktop's, and every clipboard program there is — `pbcopy`,
/// `wl-copy`, `xclip` — speaks UTF-8 under a UTF-8 locale.
namespace amberedit::app {

/// Hands `text` to `command` on its standard input — `pbcopy`, `wl-copy`. The
/// program is not handed the terminal: see `sys::feedCommand()`.
[[nodiscard]] tl::expected<void, ErrorPtr> copyThroughProgram(
    const std::vector<std::string>& command, const std::string& text);

/// What `command` writes to its standard output — `pbpaste`, `wl-paste`.
[[nodiscard]] tl::expected<std::string, ErrorPtr> pasteThroughProgram(
    const std::vector<std::string>& command);

/// The same pair over the Windows clipboard, and a failure on any other system.
[[nodiscard]] tl::expected<void, ErrorPtr> copyToSystem(const std::string& text);
[[nodiscard]] tl::expected<std::string, ErrorPtr> pasteFromSystem();

/// Pasted text as lines a message can carry: broken UTF-8 made whole with
/// U+FFFD, the line endings of any system taken off, and each line through
/// `config::text::messageLine()` — tabs opened out, control bytes dropped —
/// exactly as a file read into a message is. Whatever the clipboard held came
/// from another program, and a NUL in it would end the message there.
[[nodiscard]] std::vector<std::string> pastedLines(std::string_view text);

}  // namespace amberedit::app
