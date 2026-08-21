#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "support/result.hpp"
#include "ui/term/event.hpp"

namespace amberedit::ui {

/// Everything a key can be asked to do, and the whole of what a `keys` file may
/// name.
///
/// **Only what runs a command is here.** Moving about — the arrows, PgUp and
/// PgDn, Home and End, Space, Enter, Esc, Backspace and Tab — is the same on
/// every screen and in every dialog, and is not bindable: a layout that had
/// dropped Esc would be a layout with no way out of the screen it left you on.
/// The dialogs answer for themselves for the same reason.
///
/// The name against each is what the file calls it by, and the two are kept
/// together by `nameOf()` — there is no second list to fall out of step.
enum class KeyCommand : uint8_t {
    AppQuit,             ///< app.quit
    AreaListNextUnread,  ///< arealist.next-unread
    AreaListRescan,      ///< arealist.rescan
    ReaderReply,         ///< reader.reply
    ReaderReplyTo,       ///< reader.reply-to
    ReaderNew,           ///< reader.new
    ReaderForward,       ///< reader.forward
    ReaderChange,        ///< reader.change
    ReaderDelete,        ///< reader.delete
    ReaderExport,        ///< reader.export
    ReaderFind,          ///< reader.find
    ReaderList,          ///< reader.list
    ReaderInfo,          ///< reader.info
    ReaderNodelist,      ///< reader.nodelist
    ReaderKludges,       ///< reader.kludges
    ReaderScrollbar,     ///< reader.scrollbar
    ReaderThreadUp,      ///< reader.thread-up
    ReaderThreadDown,    ///< reader.thread-down
    ComposeSave,         ///< compose.save
    ComposeAttributes,   ///< compose.attributes
    ComposeImport,       ///< compose.import
    ComposeHeaderBack,   ///< compose.header-back
    ComposeDeleteLine,   ///< compose.delete-line
    ComposeDeleteQuote,  ///< compose.delete-quote
    ComposeDeleteWord,   ///< compose.delete-word
    ComposeWordLeft,     ///< compose.word-left
    ComposeWordRight,    ///< compose.word-right
    ComposeLineStart,    ///< compose.line-start
    ComposeLineEnd,      ///< compose.line-end
};

/// One past the last command, which is the width of the table a KeyMap keeps.
inline constexpr size_t kKeyCommandCount =
    static_cast<size_t>(KeyCommand::ComposeLineEnd) + 1;

/// Where a command is answered. Two commands of different screens may share a
/// key — `F2` is Change in the reader and Save in the editor, and neither screen
/// is ever the other — and two of the same screen may not.
enum class KeyScreen : uint8_t {
    /// Answered before every screen, so it shares a key with nothing.
    Anywhere,
    AreaList,
    Reader,
    Compose,
};

/// The name a `keys` file calls the command by: `reader.list`.
[[nodiscard]] const char* nameOf(KeyCommand command);

/// The command of that name, or nothing where no command is called that. Read
/// without regard to case.
[[nodiscard]] std::optional<KeyCommand> commandNamed(std::string_view name);

/// Which screen answers it.
[[nodiscard]] KeyScreen screenOf(KeyCommand command);

/// The key a spelling stands for — `l`, `F2`, `Ctrl-N`, `Alt-Left` — or nothing
/// where it stands for none.
///
/// A bare character is that character and case tells them apart: `G` is Shift
/// held and `g` is not. `Ctrl-` and `Alt-` may stand in front of it, in either
/// case and in either order; Ctrl goes with a letter, Alt with a letter or an
/// arrow, those being the forms a terminal can be made to report.
///
/// The keys that move about are refused rather than unknown: see `KeyCommand`.
[[nodiscard]] std::optional<term::Event> keyNamed(std::string_view spelling);

/// Whether the key is one AmberEdit keeps for moving about, and so one a `keys`
/// file may not bind. `keyNamed()` refuses these, and this is what says why.
[[nodiscard]] bool isReservedKey(std::string_view spelling);

/// The spelling `keyNamed()` would read back — what the defaults are written
/// out as, and what an error message names a key by.
[[nodiscard]] std::string spellingOf(const term::Event& key);

/// The same key as the hint bar writes it: a modifier in lower case, since that
/// row is a quiet one and `Ctrl-N` shouts on it. A key carrying no modifier is
/// written as `spellingOf()` has it — `F9` and `Del` are names, and `G` is not
/// `g`.
[[nodiscard]] std::string briefSpellingOf(const term::Event& key);

/// Which keys run which commands.
///
/// Built from the defaults, or read whole from the file a `keys` line names.
/// **The file is the layout entire**: a command it does not name has no key at
/// all, rather than keeping the one it has here. That is what makes a layout a
/// layout — the alternative is a file that can add a key but never take one
/// away — and it is why `amberkeys.cfg.example` is the whole of the defaults
/// written out, to be copied and edited rather than composed from nothing.
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
    [[nodiscard]] bool is(const term::Event& event, KeyCommand command) const;

    /// The keys it runs on, in the order they were written. Empty for a command
    /// the layout leaves unbound.
    [[nodiscard]] const std::vector<term::Event>& keysOf(KeyCommand command) const;

    /// The one key to show for the command where several run it, and nothing
    /// where none does.
    ///
    /// A bare key wins over a chord and a chord over a function key — `d`
    /// rather than `Del`, `q` rather than `F4` — since what the hint bar has
    /// room for is the shortest way to say it. Ctrl before Alt for the same
    /// reason. Between two of one kind the layout's own order decides.
    [[nodiscard]] std::optional<term::Event> preferredKey(KeyCommand command) const;

    /// The letters bound with Alt, sorted and without repeats.
    ///
    /// The terminal has to be told which they are: Alt+letter arrives on an
    /// ordinary terminal as an ESC in front of the letter, which is what
    /// pressing Escape and then that letter also looks like, so only the
    /// letters a layout actually binds may claim it.
    [[nodiscard]] std::string altLetters() const;

private:
    void bind(KeyCommand command, term::Event key);

    std::vector<std::vector<term::Event>> bound_{kKeyCommandCount};
};

}  // namespace amberedit::ui
