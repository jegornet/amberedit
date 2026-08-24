#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "app/area_manager.hpp"
#include "app/import_file.hpp"
#include "config/app_config.hpp"
#include "domain/area.hpp"
#include "encoding/iconv_recoder.hpp"
#include "msgbase/null_lastread_store.hpp"
#include "ports/i_area_source.hpp"
#include "temp_dir.hpp"
#include "test_strings.hpp"
#include "ui/app_state.hpp"
#include "ui/import_dialog.hpp"
#include "ui/input_field.hpp"
#include "ui/screens/compose_screen.hpp"
#include "ui/term/event.hpp"
#include "ui/term/screen.hpp"
#include "ui/term/utf8.hpp"

using amberedit::app::ImportMode;
using amberedit::config::AppConfig;
using amberedit::domain::AreaConfig;
using amberedit::domain::AreaKind;
using amberedit::test::contains;
using amberedit::test::TempDir;
using amberedit::ui::AppState;
using amberedit::ui::term::Event;

namespace compose = amberedit::ui::screens::compose;
namespace import_dialog = amberedit::ui::import_dialog;

namespace {

/// The word the charset tests are written round, in UTF-8 as everything above
/// the adapter is.
const std::string kPrivet = "Привет";

Event clickAt(int x, int y) {
    amberedit::ui::term::MouseEvent mouse;
    mouse.button = amberedit::ui::term::MouseEvent::Button::Left;
    mouse.motion = amberedit::ui::term::MouseEvent::Motion::Pressed;
    mouse.x = x;
    mouse.y = y;
    return Event::Mouse(mouse);
}

class EmptyAreaSource final : public amberedit::ports::IAreaConfigSource {
public:
    tl::expected<std::vector<AreaConfig>, amberedit::ErrorPtr> loadAreas() override {
        return {};
    }
};

/// A message being written in one area, and a directory of files to read into
/// it. The dialog needs both: what it lists is the disk, and what it writes the
/// cut lines from is the config of the area the message is going into.
struct ImportFixture {
    ImportFixture()
        : manager(std::make_unique<EmptyAreaSource>(),
                  std::make_unique<amberedit::msgbase::NullLastReadStore>(), config),
          state(manager, config) {
        state.currentArea.tag = "test.area";
        state.currentArea.kind = AreaKind::Echo;
        // Where the dialog opens. Set before it is opened, which is the only
        // time it is read: afterwards it is the dialog's own to walk.
        state.importDirectory = dir.path("files");
        std::filesystem::create_directories(state.importDirectory);
    }

    void write(const std::string& name, const std::string& content) const {
        std::ofstream out((std::filesystem::path(state.importDirectory) / name).string(),
                          std::ios::binary);
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
    }

    void makeDirectory(const std::string& name) const {
        std::filesystem::create_directories(std::filesystem::path(state.importDirectory) /
                                            name);
    }

    /// Puts the cursor on the entry of that name, as walking to it with the
    /// arrows would. Fails the test where the listing has no such row.
    void select(const std::string& name) {
        auto& picker = *state.importPicker;
        const auto at = std::find_if(
            picker.entries.begin(), picker.entries.end(),
            [&name](const amberedit::ui::DirEntry& e) { return e.name == name; });
        REQUIRE(at != picker.entries.end());
        picker.cursor = static_cast<int>(at - picker.entries.begin());
    }

    [[nodiscard]] std::vector<std::string> names() const {
        std::vector<std::string> out;
        out.reserve(state.importPicker->entries.size());
        for (const auto& entry : state.importPicker->entries) out.push_back(entry.name);
        return out;
    }

    /// The dialog answered, and what the message is left holding once the shell
    /// has put what was read into it — the two halves the app_shell runs
    /// together.
    import_dialog::Outcome answer(const Event& event) {
        const auto outcome = import_dialog::handleEvent(state, event);
        if (outcome == import_dialog::Outcome::Imported) {
            const std::vector<std::string> lines = state.importPicker->lines;
            state.importPicker.reset();
            compose::insertImported(state, lines);
        }
        return outcome;
    }

    /// Types a path into the box, character by character as a user would —
    /// the field is cleared first, which End and a row of Backspaces would do.
    void typePath(const std::string& path) {
        state.importPicker->path.clear();
        state.importPicker->pathCursor = 0;
        for (size_t i = 0; i < path.size();) {
            const size_t length = amberedit::ui::charLen(path, i);
            answer(Event::Character(path.substr(i, length)));
            i += length;
        }
    }

    /// The frame as it lands on the screen: the rows it covers and the columns
    /// it spans, read off the corners it is drawn with.
    struct Frame {
        int top{-1};
        int bottom{-1};
        int left{-1};
        int right{-1};
        /// Where the top rule ends, which is where the bottom one has to.
        int topRight{-1};

        [[nodiscard]] int height() const { return bottom - top + 1; }
        [[nodiscard]] int width() const { return right - left + 1; }

        friend bool operator==(const Frame& a, const Frame& b) {
            return a.top == b.top && a.bottom == b.bottom && a.left == b.left &&
                   a.right == b.right;
        }
    };

    [[nodiscard]] Frame draw() {
        amberedit::ui::term::Screen screen(state.width, state.height);
        amberedit::ui::term::render(
            screen, import_dialog::render(state, amberedit::ui::term::text("")));

        Frame frame;
        for (int y = 0; y < state.height; ++y) {
            for (int x = 0; x < state.width; ++x) {
                const std::string glyph = screen.at(x, y).glyph;
                if (glyph == "╭") {
                    frame.top = y;
                    frame.left = x;
                } else if (glyph == "╮") {
                    frame.topRight = x;
                } else if (glyph == "╯") {
                    frame.bottom = y;
                    frame.right = x;
                }
            }
        }
        return frame;
    }

    /// What one row of the drawn frame says, the sides taken off.
    [[nodiscard]] std::string rowText(int y) {
        amberedit::ui::term::Screen screen(state.width, state.height);
        amberedit::ui::term::render(
            screen, import_dialog::render(state, amberedit::ui::term::text("")));
        std::string row;
        for (int x = 0; x < state.width; ++x) row += screen.at(x, y).glyph;
        return row;
    }

    TempDir dir;
    AppConfig config;
    amberedit::app::AreaManager manager;
    AppState state;
};

}  // namespace

TEST_CASE("The import dialog lists a directory [import_dialog]") {
    ImportFixture fixture;
    fixture.write("beta.txt", "b");
    fixture.write("Alpha.txt", "a");
    fixture.makeDirectory("sub");

    import_dialog::open(fixture.state);
    REQUIRE(fixture.state.importPicker);
    // The way back first, then the directories, then the files — each in ASCII
    // case order, since what is being looked for is a file and the way to it is
    // through the directories.
    CHECK(fixture.names() ==
          std::vector<std::string>{"..", "sub", "Alpha.txt", "beta.txt"});
    // And the mode a file is read in is text.
    CHECK(fixture.state.importMode == ImportMode::Text);
}

TEST_CASE("The import dialog walks in and out of a directory [import_dialog]") {
    ImportFixture fixture;
    fixture.makeDirectory("sub");
    const std::string top = fixture.state.importDirectory;

    import_dialog::open(fixture.state);
    fixture.select("sub");
    CHECK(fixture.answer(Event::Return) == import_dialog::Outcome::Ignored);
    CHECK(std::filesystem::path(fixture.state.importDirectory).filename() == "sub");

    // ← is the way back out, whatever row the cursor is on.
    CHECK(fixture.answer(Event::ArrowLeft) == import_dialog::Outcome::Ignored);
    CHECK(std::filesystem::path(fixture.state.importDirectory) ==
          std::filesystem::path(top).lexically_normal());
}

TEST_CASE("The import dialog reads a text file into the message [import_dialog]") {
    ImportFixture fixture;
    fixture.write("note.txt", "one\ntwo\n");
    fixture.state.edit.lines = {"first", "second"};
    fixture.state.edit.row = 0;
    fixture.state.edit.col = 0;

    import_dialog::open(fixture.state);
    fixture.select("note.txt");
    CHECK(fixture.answer(Event::Return) == import_dialog::Outcome::Imported);

    // The dialog is gone and the file is in the message, fenced off by the two
    // cut lines the config names.
    CHECK_FALSE(fixture.state.importPicker);
    CHECK(fixture.state.edit.lines ==
          std::vector<std::string>{"=== Cut ===", "one", "two", "=== Cut ===", "first",
                                   "second"});
    // Under the block, which is where the writing goes on from.
    CHECK(fixture.state.edit.row == 4);
    CHECK(fixture.state.edit.col == 0);
}

TEST_CASE("An import lands after the line the cursor is in [import_dialog]") {
    ImportFixture fixture;
    fixture.write("note.txt", "read me\n");
    fixture.state.edit.lines = {"half a sentence", "and the rest"};
    fixture.state.edit.row = 0;
    fixture.state.edit.col = 4;

    import_dialog::open(fixture.state);
    fixture.select("note.txt");
    REQUIRE(fixture.answer(Event::Return) == import_dialog::Outcome::Imported);

    // The line being written is left as it was written: a file is a block of
    // whole lines, and it goes under the one the cursor stands in.
    CHECK(fixture.state.edit.lines ==
          std::vector<std::string>{"half a sentence", "=== Cut ===", "read me",
                                   "=== Cut ===", "and the rest"});
    CHECK(fixture.state.edit.row == 4);
}

TEST_CASE(
    "An import at the end of the message keeps the cursor in it "
    "[import_dialog]") {
    ImportFixture fixture;
    fixture.write("note.txt", "last word\n");
    fixture.state.edit.lines = {"written"};
    fixture.state.edit.row = 0;
    fixture.state.edit.col = 7;

    import_dialog::open(fixture.state);
    fixture.select("note.txt");
    REQUIRE(fixture.answer(Event::Return) == import_dialog::Outcome::Imported);

    // There is no line under the block to stand on, so the cursor stays at the
    // end of its last one rather than past the end of the message.
    CHECK(fixture.state.edit.row == 3);
    CHECK(fixture.state.edit.col == fixture.state.edit.line().size());
}

TEST_CASE("The import dialog reads a file as UUE [import_dialog]") {
    ImportFixture fixture;
    fixture.write("blob.bin", "abc");
    fixture.state.edit.lines = {""};

    import_dialog::open(fixture.state);
    // Onto the mode and over to UUE, which is where Tab and → say it is.
    REQUIRE(fixture.answer(Event::Tab) == import_dialog::Outcome::Ignored);
    REQUIRE(fixture.answer(Event::ArrowRight) == import_dialog::Outcome::Ignored);
    CHECK(fixture.state.importMode == ImportMode::Uue);

    fixture.select("blob.bin");
    REQUIRE(fixture.answer(Event::Return) == import_dialog::Outcome::Imported);

    // The file's own begin/end and no cut lines: a fence round a uuencoded
    // block is one more line for whoever decodes it to trip over.
    CHECK(fixture.state.edit.lines ==
          std::vector<std::string>{"begin 644 blob.bin", "#86)C", "`", "end", ""});
}

TEST_CASE("The import dialog reads text in the locale's charset [import_dialog]") {
    ImportFixture fixture;
    // The file as this machine would hold it: whatever the locale is being
    // written in, which is the charset the dialog reads a text file out of.
    // There is nothing to choose and nothing to type — the box has no charset
    // in it.
    amberedit::encoding::IconvRecoder recoder;
    fixture.write("privet.txt",
                  recoder.fromUtf8(kPrivet, amberedit::ui::term::ensureUtf8Locale()));
    fixture.state.edit.lines = {""};

    import_dialog::open(fixture.state);
    fixture.select("privet.txt");
    REQUIRE(fixture.answer(Event::Return) == import_dialog::Outcome::Imported);
    // And it comes back UTF-8, as everything above the adapter is.
    CHECK(fixture.state.edit.lines[1] == kPrivet);
}

TEST_CASE("The import dialog says what it could not read [import_dialog]") {
    ImportFixture fixture;
    fixture.write("note.txt", "one\n");

    import_dialog::open(fixture.state);
    fixture.select("note.txt");
    // Gone between the listing and the keystroke, which is what a file that
    // will not open looks like from here — the listing is a picture of the
    // directory as it stood, not a lock on it.
    std::filesystem::remove(std::filesystem::path(fixture.state.importDirectory) /
                            "note.txt");
    CHECK(fixture.answer(Event::Return) == import_dialog::Outcome::Ignored);

    // The dialog stays up with the reason along its bottom frame.
    REQUIRE(fixture.state.importPicker);
    CHECK_MESSAGE(contains(fixture.state.importPicker->error, "note.txt"),
                  fixture.state.importPicker->error);
    // As much of it as the frame has room for — a path longer than the box is
    // cut at the right edge like every other thing that will not fit.
    const std::string row = fixture.rowText(fixture.draw().bottom);
    CHECK_MESSAGE(contains(row, "cannot open"), row);
}

TEST_CASE("The import dialog searches by name [import_dialog]") {
    ImportFixture fixture;
    fixture.write("alpha.txt", "a");
    fixture.write("beta.txt", "b");
    fixture.write("gamma.txt", "g");

    import_dialog::open(fixture.state);
    fixture.answer(Event::Character("g"));
    CHECK(fixture.state.importPicker->search == "g");
    CHECK(fixture.names()[static_cast<size_t>(fixture.state.importPicker->cursor)] ==
          "gamma.txt");

    // Esc closes the search rather than the dialog while one is being typed.
    fixture.answer(Event::Escape);
    REQUIRE(fixture.state.importPicker);
    CHECK(fixture.state.importPicker->search.empty());
    CHECK(fixture.answer(Event::Escape) == import_dialog::Outcome::Dismissed);
    CHECK_FALSE(fixture.state.importPicker);
}

TEST_CASE("The import dialog remembers where it was [import_dialog]") {
    ImportFixture fixture;
    fixture.makeDirectory("sub");

    import_dialog::open(fixture.state);
    fixture.select("sub");
    fixture.answer(Event::Return);
    const std::string inside = fixture.state.importDirectory;
    fixture.answer(Event::Escape);

    // A file picker that opened where it was started every time would be asking
    // again for an answer already given.
    import_dialog::open(fixture.state);
    CHECK(fixture.state.importDirectory == inside);
}

TEST_CASE("The import dialog is drawn where it says it is [import_dialog]") {
    ImportFixture fixture;
    fixture.write("note.txt", "one\n");

    import_dialog::open(fixture.state);
    amberedit::ui::term::Screen screen(fixture.state.width, fixture.state.height);
    amberedit::ui::term::render(
        screen, import_dialog::render(fixture.state, amberedit::ui::term::text("")));

    // Every row, the path box and both mode markers come back with where they
    // landed, which is what a click is answered against. There is one row box
    // per row the box was fixed at, whatever the directory holds.
    auto& picker = *fixture.state.importPicker;
    // One row box per row that names a file; the blank rows under the end of a
    // short listing answer no click at all.
    CHECK(picker.rowBoxes.size() == picker.entries.size());
    CHECK(picker.pathBox.x_max >= picker.pathBox.x_min);
    CHECK(picker.textModeBox.x_max >= picker.textModeBox.x_min);
    CHECK(picker.uueModeBox.x_min > picker.textModeBox.x_max);
}

TEST_CASE("The import dialog keeps its size [import_dialog]") {
    ImportFixture fixture;
    fixture.makeDirectory("sub");
    for (int i = 0; i < 6; ++i) {
        fixture.write("file" + std::to_string(i) + "-with-a-long-name.txt", "x");
    }

    import_dialog::open(fixture.state);
    const auto crowded = fixture.draw();

    // Into a directory holding nothing at all, which is the listing that would
    // shrink a box measured against its contents.
    fixture.select("sub");
    REQUIRE(fixture.answer(Event::Return) == import_dialog::Outcome::Ignored);
    REQUIRE(fixture.names() == std::vector<std::string>{".."});
    CHECK(fixture.draw() == crowded);

    // And an error does not move it either: it is written along the bottom
    // frame rather than on a row of its own.
    fixture.state.importPicker->focus = AppState::ImportPicker::Focus::Path;
    fixture.typePath("/no/such/place");
    REQUIRE(fixture.answer(Event::Return) == import_dialog::Outcome::Ignored);
    REQUIRE_FALSE(fixture.state.importPicker->error.empty());
    CHECK(fixture.draw() == crowded);

    // A window that has been resized is the one thing worth measuring again.
    fixture.state.height -= 4;
    CHECK(fixture.draw().height() == crowded.height() - 4);
}

TEST_CASE("The import dialog shows a size and a date [import_dialog]") {
    ImportFixture fixture;
    fixture.write("small.txt", std::string(12, 'x'));
    fixture.write("big.bin", std::string(400000, 'x'));
    fixture.makeDirectory("sub");

    import_dialog::open(fixture.state);
    const auto frame = fixture.draw();
    // The rows of the listing: the title, the path and the rule stand over it.
    const std::string dirRow = fixture.rowText(frame.top + 4);
    const std::string bigRow = fixture.rowText(frame.top + 5);
    const std::string smallRow = fixture.rowText(frame.top + 6);

    CHECK_MESSAGE(contains(dirRow, "sub/"), dirRow);
    // A directory says what it is where a size would be; a file says how big it
    // is, shortened the way every other count in the interface is.
    CHECK_MESSAGE(contains(dirRow, "<dir>"), dirRow);
    CHECK_MESSAGE(contains(bigRow, "big.bin"), bigRow);
    CHECK_MESSAGE(contains(bigRow, "400k"), bigRow);
    CHECK_MESSAGE(contains(smallRow, "small.txt"), smallRow);
    CHECK_MESSAGE(contains(smallRow, "12"), smallRow);

    // And the stamp beside it is this year's, written the way every other stamp
    // in the interface is — `reader_datetime_format`, which the file was just
    // made under.
    const std::time_t now = std::time(nullptr);
    std::tm broken{};
    localtime_r(&now, &broken);
    std::array<char, 8> year{};
    std::strftime(year.data(), year.size(), "%y", &broken);
    CHECK_MESSAGE(contains(smallRow, year.data()), smallRow);
}

TEST_CASE("The path box walks into what it names [import_dialog]") {
    ImportFixture fixture;
    fixture.makeDirectory("sub");
    fixture.write("sub/inner.txt", "read me\n");
    fixture.state.edit.lines = {""};

    import_dialog::open(fixture.state);
    const std::string top = fixture.state.importDirectory;

    // The box stands over the list, so Shift-Tab is the way up to it — and ↑
    // off the top row goes there too. What is typed into it is a path like any
    // other: relative to what is on screen.
    REQUIRE(fixture.answer(Event::TabReverse) == import_dialog::Outcome::Ignored);
    REQUIRE(fixture.state.importPicker->focus == AppState::ImportPicker::Focus::Path);
    fixture.typePath("sub");
    REQUIRE(fixture.answer(Event::Return) == import_dialog::Outcome::Ignored);

    CHECK(std::filesystem::path(fixture.state.importDirectory).filename() == "sub");
    // The box says where the listing now is, and the typing has gone to the
    // list: a directory asked for by name is one to pick a file from.
    CHECK(fixture.state.importPicker->path == fixture.state.importDirectory);
    CHECK(fixture.state.importPicker->focus == AppState::ImportPicker::Focus::Files);

    // A file named in the box is read, wherever it stands — and where it stood
    // is where the next import starts looking.
    fixture.state.importPicker->focus = AppState::ImportPicker::Focus::Path;
    fixture.typePath(top + "/sub/inner.txt");
    REQUIRE(fixture.answer(Event::Return) == import_dialog::Outcome::Imported);
    CHECK(fixture.state.edit.lines ==
          std::vector<std::string>{"=== Cut ===", "read me", "=== Cut ===", ""});
}

TEST_CASE("The path box says when the path is not there [import_dialog]") {
    ImportFixture fixture;
    import_dialog::open(fixture.state);
    const std::string where = fixture.state.importDirectory;

    fixture.state.importPicker->focus = AppState::ImportPicker::Focus::Path;
    fixture.typePath(where + "/nothing-here");
    CHECK(fixture.answer(Event::Return) == import_dialog::Outcome::Ignored);

    // The box stays up on what was typed, with the reason along its bottom
    // frame — and the listing is untouched.
    REQUIRE(fixture.state.importPicker);
    CHECK(fixture.state.importPicker->error == "Path not found");
    CHECK(fixture.state.importDirectory == where);
    const std::string row = fixture.rowText(fixture.draw().bottom);
    CHECK_MESSAGE(contains(row, "Path not found"), row);

    // Esc puts the directory back into the box before it puts the box away.
    CHECK(fixture.answer(Event::Escape) == import_dialog::Outcome::Ignored);
    CHECK(fixture.state.importPicker->path == where);
    CHECK(fixture.answer(Event::Escape) == import_dialog::Outcome::Dismissed);
}

TEST_CASE("The path box takes a name as it is typed [import_dialog]") {
    ImportFixture fixture;
    import_dialog::open(fixture.state);
    fixture.state.importPicker->focus = AppState::ImportPicker::Focus::Path;

    // Not ASCII, and with a space in it: a path is whatever the filesystem took
    // when the file was made.
    fixture.typePath("/tmp/Мои файлы");
    CHECK(fixture.state.importPicker->path == "/tmp/Мои файлы");

    // Backspace steps over a whole character, not a byte of one.
    fixture.answer(Event::Backspace);
    CHECK(fixture.state.importPicker->path == "/tmp/Мои файл");
}

TEST_CASE("The import dialog keys stand in the bottom of the frame [import_dialog]") {
    ImportFixture fixture;
    import_dialog::open(fixture.state);

    const auto frame = fixture.draw();
    const std::string row = fixture.rowText(frame.bottom);
    CHECK_MESSAGE(contains(row, "Enter open · Tab move · Esc close"), row);
    // And the rule they stand in ends where the one at the top of the box does.
    CHECK(frame.right == frame.topRight);
}

TEST_CASE("The import dialog answers the pointer [import_dialog][mouse]") {
    ImportFixture fixture;
    fixture.write("note.txt", "one\n");
    fixture.state.edit.lines = {""};

    import_dialog::open(fixture.state);
    const auto draw = [&fixture] {
        amberedit::ui::term::Screen screen(fixture.state.width, fixture.state.height);
        amberedit::ui::term::render(
            screen, import_dialog::render(fixture.state, amberedit::ui::term::text("")));
    };
    draw();

    // Pointing at a marker is what picks the mode, and pointing at the other
    // one picks it back.
    const auto& picker = *fixture.state.importPicker;
    fixture.answer(clickAt(picker.uueModeBox.x_min, picker.uueModeBox.y_min));
    CHECK(fixture.state.importMode == ImportMode::Uue);
    fixture.answer(clickAt(picker.textModeBox.x_min, picker.textModeBox.y_min));
    CHECK(fixture.state.importMode == ImportMode::Text);

    // And pointing at a file reads it: pointing at a row and pressing is one
    // gesture, not two.
    draw();
    const auto row = std::find_if(
        picker.rowBoxes.begin(), picker.rowBoxes.end(),
        [&picker](const AppState::ImportPicker::Row& r) {
            return picker.entries[static_cast<size_t>(r.index)].name == "note.txt";
        });
    REQUIRE(row != picker.rowBoxes.end());
    CHECK(fixture.answer(clickAt(row->box.x_min + 1, row->box.y_min)) ==
          import_dialog::Outcome::Imported);
    CHECK(fixture.state.edit.lines ==
          std::vector<std::string>{"=== Cut ===", "one", "=== Cut ===", ""});
}
