#include <doctest/doctest.h>

#include <cstdint>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "app/area_manager.hpp"
#include "config/app_config.hpp"
#include "ports/i_area_source.hpp"
#include "ports/i_lastread_store.hpp"
#include "temp_squish_base.hpp"
#include "test_strings.hpp"
#include "ui/app_state.hpp"
#include "ui/area_dialog.hpp"
#include "ui/forward_dialog.hpp"
#include "ui/menu_dialog.hpp"
#include "ui/message_marks.hpp"
#include "ui/scope_dialog.hpp"
#include "ui/screens/area_list_screen.hpp"
#include "ui/screens/compose_screen.hpp"
#include "ui/screens/message_list_screen.hpp"
#include "ui/screens/message_read_screen.hpp"
#include "ui/term/element.hpp"
#include "ui/term/event.hpp"
#include "ui/term/screen.hpp"

using amberedit::app::ScreenId;
using amberedit::test::TempSquishBase;
using amberedit::ui::term::Event;

namespace area_dialog = amberedit::ui::area_dialog;
namespace forward_dialog = amberedit::ui::forward_dialog;
namespace marks = amberedit::ui::marks;
namespace menu_dialog = amberedit::ui::menu_dialog;
namespace scope_dialog = amberedit::ui::scope_dialog;
namespace area_list = amberedit::ui::screens::area_list;
namespace compose = amberedit::ui::screens::compose;
namespace domain = amberedit::domain;
namespace message_list = amberedit::ui::screens::message_list;
namespace message_read = amberedit::ui::screens::message_read;
namespace term = amberedit::ui::term;

namespace {

using Mode = amberedit::ui::AppState::ForwardPicker::Mode;

/// What the shell asks the area dialog for, given the answer to the one before
/// it — the same mapping app_shell.cpp makes.
amberedit::ui::AppState::AreaPicker::For purposeOf(Mode mode) {
    using For = amberedit::ui::AppState::AreaPicker::For;
    switch (mode) {
        case Mode::Move: return For::Move;
        case Mode::Copy: return For::Copy;
        case Mode::Forward: break;
    }
    return For::Forward;
}

/// A mark per area, which is what every real store keeps — the single-slot one
/// in area_fixture.hpp would hand the mark left in one area to the other, and
/// the two bases here are copies of each other, so its UIDs would even be
/// found there.
class PerAreaLastReadStore final : public amberedit::ports::ILastReadStore {
public:
    uint32_t getLastRead(const domain::AreaConfig& area) override {
        const auto found = marks_.find(keyOf(area));
        return found == marks_.end() ? 0 : found->second;
    }
    void setLastRead(const domain::AreaConfig& area, uint32_t uid) override {
        marks_[keyOf(area)] = uid;
    }

private:
    static std::string keyOf(const domain::AreaConfig& area) {
        return area.tag + '\n' + area.path;
    }

    std::map<std::string, uint32_t> marks_;
};

class TwoAreaSource final : public amberedit::ports::IAreaConfigSource {
public:
    TwoAreaSource(domain::AreaConfig first, domain::AreaConfig second)
        : areas_{std::move(first), std::move(second)} {}
    tl::expected<std::vector<domain::AreaConfig>, amberedit::ErrorPtr> loadAreas()
        override {
        return areas_;
    }

private:
    std::vector<domain::AreaConfig> areas_;
};

domain::AreaConfig areaAt(const std::string& tag, const std::string& path,
                          domain::AreaKind kind = domain::AreaKind::Echo) {
    domain::AreaConfig area;
    area.tag = tag;
    area.path = path;
    area.type = domain::MsgBaseType::Squish;
    area.kind = kind;
    return area;
}

/// A template with the lines GoldED's own writes for a moved reply, so that
/// what reaches the editor can be read back.
///
/// It is the shipped template's shape where it matters here: a bare `@position`
/// that every message honours, a `@quoted@position` that only a reply reaches —
/// the later of the two winning, as GoldED's own does — and a signature under
/// both, which is what tells a cursor put where the template said from one put
/// at the end of the text.
class TempTemplate {
public:
    explicit TempTemplate(const std::filesystem::path& dir) : path_(dir / "moved.tpl") {
        std::ofstream out(path_);
        out << "@moved*** Answering a msg posted in area @OEcho.\n"
               "@forward* Forwarded by @CName\n"
               "@forward* Area : @OEcho\n"
               "@message\n"
               "@position\n"
               "@quoted@position\n"
               "@quote\n"
               "@CFName\n";
    }

    [[nodiscard]] std::string path() const { return path_.string(); }

private:
    std::filesystem::path path_;
};

/// Two areas, each on a Squish base of its own — the least a reply moved from
/// one area into another can be driven through. The bases are copies of the
/// same fixture, which is exactly what a real pair of areas looks like here:
/// what tells them apart is the tag and the path.
struct TwoAreaFixture {
    /// `sort` is what `arealist_sort` would have said, so that a test can put
    /// the two areas in either order. The manager copies the config when it is
    /// built and sorts by it, which is why it goes in here rather than being
    /// set afterwards.
    /// `groups` is the `group ... endgroup` blocks the config would hold, as
    /// text — the only way to write them, since a group keeps the lines it was
    /// written on.
    /// `targetKind` is what the second area is: another echo, which is what a
    /// message moved between two areas wants, or the netmail area an answer
    /// addressed to one person goes into. It is named for what it is, so that a
    /// test picking it out of the dialog reads as one.
    explicit TwoAreaFixture(std::vector<amberedit::config::AreaSortCriterion> sort = {},
                            const std::string& groups = "",
                            domain::AreaKind targetKind = domain::AreaKind::Echo)
        : tpl(here.dir()),
          source(areaAt("localnet", here.path())),
          target(
              areaAt(targetKind == domain::AreaKind::Netmail ? "netmail" : "test.other",
                     there.path(), targetKind)),
          config(withTemplate(std::move(sort), groups)),
          lastRead(new PerAreaLastReadStore),
          manager(std::make_unique<TwoAreaSource>(source, target),
                  std::unique_ptr<PerAreaLastReadStore>(lastRead), config),
          state(manager, config) {
        static_cast<void>(manager.reload());
    }

    /// The messages an area holds, read off a base opened for the purpose.
    /// Nothing else may be open at the time: the manager keeps one base.
    uint32_t countIn(const domain::AreaConfig& area) {
        amberedit::ports::IMsgBase* base =
            amberedit::test::valueOf(manager.openArea(area));
        REQUIRE(base != nullptr);
        const uint32_t count = base->count();
        manager.closeCurrentArea();
        return count;
    }

    /// Which row of the picker names `tag`.
    [[nodiscard]] int rowOf(const std::string& tag) const {
        const auto& areas = manager.areas();
        for (size_t i = 0; i < areas.size(); ++i) {
            if (areas[i].config.tag == tag) return static_cast<int>(i);
        }
        FAIL("no such area: " << tag);
        return 0;
    }

    /// What the shell does once the dialog has been answered with an area:
    /// whichever message the picking was for.
    void pickArea() {
        using For = amberedit::ui::AppState::AreaPicker::For;
        const domain::AreaConfig picked =
            manager.areas()[static_cast<size_t>(state.areaPicker->cursor)].config;
        const auto purpose = state.areaPicker->purpose;
        const bool marked = state.areaPicker->marked;
        state.areaPicker.reset();
        switch (purpose) {
            case For::Reply: compose::startReplyElsewhere(state, picked); break;
            case For::Forward: compose::startForwardTo(state, picked); break;
            case For::Move:
                if (marked) {
                    message_read::moveMarked(state, picked);
                } else {
                    message_read::moveMessage(state, picked);
                }
                break;
            case For::Copy:
                if (marked) {
                    message_read::copyMarked(state, picked);
                } else {
                    message_read::copyMessage(state, picked);
                }
                break;
        }
    }

    /// `m` on an area with something marked, answered with the scope: which
    /// messages the key means. What is left up afterwards is the forward
    /// picker — the second of the three boxes.
    void askScope(amberedit::ui::AppState::ScopePicker::Mode mode) {
        message_read::handleEvent(state, Event::Character('m'));
        REQUIRE(state.scopePicker);
        REQUIRE(state.scopePicker->purpose ==
                amberedit::ui::AppState::ScopePicker::For::Forward);
        state.scopePicker->mode = mode;
        REQUIRE(scope_dialog::handleEvent(state, Event::Return) ==
                scope_dialog::Outcome::Picked);
        state.scopePicker.reset();
        if (mode == amberedit::ui::AppState::ScopePicker::Mode::Cancel) return;
        message_read::askForward(
            state, mode == amberedit::ui::AppState::ScopePicker::Mode::Marked);
    }

    /// The whole of `m` on a marked set: the scope, what is to become of them,
    /// and the area they are to become it in.
    void passOnMarked(Mode mode, const std::string& tag) {
        askScope(amberedit::ui::AppState::ScopePicker::Mode::Marked);
        REQUIRE(state.forwardPicker);
        CHECK(state.forwardPicker->marked);
        state.forwardPicker->mode = mode;
        REQUIRE(forward_dialog::handleEvent(state, Event::Return) ==
                forward_dialog::Outcome::Picked);
        state.forwardPicker.reset();
        message_read::askArea(state, purposeOf(mode), /*marked=*/true);
        REQUIRE(state.areaPicker);
        state.areaPicker->cursor = rowOf(tag);
        REQUIRE(area_dialog::handleEvent(state, Event::Return) ==
                area_dialog::Outcome::Picked);
        pickArea();
    }

    /// `m` and the answer to the dialog it opens — the two halves of asking for
    /// a forward, a move or a copy, as the shell puts them together. What is
    /// left up afterwards is the area dialog, which the tests answer themselves.
    void askForward(amberedit::ui::AppState::ForwardPicker::Mode mode) {
        message_read::handleEvent(state, Event::Character('m'));
        REQUIRE(state.forwardPicker);
        state.forwardPicker->mode = mode;
        REQUIRE(forward_dialog::handleEvent(state, Event::Return) ==
                forward_dialog::Outcome::Picked);
        state.forwardPicker.reset();
        message_read::askArea(state, purposeOf(mode));
    }

    /// The whole of `m`, both dialogs answered: what is to become of the
    /// message, and the area it is to become that in.
    void passOn(Mode mode, const std::string& tag) {
        askForward(mode);
        REQUIRE(state.areaPicker);
        state.areaPicker->cursor = rowOf(tag);
        REQUIRE(area_dialog::handleEvent(state, Event::Return) ==
                area_dialog::Outcome::Picked);
        pickArea();
    }

    [[nodiscard]] amberedit::config::AppConfig withTemplate(
        std::vector<amberedit::config::AreaSortCriterion> sort,
        const std::string& groups) const {
        amberedit::config::AppConfig cfg;
        cfg.userName = "Yegor Gluhov";
        // The address every message is written from: without one the header
        // screen would not let go of the message, and rightly so.
        cfg.userAddress = domain::FtnAddress::parse("192:168/2");
        cfg.templatePath = tpl.path();
        if (!sort.empty()) cfg.areaListSort = std::move(sort);
        // Read rather than built: a group is the lines it was written on, and
        // there is nothing else here a config read from text would settle
        // differently.
        if (!groups.empty()) {
            cfg.areaGroups =
                amberedit::test::valueOf(amberedit::config::AppConfig::loadFromString(
                                             "tosser_config /dev/null\n"
                                             "tosser_config_format fidoconfig\n"
                                             "default_charset CP866\n"
                                             "compose_charset CP866\n"
                                             "name Vasya Pupkin\n"
                                             "address 2:5020/9999.1\n" +
                                             groups))
                    .areaGroups;
        }
        return cfg;
    }

    TempSquishBase here;
    TempSquishBase there;
    TempTemplate tpl;
    domain::AreaConfig source;
    domain::AreaConfig target;
    amberedit::config::AppConfig config;
    PerAreaLastReadStore* lastRead;
    amberedit::app::AreaManager manager;
    amberedit::ui::AppState state;
};

/// The rows of the dialog over the reader, as text.
std::vector<std::string> rowsOf(amberedit::ui::AppState& state) {
    term::Screen screen(state.width, state.height);
    term::render(screen, area_dialog::render(state, message_read::render(state)));

    std::vector<std::string> rows;
    for (int y = 0; y < state.height; ++y) {
        std::string row;
        for (int x = 0; x < state.width; ++x) row += screen.at(x, y).glyph;
        rows.push_back(row);
    }
    return rows;
}

/// The same for the dialog `m` opens first, over the reader.
std::vector<std::string> forwardRowsOf(amberedit::ui::AppState& state) {
    term::Screen screen(state.width, state.height);
    term::render(screen, forward_dialog::render(state, message_read::render(state)));

    std::vector<std::string> rows;
    for (int y = 0; y < state.height; ++y) {
        std::string row;
        for (int x = 0; x < state.width; ++x) row += screen.at(x, y).glyph;
        rows.push_back(row);
    }
    return rows;
}

bool anyRowHas(const std::vector<std::string>& rows, const std::string& text) {
    for (const auto& row : rows) {
        if (row.find(text) != std::string::npos) return true;
    }
    return false;
}

/// The reader's own rows, for reading a title or a body line back off the
/// screen it reaches.
std::vector<std::string> readerRows(amberedit::ui::AppState& state) {
    term::Screen screen(state.width, state.height);
    term::render(screen, message_read::render(state));

    std::vector<std::string> rows;
    for (int y = 0; y < state.height; ++y) {
        std::string row;
        for (int x = 0; x < state.width; ++x) row += screen.at(x, y).glyph;
        rows.push_back(row);
    }
    return rows;
}

/// The dialog on its own, over nothing — the reader behind it names the area
/// too, in its title, and a test reading the order of the areas off the screen
/// must not find that one.
std::vector<std::string> dialogRows(amberedit::ui::AppState& state) {
    term::Screen screen(state.width, state.height);
    term::render(screen, area_dialog::render(state, term::text("")));

    std::vector<std::string> rows;
    for (int y = 0; y < state.height; ++y) {
        std::string row;
        for (int x = 0; x < state.width; ++x) row += screen.at(x, y).glyph;
        rows.push_back(row);
    }
    return rows;
}

/// The area list screen's own rows, for the same reading.
std::vector<std::string> areaListRows(amberedit::ui::AppState& state) {
    term::Screen screen(state.width, state.height);
    term::render(screen, area_list::render(state));

    std::vector<std::string> rows;
    for (int y = 0; y < state.height; ++y) {
        std::string row;
        for (int x = 0; x < state.width; ++x) row += screen.at(x, y).glyph;
        rows.push_back(row);
    }
    return rows;
}

/// Which of `tags` each row names, top to bottom — the order a screen puts the
/// areas in, as it reaches the terminal.
std::vector<std::string> tagsDown(const std::vector<std::string>& rows,
                                  const std::vector<std::string>& tags) {
    std::vector<std::string> found;
    for (const auto& row : rows) {
        for (const auto& tag : tags) {
            if (row.find(tag) != std::string::npos) {
                found.push_back(tag);
                break;
            }
        }
    }
    return found;
}

/// Puts a message into `area` with the control lines given, and answers with
/// its number.
///
/// The kludges are written in the order they are passed: `AREA:` is the one
/// that has to stand first for `areareplydirect` to follow it, and a test that
/// puts it second is testing exactly that.
uint32_t writeInto(TwoAreaFixture& fixture, const domain::AreaConfig& area,
                   std::vector<std::string> kludges, const std::string& destAddr = "") {
    domain::MessageDraft draft;
    draft.from = "Ivan Petrov";
    draft.to = "All";
    draft.subject = "posted over there";
    draft.origAddr = *domain::FtnAddress::parse("192:168/3");
    // What the tosser left in the field a netmail would name its recipient in.
    // An echo is addressed to the area, so whatever stands there means nothing.
    if (!destAddr.empty()) draft.destAddr = *domain::FtnAddress::parse(destAddr);
    draft.kludges = std::move(kludges);
    draft.lines = {"hello from the packet"};

    amberedit::ports::IMsgBase* base =
        amberedit::test::valueOf(fixture.manager.openArea(area));
    REQUIRE(base != nullptr);
    const uint32_t number = amberedit::test::valueOf(base->write(draft));
    REQUIRE(number != 0);
    fixture.manager.closeCurrentArea();
    return number;
}

/// The reader on the message just written — the one the AREA: line is on.
void readMessage(TwoAreaFixture& fixture, uint32_t number) {
    REQUIRE(message_list::enterArea(fixture.state, fixture.source).has_value());
    REQUIRE(message_read::loadMessage(fixture.state, number));
    REQUIRE(fixture.state.readBody);
}

/// What a forward passes on: the message being read without its kludges and
/// without the pair closing it, which is what @message puts in.
std::vector<std::string> visibleLines(const amberedit::ui::AppState& state) {
    std::vector<std::string> out;
    for (const auto& line : state.readBody->lines) {
        if (line.kludge || line.trailer) continue;
        out.push_back(line.text);
    }
    return out;
}

}  // namespace

TEST_CASE("n asks which area the reply goes into [other_area]") {
    TwoAreaFixture fixture;
    REQUIRE(message_list::enterArea(fixture.state, fixture.source).has_value());
    REQUIRE(fixture.state.navigator.current() == ScreenId::MessageRead);

    message_read::handleEvent(fixture.state, Event::Character('n'));
    REQUIRE(fixture.state.areaPicker);
    // It opens on the first area of the list, whichever one is being read: that
    // one is the single place the reply is not going, `q` writing there.
    CHECK(fixture.state.areaPicker->cursor == 0);

    // Only the names, both of them, over the message underneath.
    const auto rows = rowsOf(fixture.state);
    CHECK(anyRowHas(rows, "localnet"));
    CHECK(anyRowHas(rows, "test.other"));
    CHECK(anyRowHas(rows, "Reply in area"));

    // Esc leaves the reader as it was, with nothing begun.
    CHECK(area_dialog::handleEvent(fixture.state, Event::Escape) ==
          area_dialog::Outcome::Dismissed);
    CHECK_FALSE(fixture.state.areaPicker);
    CHECK(fixture.state.navigator.current() == ScreenId::MessageRead);
}

TEST_CASE("The picker searches by name, as the area list does [other_area]") {
    TwoAreaFixture fixture;
    REQUIRE(message_list::enterArea(fixture.state, fixture.source).has_value());
    message_read::handleEvent(fixture.state, Event::Character('n'));
    REQUIRE(fixture.state.areaPicker);

    // "t" is the first letter of the other area's tag and of no other.
    CHECK(area_dialog::handleEvent(fixture.state, Event::Character('t')) ==
          area_dialog::Outcome::Ignored);
    CHECK(fixture.state.areaPicker->cursor == fixture.rowOf("test.other"));
    CHECK(anyRowHas(rowsOf(fixture.state), "Area: t"));

    CHECK(area_dialog::handleEvent(fixture.state, Event::Return) ==
          area_dialog::Outcome::Picked);
}

TEST_CASE(
    "A reply moved into another area is written there and read here "
    "[other_area]") {
    TwoAreaFixture fixture;
    auto& state = fixture.state;

    const uint32_t hereBefore = fixture.countIn(fixture.source);
    const uint32_t thereBefore = fixture.countIn(fixture.target);
    const uint32_t unreadBefore =
        fixture.manager.areas()[static_cast<size_t>(fixture.rowOf("test.other"))].unread;

    REQUIRE(message_list::enterArea(state, fixture.source).has_value());
    REQUIRE(state.readHeader);
    const uint32_t reading = state.readHeader->number;
    const std::string answering = state.readHeader->from;

    message_read::handleEvent(state, Event::Character('n'));
    REQUIRE(state.areaPicker);
    state.areaPicker->cursor = fixture.rowOf("test.other");
    REQUIRE(area_dialog::handleEvent(state, Event::Return) ==
            area_dialog::Outcome::Picked);
    fixture.pickArea();

    // The header is the reply's, and the area it names is the one picked. A
    // reply opens on its quote, the header behind it already filled in.
    REQUIRE(state.navigator.current() == ScreenId::Compose);
    CHECK_FALSE(state.composeInHeader);
    CHECK(state.compose.moved);
    CHECK(state.compose.reply);
    CHECK(state.compose.toName == answering);
    CHECK(state.composeArea().tag == "test.other");
    // The reader underneath has not moved: the message being answered is still
    // the one on screen, in the area it was read in.
    CHECK(state.currentArea.tag == "localnet");
    CHECK(state.readHeader->number == reading);

    state.compose.subject = "moved answer";

    // The template's @moved lines are in the editor, naming the area left
    // behind — the whole of what the move adds to an ordinary reply.
    REQUIRE_FALSE(state.edit.lines.empty());
    CHECK(state.edit.lines[0] == "*** Answering a msg posted in area localnet.");

    compose::saveMessage(state);

    // Back on the message that was answered, in the area it was read in.
    CHECK(state.navigator.current() == ScreenId::MessageRead);
    CHECK(state.currentArea.tag == "localnet");
    REQUIRE(state.readHeader);
    CHECK(state.readHeader->number == reading);
    REQUIRE(state.base != nullptr);
    CHECK(state.base->count() == hereBefore);

    // The area list counts the message where it went: one more in the other
    // area, and one more unread there, nobody having read it.
    const auto& listed =
        fixture.manager.areas()[static_cast<size_t>(fixture.rowOf("test.other"))];
    CHECK(listed.total == thereBefore + 1);
    CHECK(listed.unread == unreadBefore + 1);

    // And the message went into the other area, not into this one.
    CHECK(fixture.countIn(fixture.source) == hereBefore);
    CHECK(fixture.countIn(fixture.target) == thereBefore + 1);
}

TEST_CASE(
    "A moved reply is written under the settings of the area it goes into "
    "[other_area]") {
    // The area on screen and the area the message goes into are in different
    // groups, so a message written under the reader's settings rather than the
    // target's would carry the wrong origin and the wrong name out into the
    // network — where the origin line is the one thing naming the system it
    // came from.
    TwoAreaFixture fixture({},
                           "group\n"
                           "  member localnet\n"
                           "  origin Here at home\n"
                           "endgroup\n"
                           "group\n"
                           "  member test.other\n"
                           "  origin Over there\n"
                           "  name Someone Else\n"
                           "endgroup\n");
    auto& state = fixture.state;

    REQUIRE(message_list::enterArea(state, fixture.source).has_value());
    REQUIRE(state.readHeader);
    // In the area being read, the settings are that area's.
    CHECK(state.areaConfig.origin == "Here at home");
    CHECK(state.composeConfig().origin == "Here at home");

    message_read::handleEvent(state, Event::Character('n'));
    REQUIRE(state.areaPicker);
    state.areaPicker->cursor = fixture.rowOf("test.other");
    REQUIRE(area_dialog::handleEvent(state, Event::Return) ==
            area_dialog::Outcome::Picked);
    fixture.pickArea();

    // The moment the target is picked, the message is being written under that
    // area's settings — while the reader behind it stays in its own.
    REQUIRE(state.composeArea().tag == "test.other");
    CHECK(state.composeConfig().origin == "Over there");
    CHECK(state.composeConfig().userName == "Someone Else");
    CHECK(state.areaConfig.origin == "Here at home");
    // And the From name it was prefilled with is the target area's too.
    CHECK(state.compose.fromName == "Someone Else");

    state.compose.subject = "moved answer";
    compose::saveMessage(state);

    // The stored message carries the target area's origin line.
    amberedit::ports::IMsgBase* base =
        amberedit::test::valueOf(fixture.manager.openArea(fixture.target));
    REQUIRE(base != nullptr);
    const uint32_t last = base->count();
    const auto body = base->body(last);
    bool found = false;
    for (const auto& line : body.lines) {
        if (line.text.find("Over there") != std::string::npos) found = true;
        CHECK(line.text.find("Here at home") == std::string::npos);
    }
    CHECK(found);
}

TEST_CASE("Writing and deleting keep the area list's counts honest [other_area]") {
    TwoAreaFixture fixture;
    auto& state = fixture.state;

    const int row = fixture.rowOf("localnet");
    const uint32_t before = fixture.manager.areas()[static_cast<size_t>(row)].total;

    REQUIRE(message_list::enterArea(state, fixture.source).has_value());
    REQUIRE(state.readHeader);

    // An ordinary reply, written where it was read.
    compose::startReply(state);
    state.compose.subject = "answer";
    REQUIRE(state.navigator.current() == ScreenId::Compose);
    compose::saveMessage(state);

    CHECK(fixture.manager.areas()[static_cast<size_t>(row)].total == before + 1);
    // Read as soon as it is written — the reader opens on it — so it is not
    // among the unread.
    CHECK(fixture.manager.areas()[static_cast<size_t>(row)].unread == 0);

    // And the other way: a message taken out is one the list stops counting.
    message_read::deleteMessage(state);
    CHECK(fixture.manager.areas()[static_cast<size_t>(row)].total == before);
}

TEST_CASE("Picking the area being read is a reply, not a move [other_area]") {
    TwoAreaFixture fixture;
    auto& state = fixture.state;
    REQUIRE(message_list::enterArea(state, fixture.source).has_value());

    message_read::handleEvent(state, Event::Character('n'));
    REQUIRE(state.areaPicker);
    state.areaPicker->cursor = fixture.rowOf("localnet");
    REQUIRE(area_dialog::handleEvent(state, Event::Return) ==
            area_dialog::Outcome::Picked);
    fixture.pickArea();

    REQUIRE(state.navigator.current() == ScreenId::Compose);
    CHECK(state.compose.reply);
    // Nothing was moved, so the template has nothing to say about a move.
    CHECK_FALSE(state.compose.moved);
    CHECK(state.composeArea().tag == "localnet");

    REQUIRE_FALSE(state.edit.lines.empty());
    CHECK(state.edit.lines[0].find("Answering a msg posted") == std::string::npos);
}

TEST_CASE("A moved reply dropped leaves both areas as they were [other_area]") {
    TwoAreaFixture fixture;
    auto& state = fixture.state;

    const uint32_t thereBefore = fixture.countIn(fixture.target);
    REQUIRE(message_list::enterArea(state, fixture.source).has_value());
    REQUIRE(state.readHeader);
    const uint32_t reading = state.readHeader->number;

    message_read::handleEvent(state, Event::Character('n'));
    REQUIRE(state.areaPicker);
    state.areaPicker->cursor = fixture.rowOf("test.other");
    REQUIRE(area_dialog::handleEvent(state, Event::Return) ==
            area_dialog::Outcome::Picked);
    fixture.pickArea();
    REQUIRE(state.navigator.current() == ScreenId::Compose);

    compose::dropMessage(state);

    CHECK(state.navigator.current() == ScreenId::MessageRead);
    CHECK(state.currentArea.tag == "localnet");
    REQUIRE(state.readHeader);
    CHECK(state.readHeader->number == reading);
    CHECK(fixture.countIn(fixture.target) == thereBefore);
}

TEST_CASE("The reply_to button asks the same question the key does [other_area]") {
    TwoAreaFixture fixture;
    fixture.config.readerMenu = {amberedit::config::Command::ReaderReplyElsewhere};
    REQUIRE(message_list::enterArea(fixture.state, fixture.source).has_value());

    // Drawn so that the button knows where it landed, then clicked in the
    // middle of it and answered the way the shell answers it.
    message_read::openMenu(fixture.state);
    REQUIRE(fixture.state.menuView);
    term::Screen screen(fixture.state.width, fixture.state.height);
    term::render(screen,
                 menu_dialog::render(fixture.state, message_read::render(fixture.state)));
    REQUIRE(fixture.state.menuView->items.size() == 1);
    const auto& button = fixture.state.menuView->items[0];
    CHECK(button.enabled);

    term::MouseEvent mouse;
    mouse.x = (button.box.x_min + button.box.x_max) / 2;
    mouse.y = (button.box.y_min + button.box.y_max) / 2;
    mouse.button = term::MouseEvent::Button::Left;
    mouse.motion = term::MouseEvent::Motion::Pressed;
    REQUIRE(menu_dialog::handleEvent(fixture.state, Event::Mouse(mouse)) ==
            menu_dialog::Outcome::Picked);
    fixture.state.menuView.reset();
    message_read::runMenuCommand(fixture.state,
                                 amberedit::config::Command::ReaderReplyElsewhere);

    CHECK(fixture.state.areaPicker);
}

TEST_CASE(
    "m asks what is to become of the message before asking where "
    "[other_area]") {
    TwoAreaFixture fixture;
    auto& state = fixture.state;
    REQUIRE(message_list::enterArea(state, fixture.source).has_value());

    message_read::handleEvent(state, Event::Character('m'));
    REQUIRE(state.forwardPicker);
    // On the one answer that writes nothing by itself: the other two act the
    // moment an area is picked, and one of them empties this area of the
    // message.
    CHECK(state.forwardPicker->mode == Mode::Forward);
    CHECK_FALSE(state.areaPicker);

    // All three are on the screen, over the message they are about.
    const auto rows = forwardRowsOf(state);
    CHECK(anyRowHas(rows, "Forward"));
    CHECK(anyRowHas(rows, "Move"));
    CHECK(anyRowHas(rows, "Copy"));

    // Esc leaves the reader as it was, with neither dialog up.
    CHECK(forward_dialog::handleEvent(state, Event::Escape) ==
          forward_dialog::Outcome::Dismissed);
    CHECK_FALSE(state.forwardPicker);
    CHECK_FALSE(state.areaPicker);
    CHECK(state.navigator.current() == ScreenId::MessageRead);
}

TEST_CASE(
    "The area dialog says which of the three it is asking for "
    "[other_area]") {
    const std::vector<std::pair<Mode, std::string>> titles{
        {Mode::Forward, "Forward to area"},
        {Mode::Move, "Move to area"},
        {Mode::Copy, "Copy to area"},
    };

    for (const auto& expected : titles) {
        TwoAreaFixture fixture;
        REQUIRE(message_list::enterArea(fixture.state, fixture.source).has_value());
        fixture.askForward(expected.first);

        REQUIRE(fixture.state.areaPicker);
        CHECK(fixture.state.areaPicker->purpose == purposeOf(expected.first));
        // The same list of names, and a title saying what picking one will do.
        const auto rows = rowsOf(fixture.state);
        CHECK(anyRowHas(rows, expected.second));
        CHECK(anyRowHas(rows, "test.other"));
    }
}

TEST_CASE("The three answers are chosen by their initials as well [other_area]") {
    TwoAreaFixture fixture;
    auto& state = fixture.state;
    REQUIRE(message_list::enterArea(state, fixture.source).has_value());

    message_read::handleEvent(state, Event::Character('m'));
    REQUIRE(state.forwardPicker);
    // → walks the row: Forward, Move, Copy and round again.
    CHECK(forward_dialog::handleEvent(state, Event::ArrowRight) ==
          forward_dialog::Outcome::Ignored);
    CHECK(state.forwardPicker->mode == Mode::Move);

    // And a letter answers outright, the way y and n answer a confirmation.
    CHECK(forward_dialog::handleEvent(state, Event::Character('c')) ==
          forward_dialog::Outcome::Picked);
    CHECK(state.forwardPicker->mode == Mode::Copy);
}

TEST_CASE(
    "A message forwarded into another area goes there as a new one "
    "[other_area]") {
    TwoAreaFixture fixture;
    auto& state = fixture.state;

    const uint32_t hereBefore = fixture.countIn(fixture.source);
    const uint32_t thereBefore = fixture.countIn(fixture.target);

    REQUIRE(message_list::enterArea(state, fixture.source).has_value());
    REQUIRE(state.readHeader);
    const uint32_t reading = state.readHeader->number;
    const std::string subject = state.readHeader->subject;
    const std::string author = state.readHeader->from;
    const std::vector<std::string> passedOn = visibleLines(state);
    REQUIRE_FALSE(passedOn.empty());

    fixture.askForward(Mode::Forward);
    REQUIRE(state.areaPicker);
    state.areaPicker->cursor = fixture.rowOf("test.other");
    REQUIRE(area_dialog::handleEvent(state, Event::Return) ==
            area_dialog::Outcome::Picked);
    fixture.pickArea();

    // A new message and not an answer: addressed to the area it is going to
    // rather than to whoever wrote what is being passed on.
    // A forward is a new message, so it opens in the header: there is a
    // recipient to name before there is anything to say to them.
    REQUIRE(state.navigator.current() == ScreenId::Compose);
    CHECK(state.composeInHeader);
    CHECK(state.compose.forward);
    CHECK_FALSE(state.compose.reply);
    CHECK_FALSE(state.compose.moved);
    CHECK(state.compose.toName == "All");
    CHECK(state.compose.toName != author);
    // The subject is what the message is about wherever it is read.
    CHECK(state.compose.subject == subject);
    CHECK(state.composeArea().tag == "test.other");

    compose::handleEvent(state, Event::Return);  // To → Subject
    compose::handleEvent(state, Event::Return);  // Subject → the text
    REQUIRE_FALSE(state.composeInHeader);

    // The @forward lines name where it was read, and @message puts the message
    // itself in — unquoted, which is what tells a forward from a reply.
    REQUIRE(state.edit.lines.size() > 2 + passedOn.size());
    CHECK(state.edit.lines[0] == "* Forwarded by Yegor Gluhov");
    CHECK(state.edit.lines[1] == "* Area : localnet");
    for (size_t i = 0; i < passedOn.size(); ++i) {
        CHECK(state.edit.lines[i + 2] == passedOn[i]);
    }

    // And the editor opens where the template's @position says, under the
    // message being passed on and **above** the signature — not at the end of
    // the text, which would put the typing below the user's own name.
    const auto lines = static_cast<int>(state.edit.lines.size());
    CHECK(state.edit.row == static_cast<int>(passedOn.size()) + 2);
    REQUIRE(lines > state.edit.row + 1);
    CHECK(state.edit.lines[static_cast<size_t>(state.edit.row)].empty());
    CHECK(state.edit.lines[static_cast<size_t>(state.edit.row) + 1] == "Yegor");
    // The pair closing the message is the last of it, under the signature.
    CHECK(amberedit::domain::isOriginLine(state.edit.lines.back()));

    compose::saveMessage(state);

    // Back on the message that was passed on, where it was read.
    CHECK(state.navigator.current() == ScreenId::MessageRead);
    CHECK(state.currentArea.tag == "localnet");
    REQUIRE(state.readHeader);
    CHECK(state.readHeader->number == reading);

    CHECK(fixture.countIn(fixture.source) == hereBefore);
    CHECK(fixture.countIn(fixture.target) == thereBefore + 1);
    CHECK(
        fixture.manager.areas()[static_cast<size_t>(fixture.rowOf("test.other"))].total ==
        thereBefore + 1);
}

TEST_CASE("A forward dropped leaves both areas as they were [other_area]") {
    TwoAreaFixture fixture;
    auto& state = fixture.state;

    const uint32_t thereBefore = fixture.countIn(fixture.target);
    REQUIRE(message_list::enterArea(state, fixture.source).has_value());

    fixture.askForward(Mode::Forward);
    REQUIRE(state.areaPicker);
    state.areaPicker->cursor = fixture.rowOf("test.other");
    REQUIRE(area_dialog::handleEvent(state, Event::Return) ==
            area_dialog::Outcome::Picked);
    fixture.pickArea();
    compose::handleEvent(state, Event::Return);
    compose::handleEvent(state, Event::Return);
    REQUIRE_FALSE(state.composeInHeader);

    compose::dropMessage(state);

    CHECK(state.navigator.current() == ScreenId::MessageRead);
    CHECK(state.currentArea.tag == "localnet");
    CHECK(fixture.countIn(fixture.target) == thereBefore);
}

// --- move and copy -----------------------------------------------------------

TEST_CASE("A message copied into another area stands in both [other_area]") {
    TwoAreaFixture fixture;
    auto& state = fixture.state;

    const uint32_t hereBefore = fixture.countIn(fixture.source);
    const uint32_t thereBefore = fixture.countIn(fixture.target);
    const uint32_t unreadBefore =
        fixture.manager.areas()[static_cast<size_t>(fixture.rowOf("test.other"))].unread;

    REQUIRE(message_list::enterArea(state, fixture.source).has_value());
    REQUIRE(state.readHeader);
    const domain::MessageHeader original = *state.readHeader;
    const std::vector<std::string> text = visibleLines(state);
    const std::string msgid = amberedit::app::msgidOf(*state.readBody);
    REQUIRE_FALSE(msgid.empty());

    fixture.passOn(Mode::Copy, "test.other");

    // Nothing is written by hand, so there is no editor: the message is already
    // written, and copying it is done the moment the area is picked.
    CHECK(state.navigator.current() == ScreenId::MessageRead);
    CHECK(state.currentArea.tag == "localnet");
    REQUIRE(state.readHeader);
    // And the reader has not moved: what it is showing is still there.
    CHECK(state.readHeader->number == original.number);
    REQUIRE(state.base != nullptr);
    CHECK(state.base->count() == hereBefore);

    const auto& listed =
        fixture.manager.areas()[static_cast<size_t>(fixture.rowOf("test.other"))];
    CHECK(listed.total == thereBefore + 1);
    CHECK(listed.unread == unreadBefore + 1);

    // The same message over there, down to the date it was written and the
    // MSGID it goes by: what was copied is this message and not a new one
    // carrying its words.
    CHECK(fixture.countIn(fixture.source) == hereBefore);
    amberedit::ports::IMsgBase* base =
        amberedit::test::valueOf(fixture.manager.openArea(fixture.target));
    REQUIRE(base != nullptr);
    REQUIRE(base->count() == thereBefore + 1);
    const domain::MessageHeader copied = base->header(thereBefore + 1);
    CHECK(copied.from == original.from);
    CHECK(copied.to == original.to);
    CHECK(copied.subject == original.subject);
    CHECK(copied.attributes == original.attributes);
    CHECK(copied.date.year == original.date.year);
    CHECK(copied.date.month == original.date.month);
    CHECK(copied.date.day == original.date.day);
    CHECK(copied.date.hour == original.date.hour);
    CHECK(copied.date.minute == original.date.minute);

    const domain::MessageBody body = base->body(thereBefore + 1);
    CHECK(amberedit::app::msgidOf(body) == msgid);
    std::vector<std::string> copiedText;
    for (const auto& line : body.lines) {
        if (!line.kludge && !line.trailer) copiedText.push_back(line.text);
    }
    CHECK(copiedText == text);
}

TEST_CASE("A message moved into another area is gone from this one [other_area]") {
    TwoAreaFixture fixture;
    auto& state = fixture.state;

    const uint32_t hereBefore = fixture.countIn(fixture.source);
    const uint32_t thereBefore = fixture.countIn(fixture.target);

    REQUIRE(message_list::enterArea(state, fixture.source).has_value());
    REQUIRE(state.readHeader);
    const domain::MessageHeader original = *state.readHeader;
    const std::vector<std::string> text = visibleLines(state);

    fixture.passOn(Mode::Move, "test.other");

    // The area is one message shorter, and the reader stands on what followed
    // the message that has gone — exactly as a delete leaves it.
    CHECK(state.navigator.current() == ScreenId::MessageRead);
    CHECK(state.currentArea.tag == "localnet");
    REQUIRE(state.base != nullptr);
    CHECK(state.base->count() == hereBefore - 1);
    CHECK(state.messageCount == hereBefore - 1);
    REQUIRE(state.readHeader);
    CHECK(state.readHeader->subject != original.subject);
    // The area list is counting it where it now is and no longer where it was.
    CHECK(fixture.manager.areas()[static_cast<size_t>(fixture.rowOf("localnet"))].total ==
          hereBefore - 1);
    CHECK(
        fixture.manager.areas()[static_cast<size_t>(fixture.rowOf("test.other"))].total ==
        thereBefore + 1);

    amberedit::ports::IMsgBase* base =
        amberedit::test::valueOf(fixture.manager.openArea(fixture.target));
    REQUIRE(base != nullptr);
    REQUIRE(base->count() == thereBefore + 1);
    const domain::MessageHeader moved = base->header(thereBefore + 1);
    CHECK(moved.from == original.from);
    CHECK(moved.subject == original.subject);
    CHECK(moved.date.year == original.date.year);
    CHECK(moved.date.hour == original.date.hour);

    const domain::MessageBody body = base->body(thereBefore + 1);
    std::vector<std::string> movedText;
    for (const auto& line : body.lines) {
        if (!line.kludge && !line.trailer) movedText.push_back(line.text);
    }
    CHECK(movedText == text);
}

TEST_CASE(
    "A move into an area that will not take it keeps the message "
    "[other_area]") {
    TwoAreaFixture fixture;
    auto& state = fixture.state;

    const uint32_t hereBefore = fixture.countIn(fixture.source);
    REQUIRE(message_list::enterArea(state, fixture.source).has_value());
    REQUIRE(state.readHeader);
    const uint32_t reading = state.readHeader->number;

    // An area stating no type at all: there is no format to write it in, so the
    // base never opens. A message taken out of here on the strength of that
    // would be a message gone from both areas.
    domain::AreaConfig broken;
    broken.tag = "no.such.area";
    broken.path = fixture.here.dir().string() + "/nothing";
    message_read::moveMessage(state, broken);

    CHECK(state.navigator.current() == ScreenId::MessageRead);
    REQUIRE(state.base != nullptr);
    CHECK(state.base->count() == hereBefore);
    REQUIRE(state.readHeader);
    CHECK(state.readHeader->number == reading);
}

TEST_CASE("Moving a message into the area it is in does nothing [other_area]") {
    TwoAreaFixture fixture;
    auto& state = fixture.state;

    const uint32_t hereBefore = fixture.countIn(fixture.source);
    REQUIRE(message_list::enterArea(state, fixture.source).has_value());
    REQUIRE(state.readHeader);
    const uint32_t reading = state.readHeader->number;

    // It would be written again and the original deleted: a message renumbered
    // and nothing else.
    fixture.passOn(Mode::Move, "localnet");
    REQUIRE(state.base != nullptr);
    CHECK(state.base->count() == hereBefore);
    REQUIRE(state.readHeader);
    CHECK(state.readHeader->number == reading);

    // A copy there is a second copy, which is the only thing copying a message
    // into the area it is in can mean — and it needs no swap, that base being
    // the open one.
    fixture.passOn(Mode::Copy, "localnet");
    REQUIRE(state.base != nullptr);
    CHECK(state.base->count() == hereBefore + 1);
    CHECK(state.messageCount == hereBefore + 1);
    CHECK(fixture.manager.areas()[static_cast<size_t>(fixture.rowOf("localnet"))].total ==
          hereBefore + 1);
}

TEST_CASE("The forward button asks the same question m does [other_area]") {
    TwoAreaFixture fixture;
    fixture.config.readerMenu = {amberedit::config::Command::ReaderForward};
    REQUIRE(message_list::enterArea(fixture.state, fixture.source).has_value());

    message_read::openMenu(fixture.state);
    REQUIRE(fixture.state.menuView);
    term::Screen screen(fixture.state.width, fixture.state.height);
    term::render(screen,
                 menu_dialog::render(fixture.state, message_read::render(fixture.state)));
    REQUIRE(fixture.state.menuView->items.size() == 1);
    const auto& button = fixture.state.menuView->items[0];
    CHECK(button.enabled);

    term::MouseEvent mouse;
    mouse.x = (button.box.x_min + button.box.x_max) / 2;
    mouse.y = (button.box.y_min + button.box.y_max) / 2;
    mouse.button = term::MouseEvent::Button::Left;
    mouse.motion = term::MouseEvent::Motion::Pressed;
    REQUIRE(menu_dialog::handleEvent(fixture.state, Event::Mouse(mouse)) ==
            menu_dialog::Outcome::Picked);
    fixture.state.menuView.reset();
    message_read::runMenuCommand(fixture.state,
                                 amberedit::config::Command::ReaderForward);

    // The same first half: what is to become of the message, before where.
    REQUIRE(fixture.state.forwardPicker);
    CHECK(fixture.state.forwardPicker->mode == Mode::Forward);
    CHECK_FALSE(fixture.state.areaPicker);
}

TEST_CASE(
    "The picker opens on the first area, not on the one being read "
    "[other_area]") {
    // Descending by echoid, so the area being read below is the last of the two
    // and the first one is somebody else — with the default order the two would
    // coincide and the check would pass either way.
    TwoAreaFixture fixture({{amberedit::config::AreaSortKey::Echoid, true}});
    REQUIRE(fixture.rowOf("test.other") == 0);

    // Read the area that sorts last.
    REQUIRE(message_list::enterArea(fixture.state, fixture.source).has_value());
    REQUIRE(fixture.rowOf("localnet") == 1);

    message_read::handleEvent(fixture.state, Event::Character('n'));
    REQUIRE(fixture.state.areaPicker);
    CHECK(fixture.state.areaPicker->cursor == 0);
    fixture.state.areaPicker.reset();

    fixture.askForward(Mode::Forward);
    REQUIRE(fixture.state.areaPicker);
    CHECK(fixture.state.areaPicker->cursor == 0);
}

TEST_CASE("reply_to_area is where the reply picker opens [other_area]") {
    TwoAreaFixture fixture;
    // Written in another case than the area carries, since an echoid names the
    // same echo however it is spelled.
    fixture.config.replyToArea = "TEST.OTHER";
    REQUIRE(message_list::enterArea(fixture.state, fixture.source).has_value());

    message_read::handleEvent(fixture.state, Event::Character('n'));
    REQUIRE(fixture.state.areaPicker);
    CHECK(fixture.state.areaPicker->cursor == fixture.rowOf("test.other"));

    // The cursor is all it decides: Return is still what picks the area, and
    // the reply goes where the cursor was left.
    REQUIRE(area_dialog::handleEvent(fixture.state, Event::Return) ==
            area_dialog::Outcome::Picked);
    fixture.pickArea();
    CHECK(fixture.state.composeArea().tag == "test.other");
}

TEST_CASE(
    "reply_to_area is the reply's alone, and only where it names an area "
    "[other_area]") {
    TwoAreaFixture fixture;
    fixture.config.replyToArea = "test.other";
    REQUIRE(message_list::enterArea(fixture.state, fixture.source).has_value());
    REQUIRE(fixture.rowOf("test.other") != 0);

    // A forward, a move and a copy carry the message itself somewhere; there is
    // no area a reply setting could name for those, so they open at the top.
    for (const Mode mode : {Mode::Forward, Mode::Move, Mode::Copy}) {
        fixture.askForward(mode);
        REQUIRE(fixture.state.areaPicker);
        CHECK(fixture.state.areaPicker->cursor == 0);
        fixture.state.areaPicker.reset();
    }
}

TEST_CASE(
    "A reply_to_area naming no area opens the picker where it always did "
    "[other_area]") {
    // The areas come from the tosser config, which this setting cannot add to.
    // It is stated before the area is entered because that is when the settings
    // of an area are resolved — an area group may state this one.
    TwoAreaFixture fixture;
    fixture.config.replyToArea = "no.such.area";
    REQUIRE(message_list::enterArea(fixture.state, fixture.source).has_value());

    message_read::handleEvent(fixture.state, Event::Character('n'));
    REQUIRE(fixture.state.areaPicker);
    CHECK(fixture.state.areaPicker->cursor == 0);
}

TEST_CASE("The picker lists the areas in the area list's own order [other_area]") {
    const std::vector<std::string> tags{"localnet", "test.other"};

    // `arealist_sort` orders one list, and both screens draw it: whatever it
    // says, the dialog says the same, ascending or descending.
    for (const bool descending : {false, true}) {
        TwoAreaFixture fixture({{amberedit::config::AreaSortKey::Echoid, descending}});
        REQUIRE(message_list::enterArea(fixture.state, fixture.source).has_value());
        message_read::handleEvent(fixture.state, Event::Character('n'));
        REQUIRE(fixture.state.areaPicker);

        const std::vector<std::string> expected =
            descending ? std::vector<std::string>{"test.other", "localnet"}
                       : std::vector<std::string>{"localnet", "test.other"};
        CHECK(tagsDown(dialogRows(fixture.state), tags) == expected);
        CHECK(tagsDown(areaListRows(fixture.state), tags) == expected);
    }
}

// --- areareplydirect ---------------------------------------------------------

TEST_CASE("A reply follows the AREA: line the message begins with [other_area]") {
    TwoAreaFixture fixture;
    auto& state = fixture.state;

    const uint32_t thereBefore = fixture.countIn(fixture.target);
    const uint32_t number = writeInto(fixture, fixture.source,
                                      {"AREA:TEST.OTHER", "MSGID: 192:168/3 5f3a1b2c"});
    readMessage(fixture, number);

    // The line is the first of the message and carries no ^A of its own, which
    // is the shape a packet header has and the only one that counts.
    REQUIRE(state.readBody->lines.front().text == "AREA:TEST.OTHER");

    message_read::handleEvent(state, Event::Character('q'));

    // The moved reply `n` would have started, begun without the dialog: the
    // message had already said where its answers belong. The tag is matched
    // whatever case either side is written in.
    REQUIRE(state.navigator.current() == ScreenId::Compose);
    CHECK(state.compose.reply);
    CHECK(state.compose.moved);
    CHECK(state.composeArea().tag == "test.other");
    // And the reader underneath has not moved.
    CHECK(state.currentArea.tag == "localnet");
    REQUIRE(state.readHeader);
    CHECK(state.readHeader->number == number);
    // And the template's @moved lines are held off it: nothing was moved as far
    // as the message is concerned — it is being answered in the echo it says it
    // was posted to, and the collector it was read in is nothing the network
    // needs to be told about.
    REQUIRE_FALSE(state.edit.lines.empty());
    CHECK(state.edit.lines[0].find("Answering a msg posted") == std::string::npos);

    state.compose.subject = "direct answer";
    compose::saveMessage(state);

    // Stored where the line said, and back on the message that was answered.
    CHECK(state.navigator.current() == ScreenId::MessageRead);
    CHECK(state.currentArea.tag == "localnet");
    REQUIRE(state.readHeader);
    CHECK(state.readHeader->number == number);
    CHECK(fixture.countIn(fixture.target) == thereBefore + 1);
}

TEST_CASE(
    "The AREA: line is service data, not a line of the message "
    "[other_area]") {
    TwoAreaFixture fixture;
    auto& state = fixture.state;

    const uint32_t number = writeInto(fixture, fixture.source, {"AREA:test.other"});
    readMessage(fixture, number);

    // Marked as a kludge by the adapter, though it carries no ^A — and so out
    // of the body until the kludges are asked for, and shown as it stands when
    // they are: there is no ^A for a '@' to stand in for.
    REQUIRE(state.readBody->lines.front().kludge);
    CHECK(state.readBody->lines.front().text == "AREA:test.other");
    CHECK_FALSE(anyRowHas(readerRows(state), "AREA:test.other"));

    message_read::handleEvent(state, Event::Character('k'));
    CHECK(anyRowHas(readerRows(state), "AREA:test.other"));
    message_read::handleEvent(state, Event::Character('k'));

    // And it is not quoted into an answer, service data never being.
    message_read::handleEvent(state, Event::Character('q'));
    REQUIRE(state.navigator.current() == ScreenId::Compose);
    for (const auto& line : state.edit.lines) {
        CHECK(line.find("AREA:") == std::string::npos);
    }
}

TEST_CASE("A message changed keeps the AREA: line at its head [other_area]") {
    TwoAreaFixture fixture;
    auto& state = fixture.state;

    const uint32_t number = writeInto(fixture, fixture.source, {"AREA:test.other"});
    readMessage(fixture, number);

    // The editor opens on the message itself, which the line is no part of.
    compose::startChange(state, /*notice=*/false);
    REQUIRE(state.navigator.current() == ScreenId::Compose);
    for (const auto& line : state.edit.lines) {
        CHECK(line.find("AREA:") == std::string::npos);
    }

    compose::saveMessage(state);
    REQUIRE(state.navigator.current() == ScreenId::MessageRead);

    // Put back where the packet had it: at the head of the message, ahead of
    // the MSGID the change gave it, and service data still.
    REQUIRE(state.readBody);
    REQUIRE_FALSE(state.readBody->lines.empty());
    CHECK(state.readBody->lines.front().kludge);
    CHECK(amberedit::app::areaTagOf(*state.readBody) == "test.other");
}

TEST_CASE("The reader's title names the echo the message came from [other_area]") {
    TwoAreaFixture fixture;
    auto& state = fixture.state;

    // Written before any of them is read: opening a base closes the one open,
    // and the reader would be left pointing at it.
    const uint32_t elsewhere = writeInto(fixture, fixture.source, {"AREA:test.other"});
    const uint32_t here = writeInto(fixture, fixture.source, {"AREA:LOCALNET"});
    const uint32_t nowhere = writeInto(fixture, fixture.source, {"MSGID: 192:168/3 1"});

    // " localnet from test.other (…) 44/44": the area being read, then the echo
    // the message says it was posted to, and only where the two differ. The
    // title is the top row of the screen.
    readMessage(fixture, elsewhere);
    CHECK(readerRows(state)[0].find(" localnet from test.other") != std::string::npos);

    // Said whatever areareplydirect is: it is a fact about the message.
    fixture.config.areaReplyDirect = false;
    CHECK(readerRows(state)[0].find(" localnet from test.other") != std::string::npos);
    fixture.config.areaReplyDirect = true;

    // A message naming the area it is being read in adds nothing to the title,
    // and neither does one naming no area at all.
    readMessage(fixture, here);
    CHECK(readerRows(state)[0].find(" from ") == std::string::npos);
    readMessage(fixture, nowhere);
    CHECK(readerRows(state)[0].find(" from ") == std::string::npos);
}

TEST_CASE("areareplydirect off leaves the answer where it was read [other_area]") {
    TwoAreaFixture fixture;
    auto& state = fixture.state;
    fixture.config.areaReplyDirect = false;

    const uint32_t number = writeInto(fixture, fixture.source, {"AREA:test.other"});
    readMessage(fixture, number);

    message_read::handleEvent(state, Event::Character('q'));
    REQUIRE(state.navigator.current() == ScreenId::Compose);
    CHECK(state.compose.reply);
    CHECK_FALSE(state.compose.moved);
    CHECK(state.composeArea().tag == "localnet");
}

TEST_CASE("A group decides whether replies follow the AREA: line [other_area]") {
    // The setting is one an area group may state, so a base whose messages
    // carry the line can follow it while the rest of the config does not.
    TwoAreaFixture fixture({},
                           "group\n"
                           "  member localnet\n"
                           "  areareplydirect off\n"
                           "endgroup\n");
    auto& state = fixture.state;

    const uint32_t number = writeInto(fixture, fixture.source, {"AREA:test.other"});
    readMessage(fixture, number);
    REQUIRE_FALSE(state.areaConfig.areaReplyDirect);

    message_read::handleEvent(state, Event::Character('q'));
    REQUIRE(state.navigator.current() == ScreenId::Compose);
    CHECK_FALSE(state.compose.moved);
    CHECK(state.composeArea().tag == "localnet");
}

TEST_CASE(
    "Only the first line of a message names the area to answer in "
    "[other_area]") {
    TwoAreaFixture fixture;
    auto& state = fixture.state;

    // Behind a MSGID it is no longer the packet header: whatever it says, it is
    // a line of a message that happens to begin with those five characters.
    const uint32_t number = writeInto(fixture, fixture.source,
                                      {"MSGID: 192:168/3 5f3a1b2c", "AREA:test.other"});
    readMessage(fixture, number);
    REQUIRE(state.readBody->lines.front().text != "AREA:test.other");

    message_read::handleEvent(state, Event::Character('q'));
    REQUIRE(state.navigator.current() == ScreenId::Compose);
    CHECK_FALSE(state.compose.moved);
    CHECK(state.composeArea().tag == "localnet");
}

TEST_CASE(
    "An AREA: line naming no area of ours is answered where it was read "
    "[other_area]") {
    TwoAreaFixture fixture;
    auto& state = fixture.state;

    // An echo the tosser config does not declare — one unsubscribed since the
    // message arrived, or renamed. There is nowhere to put the answer but here.
    const uint32_t number = writeInto(fixture, fixture.source, {"AREA:ru.unsubscribed"});
    readMessage(fixture, number);

    message_read::handleEvent(state, Event::Character('q'));
    REQUIRE(state.navigator.current() == ScreenId::Compose);
    CHECK_FALSE(state.compose.moved);
    CHECK(state.composeArea().tag == "localnet");
}

TEST_CASE("An AREA: line naming the area being read moves nothing [other_area]") {
    TwoAreaFixture fixture;
    auto& state = fixture.state;

    const uint32_t number = writeInto(fixture, fixture.source, {"AREA:LOCALNET"});
    readMessage(fixture, number);

    message_read::handleEvent(state, Event::Character('q'));
    REQUIRE(state.navigator.current() == ScreenId::Compose);
    // The base would be swapped for itself, and the template would write
    // "Answering a msg posted in area localnet" in localnet.
    CHECK_FALSE(state.compose.moved);
    CHECK(state.composeArea().tag == "localnet");
    REQUIRE_FALSE(state.edit.lines.empty());
    CHECK(state.edit.lines[0].find("Answering a msg posted") == std::string::npos);
}

TEST_CASE("An area picked by hand outranks the AREA: line [other_area]") {
    TwoAreaFixture fixture;
    auto& state = fixture.state;

    const uint32_t number = writeInto(fixture, fixture.source, {"AREA:test.other"});
    readMessage(fixture, number);

    // `n`, answered with the area being read: an area asked for by name is
    // asked for, and the line does not get to overrule it.
    message_read::handleEvent(state, Event::Character('n'));
    REQUIRE(state.areaPicker);
    state.areaPicker->cursor = fixture.rowOf("localnet");
    REQUIRE(area_dialog::handleEvent(state, Event::Return) ==
            area_dialog::Outcome::Picked);
    fixture.pickArea();

    REQUIRE(state.navigator.current() == ScreenId::Compose);
    CHECK(state.compose.reply);
    CHECK_FALSE(state.compose.moved);
    CHECK(state.composeArea().tag == "localnet");
}

TEST_CASE(
    "An echo answered into netmail is written from the akamatch AKA "
    "[other_area]") {
    // The reported case: a message out of a packet, answered with `reply_to`
    // into the netmail area. Nothing of ours was written to, so which AKA the
    // answer goes out under is the destination's to decide.
    TwoAreaFixture fixture({}, "", domain::AreaKind::Netmail);
    auto& state = fixture.state;

    // The AKA this system writes to zone 192 under, which is where the message
    // below was posted from.
    amberedit::config::AkaMatch aka;
    aka.aka = *domain::FtnAddress::parse("3:633/280");
    aka.patterns = {*amberedit::domain::AddressPattern::parse("192:*")};
    fixture.config.akaMatches = {aka};

    // It begins with the `AREA:` line naming the echo it was posted to, and
    // carries this system's own address in the field a netmail would name its
    // recipient in — which is nothing anybody wrote to.
    const uint32_t number = writeInto(fixture, fixture.source, {"AREA:RU.LINUX"},
                                      /*destAddr=*/"192:168/2");
    readMessage(fixture, number);

    message_read::handleEvent(state, Event::Character('n'));
    REQUIRE(state.areaPicker);
    state.areaPicker->cursor = fixture.rowOf("netmail");
    REQUIRE(area_dialog::handleEvent(state, Event::Return) ==
            area_dialog::Outcome::Picked);
    fixture.pickArea();

    REQUIRE(state.navigator.current() == ScreenId::Compose);
    CHECK(state.compose.netmail);
    CHECK(state.compose.toName == "Ivan Petrov");
    CHECK(state.compose.toAddr == "192:168/3");
    CHECK(state.compose.fromAddr == "3:633/280");
}

TEST_CASE("A forward is not moved by the AREA: line [other_area]") {
    TwoAreaFixture fixture;
    auto& state = fixture.state;

    const uint32_t number = writeInto(fixture, fixture.source, {"AREA:test.other"});
    readMessage(fixture, number);

    // Passing a message on is a message of one's own, and where it goes is the
    // question `m` asks: nothing about it follows the message being passed on.
    fixture.askForward(Mode::Forward);
    REQUIRE(state.areaPicker);
    CHECK(state.navigator.current() == ScreenId::MessageRead);
}

TEST_CASE(
    "A forward opens on the line the template positions the cursor on "
    "[other_area]") {
    TwoAreaFixture fixture;
    auto& state = fixture.state;
    REQUIRE(message_list::enterArea(state, fixture.source).has_value());

    // A window with three rows to write in, so that the message being passed on
    // is longer than what fits and there is something to scroll.
    constexpr int kRows = 3;
    state.height = 3 + amberedit::ui::AppState::kHeaderRows + kRows;

    fixture.askForward(Mode::Forward);
    REQUIRE(state.areaPicker);
    state.areaPicker->cursor = fixture.rowOf("test.other");
    REQUIRE(area_dialog::handleEvent(state, Event::Return) ==
            area_dialog::Outcome::Picked);
    fixture.pickArea();
    compose::handleEvent(state, Event::Return);
    compose::handleEvent(state, Event::Return);
    REQUIRE_FALSE(state.composeInHeader);

    const auto lines = static_cast<int>(state.edit.lines.size());
    REQUIRE(compose::editorRows(state) == kRows);
    REQUIRE(lines > kRows);  // otherwise there would be nothing to scroll

    // The template's @position stands under the forwarded message and over the
    // signature, so the cursor is three lines off the end — the signature, the
    // tearline and the origin below it — and not on the last of them.
    CHECK(state.edit.row == lines - 4);
    // Scrolled only far enough to show it, as every other message is: the
    // cursor is on the window, and what is under it is the end of the message.
    CHECK(state.edit.row >= state.editScroll);
    CHECK(state.edit.row < state.editScroll + kRows);
    CHECK(state.editScroll == state.edit.row - kRows + 1);
}

TEST_CASE("m asks which messages once anything is marked [other_area][marks]") {
    TwoAreaFixture fixture;
    auto& state = fixture.state;
    REQUIRE(message_list::enterArea(state, fixture.source).has_value());

    SUBCASE("with nothing marked it asks what it always asked") {
        REQUIRE(message_read::handleEvent(state, Event::Character('m')));
        CHECK_FALSE(state.scopePicker);
        REQUIRE(state.forwardPicker);
        CHECK_FALSE(state.forwardPicker->marked);
        CHECK(state.forwardPicker->mode == Mode::Forward);
    }
    SUBCASE("with a set standing it asks which messages first") {
        marks::toggle(state, 2);
        REQUIRE(message_read::handleEvent(state, Event::Character('m')));
        REQUIRE(state.scopePicker);
        CHECK_FALSE(state.forwardPicker);
        CHECK(state.scopePicker->purpose ==
              amberedit::ui::AppState::ScopePicker::For::Forward);
        CHECK(state.scopePicker->marked == 1);
    }
}

TEST_CASE(
    "The marked set is offered Copy and Move and no Forward "
    "[other_area][marks]") {
    TwoAreaFixture fixture;
    auto& state = fixture.state;
    REQUIRE(message_list::enterArea(state, fixture.source).has_value());
    marks::toggle(state, 2);

    fixture.askScope(amberedit::ui::AppState::ScopePicker::Mode::Marked);
    REQUIRE(state.forwardPicker);
    CHECK(state.forwardPicker->marked);
    // It opens on Copy, the one that takes nothing out of the area.
    CHECK(state.forwardPicker->mode == Mode::Copy);

    const std::vector<std::string> rows = forwardRowsOf(state);
    CHECK(anyRowHas(rows, "Copy"));
    CHECK(anyRowHas(rows, "Move"));
    CHECK_FALSE(anyRowHas(rows, "Forward"));

    // The ring is the two, and `f` says nothing to a box not showing Forward.
    CHECK(forward_dialog::handleEvent(state, Event::ArrowRight) ==
          forward_dialog::Outcome::Ignored);
    CHECK(state.forwardPicker->mode == Mode::Move);
    CHECK(forward_dialog::handleEvent(state, Event::ArrowRight) ==
          forward_dialog::Outcome::Ignored);
    CHECK(state.forwardPicker->mode == Mode::Copy);
    CHECK(forward_dialog::handleEvent(state, Event::Character('f')) ==
          forward_dialog::Outcome::Ignored);
    CHECK(state.forwardPicker->mode == Mode::Copy);
}

TEST_CASE(
    "Answering the scope with Current forwards the message as ever "
    "[other_area][marks]") {
    TwoAreaFixture fixture;
    auto& state = fixture.state;
    REQUIRE(message_list::enterArea(state, fixture.source).has_value());
    marks::toggle(state, 2);

    fixture.askScope(amberedit::ui::AppState::ScopePicker::Mode::Current);
    REQUIRE(state.forwardPicker);
    CHECK_FALSE(state.forwardPicker->marked);
    CHECK(state.forwardPicker->mode == Mode::Forward);
    CHECK(anyRowHas(forwardRowsOf(state), "Forward"));
}

TEST_CASE("Cancelling the scope leaves everything as it was [other_area][marks]") {
    TwoAreaFixture fixture;
    auto& state = fixture.state;
    REQUIRE(message_list::enterArea(state, fixture.source).has_value());
    marks::toggle(state, 2);

    fixture.askScope(amberedit::ui::AppState::ScopePicker::Mode::Cancel);
    CHECK_FALSE(state.forwardPicker);
    CHECK_FALSE(state.areaPicker);
    CHECK(state.marks.size() == 1);
}

TEST_CASE("The marked messages copied stand in both areas [other_area][marks]") {
    TwoAreaFixture fixture;
    auto& state = fixture.state;

    const uint32_t hereBefore = fixture.countIn(fixture.source);
    const uint32_t thereBefore = fixture.countIn(fixture.target);
    REQUIRE(message_list::enterArea(state, fixture.source).has_value());
    REQUIRE(state.messageCount > 4);

    marks::toggle(state, 2);
    marks::toggle(state, 4);
    const std::string second = state.base->header(2).subject;
    const std::string fourth = state.base->header(4).subject;
    message_read::goToMessage(state, 3);

    fixture.passOnMarked(Mode::Copy, "test.other");

    // Nothing left here, and the reader is where it was.
    CHECK(fixture.countIn(fixture.source) == hereBefore);
    REQUIRE(state.readHeader);
    CHECK(state.readHeader->number == 3);
    // The set stands: the messages are still where they were.
    CHECK(state.marks.size() == 2);

    amberedit::ports::IMsgBase* base =
        amberedit::test::valueOf(fixture.manager.openArea(fixture.target));
    REQUIRE(base != nullptr);
    REQUIRE(base->count() == thereBefore + 2);
    // In the order they stood in the area rather than in whatever order the set
    // happened to be walked in.
    CHECK(base->header(thereBefore + 1).subject == second);
    CHECK(base->header(thereBefore + 2).subject == fourth);
    fixture.manager.closeCurrentArea();
}

TEST_CASE("The marked messages moved are gone from this area [other_area][marks]") {
    TwoAreaFixture fixture;
    auto& state = fixture.state;

    const uint32_t hereBefore = fixture.countIn(fixture.source);
    const uint32_t thereBefore = fixture.countIn(fixture.target);
    REQUIRE(message_list::enterArea(state, fixture.source).has_value());
    REQUIRE(state.messageCount > 4);

    marks::toggle(state, 2);
    marks::toggle(state, 4);
    const std::string second = state.base->header(2).subject;
    const std::string fourth = state.base->header(4).subject;
    const std::string third = state.base->header(3).subject;
    message_read::goToMessage(state, 3);

    fixture.passOnMarked(Mode::Move, "test.other");

    CHECK(state.messageCount == hereBefore - 2);
    CHECK(fixture.countIn(fixture.source) == hereBefore - 2);
    // The reader is still on the message it was showing, wherever the
    // renumbering left it, and the set is spent.
    REQUIRE(state.readHeader);
    CHECK(state.readHeader->subject == third);
    CHECK(state.readHeader->number == 2);
    CHECK(state.marks.empty());

    amberedit::ports::IMsgBase* base =
        amberedit::test::valueOf(fixture.manager.openArea(fixture.target));
    REQUIRE(base != nullptr);
    REQUIRE(base->count() == thereBefore + 2);
    CHECK(base->header(thereBefore + 1).subject == second);
    CHECK(base->header(thereBefore + 2).subject == fourth);
    fixture.manager.closeCurrentArea();
}

TEST_CASE(
    "Moving the marked set into the area it is in does nothing "
    "[other_area][marks]") {
    TwoAreaFixture fixture;
    auto& state = fixture.state;
    REQUIRE(message_list::enterArea(state, fixture.source).has_value());
    const uint32_t before = state.messageCount;
    marks::toggle(state, 2);

    message_read::moveMarked(state, fixture.source);

    CHECK(state.messageCount == before);
    CHECK(state.marks.size() == 1);
}

TEST_CASE(
    "Copying the marked set into the area it is in doubles them "
    "[other_area][marks]") {
    TwoAreaFixture fixture;
    auto& state = fixture.state;
    REQUIRE(message_list::enterArea(state, fixture.source).has_value());
    const uint32_t before = state.messageCount;
    marks::toggle(state, 2);
    marks::toggle(state, 3);

    message_read::copyMarked(state, fixture.source);

    CHECK(state.messageCount == before + 2);
    // The originals are still marked; the copies beside them are not.
    CHECK(state.marks.size() == 2);
    CHECK(marks::isMarked(state, 2));
    CHECK(marks::isMarked(state, 3));
    CHECK_FALSE(marks::isMarked(state, before + 1));
}
