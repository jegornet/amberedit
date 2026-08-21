#pragma once

#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

/// A directory as the two file dialogs show it.
///
/// One reading for both: the import dialog picks a file out of it and the export
/// dialog picks a directory to write into, and the difference between them is
/// the one flag below. What they share is everything else — where `..` goes,
/// what order the names come in, and what is read about each of them.
namespace amberedit::ui {

/// One name in a directory, with what the columns beside it say.
///
/// All of it is read once, while the directory is: a row that asked the disk on
/// every frame would put it behind the scrollbar. A `size` of zero on a
/// directory means nothing was asked, and a `modified` of zero means the stamp
/// would not be read — both leave their column blank rather than the row out.
struct DirEntry {
    std::string name;
    bool directory{false};
    uintmax_t size{0};
    std::time_t modified{0};
};

/// The contents of `directory`, ready to be drawn.
///
/// `..` comes first and is there whatever else happens — a directory that
/// cannot be read is exactly the one the way out of matters in — except at the
/// root, whose parent is itself and where a row leading nowhere would be one row
/// of every listing wasted. Then the directories, then the files unless
/// `directoriesOnly`, each run in ASCII case order: what is being looked for is
/// reached through the directories.
///
/// `error` comes back with what could not be read, and empty where all was well.
[[nodiscard]] std::vector<DirEntry> readDirectory(const std::string& directory,
                                                  bool directoriesOnly,
                                                  std::string& error);

/// What a path typed into a dialog is asking for: a leading `~` for the home
/// directory as a shell reads it, and anything relative read against `base` —
/// which is what typing a name into a listing plainly means.
[[nodiscard]] std::string resolvePath(const std::string& base, const std::string& typed);

}  // namespace amberedit::ui
