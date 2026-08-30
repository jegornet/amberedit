#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "support/error.hpp"

namespace amberedit::nodelist {

/// What a `nodelist` line names.
///
/// **The extension is what says which of the three a line is**, and the name in
/// front of it is a glob in all three — `z2*.999` is every day-numbered nodelist
/// whose name starts with `z2`.
enum class SpecKind {
    /// A file, by its name. `~/ftn/nodelist/nodelist.ndl`. Where the name holds
    /// a wildcard it is a glob over the whole filename, extension and all, and
    /// the newest file it matches is the nodelist.
    Exact,
    /// The newest of the files whose extension is a day number, the `999` in
    /// `~/ftn/nodelist/Z2DAILY.999` standing for whichever that is. `.*` is the
    /// same pattern written as a wildcard.
    DayNumber,
    /// The newest of the zip archives whose extension is `Z` and two digits,
    /// the `Z99` in `~/ftn/nodelist/Z2PNT.Z99` standing for whichever that is;
    /// `.Z*` is the same pattern written as a wildcard. The nodelist is the file
    /// inside it whose name is the archive's own with a day number after it.
    ///
    /// This is the *pattern*, not the whole of what is unpacked: whether the
    /// file that was found is an archive is its own name's to say, so an
    /// `Exact` line naming `~/nodelist/MICRONET.ZIP` is unpacked as well.
    ZipArchive,
};

/// A `nodelist` line taken apart: which of the three it is, and the directory
/// and name it stands for.
struct NodelistSpec {
    SpecKind kind{SpecKind::Exact};
    /// The directory the files are looked for in — empty for a bare filename,
    /// which means the working directory as it does everywhere else. It is
    /// never a glob: a pattern over directories would be a pattern over which
    /// machine's nodelist this is, and nothing needs one.
    std::string directory;
    /// The name before the extension: `Z2DAILY` for `Z2DAILY.999`, and the
    /// whole filename for an exact one. Matched as a glob, which for a name
    /// holding no wildcard is that name and nothing else.
    std::string stem;
    /// The line as it was written, for the messages.
    std::string spec;

    [[nodiscard]] static NodelistSpec of(const std::string& spec);
};

/// A path with its extension put back into the pattern it is one of: a day
/// number (`.001` to `.366`) becomes `.999`, and a `Z` and a counter (`.z01` to
/// `.z99`) becomes `.z99` with the letter left as it was written. Anything else
/// comes back as it came, `.999` and `.Z99` included, so saying it twice says
/// the same thing.
///
/// The inverse of `NodelistSpec::of`, and here for that reason: what a `.999`
/// means is stated in one file. It is what a config wants written where a user
/// has pointed at today's nodelist — `Z2DAILY.255` is the file that is there
/// now, and `Z2DAILY.999` is that file tomorrow as well.
[[nodiscard]] std::string generalizedSpec(const std::string& path);

/// The newest file the spec covers, or nullopt where the directory holds none.
///
/// Newest is the file's own modification time, with the higher day number
/// breaking a tie. The time and not the number, because a day number is where
/// the year ends: on the second of January, `NODELIST.365` is the older file
/// and the larger number, and every nodelist that arrives is written as it
/// arrives — so the stamp says what the number cannot. The number still decides
/// between two files written in the same second, which is what unpacking a
/// batch of them at once leaves behind.
[[nodiscard]] std::optional<std::string> newestMatch(const NodelistSpec& spec);

/// What a `nodelist` line stood for when the compiled file was written: the file
/// it named then, what that file was, and what it was read in.
///
/// This is what makes AmberEdit able to compile only when it has to. The state is
/// written into the compiled file and worked out again at every start, and the
/// two being equal is the whole of "nothing has changed" — a new day's nodelist
/// is a different name, a nodelist replaced in place is a different stamp or a
/// different length, and a line whose charset has been corrected is a different
/// state though the file is the same.
///
/// A spec that matches nothing has an empty path and no stamp, and that is a
/// state like any other: a nodelist that was missing yesterday and is missing
/// today has not changed, and one that has arrived since has.
struct SourceState {
    /// The `nodelist` line, exactly as the config wrote it.
    std::string spec;
    /// The charset the line stated, and empty where it stated none — which
    /// means the locale's, and is deliberately stored as the nothing it was: a
    /// machine whose locale changes is a machine whose nodelists read
    /// differently, and the state should say what the config said.
    std::string charset;
    /// The file it named — the archive itself where the line names one, since
    /// that is the file that is there to be looked at without unpacking
    /// anything. Empty where nothing matched.
    std::string path;
    /// Seconds since the epoch, and the length in bytes. Both zero where
    /// nothing matched.
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

/// What the spec stands for right now — a directory listing and a stat, and
/// nothing read, nothing unpacked. This runs at every start, so it is
/// deliberately the cheapest question that can be asked about a nodelist.
[[nodiscard]] SourceState stateOf(const std::string& spec, const std::string& charset);

/// Reads the nodelists a config names, unpacking the ones that come zipped and
/// decoding them into UTF-8.
///
/// **Whether what was found is an archive is its own name's to say** — a `.zip`
/// or a `.Z19` — and not the line's. `Z2PNT.Z99` is the pattern for a
/// day-numbered distribution, and a `MICRONET.ZIP` that is replaced in place is
/// named outright and unpacked all the same. What stands inside is the archive's
/// own name with a day number after it either way.
///
/// The unpacking is what this class is for: an archive is unpacked **without
/// paths** into the temporary directory — only the entry that carries the
/// nodelist, so that nothing else an archive happens to hold is written
/// anywhere — and every file it wrote is taken away again when the object goes,
/// whether the compile finished or threw. Which directory that is is
/// `config::makeTempDir`'s to say, and a config that names none is answered
/// rather than refused.
class NodelistSources {
public:
    /// `tempDir` is where an archive is unpacked — the config's `tmpdir`, and
    /// empty where it states none, which `config::makeTempDir` answers with the
    /// system's own temporary directory when an archive is first read. It is
    /// passed on as it stands and nothing is made of it here: a config with a
    /// `tmpdir` and no zipped nodelist under it leaves nothing behind.
    explicit NodelistSources(std::string tempDir);
    ~NodelistSources();

    NodelistSources(const NodelistSources&) = delete;
    NodelistSources& operator=(const NodelistSources&) = delete;

    /// One nodelist, as a file that was read.
    struct Loaded {
        /// The file the spec named and what it was — the archive itself where
        /// the line names one, which is what the next start compares against.
        SourceState state;
        /// The file the text was actually read from: the nodelist itself, or
        /// the copy unpacked into the temporary directory.
        std::string readFrom;
        /// The archive it was unpacked from, empty where there was none.
        std::string archive;
        /// UTF-8, whatever the file was written in.
        std::string text;
    };

    /// Resolves the spec, unpacks it where it is an archive, and reads it, or
    /// says what was looked for and where. `charset` is what the line stated,
    /// and empty for the locale's.
    [[nodiscard]] tl::expected<Loaded, ErrorPtr> read(const std::string& spec,
                                                     const std::string& charset);

private:
    [[nodiscard]] tl::expected<Loaded, ErrorPtr> readArchive(const NodelistSpec& spec,
                                                             const SourceState& state,
                                                             const std::string& charset);

    std::string tempDir_;
    std::vector<std::string> unpacked_;
};

}  // namespace amberedit::nodelist
