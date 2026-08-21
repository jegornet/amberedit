#include <catch2/catch.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "app/area_manager.hpp"
#include "app/export_file.hpp"
#include "config/app_config.hpp"
#include "config/text_util.hpp"
#include "domain/area.hpp"
#include "domain/message.hpp"
#include "msgbase/null_lastread_store.hpp"
#include "ports/i_area_source.hpp"
#include "temp_dir.hpp"
#include "ui/app_state.hpp"
#include "ui/export_dialog.hpp"
#include "ui/export_mode_dialog.hpp"
#include "ui/input_field.hpp"
#include "ui/term/event.hpp"
#include "ui/term/screen.hpp"

using amberedit::config::AppConfig;
using amberedit::domain::AreaConfig;
using amberedit::domain::AreaKind;
using amberedit::domain::MessageBody;
using amberedit::domain::MessageDate;
using amberedit::domain::MessageHeader;
using amberedit::domain::MessageLine;
using amberedit::test::TempDir;
using amberedit::ui::AppState;
using amberedit::ui::term::Event;

namespace export_dialog = amberedit::ui::export_dialog;
namespace export_mode_dialog = amberedit::ui::export_mode_dialog;

using amberedit::app::UueFile;

namespace {

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
    std::vector<AreaConfig> loadAreas() override { return {}; }
};

/// A message on the reader's screen and a directory to write it into.
struct ExportFixture {
    ExportFixture()
        : manager(std::make_unique<EmptyAreaSource>(),
                  std::make_unique<amberedit::msgbase::NullLastReadStore>(), config),
          state(manager, config) {
        state.currentArea.tag = "localnet";
        state.currentArea.kind = AreaKind::Echo;

        MessageHeader header;
        header.number = 44;
        header.from = "Ivan Ivanov";
        header.to = "All";
        header.subject = "About the weather";
        header.date = MessageDate{2026, 8, 14, 20, 15, 0};
        state.readHeader = header;

        MessageBody body;
        body.lines.push_back(MessageLine{"\x01MSGID: 2:5020/1 1", true, false});
        body.lines.push_back(MessageLine{"Hello, All!", false, false});
        state.readBody = body;

        state.exportDirectory = dir.path("files");
        std::filesystem::create_directories(state.exportDirectory);
    }

    void makeDirectory(const std::string& name) const {
        std::filesystem::create_directories(std::filesystem::path(state.exportDirectory) /
                                            name);
    }

    void write(const std::string& name, const std::string& content) const {
        std::ofstream out((std::filesystem::path(state.exportDirectory) / name).string(),
                          std::ios::binary);
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
    }

    void select(const std::string& name) {
        auto& picker = *state.exportPicker;
        const auto at = std::find_if(
            picker.entries.begin(), picker.entries.end(),
            [&name](const amberedit::ui::DirEntry& e) { return e.name == name; });
        REQUIRE(at != picker.entries.end());
        picker.cursor = static_cast<int>(at - picker.entries.begin());
    }

    [[nodiscard]] std::vector<std::string> names() const {
        std::vector<std::string> out;
        out.reserve(state.exportPicker->entries.size());
        for (const auto& entry : state.exportPicker->entries) out.push_back(entry.name);
        return out;
    }

    export_dialog::Outcome answer(const Event& event) {
        const auto outcome = export_dialog::handleEvent(state, event);
        // What the shell does with the answer, and the whole of it.
        if (outcome == export_dialog::Outcome::Written) state.exportPicker.reset();
        return outcome;
    }

    /// Types into whichever box the typing is on, character by character.
    void type(const std::string& text) {
        for (size_t i = 0; i < text.size();) {
            const size_t length = amberedit::ui::charLen(text, i);
            answer(Event::Character(text.substr(i, length)));
            i += length;
        }
    }

    /// The rows the box covers on screen, read off the corners it is drawn with.
    struct Frame {
        int top{-1};
        int bottom{-1};
        int left{-1};
        int right{-1};
        /// Where the top rule ends, which is where the bottom one has to.
        int topRight{-1};

        friend bool operator==(const Frame& a, const Frame& b) {
            return a.top == b.top && a.bottom == b.bottom && a.left == b.left &&
                   a.right == b.right;
        }
    };

    [[nodiscard]] Frame draw() {
        amberedit::ui::term::Screen screen(state.width, state.height);
        amberedit::ui::term::render(
            screen, export_dialog::render(state, amberedit::ui::term::text("")));

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

    [[nodiscard]] std::string rowText(int y) {
        amberedit::ui::term::Screen screen(state.width, state.height);
        amberedit::ui::term::render(
            screen, export_dialog::render(state, amberedit::ui::term::text("")));
        std::string row;
        for (int x = 0; x < state.width; ++x) row += screen.at(x, y).glyph;
        return row;
    }

    /// What the file at `name` holds, in the charset it was written in.
    [[nodiscard]] std::string fileText(const std::string& name) const {
        return amberedit::config::text::readFile(
            (std::filesystem::path(state.exportDirectory) / name).string());
    }

    TempDir dir;
    AppConfig config;
    amberedit::app::AreaManager manager;
    AppState state;
};

}  // namespace

TEST_CASE("The export dialog shows directories and nothing else", "[export_dialog]") {
    ExportFixture fixture;
    fixture.makeDirectory("archive");
    fixture.makeDirectory("drafts");
    fixture.write("readme.txt", "x");

    export_dialog::open(fixture.state);
    REQUIRE(fixture.state.exportPicker);
    // What is being picked here is somewhere to write: a file in the way would
    // only be something to point at by mistake.
    CHECK(fixture.names() == std::vector<std::string>{"..", "archive", "drafts"});

    // Nothing has invented a name: the box is empty and the typing starts on
    // it, which is where the one answer the dialog cannot supply belongs.
    CHECK(fixture.state.exportName.empty());
    CHECK(fixture.state.exportPicker->focus == AppState::ExportPicker::Focus::Name);
}

TEST_CASE("The export dialog opens on nothing where there is no message",
          "[export_dialog]") {
    ExportFixture fixture;
    fixture.state.readHeader.reset();
    fixture.state.readBody.reset();

    // An empty area opens the reader on blank rows; there is nothing to write
    // out, and the button for it is drawn dimmed there.
    export_dialog::open(fixture.state);
    CHECK_FALSE(fixture.state.exportPicker);
}

TEST_CASE("The export dialog writes the message", "[export_dialog]") {
    ExportFixture fixture;
    export_dialog::open(fixture.state);
    fixture.type("localnet-44.txt");

    CHECK(fixture.answer(Event::Return) == export_dialog::Outcome::Written);
    CHECK_FALSE(fixture.state.exportPicker);

    const std::string written = fixture.fileText("localnet-44.txt");
    CHECK_THAT(written, Catch::Matchers::Contains("From : Ivan Ivanov"));
    CHECK_THAT(written, Catch::Matchers::Contains("Subj : About the weather"));
    CHECK_THAT(written, Catch::Matchers::Contains("Hello, All!"));
    // The service lines are left out, as the reader leaves them out.
    CHECK_THAT(written, !Catch::Matchers::Contains("MSGID"));
}

TEST_CASE("The export dialog writes under the name it is given", "[export_dialog]") {
    ExportFixture fixture;
    export_dialog::open(fixture.state);

    // The name box has the typing, and it is empty until it is typed into.
    fixture.type("Мои письма.txt");
    CHECK(fixture.state.exportName == "Мои письма.txt");

    REQUIRE(fixture.answer(Event::Return) == export_dialog::Outcome::Written);
    CHECK_THAT(fixture.fileText("Мои письма.txt"),
               Catch::Matchers::Contains("Hello, All!"));
}

TEST_CASE("The export dialog invents no name", "[export_dialog]") {
    ExportFixture fixture;
    export_dialog::open(fixture.state);

    // Enter on an empty name says so rather than writing a file nobody named.
    CHECK(fixture.answer(Event::Return) == export_dialog::Outcome::Ignored);
    REQUIRE(fixture.state.exportPicker);
    CHECK(fixture.state.exportPicker->error == "No file name");
    CHECK(std::filesystem::is_empty(fixture.state.exportDirectory));

    // And nothing appears in the box on the next message either — a name made
    // up from one message would be quietly wrong for the one after it.
    MessageHeader next = *fixture.state.readHeader;
    next.number = 83;
    fixture.state.readHeader = next;
    fixture.state.currentArea.tag = "R50.SysOp";
    REQUIRE(fixture.answer(Event::Escape) == export_dialog::Outcome::Dismissed);

    export_dialog::open(fixture.state);
    CHECK(fixture.state.exportName.empty());
}

TEST_CASE("A name of the user's own outlives the message", "[export_dialog]") {
    ExportFixture fixture;
    export_dialog::open(fixture.state);

    // The name a file was written under is where the message after it usually
    // belongs: this is how one file is collected into, the second export
    // answering the question about the first's file with Append.
    fixture.type("digest.txt");
    REQUIRE(fixture.answer(Event::Return) == export_dialog::Outcome::Written);

    MessageHeader second = *fixture.state.readHeader;
    second.number = 45;
    second.subject = "And another thing";
    fixture.state.readHeader = second;

    export_dialog::open(fixture.state);
    CHECK(fixture.state.exportName == "digest.txt");
    REQUIRE(fixture.answer(Event::Return) == export_dialog::Outcome::Ignored);
    REQUIRE(fixture.state.exportPicker->existing);
    REQUIRE(fixture.answer(Event::Character('a')) == export_dialog::Outcome::Written);

    // Both messages are in the one file, the second added to the first.
    const std::string written = fixture.fileText("digest.txt");
    CHECK_THAT(written, Catch::Matchers::Contains("Subj : About the weather"));
    CHECK_THAT(written, Catch::Matchers::Contains("Subj : And another thing"));
}

TEST_CASE("The export dialog walks into a directory", "[export_dialog]") {
    ExportFixture fixture;
    fixture.makeDirectory("archive");
    const std::string top = fixture.state.exportDirectory;

    export_dialog::open(fixture.state);
    fixture.answer(Event::TabReverse);  // onto the list, which is above the name
    REQUIRE(fixture.state.exportPicker->focus == AppState::ExportPicker::Focus::Files);
    fixture.select("archive");
    // Enter on a directory row walks into it rather than writing: there is
    // nothing else a directory could mean here.
    CHECK(fixture.answer(Event::Return) == export_dialog::Outcome::Ignored);
    CHECK(std::filesystem::path(fixture.state.exportDirectory).filename() == "archive");

    // And the message is written there.
    fixture.answer(Event::Tab);
    REQUIRE(fixture.state.exportPicker->focus == AppState::ExportPicker::Focus::Name);
    fixture.type("kept.txt");
    REQUIRE(fixture.answer(Event::Return) == export_dialog::Outcome::Written);
    CHECK(std::filesystem::exists(std::filesystem::path(top) / "archive" / "kept.txt"));

    // Where it went is where the next one starts from.
    export_dialog::open(fixture.state);
    CHECK(std::filesystem::path(fixture.state.exportDirectory).filename() == "archive");
}

TEST_CASE("The export path box takes a directory and a file alike", "[export_dialog]") {
    ExportFixture fixture;
    fixture.makeDirectory("archive");
    const std::string top = fixture.state.exportDirectory;

    export_dialog::open(fixture.state);
    fixture.state.exportPicker->focus = AppState::ExportPicker::Focus::Path;
    fixture.state.exportPicker->path.clear();
    fixture.state.exportPicker->pathCursor = 0;

    // A directory typed there is walked into, and the typing goes to the name,
    // which is all that is left to say.
    fixture.type(top + "/archive");
    REQUIRE(fixture.answer(Event::Return) == export_dialog::Outcome::Ignored);
    CHECK(std::filesystem::path(fixture.state.exportDirectory).filename() == "archive");
    CHECK(fixture.state.exportPicker->focus == AppState::ExportPicker::Focus::Name);

    // A path naming a file that is not there is not an error here: this dialog
    // writes, so what it was given is where and under what name.
    fixture.state.exportPicker->focus = AppState::ExportPicker::Focus::Path;
    fixture.state.exportPicker->path.clear();
    fixture.state.exportPicker->pathCursor = 0;
    fixture.type(top + "/keep.txt");
    REQUIRE(fixture.answer(Event::Return) == export_dialog::Outcome::Ignored);
    CHECK(fixture.state.exportDirectory == top);
    CHECK(fixture.state.exportName == "keep.txt");

    REQUIRE(fixture.answer(Event::Return) == export_dialog::Outcome::Written);
    CHECK(std::filesystem::exists(std::filesystem::path(top) / "keep.txt"));
}

TEST_CASE("The export dialog says what went wrong", "[export_dialog]") {
    ExportFixture fixture;
    export_dialog::open(fixture.state);

    // A path that is nowhere near a directory.
    fixture.state.exportPicker->focus = AppState::ExportPicker::Focus::Path;
    fixture.state.exportPicker->path.clear();
    fixture.state.exportPicker->pathCursor = 0;
    fixture.type("/no/such/place/at/all");
    REQUIRE(fixture.answer(Event::Return) == export_dialog::Outcome::Ignored);
    CHECK(fixture.state.exportPicker->error == "Path not found");
    CHECK_THAT(fixture.rowText(fixture.draw().bottom),
               Catch::Matchers::Contains("Path not found"));

    // And a name that is no name at all: the message has to be called
    // something, and the box says so rather than writing a file named nothing.
    fixture.state.exportPicker->focus = AppState::ExportPicker::Focus::Name;
    fixture.state.exportName = "   ";
    CHECK(fixture.answer(Event::Return) == export_dialog::Outcome::Ignored);
    CHECK(fixture.state.exportPicker->error == "No file name");
}

TEST_CASE("The keys stand in the bottom of the frame", "[export_dialog]") {
    ExportFixture fixture;
    export_dialog::open(fixture.state);

    const auto frame = fixture.draw();
    const std::string bottom = fixture.rowText(frame.bottom);
    // In the rule that closes the box rather than on a row of its own — and the
    // rule still ends where the one at the top of the box does.
    CHECK_THAT(bottom, Catch::Matchers::Contains("Enter write · Tab move · Esc close"));
    CHECK(frame.right == frame.topRight);

    // What went wrong takes their place: it is the more urgent of the two, and
    // the box does not grow a row to say it.
    fixture.state.exportPicker->error = "Path not found";
    const auto complaining = fixture.draw();
    CHECK(complaining == frame);
    const std::string said = fixture.rowText(complaining.bottom);
    CHECK_THAT(said, Catch::Matchers::Contains("Path not found"));
    CHECK_THAT(said, !Catch::Matchers::Contains("Tab move"));
    CHECK(complaining.right == complaining.topRight);
}

TEST_CASE("The export dialog keeps its size", "[export_dialog]") {
    ExportFixture fixture;
    for (int i = 0; i < 5; ++i) fixture.makeDirectory("dir" + std::to_string(i));

    export_dialog::open(fixture.state);
    const auto crowded = fixture.draw();

    fixture.answer(Event::TabReverse);
    fixture.select("dir0");
    REQUIRE(fixture.answer(Event::Return) == export_dialog::Outcome::Ignored);
    REQUIRE(fixture.names() == std::vector<std::string>{".."});
    // A box measured against the directory it shows would stand a different
    // size in every one of them.
    CHECK(fixture.draw() == crowded);
}

TEST_CASE("The export dialog answers the pointer", "[export_dialog][mouse]") {
    ExportFixture fixture;
    fixture.makeDirectory("archive");

    export_dialog::open(fixture.state);
    static_cast<void>(fixture.draw());

    // Pointing at a row walks into it — everything in this listing is a
    // directory, so there is nothing else a click could mean.
    const auto& picker = *fixture.state.exportPicker;
    const auto row = std::find_if(
        picker.rowBoxes.begin(), picker.rowBoxes.end(),
        [&picker](const AppState::ExportPicker::Row& r) {
            return picker.entries[static_cast<size_t>(r.index)].name == "archive";
        });
    REQUIRE(row != picker.rowBoxes.end());
    fixture.answer(clickAt(row->box.x_min + 1, row->box.y_min));
    CHECK(std::filesystem::path(fixture.state.exportDirectory).filename() == "archive");

    // And pointing at the name box puts the typing in it, at the character
    // under the pointer.
    fixture.state.exportName = "digest.txt";
    static_cast<void>(fixture.draw());
    fixture.answer(clickAt(picker.nameBox.x_min + 3, picker.nameBox.y_min));
    CHECK(picker.focus == AppState::ExportPicker::Focus::Name);
    CHECK(picker.nameCursor == 3);
}

TEST_CASE("The export dialog writes the files the message carries",
          "[export_dialog][uue]") {
    ExportFixture fixture;
    export_dialog::open(fixture.state,
                        {UueFile{"report.zip", "PK\x03\x04"}, UueFile{"note.txt", "hi"}});
    REQUIRE(fixture.state.exportPicker);

    const auto& picker = *fixture.state.exportPicker;
    CHECK(picker.mode == AppState::ExportPicker::Mode::Uue);
    // The names are listed where the name to type stands in a text export, and
    // the typing opens on them: the directory below is the only thing still to
    // be said, and it already says where.
    CHECK(picker.focus == AppState::ExportPicker::Focus::Name);
    const auto frame = fixture.draw();
    CHECK_THAT(fixture.rowText(frame.top), Catch::Matchers::Contains("Export files"));
    CHECK_THAT(fixture.rowText(frame.bottom), Catch::Matchers::Contains("Enter save"));

    // Nothing types over them: they are the message's names, and a file renamed
    // on its way out of one is not the file that was sent.
    fixture.type("mine.zip");
    CHECK(fixture.state.exportName.empty());
    CHECK(picker.files.size() == 2);

    REQUIRE(fixture.answer(Event::Return) == export_dialog::Outcome::Written);
    CHECK(fixture.fileText("report.zip") == "PK\x03\x04");
    CHECK(fixture.fileText("note.txt") == "hi");
}

TEST_CASE("The export dialog lists the files it is to write", "[export_dialog][uue]") {
    ExportFixture fixture;
    std::vector<UueFile> files;
    files.reserve(9);
    for (int i = 0; i < 9; ++i) {
        files.push_back(UueFile{"part" + std::to_string(i) + ".zip", "x"});
    }
    export_dialog::open(fixture.state, files);

    // The names are a label, and the button that writes them stands under it —
    // the last row of the box before the rule that closes it.
    const auto frame = fixture.draw();
    CHECK_THAT(fixture.rowText(frame.bottom - 6),
               Catch::Matchers::Contains("Files: part0.zip"));
    // Five rows of names and no more, whatever the message carries: a box that
    // grew a row per file would walk off a short window, and the last of them
    // counts what is left.
    CHECK_THAT(fixture.rowText(frame.bottom - 2),
               Catch::Matchers::Contains("… and 5 more"));
    CHECK_THAT(fixture.rowText(frame.bottom - 1), Catch::Matchers::Contains("Save"));

    // The label is not a stop in the ring: there is nothing to type over it and
    // nothing to pick among the names, so Tab walks the path, the listing and
    // the button, and the button is where the box opens.
    CHECK(fixture.state.exportPicker->focus == AppState::ExportPicker::Focus::Name);
}

TEST_CASE("A decoded file is written over nothing", "[export_dialog][uue]") {
    ExportFixture fixture;
    fixture.write("report.zip", "something of the user's own");
    export_dialog::open(fixture.state, {UueFile{"report.zip", "PK"}});

    // The one thing the two modes do not share: a text export appends, and a
    // file whose name was never the user's is not written over at all.
    CHECK(fixture.answer(Event::Return) == export_dialog::Outcome::Ignored);
    REQUIRE(fixture.state.exportPicker);
    CHECK_THAT(fixture.state.exportPicker->error,
               Catch::Matchers::Contains("report.zip"));
    CHECK(fixture.fileText("report.zip") == "something of the user's own");
}

TEST_CASE("The export dialog walks into a directory to write files into",
          "[export_dialog][uue]") {
    ExportFixture fixture;
    fixture.makeDirectory("incoming");
    const std::string top = fixture.state.exportDirectory;

    export_dialog::open(fixture.state, {UueFile{"report.zip", "PK"}});
    fixture.answer(Event::TabReverse);
    REQUIRE(fixture.state.exportPicker->focus == AppState::ExportPicker::Focus::Files);
    fixture.select("incoming");
    REQUIRE(fixture.answer(Event::Return) == export_dialog::Outcome::Ignored);

    fixture.answer(Event::Tab);
    REQUIRE(fixture.answer(Event::Return) == export_dialog::Outcome::Written);
    CHECK(
        std::filesystem::exists(std::filesystem::path(top) / "incoming" / "report.zip"));
}

TEST_CASE("The export question is asked before the where", "[export_dialog][uue]") {
    ExportFixture fixture;
    const std::vector<UueFile> files{UueFile{"report.zip", "PK"}};

    export_mode_dialog::open(fixture.state, files);
    REQUIRE(fixture.state.exportModePicker);
    // The names are what the box is about, and the count with them.
    amberedit::ui::term::Screen screen(fixture.state.width, fixture.state.height);
    amberedit::ui::term::render(
        screen, export_mode_dialog::render(fixture.state, amberedit::ui::term::text("")));
    std::string drawn;
    for (int y = 0; y < fixture.state.height; ++y) {
        for (int x = 0; x < fixture.state.width; ++x) drawn += screen.at(x, y).glyph;
    }
    CHECK_THAT(drawn,
               Catch::Matchers::Contains("The message contains UUE-encoded file(s):"));
    CHECK_THAT(drawn, Catch::Matchers::Contains("report.zip"));

    // The initials answer outright, the way y and n answer a confirmation.
    CHECK(export_mode_dialog::handleEvent(fixture.state, Event::Character('t')) ==
          export_mode_dialog::Outcome::Picked);
    CHECK(fixture.state.exportModePicker->mode == AppState::ExportPicker::Mode::Text);

    // ←→ walk the two, and Esc leaves the reader as it was.
    export_mode_dialog::handleEvent(fixture.state, Event::ArrowLeft);
    CHECK(fixture.state.exportModePicker->mode == AppState::ExportPicker::Mode::Uue);
    CHECK(export_mode_dialog::handleEvent(fixture.state, Event::Escape) ==
          export_mode_dialog::Outcome::Dismissed);
    CHECK_FALSE(fixture.state.exportModePicker);
    CHECK_FALSE(fixture.state.exportPicker);
}

TEST_CASE("The text answer writes the message and not the file", "[export_dialog][uue]") {
    ExportFixture fixture;
    export_mode_dialog::open(fixture.state, {UueFile{"report.zip", "PK"}});
    REQUIRE(export_mode_dialog::handleEvent(fixture.state, Event::Character('t')) ==
            export_mode_dialog::Outcome::Picked);
    fixture.state.exportModePicker.reset();

    // What the shell does with that answer: the export dialog in its place, and
    // the files left in the message where they stood.
    export_dialog::open(fixture.state);
    REQUIRE(fixture.state.exportPicker);
    CHECK(fixture.state.exportPicker->mode == AppState::ExportPicker::Mode::Text);
    fixture.type("digest.txt");
    REQUIRE(fixture.answer(Event::Return) == export_dialog::Outcome::Written);
    CHECK_THAT(fixture.fileText("digest.txt"), Catch::Matchers::Contains("Hello, All!"));
    CHECK_FALSE(std::filesystem::exists(
        std::filesystem::path(fixture.state.exportDirectory) / "report.zip"));
}

TEST_CASE("The export question answers the pointer", "[export_dialog][uue][mouse]") {
    ExportFixture fixture;
    export_mode_dialog::open(fixture.state, {UueFile{"report.zip", "PK"}});

    amberedit::ui::term::Screen screen(fixture.state.width, fixture.state.height);
    amberedit::ui::term::render(
        screen, export_mode_dialog::render(fixture.state, amberedit::ui::term::text("")));

    // Pointing at an answer gives it, without selecting it first: pointing at
    // Text and pressing is one gesture, not two.
    const auto& picker = *fixture.state.exportModePicker;
    const Event click = clickAt(picker.textBox.x_min + 1, picker.textBox.y_min);
    CHECK(export_mode_dialog::handleEvent(fixture.state, click) ==
          export_mode_dialog::Outcome::Picked);
    CHECK(picker.mode == AppState::ExportPicker::Mode::Text);
}

namespace {

/// Everything the box has on the screen, rows joined — for the question drawn
/// over it, which stands where the layout puts it rather than at a known row.
std::string boxText(ExportFixture& fixture) {
    std::string drawn;
    for (int y = 0; y < fixture.state.height; ++y) drawn += fixture.rowText(y);
    return drawn;
}

}  // namespace

TEST_CASE("A file already there is a question", "[export_dialog][exists]") {
    ExportFixture fixture;
    fixture.write("digest.txt", "what was there before\n");
    export_dialog::open(fixture.state);
    fixture.type("digest.txt");

    // Neither answer is a default worth having: appending is right for
    // collecting messages into a digest and wrong for every other reason a name
    // is typed twice, and writing over is wrong exactly the other way round.
    REQUIRE(fixture.answer(Event::Return) == export_dialog::Outcome::Ignored);
    REQUIRE(fixture.state.exportPicker->existing);
    CHECK(fixture.state.exportPicker->existing->answer ==
          AppState::ExportPicker::Existing::Answer::Append);
    const std::string drawn = boxText(fixture);
    CHECK_THAT(drawn, Catch::Matchers::Contains("File exists:"));
    CHECK_THAT(drawn, Catch::Matchers::Contains("digest.txt"));

    // Esc is neither, and it leaves the dialog exactly as it was: the name is
    // still in its box, to be typed over or written under after all.
    CHECK(fixture.answer(Event::Escape) == export_dialog::Outcome::Ignored);
    CHECK_FALSE(fixture.state.exportPicker->existing);
    REQUIRE(fixture.state.exportPicker);
    CHECK(fixture.state.exportName == "digest.txt");
    CHECK(fixture.fileText("digest.txt") == "what was there before\n");
}

TEST_CASE("The export writes over a file when told to", "[export_dialog][exists]") {
    ExportFixture fixture;
    fixture.write("digest.txt", "what was there before\n");
    export_dialog::open(fixture.state);
    fixture.type("digest.txt");
    REQUIRE(fixture.answer(Event::Return) == export_dialog::Outcome::Ignored);

    // The initials answer outright, the way y and n answer a confirmation.
    REQUIRE(fixture.answer(Event::Character('o')) == export_dialog::Outcome::Written);
    const std::string written = fixture.fileText("digest.txt");
    CHECK_THAT(written, Catch::Matchers::Contains("Subj : About the weather"));
    CHECK_THAT(written, !Catch::Matchers::Contains("what was there before"));
}

TEST_CASE("The question about a file answers the pointer",
          "[export_dialog][exists][mouse]") {
    ExportFixture fixture;
    fixture.write("digest.txt", "what was there before\n");
    export_dialog::open(fixture.state);
    fixture.type("digest.txt");
    REQUIRE(fixture.answer(Event::Return) == export_dialog::Outcome::Ignored);
    static_cast<void>(fixture.draw());

    // Pointing at an answer gives it, without selecting it first.
    const auto& existing = *fixture.state.exportPicker->existing;
    const Event click =
        clickAt(existing.overwriteBox.x_min + 1, existing.overwriteBox.y_min);
    CHECK(fixture.answer(click) == export_dialog::Outcome::Written);
    CHECK_THAT(fixture.fileText("digest.txt"),
               !Catch::Matchers::Contains("what was there before"));
}

TEST_CASE("The files a message carries are written by a button",
          "[export_dialog][uue][mouse]") {
    ExportFixture fixture;
    export_dialog::open(fixture.state, {UueFile{"report.zip", "PK"}});
    static_cast<void>(fixture.draw());

    // The names above it are a label; the button is what a click can land on.
    const Event click = clickAt(fixture.state.exportPicker->nameBox.x_min + 1,
                                fixture.state.exportPicker->nameBox.y_min);
    CHECK(fixture.answer(click) == export_dialog::Outcome::Written);
    CHECK(fixture.fileText("report.zip") == "PK");
}
