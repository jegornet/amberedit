#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace amberedit::echolist {

/// The two shapes an echolist comes in. Which one a file is, is its extension's
/// to say — see `formatOf`.
enum class EcholistFormat {
    /// The R50/ELIST list: `[Status],Tag,Comment,Moderator's Name,Address,`,
    /// one echo per line, `;` starting a comment. `.lst`.
    Lst,
    /// The areas-to-request list a hub publishes: the tag, blanks, and the
    /// description running to the end of the line. `.na`.
    Na,
};

/// Which format a file's name says it is. `.na` is that one and everything else
/// is `.lst`: the comma-separated list is the general shape an echolist is
/// published in, and the plain two-column one has to be asked for by name.
/// Read without regard to case, as the archives spell both ways.
[[nodiscard]] EcholistFormat formatOf(std::string_view filename);

/// Whether the name is one an echolist archive is unpacked for at all — `.lst`
/// or `.na`, and nothing else. An echolist distribution carries reports,
/// rulebooks and further archives beside the lists themselves, and none of that
/// has any business being written to disk.
[[nodiscard]] bool isEcholistName(std::string_view filename);

/// One echo, as an echolist gives it.
struct EchoEntry {
    /// The tag as the file spells it. Nothing folds it here: the compiled file
    /// keeps what was written and folds only what it searches by.
    std::string tag;
    std::string description;
};

/// A line the parser could not use, and why. Nothing is ever guessed at: a line
/// that does not parse is left out and named here, so that an echolist somebody
/// mangled shows up as a warning against a line number rather than as an echo
/// quietly missing its description.
struct ParseWarning {
    int line{0};
    std::string message;
};

struct ParseResult {
    /// In the order the file wrote them, which is what settles precedence
    /// between two lines of one file naming one tag.
    std::vector<EchoEntry> entries;
    std::vector<ParseWarning> warnings;
};

/// Reads an echolist. `text` is already UTF-8 — the charset it arrived in is
/// settled while the file is read, and no encoding survives into here.
///
/// **An echo with nothing said about it is not an entry.** The description is
/// the whole of what an echolist is read for, so a line naming a tag and
/// leaving the description empty carries nothing and is passed over in silence
/// rather than warned about: a `.na` list of tags with no descriptions is a
/// perfectly ordinary file and not a mistake anybody made.
///
/// A DOS end-of-file mark (`^Z`) ends the file where there is one. It is
/// **not** expected: unlike a nodelist, an echolist is published as often
/// without one as with, and none of the real ones in `testdata/echolist` carries
/// one. It is honoured where it stands because a file that has it means the
/// bytes after it to be nothing.
[[nodiscard]] ParseResult parseEcholist(std::string_view text, EcholistFormat format);

}  // namespace amberedit::echolist
