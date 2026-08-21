#pragma once

#include <cstddef>
#include <iosfwd>
#include <string>
#include <vector>

#include "nodelist/nodelist_source.hpp"

namespace amberedit::nodelist {

/// What to compile, where to put it, and where to unpack an archive on the way
/// — the three config lines `nodelist`, `nodelist_db` and `tmpdir`.
struct CompileOptions {
    std::vector<std::string> sources;
    std::string dbPath;
    std::string tempDir;
    /// How many of a nodelist's own bad lines are worth naming before the count
    /// says the rest. A segment somebody mangled is worth seeing; a file that is
    /// not a nodelist at all would otherwise fill the screen.
    size_t warningsShown{10};
};

/// One nodelist that went into the compiled file.
struct CompiledSource {
    /// Which file the `nodelist` line named and what it was.
    SourceState state;
    /// The archive it was unpacked from, empty where it was not in one.
    std::string archive;
    size_t nodes{0};
    size_t points{0};
    size_t warnings{0};
    /// Whether it carried `Boss` lines — whether it was a pointlist.
    bool pointList{false};
    /// What stopped it from being read, in the words the user is to see, or
    /// empty. A source that could not be read is still one of the sources: what
    /// it was is written down, so that the next start knows to try again only
    /// when the file itself has changed.
    std::string problem;
};

struct CompileReport {
    std::vector<CompiledSource> sources;
    size_t nodes{0};
    size_t points{0};
    /// Entries left out for standing at an address an earlier nodelist, or an
    /// earlier line, already holds.
    size_t duplicates{0};
    size_t warnings{0};
    size_t bytes{0};
    /// Whether a compiled file was actually written. False from `refreshNodelist`
    /// where nothing had changed, and false where nothing could be written.
    bool written{false};
    /// Everything that went wrong, in the words the user is to read. A nodelist
    /// that is not there is one of these and is never anything more: AmberEdit is
    /// a mail reader whose nodelist is a convenience, and a missing file must
    /// not stand between the user and their mail.
    std::vector<std::string> problems;
};

/// Reads every nodelist the options name and writes the compiled file.
///
/// **Nothing here fails as a whole.** A nodelist that is missing, unreadable or
/// not a nodelist at all becomes a line in `problems` and an empty source in the
/// compiled file; a compiled file that cannot be written becomes a line in
/// `problems` and `written` staying false. That is why the answer is a report
/// and not a Result: several sources are read and each has an outcome of its
/// own, and the ones that worked are the point. This runs at every start, and a
/// mail reader that would not open because a nodelist had gone would be worse
/// than one without a nodelist.
///
/// `log` takes a line per nodelist as it goes and may be null.
CompileReport compileNodelists(const CompileOptions& options, std::ostream* log);

/// Whether the compiled file is out of step with the nodelists on disk.
///
/// True when there is no compiled file, when it cannot be read or was written
/// by another version of the format, when the config's `nodelist` lines are no
/// longer the ones it was made from, or when any of them now names a different
/// file — or the same file with a different stamp or length. All of that is a
/// listing and a stat per line: nothing is read and no archive is unpacked.
[[nodiscard]] bool nodelistNeedsCompiling(const CompileOptions& options);

/// Compiles the nodelists when they need it, or whatever their state when
/// `force` — what `--compile` asks for.
///
/// Answers with what was done: `written` false and nothing else filled in means
/// the compiled file was already the answer for the nodelists that are there.
/// Does not throw, for the reason `compileNodelists` does not.
CompileReport refreshNodelist(const CompileOptions& options, bool force,
                              std::ostream* log);

}  // namespace amberedit::nodelist
