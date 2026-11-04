#pragma once

#include <string>
#include <vector>

#include "ui/term/event.hpp"

/// What a terminal sends for each key AmberEdit understands, and what AmberEdit
/// makes of it.
///
/// The keyboard table in `ui/term/terminal.cpp` lives under ncurses and no unit
/// test reaches it, so the question is split in two. **What the terminal sends**
/// only the terminal knows: `--keytest` is run inside it, asks for each key in
/// turn and writes down the bytes that arrived, read off the tty before ncurses
/// sees them. **What AmberEdit makes of them** is asked without the terminal:
/// `--keycheck` plays a file of such bytes into AmberEdit's own `Terminal` over a
/// pty, under the `TERM` the file was taken with, and compares what `poll()`
/// answered against the key the bytes were taken for.
///
/// The list of keys is not kept by hand: it is every key `KeyMap::defaults()`
/// binds, the keys that move about, each of those under the modifiers
/// `keyNamed()` accepts for it, and Ctrl and Alt with every letter — so a command
/// given a key of its own is in the next run without anybody adding it.
///
/// A diagnostic for whoever works on the keyboard, not part of the interface:
/// built only where CMake is configured with `-DAMBEREDIT_KEYTEST=ON`, nothing
/// here is translated, and none of it works on Windows, where the console
/// reports key codes rather than bytes.
namespace amberedit::ui::keytest {

/// Every key a run asks for, in the order it asks.
[[nodiscard]] std::vector<term::Event> keys();

/// The name a key is written under in a file and on the screen — `spellingOf()`
/// for everything it can spell, and `Shift-Tab`, `Space` and `Shift-Space`,
/// which it has no spelling for, being keys no layout may bind.
[[nodiscard]] std::string nameOf(const term::Event& key);

/// Bytes written so that a line holds them: `\e` for ESC, `\xNN` for every other
/// byte that is not printable ASCII, the space and the backslash included, and
/// for `-` too, which on its own is how a file writes that nothing arrived.
[[nodiscard]] std::string escapeBytes(const std::string& bytes);
[[nodiscard]] std::string unescapeBytes(const std::string& text);

/// Whether `argv[1]` is one of the three keytest modes.
[[nodiscard]] bool wanted(int argc, char* argv[]);

/// Runs the mode `argv[1]` names and answers with the exit code.
///
/// - `--keytest [protocols] [--out FILE] [--label TEXT] [--sync FIFO]
///   [--wait MS] [--only KEY,KEY...]` asks for each key in turn. `protocols` is
///   what `modifier_key_reporting` takes, `"xterm kitty"` where none is given.
///   `--sync` names a FIFO a driver pressing the keys reads `ready <key>` and
///   `done <key> <bytes>` off, so that nothing has to be timed.
/// - `--keycheck FILE...` plays the files back and says which keys came out
///   as something else; non-zero where any did.
/// - `--keytest-replay` is the far end of the pty `--keycheck` plays into: a
///   `Terminal`, and the name of each event it reads on stderr. Not for people.
[[nodiscard]] int main(int argc, char* argv[]);

}  // namespace amberedit::ui::keytest
