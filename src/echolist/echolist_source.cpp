#include "echolist/echolist_source.hpp"

#include <sys/stat.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string_view>
#include <utility>

#include "archive/zip_reader.hpp"
#include "config/temp_dir.hpp"
#include "config/text_util.hpp"
#include "echolist/echolist_parser.hpp"
#include "encoding/charset_detector.hpp"
#include "encoding/iconv_recoder.hpp"
#include "encoding/locale_charset.hpp"

namespace amberedit::echolist {
namespace {

namespace fs = std::filesystem;

/// What the file system says about a file. Nullopt where there is no file, or
/// where it is not one — a directory answering to the name is not an echolist.
///
/// `stat` rather than `std::filesystem`, for the reason the nodelist's own
/// `fileState` gives: the stamp is written into the compiled file and compared
/// against it at the next start, and `file_time_type` is a clock of the
/// implementation's own choosing in C++17 with no portable way to put a number
/// on it.
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

/// One file a pattern covers, with what decides which of them is newest.
struct Candidate {
    std::string path;
    /// The filename, ASCII case folded, for the tie the stamp cannot settle.
    std::string name;
    uint64_t modified{0};
};

bool newerThan(const Candidate& a, const Candidate& b) {
    if (a.modified != b.modified) return a.modified > b.modified;
    // The answer must not depend on the order a directory listing happened to
    // hand two files over in. The later name wins, since an echolist carrying
    // its month in its name carries it in the order such names sort in.
    return a.name > b.name;
}

/// The newest file a path stands for, or nullopt where it stands for none.
///
/// A path with no wildcard in its filename is that file and is looked up as it
/// stands. One with a wildcard is a glob over the names in its directory —
/// `echo*.zip` — and the newest file it matches is the echolist. Newest is the
/// modification time, and on a tie the later name, so that an archive read
/// twice answers the same twice and a name carrying a date breaks the tie in
/// the order such names sort in.
///
/// Only the filename may be a glob. A pattern over directories would be a
/// pattern over whose echolist this is, and nothing needs one.
std::optional<std::string> newestMatch(const std::string& spec) {
    const fs::path path(spec);
    const std::string name = path.filename().string();
    if (!config::text::hasWildcard(name)) {
        return fileState(spec) ? std::optional<std::string>(spec) : std::nullopt;
    }

    const fs::path directory =
        path.parent_path().empty() ? fs::path(".") : path.parent_path();
    std::error_code ec;
    fs::directory_iterator entries(directory, ec);
    if (ec) return std::nullopt;

    std::optional<Candidate> best;
    for (const auto& item : entries) {
        const std::string candidate = item.path().filename().string();
        if (!config::text::globMatches(name, candidate)) continue;

        const auto state = fileState(item.path().string());
        if (!state) continue;

        Candidate found;
        found.path = item.path().string();
        found.name = config::text::toLower(candidate);
        found.modified = state->modified;
        if (!best || newerThan(found, *best)) best = std::move(found);
    }

    if (!best) return std::nullopt;
    return best->path;
}

Result<std::string> readWholeFile(const std::string& path) {
    const auto isFile = config::text::insistItIsAFile(path);
    if (!isFile) return tl::make_unexpected(isFile.error());

    std::ifstream in(path, std::ios::binary);
    if (!in) return failure("cannot read the echolist: " + path);
    std::string text(std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>{});
    if (in.bad()) return failure("cannot read the echolist: " + path);
    return text;
}

/// The charset an echolist is read in: the one the line stated, and the
/// locale's where it stated none. The stated one goes through the same
/// normalisation a CHRS kludge does, so that the Fidonet spellings — `+7_FIDO`,
/// a bare `866` — mean here what they mean everywhere else in AmberEdit; a name
/// that identifies no particular encoding (`IBMPC`) says nothing, and the
/// locale answers for it as though the line had been left bare.
std::string charsetToReadIn(const std::string& stated) {
    std::string normalized = encoding::CharsetDetector::normalize(stated);
    if (!normalized.empty()) return normalized;
    return encoding::localeCharset();
}

}  // namespace

bool isArchiveName(const std::string& path) {
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return false;
    return config::text::iequals(std::string_view(path).substr(dot + 1), "zip");
}

SourceState stateOf(const std::string& spec, const std::string& charset) {
    SourceState state;
    state.spec = spec;
    state.charset = charset;
    const auto found = newestMatch(spec);
    if (!found) return state;
    // Between the listing above and the stat below the file could in principle
    // go; the state is then the one a missing file has, which is the truth as of
    // now and is compared against the next start's truth like any other.
    const auto file = fileState(*found);
    if (!file) return state;
    state.path = *found;
    state.modified = file->modified;
    state.size = file->size;
    return state;
}

EcholistSources::EcholistSources(std::string tempDir) : tempDir_(std::move(tempDir)) {}

EcholistSources::~EcholistSources() {
    // What was unpacked is taken away again, whether the compile finished or
    // threw its way out. The directory itself is left alone either way: a
    // config named it for this and for whatever else it is used for and it was
    // very likely there before we were, and one `makeTempDir` fell back on is a
    // name worth keeping hold of — a directory that stays ours is one nobody
    // else can put anything in the way of.
    std::error_code ec;
    for (const auto& path : unpacked_) fs::remove(path, ec);
}

Result<EcholistSources::Loaded> EcholistSources::read(const std::string& spec,
                                                      const std::string& charset) {
    const SourceState state = stateOf(spec, charset);
    if (state.path.empty()) {
        if (!config::text::hasWildcard(fs::path(spec).filename().string())) {
            return failure("echolist not found: " + spec);
        }
        return failure("no echolist matching " + spec +
                       " — nothing in that directory is called that");
    }

    // Asked of the file that was found and not of the line that found it: a
    // pattern may cover archives and plain lists at once (`echo*.*`), and what
    // the newest of them is, is its own name's to say.
    if (isArchiveName(state.path)) return readArchive(state, charset);

    encoding::IconvRecoder recoder;
    Part part;
    part.readFrom = state.path;
    part.name = fs::path(state.path).filename().string();
    const auto text = readWholeFile(state.path);
    if (!text) return tl::make_unexpected(text.error());
    part.text = recoder.toUtf8(*text, charsetToReadIn(charset));
    Loaded loaded;
    loaded.state = state;
    loaded.parts.push_back(std::move(part));
    return loaded;
}

Result<EcholistSources::Loaded> EcholistSources::readArchive(const SourceState& state,
                                                             const std::string& charset) {
    const std::string& archivePath = state.path;

    // Made here and not when the sources were: a config with `tmpdir` in it and
    // no zipped echolist under it should leave nothing behind, and most do. What
    // is wrong with the directory is `makeTempDir`'s to say and what it was
    // wanted for is ours, which is why the two are said together.
    const auto workDirMade = config::makeTempDir(tempDir_);
    if (!workDirMade) {
        return failure(state.spec + " names a zipped echolist, and " +
                       workDirMade.error());
    }
    const std::string& workDir = *workDirMade;

    const auto opened = archive::ZipArchive::open(archivePath);
    if (!opened) return tl::make_unexpected(opened.error());
    const archive::ZipArchive& zip = *opened;

    // Only the echolists are unpacked. An echolist distribution carries reports,
    // a rulebook and the rules and descriptions in archives of their own beside
    // the lists themselves, and none of that has any business being written to
    // disk.
    std::vector<const archive::ZipEntry*> wanted;
    for (const auto& entry : zip.entries()) {
        if (isEcholistName(entry.baseName())) wanted.push_back(&entry);
    }
    if (wanted.empty()) {
        return failure(archivePath +
                       " holds no echolist — nothing in it is a .lst or a .na");
    }

    // By name, so that an archive read twice reads the same way twice: where two
    // of its lists name one echo, which of them keeps it must not depend on the
    // order the archive happens to have been packed in.
    std::sort(wanted.begin(), wanted.end(),
              [](const archive::ZipEntry* a, const archive::ZipEntry* b) {
                  return config::text::toLower(a->baseName()) <
                         config::text::toLower(b->baseName());
              });

    Loaded loaded;
    loaded.state = state;
    loaded.archive = archivePath;

    encoding::IconvRecoder recoder;
    const std::string from = charsetToReadIn(charset);
    // Built once rather than per entry: it says the same thing whichever of them
    // could not be written, and joining it inside the loop is a string built and
    // thrown away for every list an archive holds.
    const std::string cannotUnpack = "cannot unpack " + archivePath + " into " + workDir;
    for (const archive::ZipEntry* entry : wanted) {
        // Without paths: the name the entry is unpacked under is its last
        // component and nothing else, so an archive naming its entry
        // `../../etc/passwd` writes a file called `passwd` into the temporary
        // directory and nowhere else.
        const fs::path unpacked = fs::path(workDir) / entry->baseName();
        {
            std::ofstream out(unpacked, std::ios::binary | std::ios::trunc);
            if (!out) return failure(cannotUnpack);
            const auto text = zip.read(*entry);
            if (!text) return tl::make_unexpected(text.error());
            out.write(text->data(), static_cast<std::streamsize>(text->size()));
            out.close();
            if (!out) return failure(cannotUnpack);
        }
        unpacked_.push_back(unpacked.string());

        // Read back from the file that was written rather than from what was
        // unpacked in memory: what the compiler reads is then the file that is
        // there, and a temporary directory that cannot hold it says so now.
        Part part;
        part.readFrom = unpacked.string();
        part.name = entry->baseName();
        const auto text = readWholeFile(part.readFrom);
        if (!text) return tl::make_unexpected(text.error());
        part.text = recoder.toUtf8(*text, from);
        loaded.parts.push_back(std::move(part));
    }
    return loaded;
}

}  // namespace amberedit::echolist
