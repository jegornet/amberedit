#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "support/error.hpp"
#include "ui/term/event.hpp"

/// What a terminal sends for each key AmberEdit understands, and what AmberEdit
/// makes of it.
///
/// The keyboard table in `ui/term/terminal.cpp` lives under ncurses, so the
/// question is split in two. **What the terminal sends** only the terminal
/// knows: `--keytest` is run inside it, asks for each key in turn and writes
/// down the bytes that arrived, read off the tty before ncurses sees them.
/// **What AmberEdit makes of them** is asked without the terminal: a `Checker`
/// plays such a file into AmberEdit's own `Terminal` over a pty, under the
/// `TERM` the file was taken with, and compares what `poll()` answered against
/// the key the bytes were taken for. `--keycheck` is that from the command
/// line, and `tests/ui/keytest_test.cpp` is it over `testdata/keys/`.
///
/// The list of keys is not kept by hand: it is every key `KeyMap::defaults()`
/// binds, the keys that move about, each of those under the modifiers
/// `keyNamed()` accepts for it, and Ctrl and Alt with every letter — so a command
/// given a key of its own is in the next run without anybody adding it.
///
/// A diagnostic for whoever works on the keyboard, not part of the interface:
/// the program offers the two options only where CMake is configured with
/// `-DAMBEREDIT_KEYTEST=ON`, nothing here is translated, and none of it works on
/// Windows, where the console reports key codes rather than bytes.
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

/// A key a terminal sends as bytes that are some other key's as well — Ctrl-H
/// as the 08H Backspace is sent as — and what AmberEdit reads them as, which is
/// that other key and can be nothing better. A line of `testdata/keys/limits.txt`.
struct Limit {
    std::string key;
    std::string bytes;   ///< unescaped
    std::string readAs;  ///< the events, blank separated
};

/// The limits a file states, one `key bytes read-as...` per line, a `#` opening
/// a comment.
[[nodiscard]] tl::expected<std::vector<Limit>, ErrorPtr> readLimits(
    const std::string& path);

/// A key a file says was taken one way and AmberEdit read another.
struct Mismatch {
    std::string key;
    std::string bytes;  ///< unescaped
    std::string readAs;
};

/// What playing one file back came to.
struct CheckResult {
    std::string term;
    /// Why the file was not played at all — this machine has no terminfo for
    /// its TERM — or empty where it was.
    std::string skipped;
    std::size_t keys{0};
    /// Keys read as something else that the terminal could not have sent
    /// otherwise: a key read as another one the same file took as the same
    /// bytes, an Alt-letter a macOS terminal composed into a character, or one
    /// a limit names.
    std::size_t excused{0};
    std::vector<Mismatch> wrong;
};

/// Plays files taken by `--keytest` back into AmberEdit's `Terminal`.
///
/// One `Terminal` per TERM, each in a process of its own on a pty, and the
/// answer for each run of bytes kept: the files for one terminal under four
/// protocols send mostly the same bytes, and asking once is what keeps a
/// hundred files to seconds.
class Checker {
public:
    explicit Checker(std::vector<Limit> limits = {});
    ~Checker();
    Checker(const Checker&) = delete;
    Checker& operator=(const Checker&) = delete;
    Checker(Checker&&) = delete;
    Checker& operator=(Checker&&) = delete;

    [[nodiscard]] tl::expected<CheckResult, ErrorPtr> check(const std::string& path);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// Whether `argv[1]` is one of the keytest modes.
[[nodiscard]] bool wanted(int argc, char* argv[]);

/// Runs the mode `argv[1]` names and answers with the exit code.
///
/// - `--keytest [protocols] [--out FILE] [--label TEXT] [--sync FIFO]
///   [--wait MS] [--only KEY,KEY...]` asks for each key in turn. `protocols` is
///   what `modifier_key_reporting` takes, `"xterm kitty"` where none is given.
///   `--sync` names a FIFO a driver pressing the keys reads lines off, so that
///   nothing has to be timed: `warmup` (press Space; said until something
///   arrives), `ready <key>` (press it), `done <key> <bytes>`, and `flush`
///   after a key nothing came of (put away what it opened: Escape, Space).
/// - `--keycheck [--limits FILE] FILE...` plays the files back and says which
///   keys came out as something else; non-zero where any did that no limit
///   accounts for.
[[nodiscard]] int main(int argc, char* argv[]);

}  // namespace amberedit::ui::keytest
