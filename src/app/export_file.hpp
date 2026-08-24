#pragma once

#include <string>
#include <vector>

#include "domain/message.hpp"
#include "support/error.hpp"

/// A message written out to a text file — what the reader's Export command ends
/// at, once the directory and the name have been picked.
///
/// A text file and nothing else: what leaves here is meant to be read, by
/// whatever shows a text file on the machine it lands on. It is the inverse of
/// the import as far as the charset goes — the message is UTF-8 above the
/// adapter, and what is written is the charset the file is to be read in.
///
/// The one thing that is not text is the file a message *carries*: a message
/// holding `begin 644 …` holds something that was never meant to be read as
/// text at all, and `uueFiles()` and `saveUueFiles()` at the foot of this header
/// are the other half of the export — the import's `uuencode()` run backwards.
namespace amberedit::app {

/// What becomes of a file already standing where the export is to write.
///
/// **Nothing here decides it**: both answers lose something a user might have
/// wanted kept — one the file that was there, the other the shape of a file that
/// was not meant to be collected into — and which of the two is wanted is a
/// question about what the user is doing rather than about the message. The
/// dialog asks, and this is how the answer reaches the writing.
enum class ExportWrite {
    Append,     ///< the message added under whatever the file already holds
    Overwrite,  ///< the file written afresh, what it held gone with it
};

/// What the reader asks for: where to write, what to write, and how.
struct ExportRequest {
    std::string path;
    /// The charset the file is written in. The caller names it; nothing here
    /// reaches for a locale.
    std::string charset;
    /// How the stamp at the head of it is written — `reader_datetime_format`,
    /// so the file says what the screen said.
    std::string dateFormat;
    /// Only ever read where the file is already there. A file that is not is
    /// written the same way either way.
    ExportWrite write{ExportWrite::Append};
};


/// The message as the file holds it: the header block the reader draws, a rule
/// under it, and then the text.
///
/// Service lines are left out, exactly as the reader leaves them out: MSGID and
/// SEEN-BY are this network's business and not the message's, and what is being
/// exported is what somebody wrote. The tearline and the origin stay — they are
/// lines of the message like any other, and the reader shows them.
[[nodiscard]] std::vector<std::string> exportedLines(const domain::MessageHeader& header,
                                                     const domain::MessageBody& body,
                                                     const std::string& dateFormat);

/// Writes it, `request.write` saying what becomes of a file already there.
///
/// The rule at the head of each message is what keeps two of them apart where
/// several are appended into one file.
[[nodiscard]] tl::expected<void, ErrorPtr> exportMessage(const ExportRequest& request,
                                         const domain::MessageHeader& header,
                                         const domain::MessageBody& body);

/// One file the message carries uuencoded: the name its `begin` line gave it,
/// and the bytes that stood between that line and `end`.
struct UueFile {
    std::string name;
    std::string bytes;
};

/// The uuencoded files the message carries, in the order they stand in it — the
/// inverse of the import's `uuencode()`, and what decides whether the export
/// asks its question at all.
///
/// A block is `begin <mode> <name>`, data lines, and `end`; the message may hold
/// several of them, and everything between two blocks is text like any other.
/// **A block with no `end` is not a file**, which is also how a message carrying
/// one section of a file split across several is passed over: half a file
/// decoded into a whole one would be a file that will not open, and the reader
/// has nothing to join the other half on with.
///
/// The name is taken as a name and never as a path — `filename()` of what the
/// line said, and a block naming `.` or `..` or nothing at all is dropped. What
/// stands in the message came from another machine, and a `../` in it would
/// write outside the directory the user picked.
[[nodiscard]] std::vector<UueFile> uueFiles(const domain::MessageBody& body);

/// The same over lines that have already been taken out of a message — what
/// `uueFiles()` is written on, and what a test can hand a block to directly.
[[nodiscard]] std::vector<UueFile> uueFilesIn(const std::vector<std::string>& lines);

/// Writes them into `directory`, each under its own name.
///
/// **Nothing is written over**, which is where this parts company with the text
/// export next door: those names are the message's rather than the user's, there
/// is nowhere in the dialog to change one, and a file appended to or replaced by
/// a decoded one cannot be got back. Every name is looked at before any of them
/// is written, so a name already taken stops the export rather than leaving the
/// directory half filled.
[[nodiscard]] tl::expected<void, ErrorPtr> saveUueFiles(const std::string& directory,
                                        const std::vector<UueFile>& files);

}  // namespace amberedit::app
