#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "config/commands.hpp"
#include "support/result.hpp"
#include "ui/term/event.hpp"

namespace amberedit::ui {

/// What a key runs is `config::Command`, and so is what a menu button offers and
/// what a hint names: one list of commands for the whole interface, and
/// `config::Commands` is what is asked about any of them — a command's name, the
/// screen that answers it, the word and glyph it is drawn with, and the keys it
/// runs on when no layout has been named.
///
/// Brought in here so that a screen comparing a keystroke against a command
/// writes `Command::ReaderList` rather than the layer it happens to live in.
using config::Command;
using config::Commands;
using config::CommandScreen;
using config::kCommandCount;

/// The key a spelling stands for — `l`, `F2`, `Ctrl-N`, `Alt-Left` — or nothing
/// where it stands for none.
///
/// A bare character is that character and case tells them apart: `G` is Shift
/// held and `g` is not. `Ctrl-` and `Alt-` may stand in front of it, in either
/// case and in either order; Ctrl goes with a letter, Alt with a letter or an
/// arrow, those being the forms a terminal can be made to report.
///
/// The keys that move about are refused rather than unknown: see
/// `config::Command`.
[[nodiscard]] std::optional<term::Event> keyNamed(std::string_view spelling);

/// Whether the key is one AmberEdit keeps for moving about, and so one a `keys`
/// file may not bind. `keyNamed()` refuses these, and this is what says why.
[[nodiscard]] bool isReservedKey(std::string_view spelling);

/// The spelling `keyNamed()` would read back — what the defaults are written
/// out as, and what an error message names a key by.
[[nodiscard]] std::string spellingOf(const term::Event& key);

/// The same key as the hint bar writes it, in the case the row is written in:
/// `Q`, `Ctrl-R`, `F7` where `hint_bar_capitalize` is on, `q`, `ctrl-r`, `f7`
/// where it is off. The word beside it is cased to match, the row being one
/// thing.
///
/// It is what the row shows and not what a layout is written with: `spellingOf()`
/// is where case still tells `g` and `G` apart, those being two keys. Here they
/// are one hint either way — a row cased throughout has no room to make that
/// distinction, and it is the key's own spelling that would have to give.
[[nodiscard]] std::string hintSpellingOf(const term::Event& key, bool capitalize);

/// Which keys run which commands.
///
/// Built from the defaults `config::Commands` states, or read whole from the
/// file a `keys` line names. **The file is the layout entire**: a command it does
/// not name has no key at all, rather than keeping the one it has there. That is
/// what makes a layout a layout — the alternative is a file that can add a key
/// but never take one away — and it is why `amberkeys.cfg.example` is the whole
/// of the defaults written out, to be copied and edited rather than composed
/// from nothing.
class KeyMap {
public:
    /// The layout AmberEdit has when no `keys` file is named.
    [[nodiscard]] static KeyMap defaults();

    /// Reads a layout, or says why, naming the file and the line: a `keys` line
    /// that cannot be read is a layout the user asked for by name and did not
    /// get, and starting on the defaults instead would leave every key doing
    /// something other than what was asked.
    [[nodiscard]] static Result<KeyMap> loadFromFile(const std::string& path);

    /// The same from text already in hand. `origin` is what a failure names the
    /// text by.
    [[nodiscard]] static Result<KeyMap> parse(std::string_view text,
                                              const std::string& origin);

    /// Whether that keystroke runs that command.
    [[nodiscard]] bool is(const term::Event& event, Command command) const;

    /// The keys it runs on, in the order they were written. Empty for a command
    /// the layout leaves unbound.
    [[nodiscard]] const std::vector<term::Event>& keysOf(Command command) const;

    /// The one key to show for the command where several run it, and nothing
    /// where none does.
    ///
    /// A bare key wins over a chord and a chord over a function key — `d`
    /// rather than `Del`, `q` rather than `F4` — since what the hint bar has
    /// room for is the shortest way to say it. Ctrl before Alt for the same
    /// reason. Between two of one kind the layout's own order decides.
    [[nodiscard]] std::optional<term::Event> preferredKey(Command command) const;

    /// The letters bound with Alt, sorted and without repeats.
    ///
    /// The terminal has to be told which they are: Alt+letter arrives on an
    /// ordinary terminal as an ESC in front of the letter, which is what
    /// pressing Escape and then that letter also looks like, so only the
    /// letters a layout actually binds may claim it.
    [[nodiscard]] std::string altLetters() const;

    /// Whether anything is bound to Alt with Backspace.
    ///
    /// Asked for the same reason the letters are: that chord reaches an
    /// ordinary terminal as an ESC in front of the byte Backspace sends, which
    /// is what pressing Escape and then Backspace also looks like, so the
    /// sequence is claimed only where a layout has a use for it.
    [[nodiscard]] bool altBackspace() const;

private:
    void bind(Command command, term::Event key);

    std::vector<std::vector<term::Event>> bound_{kCommandCount};
};

}  // namespace amberedit::ui
