#include "ui/import_dialog.hpp"

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "app/import_file.hpp"
#include "app/message_builder.hpp"
#include "config/text_util.hpp"
#include "domain/message.hpp"
#include "ui/area_list_format.hpp"
#include "ui/dialog_frame.hpp"
#include "ui/dir_listing.hpp"
#include "ui/event_util.hpp"
#include "ui/input_field.hpp"
#include "ui/list_page.hpp"
#include "ui/quick_search.hpp"
#include "ui/term/utf8.hpp"
#include "ui/text_layout.hpp"
#include "ui/theme.hpp"

namespace amberedit::ui::import_dialog {

using namespace term;

namespace {

namespace fs = std::filesystem;

using Picker = AppState::ImportPicker;
using Focus = AppState::ImportPicker::Focus;

/// How wide the box stands inside its frame, and the narrowest it will be
/// squeezed to in a window with less than that.
///
/// A width, not a measurement of what is in it. A box measured against the
/// longest name in the directory would be a different width in every directory,
/// and walking a tree would have it growing and shrinking under the pointer;
/// what does not fit is truncated, which is what every other column here does.
/// Fifty-six columns is a path and a filename with room to read them, and
/// leaves the whole box inside the eighty a terminal has always had.
constexpr int kInnerWidth = 56;
constexpr int kMinInner = 40;
/// A column of margin at each side of a row, so that the fill under the current
/// one covers them rather than starting a column in.
constexpr int kMargin = 1;
/// The frame itself, a column on each side.
constexpr int kFrame = 2;

/// The size column: five columns is "1023k", and `area_format::countText()`
/// shortens whatever is wider — 307200 bytes as `307k`, ten megabytes as `10M`.
constexpr int kSizeWidth = 5;
/// What the size column says of a directory. Exactly as wide as a size, and it
/// still says what the row is where the name has been truncated.
constexpr const char* kDirectoryMark = "<dir>";
/// The name column never gives up more than this to the two beside it.
constexpr int kMinNameWidth = 12;

/// Rows of the box that are not the file list: the title, the path, the two
/// rules, the mode and the bottom of the frame. Six whatever happens — the keys
/// and what went wrong are both written along that bottom frame rather than on
/// rows of their own.
constexpr int kChromeRows = 6;
/// And the rows it keeps clear of the top and bottom of the window.
constexpr int kWindowMargin = 2;

/// What the bottom rule says the keys do.
constexpr const char* kHint = "Enter open · Tab move · Esc close";

/// What a path that is not there is answered with.
constexpr const char* kNotFound = "Path not found";

/// Settles how big the box is, once — and again only where the window itself has
/// changed size.
///
/// The list keeps its rows and the frame its width for as long as the dialog is
/// up: a box that measured itself against the directory it was showing would
/// stand a different size in every one of them, and the row the pointer was over
/// would move as the listing came in. What the window can no longer hold is the
/// one thing worth measuring again, exactly as the info box lays its report out
/// again when the width changes.
void fitBox(const AppState& state, Picker& picker) {
    if (picker.layoutWidth == state.width && picker.layoutHeight == state.height) return;
    picker.layoutWidth = state.width;
    picker.layoutHeight = state.height;
    picker.inner = std::min(kInnerWidth, std::max(kMinInner, state.width - kFrame));
    picker.rows = std::max(1, state.height - kChromeRows - kWindowMargin);
}

/// The row as it is written: a directory says so with a trailing slash, which
/// is the one thing about it worth a column of the name.
std::string nameOf(const DirEntry& entry) {
    return entry.directory && entry.name != ".." ? entry.name + "/" : entry.name;
}

/// The directory as it stands on disk, read into the picker.
void readDirectory(AppState& state, Picker& picker) {
    picker.cursor = 0;
    picker.offset = 0;
    picker.search.clear();
    // The path box says where the listing is from. Whatever was typed into it is
    // done with: it has been answered, and what it asked for is on the screen.
    picker.path = state.importDirectory;
    picker.pathCursor = picker.path.size();
    // Files and directories both: what is being picked here is a file.
    picker.entries =
        ui::readDirectory(state.importDirectory, /*directoriesOnly=*/false, picker.error);
}

/// Walks into `name`, or back out of the directory when it is `..`.
void enterDirectory(AppState& state, Picker& picker, const std::string& name) {
    const fs::path here(state.importDirectory);
    const fs::path next = name == ".." ? here.parent_path() : here / name;
    state.importDirectory = next.lexically_normal().string();
    readDirectory(state, picker);
}

void clampCursor(Picker& picker) {
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

void moveBy(Picker& picker, int delta) {
    picker.cursor += delta;
    clampCursor(picker);
}

/// Which entry a quick-search query points at: the first whose name begins with
/// it, by the same ASCII case folding the area list searches by.
std::optional<int> findByPrefix(const std::vector<DirEntry>& entries,
                                const std::string& query) {
    if (query.empty()) return std::nullopt;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (startsWithIgnoreCase(entries[i].name, query)) return static_cast<int>(i);
    }
    return std::nullopt;
}

/// The printable ASCII an event types, if it types any — what the quick search
/// takes, a name being searched for here the way the area list searches for
/// one.
std::optional<char> typedAscii(const Event& event, bool spaceCounts) {
    if (!event.is_character() || event.input().size() != 1) return std::nullopt;
    if (event.ctrl() || event.alt()) return std::nullopt;
    const auto c = static_cast<unsigned char>(event.input()[0]);
    if (c > '~' || c < ' ' || (c == ' ' && !spaceCounts)) return std::nullopt;
    return static_cast<char>(c);
}

/// What an event types into the path box, which is anything a path can hold:
/// a name is whatever the filesystem took when it was made, and Cyrillic
/// filenames are as ordinary as spaces in them.
std::optional<std::string> typedText(const Event& event) {
    if (!event.is_character() || event.ctrl() || event.alt()) return std::nullopt;
    const std::string& input = event.character();
    if (input.empty()) return std::nullopt;
    if (input.size() == 1 && static_cast<unsigned char>(input[0]) < 0x20) {
        return std::nullopt;
    }
    return input;
}

/// The next stop in the ring, `step` forwards or back.
void focusAfter(Picker& picker, int step) {
    constexpr Focus kRing[] = {Focus::Path, Focus::Files, Focus::Mode};
    constexpr int kStops = static_cast<int>(sizeof(kRing) / sizeof(kRing[0]));

    const auto* at = std::find(std::begin(kRing), std::end(kRing), picker.focus);
    const auto index =
        static_cast<int>(at == std::end(kRing) ? 0 : at - std::begin(kRing));
    picker.focus =
        kRing[static_cast<size_t>(((index + step) % kStops + kStops) % kStops)];
    if (picker.focus == Focus::Path) {
        picker.pathCursor = std::min(picker.pathCursor, picker.path.size());
    }
}

/// The path the file stands at, the directory and the name joined the one way a
/// path is joined.
std::string pathOf(const AppState& state, const std::string& name) {
    return (fs::path(state.importDirectory) / name).string();
}

/// Reads the file at `path` into the message.
Outcome importPath(AppState& state, Picker& picker, const std::string& path) {
    // The settings of the area the message is going into: the cut lines are a
    // per-area setting, since what a message written there looks like is what an
    // area group is for.
    const config::AppConfig& config = state.composeConfig();
    // A text file is decoded out of the charset the locale names. There is no
    // asking: the terminal is being read in that charset, and a file on the same
    // machine was written by the same hands — a box for it would be one more
    // question with one answer.
    auto imported =
        app::importFile(app::ImportRequest{path, state.importMode, ensureUtf8Locale(),
                                           config.importBegin, config.importEnd});

    if (!imported) {
        picker.error = imported.error();
        return Outcome::Ignored;
    }
    // A file read from somewhere else is where the next import starts looking.
    const fs::path parent = fs::path(path).parent_path();
    if (!parent.empty()) state.importDirectory = parent.lexically_normal().string();

    picker.error.clear();
    picker.lines = std::move(*imported);
    return Outcome::Imported;
}

/// Reads the file under the cursor, or walks into it where it is a directory.
///
/// A file that will not open leaves the dialog up with the reason written along
/// its bottom frame: the file was named a moment ago and can be named again, and
/// there is nothing to go back to a message for.
Outcome runImport(AppState& state, Picker& picker) {
    if (picker.entries.empty()) return Outcome::Ignored;
    clampCursor(picker);
    // Copied rather than pointed at: walking into a directory reads the listing
    // again, and the name being walked into is one of the entries it throws
    // away.
    const DirEntry entry = picker.entries[static_cast<size_t>(picker.cursor)];

    if (entry.directory) {
        enterDirectory(state, picker, entry.name);
        return Outcome::Ignored;
    }
    return importPath(state, picker, pathOf(state, entry.name));
}

/// What Enter on the path box does: walk into what it names, read what it names,
/// or say the path is not there.
///
/// One box for the two because a path is one thing to type. Which of them it is
/// is the filesystem's answer and not a mode the user has to have set first.
Outcome goToPath(AppState& state, Picker& picker) {
    const fs::path wanted = resolvePath(state.importDirectory, picker.path);

    std::error_code ec;
    const fs::file_status status = fs::status(wanted, ec);
    if (ec || !fs::exists(status)) {
        picker.error = kNotFound;
        return Outcome::Ignored;
    }

    if (fs::is_directory(status)) {
        state.importDirectory = wanted.lexically_normal().string();
        // readDirectory() puts the box back to where the listing now is, and
        // says so itself where the directory will not open.
        readDirectory(state, picker);
        // A directory asked for by name is one to pick a file from, so the
        // typing goes to the list — Tab comes straight back here.
        picker.focus = Focus::Files;
        return Outcome::Ignored;
    }
    return importPath(state, picker, wanted.lexically_normal().string());
}

// --- drawing -----------------------------------------------------------------

/// The row under the list, and what it leaves for the blank beside it.
constexpr const char* kModeLabel = " Mode:    ";
/// Between the two mode markers.
constexpr const char* kModeGap = "   ";

/// A marker and its label: "(*) Text". The brackets are ASCII on purpose — a
/// terminal in a single-byte charset has no bullet to draw, and this row is
/// where the answer is read off.
int markWidth(const std::string& label) {
    return displayWidth("(*) ") + displayWidth(label);
}

/// One of the two mode markers.
Element modeMark(const std::string& label, bool chosen, bool focused) {
    auto element = text(std::string(chosen ? "(*) " : "( ) ") + label);
    if (focused) {
        return std::move(element) | bold | color(theme::palette.selectionText) |
               bgcolor(theme::palette.selection);
    }
    return std::move(element) |
           color(chosen ? theme::palette.header : theme::palette.text);
}

/// A file's stamp, written the way every other stamp in the interface is —
/// `reader_datetime_format`, through `MessageDate::format()`. No zone is passed:
/// a `%z` in that format says which clock a *message* states it was written by,
/// and a file on this disk states nothing of the kind.
std::string stampText(const AppState& state, std::time_t when) {
    if (when == 0) return {};
    return app::localStamp(when).format(state.config.readerDateTimeFormat);
}

/// How wide the stamp column stands: what the format writes for a date that is
/// certainly one, rather than what the files in this directory happen to
/// measure. Every stamp in the column comes off the same format, so a sample
/// answers for all of them — and answers the same in every directory, which is
/// what keeps the columns from shifting as one is walked into.
int stampWidth(const AppState& state) {
    return displayWidth(domain::MessageDate{2026, 12, 30, 23, 59, 59}.format(
        state.config.readerDateTimeFormat));
}

/// One row of the listing: the name, then the size and the stamp in columns of
/// their own against the right edge.
///
/// The name gives up what the two beside it need and no more than that: a window
/// narrow enough to leave it under `kMinNameWidth` takes the columns off the
/// stamp instead, the name being what a file is picked by.
std::string rowText(const AppState& state, const DirEntry& entry, int room) {
    const int stamps = std::clamp(stampWidth(state), 0,
                                  std::max(0, room - kMinNameWidth - kSizeWidth - 2));
    const int names = std::max(1, room - kSizeWidth - stamps - (stamps > 0 ? 2 : 1));

    std::string row = padRight(truncateToWidth(nameOf(entry), names), names);
    row += " ";
    // A directory has no size worth the column; what it has instead is the one
    // word saying it is one, which stands even where the name was truncated.
    // `..` is the way out of the directory rather than a thing in it, and says
    // nothing at all.
    const std::string size = entry.name == ".." ? std::string{}
                             : entry.directory
                                 ? kDirectoryMark
                                 : area_format::countText(entry.size, kSizeWidth);
    row += padLeft(size, kSizeWidth);
    if (stamps > 0) {
        row += " ";
        const std::string stamp =
            entry.name == ".." ? std::string{} : stampText(state, entry.modified);
        row += padRight(truncateToWidth(stamp, stamps), stamps);
    }
    return row;
}

// --- clicks ------------------------------------------------------------------

Outcome handleClick(AppState& state, Picker& picker, const MouseEvent& click) {
    // Which row the pointer landed on, taken off the frame before anything is
    // done about it: showing the click draws another one, and render() rebuilds
    // the rows every time it does.
    int hit = -1;
    for (const auto& row : picker.rowBoxes) {
        if (row.box.Contain(click.x, click.y)) hit = row.index;
    }
    if (hit >= 0) {
        picker.cursor = hit;
        picker.focus = Focus::Files;
        picker.search.clear();
        clampCursor(picker);
        // Selected first and read after, so the row the pointer landed on is on
        // screen as the current one for the length of the animation.
        state.showClick();
        return runImport(state, picker);
    }
    if (picker.pathBox.Contain(click.x, click.y)) {
        picker.focus = Focus::Path;
        picker.pathCursor = offsetAtColumn(picker.path, picker.pathOrigin,
                                           click.x - picker.pathBox.x_min);
        return Outcome::Ignored;
    }
    if (picker.textModeBox.Contain(click.x, click.y)) {
        state.importMode = app::ImportMode::Text;
        picker.focus = Focus::Mode;
        return Outcome::Ignored;
    }
    if (picker.uueModeBox.Contain(click.x, click.y)) {
        state.importMode = app::ImportMode::Uue;
        picker.focus = Focus::Mode;
        return Outcome::Ignored;
    }
    // Anywhere else, inside the box or outside it: swallowed, as every other
    // event is while the dialog is modal.
    return Outcome::Ignored;
}

// --- keys --------------------------------------------------------------------

Outcome pathKey(Picker& picker, const Event& event) {
    std::string& value = picker.path;
    size_t& cursor = picker.pathCursor;
    cursor = std::min(cursor, value.size());

    if (const auto typed = typedText(event)) {
        value.insert(cursor, *typed);
        cursor += typed->size();
        return Outcome::Ignored;
    }
    if (event == Event::Backspace) {
        const size_t from = prevChar(value, cursor);
        value.erase(from, cursor - from);
        cursor = from;
        return Outcome::Ignored;
    }
    if (event == Event::Delete) {
        value.erase(cursor, charLen(value, cursor));
        return Outcome::Ignored;
    }
    if (event == Event::ArrowLeft) {
        cursor = prevChar(value, cursor);
        return Outcome::Ignored;
    }
    if (event == Event::ArrowRight) {
        cursor = std::min(value.size(), cursor + charLen(value, cursor));
        return Outcome::Ignored;
    }
    if (event == Event::Home) {
        cursor = 0;
        return Outcome::Ignored;
    }
    if (event == Event::End) {
        cursor = value.size();
        return Outcome::Ignored;
    }
    if (event == Event::ArrowDown) {
        picker.focus = Focus::Files;
        return Outcome::Ignored;
    }
    return Outcome::Ignored;
}

Outcome filesKey(AppState& state, Picker& picker, const Event& event) {
    const auto total = static_cast<int>(picker.entries.size());

    // Typing a name is how the search starts, so the letters are claimed before
    // anything that would rather have them.
    if (const auto typed = typedAscii(event, /*spaceCounts=*/false)) {
        picker.search += *typed;
        if (const auto match = findByPrefix(picker.entries, picker.search)) {
            picker.cursor = *match;
            clampCursor(picker);
        }
        return Outcome::Ignored;
    }
    if (event == Event::Backspace && !picker.search.empty()) {
        // An emptied query puts the title back and leaves the cursor on whatever
        // it last found: erasing a search is not undoing it.
        picker.search.pop_back();
        if (const auto match = findByPrefix(picker.entries, picker.search)) {
            picker.cursor = *match;
            clampCursor(picker);
        }
        return Outcome::Ignored;
    }
    // Every other key ends the search — once the cursor is being moved by hand
    // or a file is being read, the query has said what it had to say. Esc while
    // one is up is answered before this, by closing the search and not the box.
    picker.search.clear();

    if (const int wheel = wheelDelta(event); wheel != 0) {
        moveBy(picker, wheel);
        return Outcome::Ignored;
    }
    if (event == Event::ArrowUp) {
        // Off the top of the list is up into the path box, which is where it is
        // drawn: the two are one column of the dialog.
        if (picker.cursor == 0) {
            picker.focus = Focus::Path;
            picker.pathCursor = picker.path.size();
            return Outcome::Ignored;
        }
        moveBy(picker, -1);
        return Outcome::Ignored;
    }
    if (event == Event::ArrowDown) {
        moveBy(picker, 1);
        return Outcome::Ignored;
    }
    if (event == Event::PageUp) {
        picker.cursor = pageUpTarget(picker.cursor, picker.offset, picker.rows);
        clampCursor(picker);
        return Outcome::Ignored;
    }
    if (event == Event::PageDown) {
        picker.cursor = pageDownTarget(picker.cursor, picker.offset, picker.rows, total);
        clampCursor(picker);
        return Outcome::Ignored;
    }
    if (event == Event::Home) {
        picker.cursor = 0;
        clampCursor(picker);
        return Outcome::Ignored;
    }
    if (event == Event::End) {
        picker.cursor = total - 1;
        clampCursor(picker);
        return Outcome::Ignored;
    }
    // Into what the cursor names, as Enter does — the arrow the lists are
    // walked into a screen with.
    if (event == Event::ArrowRight) return runImport(state, picker);
    // Out of the directory, whether or not a `..` row is under the cursor —
    // ← is the way back everywhere else in the interface too. At the root there
    // is nowhere to go and the key does nothing.
    if (event == Event::ArrowLeft || event == Event::Backspace) {
        const fs::path here(state.importDirectory);
        if (here.parent_path() != here) enterDirectory(state, picker, "..");
        return Outcome::Ignored;
    }
    return Outcome::Ignored;
}

Outcome modeKey(AppState& state, Picker& picker, const Event& event) {
    if (event == Event::ArrowLeft) {
        state.importMode = app::ImportMode::Text;
        return Outcome::Ignored;
    }
    if (event == Event::ArrowRight) {
        state.importMode = app::ImportMode::Uue;
        return Outcome::Ignored;
    }
    if (event == Event::Character(' ')) {
        state.importMode = state.importMode == app::ImportMode::Text
                               ? app::ImportMode::Uue
                               : app::ImportMode::Text;
        return Outcome::Ignored;
    }
    // Up into the list, which is what the row stands under. There is nothing
    // below it but the line saying what the keys do.
    if (event == Event::ArrowUp) {
        picker.focus = Focus::Files;
        return Outcome::Ignored;
    }
    return Outcome::Ignored;
}

}  // namespace

void open(AppState& state) {
    // Where the last import was. On the first opening there is nothing to
    // remember, and the directory AmberEdit was started in is where the files a
    // user means are.
    if (state.importDirectory.empty()) {
        std::error_code ec;
        const fs::path here = fs::current_path(ec);
        state.importDirectory = ec ? std::string("/") : here.string();
    }
    state.importPicker = Picker{};
    // The size the box keeps for as long as it is up, settled before anything is
    // in it: what the window holds is what decides it, not what the directory
    // does.
    fitBox(state, *state.importPicker);
    readDirectory(state, *state.importPicker);
}

Element render(AppState& state, Element background) {
    Picker& picker = *state.importPicker;
    fitBox(state, picker);
    clampCursor(picker);
    // The box that is typed into, in case what it holds has been put back under
    // a cursor left where it was.
    picker.pathCursor = std::min(picker.pathCursor, picker.path.size());

    const int inner = picker.inner;
    const auto total = static_cast<int>(picker.entries.size());

    // Which label the frame carries. A query that matches nothing turns red and
    // leaves the cursor where it was, as it does on the area list: erasing it
    // takes the user back to what was still matching.
    std::string label = " Import file ";
    theme::Color tint = theme::palette.tableHeader;
    if (!picker.search.empty()) {
        // "▌" stands in for the cursor: the terminal's own is hidden for the
        // whole application, and an input line without one reads as a label.
        label = " File: " + picker.search + "▌ ";
        tint = findByPrefix(picker.entries, picker.search) ? theme::palette.tableHeader
                                                           : theme::palette.error;
    }

    Elements lines{dialog::titleBar(label, inner, tint)};

    // The path, as a box that is typed into: it says where the listing is from
    // and takes a path to go to, and Enter on it decides which of the two it was
    // given. On the same fill the compose screen's header fields wear, so that a
    // row asking for something looks like one before anything is typed into it.
    const bool onPath = picker.focus == Focus::Path;
    picker.pathBox = Box::Nowhere();
    lines.push_back(dialog::framed(
        inputField(picker.path, picker.pathCursor, inner, onPath,
                   onPath ? theme::palette.selectionText : theme::palette.header,
                   &picker.pathOrigin) |
        bgcolor(onPath ? theme::palette.selection : theme::palette.inputField) |
        reflect(picker.pathBox)));
    lines.push_back(dialog::divider(inner));

    // The room is reserved first: the boxes are written into while the frame is
    // laid out, and a vector that grew under them would leave the earlier rows
    // pointing at freed memory.
    picker.rowBoxes.clear();
    picker.rowBoxes.reserve(static_cast<size_t>(picker.rows));

    const int room = std::max(1, inner - (2 * kMargin));
    for (int i = 0; i < picker.rows; ++i) {
        const int index = picker.offset + i;
        if (index >= total) {
            lines.push_back(dialog::line("", inner, theme::palette.text));
            continue;
        }
        const auto& entry = picker.entries[static_cast<size_t>(index)];
        const std::string row = " " + rowText(state, entry, room);

        Element cell = text(padRight(truncateToWidth(row, inner), inner));
        if (index == picker.cursor && picker.focus == Focus::Files) {
            cell = std::move(cell) | bold | color(theme::palette.selectionText) |
                   bgcolor(theme::palette.selection);
        } else if (index == picker.cursor) {
            // The cursor is still on this row while the typing is elsewhere in
            // the box — Enter reads what it names — so the row says so quietly
            // rather than wearing the fill of a list being walked.
            cell = std::move(cell) | bold | color(theme::palette.header);
        } else {
            cell = std::move(cell) |
                   color(entry.directory ? theme::palette.header : theme::palette.text);
        }

        picker.rowBoxes.push_back({index, {}});
        lines.push_back(
            dialog::framed(std::move(cell) | reflect(picker.rowBoxes.back().box)));
    }
    lines.push_back(dialog::divider(inner));

    // How the file is to go in. A text file is decoded out of the charset the
    // locale names — what a file on this machine is written in — so there is
    // nothing under this row left to ask.
    picker.textModeBox = Box::Nowhere();
    picker.uueModeBox = Box::Nowhere();
    const bool onMode = picker.focus == Focus::Mode;

    const int modeUsed = displayWidth(kModeLabel) + displayWidth(kModeGap) +
                         markWidth("Text") + markWidth("UUE");
    lines.push_back(dialog::framed(hbox(
        {text(kModeLabel) | color(theme::palette.header),
         modeMark("Text", state.importMode == app::ImportMode::Text, onMode) |
             reflect(picker.textModeBox),
         text(kModeGap),
         modeMark("UUE", state.importMode == app::ImportMode::Uue, onMode) |
             reflect(picker.uueModeBox),
         text(std::string(static_cast<size_t>(std::max(0, inner - modeUsed)), ' '))})));

    // The keys, and — in their place where there is one — what went wrong with
    // the last attempt: the dialog is still up, and the answer that caused it is
    // on the screen to be corrected.
    lines.push_back(dialog::bottomBar(kHint, picker.error, inner));

    // clear_under wipes the screen behind the box, so the message underneath
    // does not show through it.
    return dbox({std::move(background), vbox(std::move(lines)) | clear_under | center});
}

Outcome handleEvent(AppState& state, const Event& event) {
    Picker& picker = *state.importPicker;

    if (const auto click = leftClick(event)) return handleClick(state, picker, *click);

    // The ring, and the two keys that end the dialog one way or the other. They
    // mean the same thing wherever the typing is, so they are answered before
    // the stop it is on gets a look.
    if (event == Event::Tab) {
        focusAfter(picker, 1);
        return Outcome::Ignored;
    }
    if (event == Event::TabReverse) {
        focusAfter(picker, -1);
        return Outcome::Ignored;
    }
    if (event == Event::Return) {
        return picker.focus == Focus::Path ? goToPath(state, picker)
                                           : runImport(state, picker);
    }
    if (event == Event::Escape) {
        // Two things Esc puts away before it puts the dialog away, both of them
        // something half typed: a path that has not been gone to, and a search
        // in the list. Either is what the key most plainly means where it is.
        if (picker.focus == Focus::Path && picker.path != state.importDirectory) {
            picker.path = state.importDirectory;
            picker.pathCursor = picker.path.size();
            return Outcome::Ignored;
        }
        if (picker.focus == Focus::Files && !picker.search.empty()) {
            picker.search.clear();
            return Outcome::Ignored;
        }
        state.importPicker.reset();
        return Outcome::Dismissed;
    }

    switch (picker.focus) {
        case Focus::Path: return pathKey(picker, event);
        case Focus::Mode: return modeKey(state, picker, event);
        case Focus::Files: break;
    }
    return filesKey(state, picker, event);
}

}  // namespace amberedit::ui::import_dialog
