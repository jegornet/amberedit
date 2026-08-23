#include "ui/setup/file_picker.hpp"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <system_error>
#include <utility>

#include "ui/dialog_frame.hpp"
#include "ui/event_util.hpp"
#include "ui/list_page.hpp"
#include "ui/quick_search.hpp"
#include "ui/text_layout.hpp"
#include "ui/theme.hpp"

namespace amberedit::ui::setup {

using namespace term;

namespace {

namespace fs = std::filesystem;

/// What a directory says of itself in the size column, and how wide that column
/// is — the same five as the import dialog's, for the same reason: it still says
/// what the row is where the name has had to be cut.
constexpr const char* kDirectoryMark = "<dir>";
constexpr int kSizeWidth = 5;
constexpr int kMargin = 1;

/// The row as it is written: a directory says so with a trailing slash.
std::string nameOf(const DirEntry& entry) {
    return entry.directory && entry.name != ".." ? entry.name + "/" : entry.name;
}

void clampCursor(FilePicker& picker) {
    const auto total = static_cast<int>(picker.entries.size());
    if (total == 0) {
        picker.cursor = 0;
        picker.offset = 0;
        return;
    }
    picker.cursor = std::clamp(picker.cursor, 0, total - 1);

    const int rows = std::min(std::max(1, picker.rows), total);
    picker.offset = std::min(picker.offset, picker.cursor);
    if (picker.cursor >= picker.offset + rows) picker.offset = picker.cursor - rows + 1;
    picker.offset = std::clamp(picker.offset, 0, std::max(0, total - rows));
}

void moveBy(FilePicker& picker, int delta) {
    picker.cursor += delta;
    clampCursor(picker);
}

/// The directory as it stands on disk, with the files the caller will not have
/// left out of it.
void readInto(FilePicker& picker) {
    picker.cursor = 0;
    picker.offset = 0;
    picker.search.clear();
    setFieldValue(picker.path, picker.directory);

    picker.error.clear();
    std::vector<DirEntry> entries =
        ui::readDirectory(picker.directory, /*directoriesOnly=*/false, picker.error);
    if (!picker.accepts) {
        picker.entries = std::move(entries);
        return;
    }

    picker.entries.clear();
    for (auto& entry : entries) {
        if (entry.directory || picker.accepts(entry.name)) {
            picker.entries.push_back(std::move(entry));
        }
    }
}

void enterDirectory(FilePicker& picker, const std::string& name) {
    const fs::path here(picker.directory);
    const fs::path next = name == ".." ? here.parent_path() : here / name;
    picker.directory = next.lexically_normal().string();
    readInto(picker);
}

std::optional<int> findByPrefix(const std::vector<DirEntry>& entries,
                                const std::string& query) {
    if (query.empty()) return std::nullopt;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (startsWithIgnoreCase(entries[i].name, query)) return static_cast<int>(i);
    }
    return std::nullopt;
}

/// The printable ASCII an event types, which is what the quick search takes.
std::optional<char> typedAscii(const Event& event) {
    if (!event.is_character() || event.input().size() != 1) return std::nullopt;
    if (event.ctrl() || event.alt()) return std::nullopt;
    const auto c = static_cast<unsigned char>(event.input()[0]);
    if (c > '~' || c <= ' ') return std::nullopt;
    return static_cast<char>(c);
}

std::string pathOf(const FilePicker& picker, const std::string& name) {
    return (fs::path(picker.directory) / name).lexically_normal().string();
}

/// What Enter on the row under the cursor does: walk into a directory, or pick
/// the file.
PickOutcome enterRow(FilePicker& picker) {
    if (picker.entries.empty()) return PickOutcome::Ignored;
    const auto& entry = picker.entries[static_cast<size_t>(picker.cursor)];
    if (entry.directory) {
        enterDirectory(picker, entry.name);
        return PickOutcome::Ignored;
    }
    picker.chosen = pathOf(picker, entry.name);
    return PickOutcome::Picked;
}

/// What Enter on the path box does: go to what it names, pick what it names, or
/// say the path is not there. Which of the three is the filesystem's answer and
/// not a mode the user has to have set first.
PickOutcome goToPath(FilePicker& picker) {
    const fs::path wanted = resolvePath(picker.directory, picker.path.value);

    std::error_code ec;
    const fs::file_status status = fs::status(wanted, ec);
    if (ec || !fs::exists(status)) {
        picker.error = "Path not found";
        return PickOutcome::Ignored;
    }
    if (fs::is_directory(status)) {
        picker.directory = wanted.lexically_normal().string();
        readInto(picker);
        // A directory asked for by name is one to pick a file from, so the
        // typing goes to the list.
        picker.onPath = false;
        return PickOutcome::Ignored;
    }
    picker.chosen = wanted.lexically_normal().string();
    return PickOutcome::Picked;
}

}  // namespace

void open(FilePicker& picker, const std::string& directory) {
    std::error_code ec;
    picker.directory = fs::is_directory(directory, ec)
                           ? fs::path(directory).lexically_normal().string()
                           : fs::current_path(ec).string();
    picker.chosen.clear();
    picker.onPath = false;
    readInto(picker);
}

std::string fileUnderCursor(const FilePicker& picker) {
    if (picker.entries.empty()) return {};
    const auto at = static_cast<size_t>(picker.cursor);
    if (at >= picker.entries.size()) return {};
    const auto& entry = picker.entries[at];
    if (entry.directory) return {};
    return pathOf(picker, entry.name);
}

Element render(FilePicker& picker, int inner) {
    clampCursor(picker);
    const auto total = static_cast<int>(picker.entries.size());

    Elements lines;
    lines.push_back(dialog::framed(renderField(picker.path, inner, picker.onPath)));
    lines.push_back(dialog::divider(inner));

    // The room is reserved first: the boxes are written into while the frame is
    // laid out, and a vector that grew under them would leave the rows already
    // drawn pointing at freed memory.
    picker.rowBoxes.clear();
    picker.rowBoxes.reserve(static_cast<size_t>(std::max(1, picker.rows)));

    const int room = std::max(1, inner - (2 * kMargin) - kSizeWidth - 1);
    for (int i = 0; i < picker.rows; ++i) {
        const int index = picker.offset + i;
        if (index >= total) {
            lines.push_back(dialog::line("", inner, theme::palette.dialogText));
            continue;
        }
        const auto& entry = picker.entries[static_cast<size_t>(index)];
        const std::string mark = entry.directory ? kDirectoryMark : std::string();
        const std::string row = " " +
                                padRight(truncateToWidth(nameOf(entry), room), room) +
                                " " + padLeft(mark, kSizeWidth);

        Element cell = text(padRight(truncateToWidth(row, inner), inner));
        if (index == picker.cursor && !picker.onPath) {
            cell = std::move(cell) | bold | color(theme::palette.selectionText) |
                   bgcolor(theme::palette.selection);
        } else if (index == picker.cursor) {
            cell = std::move(cell) | bold | color(theme::palette.dialogLabel);
        } else {
            cell = std::move(cell) | color(entry.directory ? theme::palette.dialogLabel
                                                           : theme::palette.dialogText);
        }

        picker.rowBoxes.push_back({index, {}});
        lines.push_back(
            dialog::framed(std::move(cell) | reflect(picker.rowBoxes.back().box)));
    }
    return vbox(std::move(lines));
}

PickOutcome handleClick(FilePicker& picker, const MouseEvent& click) {
    if (picker.path.box.Contain(click.x, click.y)) {
        picker.onPath = true;
        clickField(picker.path, click.x);
        return PickOutcome::Ignored;
    }
    for (const auto& row : picker.rowBoxes) {
        if (!row.box.Contain(click.x, click.y)) continue;
        picker.onPath = false;
        // A click on a row the cursor is already on is the second half of a
        // double click, and opens it — as one on any other row moves there
        // first, which is what makes a mis-aimed click harmless.
        if (row.index == picker.cursor) return enterRow(picker);
        picker.cursor = row.index;
        clampCursor(picker);
        return PickOutcome::Ignored;
    }
    return PickOutcome::Unclaimed;
}

PickOutcome handleEvent(FilePicker& picker, const Event& event) {
    if (event == Event::Tab || event == Event::TabReverse || event == Event::Escape) {
        return PickOutcome::Unclaimed;
    }
    if (event == Event::Return) {
        return picker.onPath ? goToPath(picker) : enterRow(picker);
    }

    if (picker.onPath) {
        if (event == Event::ArrowDown) {
            picker.onPath = false;
            return PickOutcome::Ignored;
        }
        return handleFieldKey(picker.path, event) ? PickOutcome::Ignored
                                                  : PickOutcome::Unclaimed;
    }

    // Typing a name is how the search starts, so the letters are claimed before
    // anything that would rather have them.
    if (const auto typed = typedAscii(event)) {
        picker.search += *typed;
        if (const auto match = findByPrefix(picker.entries, picker.search)) {
            picker.cursor = *match;
            clampCursor(picker);
        }
        return PickOutcome::Ignored;
    }
    if (event == Event::Backspace && !picker.search.empty()) {
        // An emptied query leaves the cursor on whatever it last found: erasing
        // a search is not undoing it.
        picker.search.pop_back();
        if (const auto match = findByPrefix(picker.entries, picker.search)) {
            picker.cursor = *match;
            clampCursor(picker);
        }
        return PickOutcome::Ignored;
    }
    // Every other key ends the search: once the cursor is being moved by hand,
    // the query has said what it had to say.
    picker.search.clear();

    const auto total = static_cast<int>(picker.entries.size());
    if (const int wheel = wheelDelta(event); wheel != 0) {
        moveBy(picker, wheel);
        return PickOutcome::Ignored;
    }
    if (event == Event::ArrowUp) {
        // Off the top of the list is up into the path box, which is where it is
        // drawn: the two are one column of the dialog.
        if (picker.cursor == 0) {
            picker.onPath = true;
            picker.path.cursor = picker.path.value.size();
            return PickOutcome::Ignored;
        }
        moveBy(picker, -1);
        return PickOutcome::Ignored;
    }
    if (event == Event::ArrowDown) {
        moveBy(picker, 1);
        return PickOutcome::Ignored;
    }
    if (event == Event::PageUp) {
        picker.cursor = pageUpTarget(picker.cursor, picker.offset, picker.rows);
        clampCursor(picker);
        return PickOutcome::Ignored;
    }
    if (event == Event::PageDown) {
        picker.cursor = pageDownTarget(picker.cursor, picker.offset, picker.rows, total);
        clampCursor(picker);
        return PickOutcome::Ignored;
    }
    if (event == Event::Home) {
        picker.cursor = 0;
        clampCursor(picker);
        return PickOutcome::Ignored;
    }
    if (event == Event::End) {
        picker.cursor = std::max(0, total - 1);
        clampCursor(picker);
        return PickOutcome::Ignored;
    }
    return PickOutcome::Unclaimed;
}

}  // namespace amberedit::ui::setup
