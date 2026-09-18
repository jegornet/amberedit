#pragma once

#include <string>
#include <vector>

#include "support/error.hpp"

/// A message handed to somebody else's program in a file, and read back out of
/// it afterwards.
///
/// What it is for: `external_editor` says that the writing is not AmberEdit's
/// to do. The message goes to a file, the user's own editor is opened on it,
/// and what comes back is what they made of it — so a lifetime's habits in vi
/// or mcedit come to writing mail unchanged.
///
/// An external utility whose `extern_utilN` line writes `$msg` down is handed
/// the same file the same way. The trip is one trip — write, run, read back —
/// and the two differ only in what is made of the answer: the reader drops it,
/// the editor takes it as the message.
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

/// `command` with `$msg` replaced by `path` wherever it stands, which is the
/// command line that will be run. Every occurrence in every argument, so
/// `--file=$msg` says what it looks like it says.
///
/// `command` is the editor's or an external utility's alike — the word is the
/// same word, and a utility that writes it down is handed the message exactly
/// as the editor is. An empty `path` is what the area list hands one: there is
/// no message on that screen, so the placeholder stands for nothing and comes
/// out of the command line altogether.
[[nodiscard]] std::vector<std::string> commandWithMessageFile(
    const std::vector<std::string>& command, const std::string& path);

/// Whether `command` says where the message goes — `$msg` written in any of its
/// arguments.
///
/// It is what decides whether an external utility is handed one at all: a
/// utility is a program that had the terminal to itself, and only the line that
/// names it can say whether it is also to be handed what is being read or
/// written. `external_editor` is not asked — a line naming no `$msg` is refused
/// as the config is read, an editor with no file being an editor opened on
/// nothing.
[[nodiscard]] bool namesMessageFile(const std::vector<std::string>& command);

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

/// The file an external utility is handed the message in — the editor's file
/// above written for another program, and a name of its own.
///
/// Not the editor's own: a utility is reached from the editor as readily as
/// from the reader, and a message being written is *in* that file for as long
/// as it is being written. A utility writing its copy over it would be writing
/// over the thing the editor is opened on.
[[nodiscard]] tl::expected<std::string, ErrorPtr> externUtilMsgPath(
    const std::string& configuredTempDir);

/// What an external utility made of the message: `runExternalEditor()` above,
/// run on the program an `extern_utilN` line names, with the file taken away
/// again at the end of it.
///
/// The file goes because nothing outlives the run to take it away later — the
/// editor's file is kept for as long as the message is being written and
/// `leaveEditor()` unlinks it, and there is no such moment here. A failure to
/// unlink is not reported: the utility has run either way, and `tmpdir` is left
/// as it was found in every case that matters.
///
/// **What comes back is the caller's to use or to drop**, and the two screens
/// differ in exactly that. The reader hands the message over to be looked at —
/// `less`, a spell checker — and what the program left in the file is nothing
/// to a message already stored in a base. The editor hands over a message being
/// written, and what comes back becomes it.
[[nodiscard]] tl::expected<ExternalEdit, ErrorPtr> runUtilOnMessage(
    const std::vector<std::string>& command, const std::string& path,
    const std::vector<std::string>& lines, const std::string& charset);

}  // namespace amberedit::app
