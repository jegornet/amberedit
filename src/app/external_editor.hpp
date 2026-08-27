#pragma once

#include <string>
#include <vector>

#include "support/error.hpp"

/// The program a message is written in when the config names one, run from
/// inside AmberEdit and waited for.
///
/// What it is for: `external_editor` says that the writing is not AmberEdit's
/// to do. The message goes to a file, the user's own editor is opened on it,
/// and what comes back is what they made of it — so a lifetime's habits in vi
/// or mcedit come to writing mail unchanged.
///
/// **Nothing here draws or knows a terminal.** Giving the screen back before
/// the editor starts and taking it again afterwards is `ui::term::Terminal`'s,
/// which is what this is called inside of. Running the command and waiting for
/// it is `app/run_program`, which the link handler and the external utilities
/// share; the file either side of it is what is here.
///
/// The file reaches the program as one argument of an `exec`, never through a
/// shell — the same rule the link handler follows, and it is what makes the
/// quoting question not arise for a path with a space in it.
namespace amberedit::app {

/// The file a message is handed over in: one of ours under the temporary
/// directory, made if it was not there — `configuredTempDir` being `tmpdir`
/// from the config, and empty where it names none.
///
/// The name carries the process id, so two AmberEdits in two terminals write a
/// message each rather than over each other. One name per process and not one
/// per message: only one message is ever being written, `leaveEditor()` takes
/// the file away when it has been stored or dropped, and a name that changed
/// under the user would leave a trail of drafts in a directory this promises to
/// leave as it was found.
[[nodiscard]] tl::expected<std::string, ErrorPtr> externalEditPath(
    const std::string& configuredTempDir);

/// `editor` with `$msg` replaced by `path` wherever it stands, which is the
/// command line that will be run. Every occurrence in every argument, so
/// `--file=$msg` says what it looks like it says.
[[nodiscard]] std::vector<std::string> externalEditorCommand(
    const std::vector<std::string>& editor, const std::string& path);

/// What the editor left behind.
struct ExternalEdit {
    /// Whether the file came back holding anything other than what was written
    /// into it. **This is the whole of the answer to "did the user want this
    /// message?"** — an editor left without writing is how every editor there
    /// is says "no", and there is nothing else for AmberEdit to read it in.
    ///
    /// It is the bytes and not the timestamp: an editor that writes the file
    /// back unchanged — which most of them do on `:wq` — has changed nothing
    /// about the message, and a message dropped because a mtime moved would be
    /// a message lost to a habit.
    bool changed{false};
    /// The text as the file now holds it: decoded out of the charset it was
    /// written in, split at the line endings whichever kind the editor left —
    /// a carriage return is not something a message carries — and held to what
    /// a message may hold, `config::text::messageLine()`. What was handed over,
    /// where nothing was changed.
    std::vector<std::string> lines;
};

/// Writes `lines` to `path` in `charset`, runs the editor on it, and reads back
/// what is there afterwards.
///
/// `charset` is the terminal's own — the editor runs in this terminal, and a
/// file it can show is a file written the way this terminal reads one.
///
/// The failures are the file not being writable or readable, the charset not
/// being one iconv knows, and the program not starting. An editor that ran and
/// exited non-zero is not one: what it did while it had the terminal is
/// between it and the user, and what it left in the file is read either way —
/// which is the same rule `app/run_program` states for a utility.
[[nodiscard]] tl::expected<ExternalEdit, ErrorPtr> runExternalEditor(
    const std::vector<std::string>& editor, const std::string& path,
    const std::vector<std::string>& lines, const std::string& charset);

}  // namespace amberedit::app
