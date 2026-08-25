#include "ui/dir_listing.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "config/text_util.hpp"
#include "i18n/i18n.hpp"

namespace amberedit::ui {
namespace {

namespace fs = std::filesystem;

/// A file's own stamp as a `std::time_t`.
///
/// `file_time_type` is not `system_clock` on every implementation and C++17 has
/// no way to say so — `clock_cast` is C++20 — so the two are read at the same
/// moment and the difference between them carries the stamp across. Both clocks
/// tick at the same rate, which is what makes this exact rather than merely
/// close.
std::time_t stampOf(const fs::file_time_type& when) {
    const auto system = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        when - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    return std::chrono::system_clock::to_time_t(system);
}

}  // namespace

std::vector<DirEntry> readDirectory(const std::string& directory, bool directoriesOnly,
                                    std::string& error) {
    error.clear();

    std::vector<DirEntry> entries;
    const fs::path here(directory);
    if (here.parent_path() != here) entries.push_back({"..", true, 0, 0});

    std::error_code ec;
    std::vector<DirEntry> found;
    for (fs::directory_iterator it(here, ec); !ec && it != fs::directory_iterator();
         it.increment(ec)) {
        DirEntry entry{it->path().filename().string(), false, 0, 0};
        std::error_code kindEc;
        entry.directory = it->is_directory(kindEc);
        if (directoriesOnly && !entry.directory) continue;

        // Whatever will not answer leaves the column blank rather than the row
        // out: a file that cannot be measured can still be read.
        std::error_code sizeEc;
        if (!entry.directory) entry.size = it->file_size(sizeEc);
        if (sizeEc) entry.size = 0;

        std::error_code timeEc;
        const fs::file_time_type when = it->last_write_time(timeEc);
        if (!timeEc) entry.modified = stampOf(when);

        found.push_back(std::move(entry));
    }
    if (ec) {
        error = i18n::format(_("cannot read {0}"), {directory});
        return entries;
    }

    std::sort(found.begin(), found.end(), [](const DirEntry& a, const DirEntry& b) {
        if (a.directory != b.directory) return a.directory;
        return config::text::toLower(a.name) < config::text::toLower(b.name);
    });
    entries.insert(entries.end(), found.begin(), found.end());
    return entries;
}

std::string resolvePath(const std::string& base, const std::string& typed) {
    std::string text(config::text::trim(typed));
    if (text == "~" || config::text::startsWith(text, "~/")) {
        if (const char* home = std::getenv("HOME")) {
            text = std::string(home) + text.substr(1);
        }
    }

    const fs::path path(text);
    if (path.is_absolute()) return path.lexically_normal().string();
    return (fs::path(base) / path).lexically_normal().string();
}

}  // namespace amberedit::ui
