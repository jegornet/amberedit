#include "nodelist/nodelist_source.hpp"

#include <sys/stat.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

#include "archive/zip_reader.hpp"
#include "config/temp_dir.hpp"
#include "config/text_util.hpp"

namespace amberedit::nodelist {
namespace {

namespace fs = std::filesystem;

/// The highest day number a nodelist extension may carry. 366 and not 365: a
/// leap year has one, and a nodelist published on the last day of it would
/// otherwise be the one file the pattern refused.
constexpr int kMaxDay = 366;

/// The day number an extension states — `225` for `.225` — or nullopt where the
/// extension is not one.
std::optional<int> dayNumber(std::string_view extension) {
    if (extension.size() != 3) return std::nullopt;
    int value = 0;
    for (char c : extension) {
        if (c < '0' || c > '9') return std::nullopt;
        value = (value * 10) + (c - '0');
    }
    if (value < 1 || value > kMaxDay) return std::nullopt;
    return value;
}

/// The counter a zipped nodelist's extension states — `19` for `.Z19` — or
/// nullopt where it is not one. The letter is `Z` or `z`, as the archives come
/// spelled both ways.
std::optional<int> archiveNumber(std::string_view extension) {
    if (extension.size() != 3) return std::nullopt;
    if (config::text::asciiLower(extension[0]) != 'z') return std::nullopt;
    int value = 0;
    for (size_t i = 1; i < 3; ++i) {
        if (extension[i] < '0' || extension[i] > '9') return std::nullopt;
        value = (value * 10) + (extension[i] - '0');
    }
    return value;
}

/// The part after the last dot, empty where the name has none.
std::string_view extensionOf(std::string_view name) {
    const size_t dot = name.find_last_of('.');
    return dot == std::string_view::npos ? std::string_view{} : name.substr(dot + 1);
}

std::string_view stemOf(std::string_view name) {
    const size_t dot = name.find_last_of('.');
    return dot == std::string_view::npos ? name : name.substr(0, dot);
}

/// What the file system says about a file. Nullopt where there is no file, or
/// where it is not one — a directory answering to the name is not a nodelist.
///
/// `stat` rather than `std::filesystem`: the stamp is written into the compiled
/// file and compared against it at the next start, and `file_time_type` is a
/// clock of the implementation's own choosing in C++17, with no portable way to
/// put a number on it. Seconds since the epoch is a number every system agrees
/// about, and the rest of AmberEdit is POSIX already.
struct FileState {
    uint64_t modified{0};
    uint64_t size{0};
};

std::optional<FileState> fileState(const std::string& path) {
    struct ::stat info{};
    if (::stat(path.c_str(), &info) != 0) return std::nullopt;
    if (!S_ISREG(info.st_mode)) return std::nullopt;
    return FileState{static_cast<uint64_t>(info.st_mtime),
                     static_cast<uint64_t>(info.st_size)};
}

/// One file the pattern covers, with what decides which of them is newest.
struct Candidate {
    std::string path;
    /// The filename, ASCII case folded — the last word in which of two files is
    /// newer, and only ever reached for two the rest cannot tell apart.
    std::string name;
    uint64_t modified{0};
    int number{0};
};

bool newerThan(const Candidate& a, const Candidate& b) {
    if (a.modified != b.modified) return a.modified > b.modified;
    if (a.number != b.number) return a.number > b.number;
    // Two files of one stamp and one number can only be two the pattern's stem
    // matched loosely, and the answer must not depend on the order a directory
    // listing happened to hand them over in. The later name wins, since a name
    // carrying a date carries it in the order it sorts in.
    return a.name > b.name;
}

tl::expected<std::string, ErrorPtr> readWholeFile(const std::string& path) {
    auto isFile = config::text::insistItIsAFile(path);
    if (!isFile) return tl::make_unexpected(std::move(isFile).error());

    std::ifstream in(path, std::ios::binary);
    if (!in) return failure("cannot read the nodelist: " + path);
    std::string text(std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>{});
    if (in.bad()) return failure("cannot read the nodelist: " + path);
    return text;
}

}  // namespace

NodelistSpec NodelistSpec::of(const std::string& spec) {
    NodelistSpec parsed;
    parsed.spec = spec;

    const fs::path path(spec);
    parsed.directory = path.parent_path().string();
    const std::string name = path.filename().string();
    const std::string_view extension = extensionOf(name);

    // `999` is not a day of any year and `Z99` is not a counter any archive
    // carries, so neither can be the name of a real file the user meant
    // literally — which is what lets one word stand for both the pattern and
    // the thing it matches. `*` and `Z*` are the same two patterns written as
    // wildcards, for somebody who would rather say "whatever is there" than
    // remember which sentinel means which.
    if (extension == "999" || extension == "*") {
        parsed.kind = SpecKind::DayNumber;
        parsed.stem = std::string(stemOf(name));
    } else if (config::text::iequals(extension, "Z99") ||
               config::text::iequals(extension, "Z*")) {
        parsed.kind = SpecKind::ZipArchive;
        parsed.stem = std::string(stemOf(name));
    } else {
        parsed.kind = SpecKind::Exact;
        parsed.stem = name;
    }
    return parsed;
}

std::string generalizedSpec(const std::string& path) {
    const fs::path parsed(path);
    const std::string name = parsed.filename().string();
    const std::string_view extension = extensionOf(name);

    std::string wanted;
    if (dayNumber(extension)) {
        wanted = "999";
    } else if (const auto counter = archiveNumber(extension); counter && *counter >= 1) {
        // The letter as the archive spells it: a config naming `z2pnt.Z99` for a
        // directory of `z2pnt.z01` files reads no worse, but a path that comes
        // back in a case the user did not write it in reads like a mistake.
        wanted = std::string(1, extension[0]) + "99";
    } else {
        return path;
    }

    fs::path generalized = parsed;
    generalized.replace_filename(std::string(stemOf(name)) + "." + wanted);
    return generalized.string();
}

std::optional<std::string> newestMatch(const NodelistSpec& spec) {
    // A name with nothing to match in it is looked up as it stands: it is the
    // ordinary case, and going through a directory listing for it would answer
    // the same thing more slowly and stop naming the file that is missing.
    if (spec.kind == SpecKind::Exact && !config::text::hasWildcard(spec.stem)) {
        return fileState(spec.spec) ? std::optional<std::string>(spec.spec)
                                    : std::nullopt;
    }

    const fs::path directory =
        spec.directory.empty() ? fs::path(".") : fs::path(spec.directory);
    std::error_code ec;
    fs::directory_iterator entries(directory, ec);
    if (ec) return std::nullopt;

    std::optional<Candidate> best;
    for (const auto& item : entries) {
        const std::string name = item.path().filename().string();

        // The stem is a glob, which for a stem holding no wildcard is the stem
        // itself and nothing else — `globMatches` is anchored at both ends and
        // folds ASCII case, which is exactly what comparing them did before.
        // An `Exact` spec that got here is a glob over the whole filename, the
        // extension included, since nothing about it named a kind.
        const std::string_view against =
            spec.kind == SpecKind::Exact ? std::string_view(name) : stemOf(name);
        if (!config::text::globMatches(spec.stem, against)) continue;

        int number = 0;
        if (spec.kind != SpecKind::Exact) {
            const std::string_view extension = extensionOf(name);
            const auto found = spec.kind == SpecKind::DayNumber
                                   ? dayNumber(extension)
                                   : archiveNumber(extension);
            if (!found) continue;
            number = *found;
        }

        const std::string path = item.path().string();
        const auto state = fileState(path);
        if (!state) continue;

        Candidate candidate;
        candidate.path = path;
        candidate.name = config::text::toLower(name);
        candidate.number = number;
        candidate.modified = state->modified;
        if (!best || newerThan(candidate, *best)) best = std::move(candidate);
    }

    if (!best) return std::nullopt;
    return best->path;
}

SourceState stateOf(const std::string& spec) {
    SourceState state;
    state.spec = spec;
    const auto found = newestMatch(NodelistSpec::of(spec));
    if (!found) return state;
    // Between the listing above and the stat below the file could in principle
    // go; the state is then the one a missing file has, which is the truth as
    // of now and is compared against the next start's truth like any other.
    const auto file = fileState(*found);
    if (!file) return state;
    state.path = *found;
    state.modified = file->modified;
    state.size = file->size;
    return state;
}

NodelistSources::NodelistSources(std::string tempDir) : tempDir_(std::move(tempDir)) {}

NodelistSources::~NodelistSources() {
    // What was unpacked is taken away again, whether the compile finished or
    // threw its way out. The directory itself is left alone either way: a
    // config named it for this and for whatever else it is used for and it was
    // very likely there before we were, and one `makeTempDir` fell back on is a
    // name worth keeping hold of — a directory that stays ours is one nobody
    // else can put anything in the way of.
    std::error_code ec;
    for (const auto& path : unpacked_) fs::remove(path, ec);
}

tl::expected<NodelistSources::Loaded, ErrorPtr> NodelistSources::read(
    const std::string& spec) {
    const NodelistSpec pattern = NodelistSpec::of(spec);
    const auto found = newestMatch(pattern);
    if (!found) {
        if (pattern.kind == SpecKind::Exact) {
            if (!config::text::hasWildcard(pattern.stem)) {
                return failure("nodelist not found: " + spec);
            }
            return failure("no nodelist matching " + spec +
                           " — nothing in that directory is called '" + pattern.stem +
                           "'");
        }
        const char* what = pattern.kind == SpecKind::DayNumber
                               ? "a day number from 001 to 366"
                               : "Z and two digits";
        return failure("no nodelist matching " + spec +
                       " — nothing in that directory is called '" + pattern.stem +
                       "' with " + what + " after it");
    }

    // Taken here rather than by asking `stateOf` again: this is the file that is
    // about to be read, and a second listing could land on a different one.
    SourceState state;
    state.spec = spec;
    state.path = *found;
    if (const auto file = fileState(*found)) {
        state.modified = file->modified;
        state.size = file->size;
    }

    if (pattern.kind == SpecKind::ZipArchive) return readArchive(pattern, state);
    auto text = readWholeFile(*found);
    if (!text) return tl::make_unexpected(std::move(text).error());
    return Loaded{state, *found, {}, std::move(*text)};
}

tl::expected<NodelistSources::Loaded, ErrorPtr> NodelistSources::readArchive(
    const NodelistSpec& spec, const SourceState& state) {
    const std::string& archivePath = state.path;

    // Made here and not when the sources were: a config with `tmpdir` in it and
    // no zipped nodelist under it should leave nothing behind, and most do. What
    // is wrong with the directory is `makeTempDir`'s to say and what it was
    // wanted for is ours, which is why the two are said together.
    const auto workDirMade = config::makeTempDir(tempDir_);
    if (!workDirMade) {
        return failure(spec.spec + " names a zipped nodelist, and " +
                       workDirMade.error()->message());
    }
    const std::string& workDir = *workDirMade;

    auto opened = archive::ZipArchive::open(archivePath);
    if (!opened) return tl::make_unexpected(std::move(opened).error());
    const archive::ZipArchive& zip = *opened;

    // The nodelist inside is the archive's own name with a day number after it,
    // and the newest of them where an archive holds several. Only that entry is
    // unpacked: an archive may carry a readme or a signature beside the
    // nodelist, and none of that has any business being written to disk.
    //
    // The archive that was found is what names it, not the line that found it:
    // `Z2PNT.Z99` and `z2*.z*` both land on `Z2PNT.Z19`, and what stands inside
    // it is `Z2PNT` with a day number either way.
    const std::string innerStem =
        std::string(stemOf(fs::path(archivePath).filename().string()));
    const archive::ZipEntry* best = nullptr;
    int bestNumber = 0;
    for (const auto& entry : zip.entries()) {
        const std::string name = entry.baseName();
        if (!config::text::iequals(stemOf(name), innerStem)) continue;
        const auto number = dayNumber(extensionOf(name));
        if (!number) continue;
        if (best == nullptr || entry.modified > best->modified ||
            (entry.modified == best->modified && *number > bestNumber)) {
            best = &entry;
            bestNumber = *number;
        }
    }
    if (best == nullptr) {
        return failure(archivePath + " holds no '" + innerStem +
                       "' with a day number after it");
    }

    // Without paths: the name the entry is unpacked under is its last component
    // and nothing else, so an archive naming its entry `../../etc/passwd`
    // writes a file called `passwd` into the temporary directory and nowhere
    // else.
    const fs::path unpacked = fs::path(workDir) / best->baseName();
    {
        std::ofstream out(unpacked, std::ios::binary | std::ios::trunc);
        if (!out) return failure("cannot unpack " + archivePath + " into " + workDir);
        auto text = zip.read(*best);
        if (!text) return tl::make_unexpected(std::move(text).error());
        out.write(text->data(), static_cast<std::streamsize>(text->size()));
        out.close();
        if (!out) return failure("cannot unpack " + archivePath + " into " + workDir);
    }
    unpacked_.push_back(unpacked.string());

    // Read back from the file that was written rather than from what was
    // unpacked in memory: what the compiler reads is then the file that is
    // there, and a temporary directory that cannot hold it says so now.
    auto text = readWholeFile(unpacked.string());
    if (!text) return tl::make_unexpected(std::move(text).error());
    return Loaded{state, unpacked.string(), archivePath, std::move(*text)};
}

}  // namespace amberedit::nodelist
