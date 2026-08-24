#pragma once

#include <cstddef>
#include <iosfwd>
#include <string>
#include <vector>

#include "echolist/echolist_source.hpp"

namespace amberedit::echolist {

/// One `echolist` line: the file it names, and the charset it says that file is
/// written in — empty for the locale's, which is what a line that states none
/// means.
struct EcholistSpec {
    std::string path;
    std::string charset;
};

/// What to compile, where to put it, and where to unpack an archive on the way
/// — the three config lines `echolist`, `echolist_db` and `tmpdir`.
struct CompileOptions {
    std::vector<EcholistSpec> sources;
    std::string dbPath;
    std::string tempDir;
    /// How many of an echolist's own bad lines are worth naming before the count
    /// says the rest. A list somebody mangled is worth seeing; a file that is
    /// not an echolist at all would otherwise fill the screen.
    size_t warningsShown{10};
};

/// One `echolist` line that went into the compiled file.
struct CompiledSource {
    /// Which file the line named and what it was.
    SourceState state;
    /// The archive its lists were unpacked from, empty where it named no
    /// archive.
    std::string archive;
    /// How many files were read under this line — one, or the lists an archive
    /// held.
    size_t files{0};
    size_t areas{0};
    size_t warnings{0};
    /// What stopped it from being read, in the words the user is to see, or
    /// empty. A source that could not be read is still one of the sources: what
    /// it was is written down, so that the next start knows to try again only
    /// when the file itself has changed.
    std::string problem;
};

struct CompileReport {
    std::vector<CompiledSource> sources;
    size_t areas{0};
    /// Entries left out for naming a tag an earlier echolist, or an earlier
    /// line, already holds.
    size_t duplicates{0};
    size_t warnings{0};
    size_t bytes{0};
    /// Whether a compiled file was actually written. False from `refreshEcholist`
    /// where nothing had changed, and false where nothing could be written.
    bool written{false};
    /// Everything that went wrong, in the words the user is to read. An echolist
    /// that is not there is one of these and is never anything more: AmberEdit is
    /// a mail reader whose echolist is a convenience, and a missing file must
    /// not stand between the user and their mail.
    std::vector<std::string> problems;
};

/// Reads every echolist the options name and writes the compiled file.
///
/// **Nothing here fails as a whole.** An echolist that is missing, unreadable
/// or not an echolist at all becomes a line in `problems` and an empty source in
/// the compiled file; a compiled file that cannot be written becomes a line in
/// `problems` and `written` staying false. That is why the answer is a report
/// and not an expected: several sources are read and each has an outcome of its
/// own, and the ones that worked are the point. This runs at every start, and a
/// mail reader that would not open because an echolist had gone would be worse
/// than one without any descriptions.
///
/// `log` takes a line per echolist as it goes and may be null.
CompileReport compileEcholists(const CompileOptions& options, std::ostream* log);

/// Whether the compiled file is out of step with the echolists on disk.
///
/// True when there is no compiled file, when it cannot be read or was written
/// by another version of the format, when the config's `echolist` lines are no
/// longer the ones it was made from, or when any of them now names a different
/// file — or the same file with a different stamp or length. All of that is a
/// stat per line: nothing is read and no archive is unpacked.
[[nodiscard]] bool echolistNeedsCompiling(const CompileOptions& options);

/// Compiles the echolists when they need it, or whatever their state when
/// `force` — what `--compile` asks for.
///
/// Answers with what was done: `written` false and nothing else filled in means
/// the compiled file was already the answer for the echolists that are there.
/// Does not throw, for the reason `compileEcholists` does not.
CompileReport refreshEcholist(const CompileOptions& options, bool force,
                              std::ostream* log);

}  // namespace amberedit::echolist
