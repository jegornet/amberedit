#include "config/temp_dir.hpp"

#include <unistd.h>

#include <filesystem>
#include <system_error>

namespace amberedit::config {
namespace {

namespace fs = std::filesystem;

/// Where temporary work goes when the config names nowhere: a directory of ours
/// inside the system's own temporary one, which is the answer every other
/// program on the machine gives. Empty where the system will not name one at
/// all, which is a machine whose `$TMPDIR` points at something that is not a
/// directory.
///
/// Ours and not the system's directory itself, because what is written there is
/// named by the work rather than by us — a zipped nodelist is unpacked under the
/// name the archive gives it. `NODELIST.219` written straight into a `/tmp`
/// everybody logged in can write to is a name any of them can have made first,
/// as a file of their own or as a link pointing wherever they like. One
/// directory, made by us and checked once, turns a question about every file
/// written into a question asked once.
///
/// Per user for the same reason: Unix famously gives no user a temporary
/// directory of their own, and a shared `amberedit` there would be the first
/// user's to keep and nobody else's to write to.
std::string defaultTempDir() {
    std::error_code ec;
    const fs::path base = fs::temp_directory_path(ec);
    if (ec || base.empty()) return {};

    const std::string name =
        "amberedit-" + std::to_string(static_cast<unsigned long>(::getuid()));
    return (base / name).string();
}

/// That the directory we chose is the directory we made: a link is refused by
/// name, since writing through one writes wherever it points, and anything
/// belonging to somebody else refuses itself — the permissions over a directory
/// are the owner's alone to set, so setting them is the question and the answer
/// at once.
[[nodiscard]] Result<void> insistItIsOurs(const std::string& path) {
    std::error_code ec;
    if (fs::is_symlink(fs::symlink_status(path, ec))) {
        return failure(
            "the temporary directory to work in is a symbolic link and so not "
            "ours to use — set tmpdir to a directory of your own: " +
            path);
    }
    fs::permissions(path, fs::perms::owner_all, fs::perm_options::replace, ec);
    if (ec) {
        return failure(
            "the temporary directory to work in is not ours — set tmpdir to a "
            "directory of your own: " +
            path);
    }
    return {};
}

}  // namespace

Result<std::string> makeTempDir(const std::string& configured) {
    const bool ours = configured.empty();
    const std::string path = ours ? defaultTempDir() : configured;
    if (path.empty()) {
        return failure(
            "the system names no temporary directory of its own, so tmpdir has "
            "to name one");
    }

    std::error_code ec;
    fs::create_directories(path, ec);
    if (!fs::is_directory(path, ec)) {
        return failure(
            "the temporary directory to work in is not one that can be made: " + path);
    }
    if (ours) {
        auto checked = insistItIsOurs(path);
        if (!checked) return tl::make_unexpected(std::move(checked).error());
    }
    return path;
}

}  // namespace amberedit::config
