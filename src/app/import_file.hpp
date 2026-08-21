#pragma once

#include <string>
#include <string_view>
#include <vector>

/// A file read into the message being written — what the editor's Import
/// command ends at, once the file, the mode and the charset have been picked.
///
/// The two modes are two different things, and only one of them is text. A file
/// imported **as text** is meant to be read at the other end, so it is decoded
/// out of its own charset into UTF-8 like everything else above the adapter and
/// fenced off by the lines `import_begin` and `import_end` name. A file imported
/// **as UUE** is meant to be taken back out again, so it is encoded rather than
/// decoded and brings its own `begin`/`end` with it — a fence round that would
/// be one more line for whoever decodes it to trip over, which is why there is
/// none.
namespace amberedit::app {

enum class ImportMode {
    Text,  ///< decoded out of `charset` and fenced off by the two cut lines
    Uue,   ///< uuencoded, begin/end and nothing else
};

/// What the editor asks for: the file, how it is to go in, and — for text — the
/// charset it is written in and the lines to stand either side of it.
struct ImportRequest {
    std::string path;
    ImportMode mode{ImportMode::Text};
    /// The charset the file is written in, as the user named it. FTN spellings
    /// are understood as well as iconv's own, so `+7_FIDO` reaches iconv as
    /// CP866. Only read in text mode.
    std::string charset;
    /// `import_begin` and `import_end`. An empty one writes no line at all,
    /// which is what `import_begin ""` asks for. Only read in text mode.
    std::string beginLine;
    std::string endLine;
};

/// The lines to put into the message, or what went wrong.
///
/// A file that cannot be read, or a charset iconv does not know, comes back as
/// an error rather than as something approximate: the dialog is still up and can
/// say so, and a charset the user has just mistyped would otherwise go into the
/// message as mojibake nobody could undo.
struct ImportResult {
    std::vector<std::string> lines;
    std::string error;

    [[nodiscard]] bool ok() const { return error.empty(); }
};

[[nodiscard]] ImportResult importFile(const ImportRequest& request);

/// `bytes` in the uuencoded form FTN mail has always carried a file in: the
/// `begin 644 <name>` line, the data at 45 bytes to the line, the `` ` `` that
/// closes the data and `end`.
///
/// Zero is written as a backquote rather than as the space the original
/// encoding used. The two decode alike, and mail strips trailing spaces:
/// a line ending in one would come out of the network a byte short.
[[nodiscard]] std::vector<std::string> uuencode(std::string_view bytes,
                                                std::string_view name);

}  // namespace amberedit::app
