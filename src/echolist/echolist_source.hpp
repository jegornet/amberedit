#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "support/error.hpp"

namespace amberedit::echolist {

/// Whether the name is a zip archive's — the one thing an `echolist` line names
/// that is not an echolist itself. Read without regard to case, and asked of a
/// file that was found rather than of the line that found it: a pattern may
/// cover archives and plain lists at once.
[[nodiscard]] bool isArchiveName(const std::string& path);

/// What an `echolist` line stood for when the compiled file was written: the
/// file it named then, what that file was, and what it was read in.
///
/// This is what makes AmberEdit able to compile only when it has to. The state is
/// written into the compiled file and worked out again at every start, and the
/// two being equal is the whole of "nothing has changed" — a new month's
/// echolist is a different stamp and a different length, a line whose charset
/// has been corrected is a different state though the file is the same, and a
/// pattern that has come to stand for a newer file is a different path.
///
/// A line naming a file that is not there has an empty path and no stamp, and
/// that is a state like any other: an echolist that was missing yesterday and is
/// missing today has not changed, and one that has arrived since has.
struct SourceState {
    /// The path the `echolist` line named, exactly as the config wrote it —
    /// the pattern where it wrote one, since that is what the next start looks
    /// up again.
    std::string spec;
    /// The charset the line stated, and empty where it stated none — which
    /// means the locale's, and is deliberately stored as the nothing it was: a
    /// machine whose locale changes is a machine whose echolists read
    /// differently, and the state should say what the config said.
    std::string charset;
    /// The file it named — the archive itself where the line names one, since
    /// that is the file that is there to be looked at without unpacking
    /// anything. Empty where there is none.
    std::string path;
    /// Seconds since the epoch, and the length in bytes. Both zero where there
    /// is no file.
    uint64_t modified{0};
    uint64_t size{0};

    /// Whether the two name the same file, in the same state, read the same
    /// way. The spec is part of it: a config whose lines have been reordered or
    /// replaced is one whose compiled file no longer answers for it.
    friend bool operator==(const SourceState& a, const SourceState& b) {
        return a.spec == b.spec && a.charset == b.charset && a.path == b.path &&
               a.modified == b.modified && a.size == b.size;
    }
    friend bool operator!=(const SourceState& a, const SourceState& b) {
        return !(a == b);
    }
};

/// What the line stands for right now — a stat, or a directory listing and a
/// stat where the filename holds a wildcard, and nothing read, nothing
/// unpacked. This runs at every start, so it is deliberately the cheapest
/// question that can be asked about an echolist.
///
/// **The filename may be a glob**: `~/ftn/echolist/echo*.zip` is the newest file
/// in that directory whose name matches, by modification time and then by the
/// later name. Only the filename — a pattern over directories would be a
/// pattern over whose echolist this is.
[[nodiscard]] SourceState stateOf(const std::string& spec, const std::string& charset);

/// Reads the echolists a config names, unpacking the ones that come zipped and
/// decoding them into UTF-8.
///
/// The unpacking is what this class is for: an archive is unpacked **without
/// paths** into the temporary directory — only the entries that are echolists,
/// so that the reports, rulebooks and further archives an echolist distribution
/// carries are not written anywhere — and every file it wrote is taken away
/// again when the object goes, whether the compile finished or threw. Which
/// directory that is is `config::makeTempDir`'s to say, and a config that names
/// none is answered rather than refused.
class EcholistSources {
public:
    /// `tempDir` is where an archive is unpacked — the config's `tmpdir`, and
    /// empty where it states none, which `config::makeTempDir` answers with the
    /// system's own temporary directory when an archive is first read. It is
    /// passed on as it stands and nothing is made of it here: a config with a
    /// `tmpdir` and no zipped echolist under it leaves nothing behind.
    explicit EcholistSources(std::string tempDir);
    ~EcholistSources();

    EcholistSources(const EcholistSources&) = delete;
    EcholistSources& operator=(const EcholistSources&) = delete;

    /// One echolist file, read and decoded.
    struct Part {
        /// The file the text was actually read from: the echolist itself, or
        /// the copy unpacked into the temporary directory.
        std::string readFrom;
        /// The name whose extension says which of the two formats this is —
        /// the entry's own name where it came out of an archive.
        std::string name;
        /// UTF-8, whatever the file was written in.
        std::string text;
    };

    /// One `echolist` line, as the files it turned out to name.
    struct Loaded {
        /// The file the line named and what it was — the archive itself where
        /// it names one, which is what the next start compares against.
        SourceState state;
        /// The archive the parts were unpacked from, empty where there was none.
        std::string archive;
        /// In the order they are to be read, which settles precedence between
        /// two of them naming one echo: the file itself for a line naming one,
        /// and an archive's echolists by name, so that an archive read twice
        /// gives the same answer twice.
        std::vector<Part> parts;
    };

    /// Resolves the line, unpacks it where it names an archive, reads it and
    /// decodes it, or says what was looked for and where. `charset` is what the
    /// line stated, empty for the locale's.
    [[nodiscard]] tl::expected<Loaded, ErrorPtr> read(const std::string& spec,
                                                      const std::string& charset);

private:
    [[nodiscard]] tl::expected<Loaded, ErrorPtr> readArchive(const SourceState& state,
                                                             const std::string& charset);

    std::string tempDir_;
    std::vector<std::string> unpacked_;
};

}  // namespace amberedit::echolist
