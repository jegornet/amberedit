#include "ui/export_dialog.hpp"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "app/export_file.hpp"
#include "config/text_util.hpp"
#include "i18n/i18n.hpp"
#include "ui/dialog_frame.hpp"
#include "ui/dir_listing.hpp"
#include "ui/event_util.hpp"
#include "ui/input_field.hpp"
#include "ui/list_page.hpp"
#include "ui/message_marks.hpp"
#include "ui/quick_search.hpp"
#include "ui/term/utf8.hpp"
#include "ui/text_layout.hpp"
#include "ui/theme.hpp"

namespace amberedit::ui::export_dialog {

using namespace term;

namespace {

namespace fs = std::filesystem;

using Picker = AppState::ExportPicker;
using Focus = AppState::ExportPicker::Focus;
using Answer = AppState::ExportPicker::Existing::Answer;

/// The box's width, and the narrowest it will be squeezed to — the import
/// dialog's, so that the two boxes stand the same size in the same window.
constexpr int kInnerWidth = 56;
constexpr int kMinInner = 40;
/// A column of margin at each side of a row.
constexpr int kMargin = 1;
/// The frame itself, a column on each side.
constexpr int kFrame = 2;

/// Rows that are not the listing: the title, the path, the two rules, the name
/// and the bottom of the frame — the keys stand in that frame rather than on a
/// row of their own.
constexpr int kChromeRows = 6;
/// And the rows the box keeps clear of the top and bottom of the window.
constexpr int kWindowMargin = 2;

/// The widest the name of a file already there is drawn in the question about
/// it. It is being shown rather than typed into, and the box is the question's
/// own rather than the export box's.
constexpr int kExistingNameWidth = 40;

/// How many names the box lists where it is the files it is writing. Past that
/// the last row counts what is left: the block stands where one row held a name
/// to type, and a box that grew a row per file would walk off a short window.
constexpr int kMaxNameRows = 5;

/// What the bottom rule says the keys do.
///
/// Functions and not constants: what each of them says is the interface's own
/// language, and a catalog is read at startup rather than compiled in.
const char* hint() {
    return _("Enter write · Tab move · Esc close");
}
const char* filesHint() {
    return _("Enter save · Tab move · Esc close");
}
const char* nameLabel() {
    return _(" File: ");
}
const char* filesLabel() {
    return _(" Files: ");
}
/// The button that writes them, which is what the ring stops on where a text
/// export stops on the box its name is typed into. The bare word: the spaces
/// either side of it, or the frame in their place, are `dialog::button()`'s.
std::string saveLabel() {
    return _("Save");
}
/// What a path that is not there is answered with.
const char* notFound() {
    return _("Path not found");
}
/// And what a name that is no name at all is.
const char* noName() {
    return _("No file name");
}

/// Whether the box is writing the files the message carries rather than the
/// message itself — which is what it was opened with and never changes while it
/// stands.
bool writingFiles(const Picker& picker) {
    return picker.mode == Picker::Mode::Uue;
}

/// Rows the names stand on: a row per file, the last of them counting whatever
/// is left over. Nothing in text mode, whose one row is the name box below.
int nameRows(const Picker& picker) {
    if (!writingFiles(picker)) return 0;
    return std::min(static_cast<int>(picker.files.size()), kMaxNameRows);
}

/// Rows between the last rule and the bottom of the frame: the box a name is
/// typed into, or the names listed and the button that writes them.
///
/// `tall` is `dialog_tall_buttons`, and only the files mode reads it — the one
/// row a text export keeps here is the box its name is typed into, which is a
/// field and not a button.
int belowRows(const Picker& picker, bool tall) {
    return writingFiles(picker) ? nameRows(picker) + dialog::buttonRows(tall) : 1;
}

/// Settles how big the box is, once — and again only where the window has
/// changed size. The import dialog's rule and the same reason for it: a box
/// measured against the directory it shows would stand a different size in
/// every one of them. The names are not the directory: they are as many as the
/// message carries and do not change while the box is up, so the listing gives
/// up the rows they take.
void fitBox(const AppState& state, Picker& picker) {
    if (picker.layoutWidth == state.width && picker.layoutHeight == state.height) return;
    picker.layoutWidth = state.width;
    picker.layoutHeight = state.height;
    picker.inner = std::min(kInnerWidth, std::max(kMinInner, state.width - kFrame));
    // The window is the whole of the cache key, `dialog_tall_buttons` included:
    // the setting is read off a config that does not change while the program
    // runs, and where it is `when_narrow` or `when_wide` it is the width above
    // that answers it.
    picker.rows = std::max(1, state.height - kChromeRows - kWindowMargin -
                                  (belowRows(picker, state.dialogTallButtons()) - 1));
}

/// The row as it is written. Everything in the listing is a directory, so the
/// slash is the whole of what a row says beyond its name.
std::string nameOf(const DirEntry& entry) {
    return entry.name == ".." ? entry.name : entry.name + "/";
}

/// The listing, which is directories and nothing else: what is being picked
/// here is somewhere to write, and the files standing in a directory are only
/// something to point at by mistake. The name to write under is typed, not
/// pointed at — a file that is already there is added to rather than replaced,
/// so picking one off a list would say the wrong thing about what happens next.
void readInto(AppState& state, Picker& picker) {
    picker.cursor = 0;
    picker.offset = 0;
    picker.search.clear();
    picker.path = state.exportDirectory;
    picker.pathCursor = picker.path.size();
    picker.entries =
        ui::readDirectory(state.exportDirectory, /*directoriesOnly=*/true, picker.error);
}

void enterDirectory(AppState& state, Picker& picker, const std::string& name) {
    const fs::path here(state.exportDirectory);
    const fs::path next = name == ".." ? here.parent_path() : here / name;
    state.exportDirectory = next.lexically_normal().string();
    readInto(state, picker);
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

std::optional<int> findByPrefix(const std::vector<DirEntry>& entries,
                                const std::string& query) {
    if (query.empty()) return std::nullopt;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (startsWithIgnoreCase(entries[i].name, query)) return static_cast<int>(i);
    }
    return std::nullopt;
}

/// The printable ASCII an event types, if it types any — what the quick search
/// of the listing takes.
std::optional<char> typedAscii(const Event& event) {
    if (!event.is_character() || event.input().size() != 1) return std::nullopt;
    if (event.ctrl() || event.alt()) return std::nullopt;
    const auto c = static_cast<unsigned char>(event.input()[0]);
    if (c <= ' ' || c > '~') return std::nullopt;
    return static_cast<char>(c);
}

/// What an event types into the path or the name, which is anything a filename
/// can hold: a name is whatever the filesystem will take, and Cyrillic names are
/// as ordinary as spaces in them.
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
    constexpr Focus kRing[] = {Focus::Path, Focus::Files, Focus::Name};
    constexpr int kStops = static_cast<int>(sizeof(kRing) / sizeof(kRing[0]));

    const auto* at = std::find(std::begin(kRing), std::end(kRing), picker.focus);
    const auto index =
        static_cast<int>(at == std::end(kRing) ? 0 : at - std::begin(kRing));
    picker.focus =
        kRing[static_cast<size_t>(((index + step) % kStops + kStops) % kStops)];
}

/// Where the export leaves the dialog for the next one: the directory it wrote
/// into and the name it wrote under. The message after this one usually belongs
/// beside it, and appending is one of the two answers the question offers.
void rememberPath(AppState& state, const std::string& path) {
    const fs::path written(path);
    if (written.has_parent_path()) {
        state.exportDirectory = written.parent_path().string();
    }
    state.exportName = written.filename().string();
}

/// Writes the message to `path`, `how` saying what becomes of a file already
/// standing there — which is what the question above this one was asked for.
Outcome writeMessage(AppState& state, Picker& picker, const std::string& path,
                     app::ExportWrite how) {
    // What the message is written in: the charset the locale names, which is
    // what a file on this machine is read in. There is nothing to ask.
    const app::ExportRequest request{path, ensureUtf8Locale(),
                                     state.config.readerDateTimeFormat, how};

    const auto written_ = app::exportMessage(request, state.currentArea,
                                             *state.readHeader, *state.readBody);
    if (!written_) {
        picker.error = written_.error()->message();
        return Outcome::Ignored;
    }

    rememberPath(state, path);
    return Outcome::Written;
}

/// The same for the marked messages: all of them into the one file, in the order
/// they stand in the area.
///
/// `how` is the **first** one's — what the question about a file already there
/// was answered with — and every one after it is appended, since each writing
/// afresh would leave the file holding the last message alone. The rule
/// `exportMessage()` puts at the head of each is what keeps them apart.
///
/// A write that fails stops the run and says so, with what went in before it
/// left in the file: there is nothing here that could put a file back the way it
/// was, and the honest thing is to name the failure rather than to go on writing
/// into a file that is already wrong.
Outcome writeMarked(AppState& state, Picker& picker, const std::string& path,
                    app::ExportWrite how) {
    if (state.base == nullptr) return Outcome::Ignored;

    // The numbers before anything is written: nothing here touches the base, but
    // gathering once is what makes the run the set the user was shown.
    std::vector<uint32_t> numbers;
    for (uint32_t number = 1; number <= state.messageCount; ++number) {
        if (marks::isMarked(state, number)) numbers.push_back(number);
    }
    if (numbers.empty()) return Outcome::Ignored;

    app::ExportWrite next = how;
    for (const uint32_t number : numbers) {
        const app::ExportRequest request{path, ensureUtf8Locale(),
                                         state.config.readerDateTimeFormat, next};
        const auto written_ =
            app::exportMessage(request, state.currentArea, state.base->header(number),
                               state.base->body(number));
        if (!written_) {
            picker.error = written_.error()->message();
            return Outcome::Ignored;
        }
        next = app::ExportWrite::Append;
    }

    rememberPath(state, path);
    return Outcome::Written;
}

/// Whichever of the two the box was opened for.
Outcome writeExport(AppState& state, Picker& picker, const std::string& path,
                    app::ExportWrite how) {
    return picker.marked ? writeMarked(state, picker, path, how)
                         : writeMessage(state, picker, path, how);
}

/// Writes the message where the two boxes between them say.
///
/// A name with a directory in it is taken as it is written — typing
/// `../keep/it.txt` into the name box is a plain enough thing to mean — so the
/// two are joined the one way a path is joined and what comes out is where the
/// file goes.
///
/// **A file already standing under that name is a question and not an answer.**
/// It used to be appended to without asking, which is right for collecting one
/// message after another into a digest and quietly wrong for every other reason
/// a name is typed twice — and writing over is wrong exactly the other way
/// round. So the write stops here and the box asks, and it is the answer that
/// comes back through `writeMessage()`.
Outcome runExport(AppState& state, Picker& picker) {
    if (!state.readHeader || !state.readBody) return Outcome::Ignored;

    if (writingFiles(picker)) {
        // Whole files under the names the message gave them, into the directory
        // the listing is standing in — there is nothing else to say, which is
        // why nothing under the list is typed into.
        const auto saved = app::saveUueFiles(state.exportDirectory, picker.files);
        if (!saved) {
            picker.error = saved.error()->message();
            return Outcome::Ignored;
        }
        // The directory stays for the next export, exactly as a text one leaves
        // it. `exportName` is not touched: it is the name a *message* was
        // written under, and these names were never the user's to begin with.
        return Outcome::Written;
    }

    const std::string name(config::text::trim(state.exportName));
    if (name.empty()) {
        picker.error = noName();
        return Outcome::Ignored;
    }

    const std::string path = resolvePath(state.exportDirectory, name);

    std::error_code ec;
    if (fs::exists(path, ec)) {
        // The error goes with it: what the box has to say now is the question,
        // and the bottom rule is where the question's own answers stand.
        picker.error.clear();
        Picker::Existing existing;
        existing.path = path;
        picker.existing = std::move(existing);
        return Outcome::Ignored;
    }
    return writeExport(state, picker, path, app::ExportWrite::Append);
}

/// What Enter on the path box does: walk into what it names, or say it is not
/// there. A file named there is not opened — this dialog writes — so it is taken
/// as the name to write under and the box goes back to saying where.
Outcome goToPath(AppState& state, Picker& picker) {
    const fs::path wanted = resolvePath(state.exportDirectory, picker.path);

    std::error_code ec;
    const fs::file_status status = fs::status(wanted, ec);
    if (!ec && fs::is_directory(status)) {
        state.exportDirectory = wanted.string();
        readInto(state, picker);
        // A directory asked for by name is one to write into, so the typing
        // goes to the name — which is all that is left to say.
        picker.focus = Focus::Name;
        return Outcome::Ignored;
    }

    // Not a directory, and its parent is: the box was given a file to write, so
    // that is what it becomes. This is the one way to say "there, under that
    // name" in a single line — and it says nothing at all where the names are
    // the message's, since there is no name here to be given.
    const fs::path parent = wanted.parent_path();
    std::error_code parentEc;
    if (!writingFiles(picker) && !wanted.filename().empty() &&
        fs::is_directory(fs::status(parent, parentEc))) {
        state.exportDirectory = parent.string();
        state.exportName = wanted.filename().string();
        readInto(state, picker);
        picker.focus = Focus::Name;
        return Outcome::Ignored;
    }

    picker.error = notFound();
    return Outcome::Ignored;
}

// --- drawing -----------------------------------------------------------------

/// One line of text as a box that is typed into, on the fill the compose
/// screen's header fields wear so that a row asking for something looks like one
/// before anything is in it.
Element box(const std::string& value, size_t cursor, int width, bool focused,
            size_t* origin, term::Box& where) {
    return inputField(value, cursor, width, focused,
                      focused ? theme::palette.selectionText : theme::palette.dialogLabel,
                      fieldFiller(theme::palette.dialogHint), origin) |
           bgcolor(focused ? theme::palette.selection : theme::palette.dialogField) |
           reflect(where);
}

/// The question a file already there raises, drawn over the box that raised it:
/// the export dialog is still the screen, and the name being asked about is
/// standing in it.
Element existingQuestion(AppState& state, Picker& picker) {
    Picker::Existing& existing = *picker.existing;

    const bool tall = state.dialogTallButtons();
    const auto answer = [&](const std::string& label, Answer which, term::Box& where) {
        // reflect() writes back where the button landed once the box has been
        // centred, which is what handleEvent() hit-tests a click on.
        return dialog::button(label, existing.answer == which,
                              state.isPressed(AppState::Pressed::ExistingChoice,
                                              static_cast<uint32_t>(which)),
                              tall) |
               reflect(where);
    };

    auto content = vbox({
        text(_("File exists:")) | bold | color(theme::palette.dialogText),
        text("  " + truncateToWidth(fs::path(existing.path).filename().string(),
                                    kExistingNameWidth)) |
            color(theme::palette.dialogLabel),
        text(""),
        hbox({
            answer(_("Overwrite"), Answer::Overwrite, existing.overwriteBox),
            text("   "),
            answer(_("Append"), Answer::Append, existing.appendBox),
        }) | center,
        text(""),
        text(_("←→ choose · Enter confirm · o/a · Esc cancel")) |
            color(theme::palette.dialogHint),
    });

    // The frame is drawn round a padded box: without the margins the hint line
    // sets the width and ends up flush against the border.
    return hbox({text("  "), std::move(content), text("  ")}) | border |
           color(theme::palette.dialogBorder);
}

// --- events ------------------------------------------------------------------

/// Which of the two the question is standing on, `step` forwards or back.
void stepAnswer(Picker::Existing& existing) {
    existing.answer =
        existing.answer == Answer::Append ? Answer::Overwrite : Answer::Append;
}

/// Writes the message the way the question was answered, and puts the question
/// away — the write it interrupted, finished.
Outcome answerExisting(AppState& state, Picker& picker, Answer answer) {
    // Copied out before the question is put away, which is what holds it.
    const std::string path = picker.existing->path;
    picker.existing.reset();
    return writeExport(state, picker, path,
                       answer == Answer::Overwrite ? app::ExportWrite::Overwrite
                                                   : app::ExportWrite::Append);
}

/// Everything while the question is up: it is modal over the box the way the
/// box is modal over the reader, so nothing reaches the boxes underneath.
Outcome existingKey(AppState& state, Picker& picker, const Event& event) {
    Picker::Existing& existing = *picker.existing;

    // A click answers with the button it landed on, without selecting it first:
    // pointing at Append and pressing is one gesture, not two.
    if (const auto click = leftClick(event)) {
        const auto pick = [&](Answer answer) {
            existing.answer = answer;
            state.showClick(AppState::Pressed::ExistingChoice,
                            static_cast<uint32_t>(answer));
            return answerExisting(state, picker, answer);
        };
        if (existing.overwriteBox.Contain(click->x, click->y)) {
            return pick(Answer::Overwrite);
        }
        if (existing.appendBox.Contain(click->x, click->y)) return pick(Answer::Append);
        // Anywhere else, inside the box or outside it: swallowed, as every other
        // event is while the question is up.
        return Outcome::Ignored;
    }

    // The initials answer outright, the way y and n answer a confirmation.
    if (event == Event::Character('o') || event == Event::Character('O')) {
        return answerExisting(state, picker, Answer::Overwrite);
    }
    if (event == Event::Character('a') || event == Event::Character('A')) {
        return answerExisting(state, picker, Answer::Append);
    }
    if (event == Event::ArrowLeft || event == Event::ArrowRight || event == Event::Tab ||
        event == Event::TabReverse) {
        stepAnswer(existing);
        return Outcome::Ignored;
    }
    if (event == Event::Return) return answerExisting(state, picker, existing.answer);
    if (event == Event::Escape || event == Event::Backspace) {
        // Neither, and the export dialog is left exactly as it was: the name is
        // still in its box, to be typed over or written under after all.
        picker.existing.reset();
        return Outcome::Ignored;
    }
    return Outcome::Ignored;
}

Outcome handleClick(AppState& state, Picker& picker, const MouseEvent& click) {
    int hit = -1;
    for (const auto& row : picker.rowBoxes) {
        if (row.box.Contain(click.x, click.y)) hit = row.index;
    }
    if (hit >= 0) {
        // The row is walked into rather than written to: everything in this
        // listing is a directory.
        picker.cursor = hit;
        picker.focus = Focus::Files;
        picker.search.clear();
        clampCursor(picker);
        state.showClick();
        const DirEntry entry = picker.entries[static_cast<size_t>(picker.cursor)];
        enterDirectory(state, picker, entry.name);
        return Outcome::Ignored;
    }
    if (picker.pathBox.Contain(click.x, click.y)) {
        picker.focus = Focus::Path;
        picker.pathCursor = offsetAtColumn(picker.path, picker.pathOrigin,
                                           click.x - picker.pathBox.x_min);
        return Outcome::Ignored;
    }
    if (picker.nameBox.Contain(click.x, click.y)) {
        picker.focus = Focus::Name;
        if (writingFiles(picker)) {
            // A button pointed at is a button pressed: there is nothing to put a
            // cursor into there, and the Save under the pointer is the whole of
            // what is left to say.
            state.showClick(AppState::Pressed::ExportSave);
            return runExport(state, picker);
        }
        // Where in the name the pointer landed, the field being drawn scrolled
        // where what is typed outgrows it.
        picker.nameCursor = offsetAtColumn(state.exportName, picker.nameOrigin,
                                           click.x - picker.nameBox.x_min);
        return Outcome::Ignored;
    }
    // Anywhere else, inside the box or outside it: swallowed, as every other
    // event is while the dialog is modal.
    return Outcome::Ignored;
}

/// The keys a box that is typed into answers, whichever of the two it is.
/// False is a key the box does not bind, which the dialog then goes on to
/// answer for itself.
bool fieldKey(std::string& value, size_t& cursor, const Event& event) {
    cursor = std::min(cursor, value.size());

    if (const auto typed = typedText(event)) {
        value.insert(cursor, *typed);
        cursor += typed->size();
        return true;
    }
    if (event == Event::Backspace) {
        const size_t from = prevChar(value, cursor);
        value.erase(from, cursor - from);
        cursor = from;
        return true;
    }
    if (event == Event::Delete) {
        value.erase(cursor, charLen(value, cursor));
        return true;
    }
    if (event == Event::ArrowLeft) {
        cursor = prevChar(value, cursor);
        return true;
    }
    if (event == Event::ArrowRight) {
        cursor = std::min(value.size(), cursor + charLen(value, cursor));
        return true;
    }
    if (event == Event::Home) {
        cursor = 0;
        return true;
    }
    if (event == Event::End) {
        cursor = value.size();
        return true;
    }
    return false;
}

Outcome filesKey(AppState& state, Picker& picker, const Event& event) {
    const auto total = static_cast<int>(picker.entries.size());

    if (const auto typed = typedAscii(event)) {
        picker.search += *typed;
        if (const auto match = findByPrefix(picker.entries, picker.search)) {
            picker.cursor = *match;
            clampCursor(picker);
        }
        return Outcome::Ignored;
    }
    if (event == Event::Backspace && !picker.search.empty()) {
        picker.search.pop_back();
        if (const auto match = findByPrefix(picker.entries, picker.search)) {
            picker.cursor = *match;
            clampCursor(picker);
        }
        return Outcome::Ignored;
    }
    // Every other key ends the search — once the cursor is being moved by hand
    // the query has said what it had to say.
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
    // Into what the cursor names, and out of the directory — the two arrows a
    // listing is walked with everywhere else in the interface.
    if (event == Event::ArrowRight && total > 0) {
        const DirEntry entry = picker.entries[static_cast<size_t>(picker.cursor)];
        enterDirectory(state, picker, entry.name);
        return Outcome::Ignored;
    }
    if (event == Event::ArrowLeft || event == Event::Backspace) {
        const fs::path here(state.exportDirectory);
        if (here.parent_path() != here) enterDirectory(state, picker, "..");
        return Outcome::Ignored;
    }
    return Outcome::Ignored;
}

}  // namespace

void open(AppState& state, std::vector<app::UueFile> files, bool marked) {
    // Nothing to write: an empty area opens the reader on blank rows, and the
    // button for this is drawn dimmed there.
    if (!state.readHeader || !state.readBody) return;

    if (state.exportDirectory.empty()) {
        std::error_code ec;
        const fs::path here = fs::current_path(ec);
        state.exportDirectory = ec ? std::string("/") : here.string();
    }
    // Nothing is put in the name box: it holds what the last export was called,
    // and nothing at all until one has been. A name made up from the message
    // would be right for that message and quietly wrong for the next, and a file
    // written under it is not something the user asked for.

    Picker picker;
    picker.mode = files.empty() ? Picker::Mode::Text : Picker::Mode::Uue;
    picker.files = std::move(files);
    // Only a text export can be about a set: the files were decoded out of the
    // message on screen, and there is no set of them to write.
    picker.marked = marked && picker.mode == Picker::Mode::Text;
    picker.nameCursor = state.exportName.size();
    state.exportPicker = std::move(picker);
    fitBox(state, *state.exportPicker);
    readInto(state, *state.exportPicker);
}

Element render(AppState& state, Element background) {
    Picker& picker = *state.exportPicker;
    fitBox(state, picker);
    clampCursor(picker);
    picker.pathCursor = std::min(picker.pathCursor, picker.path.size());
    picker.nameCursor = std::min(picker.nameCursor, state.exportName.size());

    const int inner = picker.inner;
    const auto total = static_cast<int>(picker.entries.size());

    std::string label = writingFiles(picker) ? _(" Export files ")
                        : picker.marked      ? _(" Export marked ")
                                             : _(" Export message ");
    theme::Color tint = theme::palette.dialogTitle;
    if (!picker.search.empty()) {
        // "▌" stands in for the cursor: the terminal's own is hidden for the
        // whole application, and an input line without one reads as a label.
        label = i18n::format(_(" Dir: {0}▌ "), {picker.search});
        tint = findByPrefix(picker.entries, picker.search) ? theme::palette.dialogTitle
                                                           : theme::palette.error;
    }

    Elements lines{dialog::titleBar(label, inner, tint)};
    picker.pathBox = Box::Nowhere();
    lines.push_back(dialog::framed(box(picker.path, picker.pathCursor, inner,
                                       picker.focus == Focus::Path, &picker.pathOrigin,
                                       picker.pathBox)));
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
            lines.push_back(dialog::line("", inner, theme::palette.dialogText));
            continue;
        }
        const auto& entry = picker.entries[static_cast<size_t>(index)];
        const std::string row =
            " " + padRight(truncateToWidth(nameOf(entry), room), room);

        Element cell = text(padRight(truncateToWidth(row, inner), inner));
        if (index == picker.cursor && picker.focus == Focus::Files) {
            cell = std::move(cell) | bold | color(theme::palette.selectionText) |
                   bgcolor(theme::palette.selection);
        } else if (index == picker.cursor) {
            // The cursor is still on this row while the typing is elsewhere in
            // the box, so the row says so quietly rather than wearing the fill
            // of a list being walked.
            cell = std::move(cell) | bold | color(theme::palette.dialogLabel);
        } else {
            cell = std::move(cell) | color(theme::palette.dialogLabel);
        }

        picker.rowBoxes.push_back({index, {}});
        lines.push_back(
            dialog::framed(std::move(cell) | reflect(picker.rowBoxes.back().box)));
    }
    lines.push_back(dialog::divider(inner));

    picker.nameBox = Box::Nowhere();
    if (writingFiles(picker)) {
        // What the message carries, **a label and nothing more**: the names are
        // the message's rather than the user's, there is nothing to type over
        // them and nothing to pick among them, so the ring does not stop here
        // and no fill says it might. What acts is the button under it.
        const auto shown = static_cast<size_t>(nameRows(picker));
        const auto labelWidth = static_cast<size_t>(displayWidth(filesLabel()));
        const int nameRoom = std::max(1, inner - static_cast<int>(labelWidth));
        // The label stands against the first row only, the rest lining up under
        // it: one row is one file, and a label repeated down the block would be
        // saying the same thing five times.
        const std::string blank(labelWidth, ' ');
        for (size_t i = 0; i < shown; ++i) {
            const bool counting = i + 1 == shown && picker.files.size() > shown;
            const std::string name =
                counting ? i18n::format(_("… and {0} more"),
                                        {std::to_string(picker.files.size() - shown + 1)})
                         : picker.files[i].name;
            lines.push_back(
                dialog::framed(text((i == 0 ? std::string(filesLabel()) : blank) +
                                    padRight(truncateToWidth(name, nameRoom), nameRoom)) |
                               color(theme::palette.dialogLabel)));
        }

        // The button that writes them, which is the ring's third stop where a
        // text export has its name box: something has to take the Enter, and a
        // label cannot.
        const bool tall = state.dialogTallButtons();
        Element save = dialog::button(saveLabel(), picker.focus == Focus::Name,
                                      state.isPressed(AppState::Pressed::ExportSave),
                                      tall);

        // Centred by measuring rather than by a filler: a row of this box is as
        // wide as it is written, and a row narrower than the rest would take the
        // frame in with it. The spacers either side are one row of the button's
        // own height — blanks, which the dialog's fill has already painted.
        const int spare = std::max(0, inner - dialog::buttonWidth(saveLabel(), tall));
        lines.push_back(dialog::framed(
            hbox({text(std::string(static_cast<size_t>(spare / 2), ' ')),
                  std::move(save) | reflect(picker.nameBox),
                  text(std::string(static_cast<size_t>(spare - (spare / 2)), ' '))}),
            dialog::buttonRows(tall)));
    } else {
        // The name it goes under. A file already there is added to, not written
        // over, which is why this is typed rather than picked off the list above.
        const int nameWidth = std::max(1, inner - displayWidth(nameLabel()));
        lines.push_back(dialog::framed(hbox(
            {text(nameLabel()) | color(theme::palette.dialogLabel),
             box(state.exportName, picker.nameCursor, nameWidth,
                 picker.focus == Focus::Name, &picker.nameOrigin, picker.nameBox)})));
    }

    lines.push_back(dialog::bottomBar(writingFiles(picker) ? filesHint() : hint(),
                                      picker.error, inner));

    // dialog::surface() wipes the screen behind the box and lays the dialog's
    // own fill down in its place, so the message underneath neither shows
    // through it nor colors it.
    Element drawn =
        dbox({std::move(background), dialog::surface(vbox(std::move(lines))) | center});
    if (picker.existing) {
        // Over the box rather than in its place: what is being asked about is
        // the name standing in it.
        drawn = dbox({std::move(drawn),
                      dialog::surface(existingQuestion(state, picker)) | center});
    }
    return drawn;
}

Outcome handleEvent(AppState& state, const Event& event) {
    Picker& picker = *state.exportPicker;

    // The question about a file already there takes every key while it is up:
    // the write it interrupted is the only thing either answer is about, and
    // the boxes underneath have nothing left to say until it is answered.
    if (picker.existing) return existingKey(state, picker, event);

    if (const auto click = leftClick(event)) return handleClick(state, picker, *click);

    if (event == Event::Tab) {
        focusAfter(picker, 1);
        return Outcome::Ignored;
    }
    if (event == Event::TabReverse) {
        focusAfter(picker, -1);
        return Outcome::Ignored;
    }
    if (event == Event::Return) {
        // On the path box Enter is about where; on a directory row it is the
        // way into it; anywhere else there is nothing left to say and the
        // message is written.
        if (picker.focus == Focus::Path) return goToPath(state, picker);
        if (picker.focus == Focus::Files && !picker.entries.empty()) {
            clampCursor(picker);
            const DirEntry entry = picker.entries[static_cast<size_t>(picker.cursor)];
            enterDirectory(state, picker, entry.name);
            return Outcome::Ignored;
        }
        return runExport(state, picker);
    }
    if (event == Event::Escape) {
        // A path typed and not gone to, and a search in the list, are both put
        // away before the dialog is: either is what the key most plainly means
        // where it stands.
        if (picker.focus == Focus::Path && picker.path != state.exportDirectory) {
            picker.path = state.exportDirectory;
            picker.pathCursor = picker.path.size();
            return Outcome::Ignored;
        }
        if (picker.focus == Focus::Files && !picker.search.empty()) {
            picker.search.clear();
            return Outcome::Ignored;
        }
        state.exportPicker.reset();
        return Outcome::Dismissed;
    }

    switch (picker.focus) {
        case Focus::Path:
            if (fieldKey(picker.path, picker.pathCursor, event)) return Outcome::Ignored;
            if (event == Event::ArrowDown) picker.focus = Focus::Files;
            return Outcome::Ignored;
        case Focus::Name:
            // The names of the files a message carries are shown and not typed
            // into, so every key that edits a box is swallowed there — the block
            // is a button, and Enter above is the whole of what it answers.
            if (!writingFiles(picker) &&
                fieldKey(state.exportName, picker.nameCursor, event)) {
                return Outcome::Ignored;
            }
            if (event == Event::ArrowUp) picker.focus = Focus::Files;
            return Outcome::Ignored;
        case Focus::Files: break;
    }
    return filesKey(state, picker, event);
}

}  // namespace amberedit::ui::export_dialog
