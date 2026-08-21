#include <doctest/doctest.h>

#include <cstdint>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "app/area_manager.hpp"
#include "temp_dir.hpp"
#include "temp_squish_base.hpp"
#include "ui/app_state.hpp"
#include "ui/error_dialog.hpp"
#include "ui/screens/area_list_screen.hpp"
#include "ui/term/element.hpp"
#include "ui/term/screen.hpp"
#include "ui/theme.hpp"

using amberedit::app::AreaManager;
using amberedit::config::AppConfig;
using amberedit::domain::AreaConfig;
using amberedit::domain::MsgBaseType;
using amberedit::test::TempSquishBase;
using amberedit::ui::AppState;
using amberedit::ui::term::Event;

namespace area_list = amberedit::ui::screens::area_list;

namespace {

/// An area source the test can change between reloads, which is what a rescan
/// is for: the tosser config is read again, not only the bases.
class MutableAreaSource final : public amberedit::ports::IAreaConfigSource {
public:
    explicit MutableAreaSource(std::vector<AreaConfig>* areas) : areas_(areas) {}
    std::vector<AreaConfig> loadAreas() override { return *areas_; }

private:
    std::vector<AreaConfig>* areas_;
};

/// A mark the test moves by hand, so that "unread" can be made to change
/// without a lastread file on disk.
///
/// One mark for every area, and a mark of its own for an area named: the areas
/// a test builds out of copies of the same base would otherwise all stand read
/// to the same point, and what tells them apart is the whole of what some tests
/// are about.
class StubLastReadStore final : public amberedit::ports::ILastReadStore {
public:
    uint32_t getLastRead(const AreaConfig& area) override {
        const auto found = perArea_.find(area.tag);
        return found == perArea_.end() ? uid_ : found->second;
    }
    void setLastRead(const AreaConfig& /*area*/, uint32_t uid) override { uid_ = uid; }

    void set(uint32_t uid) { uid_ = uid; }
    void set(const std::string& tag, uint32_t uid) { perArea_[tag] = uid; }

private:
    uint32_t uid_{0};
    std::map<std::string, uint32_t> perArea_;
};

/// One drawn row, as it stands on the screen.
std::string rowText(const amberedit::ui::term::Screen& screen, int y) {
    std::string row;
    for (int x = 0; x < screen.width(); ++x) row += screen.at(x, y).glyph;
    return row;
}

/// The rightmost column over `count` rows starting at `from` — where the
/// scrollbar stands when it is drawn at all.
std::string barColumn(const amberedit::ui::term::Screen& screen, int from, int count) {
    std::string column;
    for (int y = from; y < from + count; ++y)
        column += screen.at(screen.width() - 1, y).glyph;
    return column;
}

AreaConfig passthroughArea(const std::string& tag) {
    AreaConfig area;
    area.tag = tag;
    // Passthrough keeps reload() away from the disk where the test is about the
    // list rather than about any base in it.
    area.type = MsgBaseType::Passthrough;
    return area;
}

/// A list of areas named "area1" upwards — more of them than a short window can
/// show, which is what the scrollbar is drawn for.
std::vector<AreaConfig> numberedAreas(int count) {
    std::vector<AreaConfig> areas;
    areas.reserve(static_cast<size_t>(count));
    for (int i = 1; i <= count; ++i)
        areas.push_back(passthroughArea("area" + std::to_string(i)));
    return areas;
}

AreaConfig squishArea(const std::string& tag, const std::string& path) {
    AreaConfig area;
    area.tag = tag;
    area.path = path;
    area.type = MsgBaseType::Squish;
    return area;
}

/// A state and everything it refers to, kept alive together: AppState holds
/// references to both the manager and the config.
struct Fixture {
    explicit Fixture(std::vector<AreaConfig> initial)
        : areas(std::move(initial)),
          config(unsorted()),
          lastRead(new StubLastReadStore),
          manager(std::make_unique<MutableAreaSource>(&areas),
                  std::unique_ptr<StubLastReadStore>(lastRead), config),
          state(manager, config) {
        manager.reload();
    }

    /// The tosser config's own order, so that what a rescan does to the list is
    /// not read through what `arealist_sort` does to it — that has its own tests.
    static AppConfig unsorted() {
        AppConfig cfg;
        cfg.areaListSort.clear();
        return cfg;
    }

    std::vector<AreaConfig> areas;
    AppConfig config;
    /// Owned by the manager; kept here so the test can move the mark.
    StubLastReadStore* lastRead;
    AreaManager manager;
    AppState state;
};

}  // namespace

TEST_CASE("Ctrl-R asks for a rescan rather than typing into the quick search "
          "[arealist]") {
    Fixture fixture({passthroughArea("one"), passthroughArea("two")});

    REQUIRE(
        area_list::handleEvent(fixture.state, Event::Character("r", true, false, false)));
    CHECK(fixture.state.rescanning);
    CHECK(fixture.state.areaSearch.empty());
}

TEST_CASE("The bare letter still types into the quick search [arealist]") {
    Fixture fixture({passthroughArea("one"), passthroughArea("two")});

    REQUIRE(area_list::handleEvent(fixture.state, Event::Character("t")));
    CHECK(fixture.state.areaSearch == "t");
    CHECK_FALSE(fixture.state.rescanning);
    // And it found the area it names, the search being what the letter is for.
    CHECK(fixture.state.areaCursor == 1);
}

TEST_CASE("A letter a layout has made a command stops typing into the search "
          "[arealist][keys]") {
    Fixture fixture({passthroughArea("one"), passthroughArea("two")});
    fixture.state.keys = amberedit::ui::KeyMap::parse("t arealist.rescan\n", "keys");

    // What a bare letter costs when it is bound here: the command is answered
    // before the search, so the letter is no longer one an area can be found by.
    REQUIRE(area_list::handleEvent(fixture.state, Event::Character("t")));
    CHECK(fixture.state.rescanning);
    CHECK(fixture.state.areaSearch.empty());

    // The slash the defaults keep for the next unread area is a letter like any
    // other under a layout that does not name it.
    fixture.state.rescanning = false;
    REQUIRE(area_list::handleEvent(fixture.state, Event::Character("/")));
    CHECK(fixture.state.areaSearch == "/");
    CHECK_FALSE(fixture.state.rescanning);
}

TEST_CASE("The slash goes to the next area with unread messages [arealist][squish]") {
    const TempSquishBase first;
    const TempSquishBase second;
    const TempSquishBase third;
    Fixture fixture({squishArea("first", first.path()),
                     squishArea("second", second.path()),
                     squishArea("third", third.path())});
    REQUIRE(fixture.manager.areas().size() == 3);

    // The three are copies of the same base, so nothing is read in any of them.
    // "second" is marked read to its end — the mark is a UID, so it has to be
    // taken from the base itself — and the counts read again with it in place.
    const AreaConfig middle = fixture.manager.areas()[1].config;
    const uint32_t total = fixture.manager.areas()[1].total;
    REQUIRE(total > 0);
    amberedit::ports::IMsgBase* opened = fixture.manager.openArea(middle);
    REQUIRE(opened != nullptr);
    const uint32_t uid = opened->uidOf(total);
    REQUIRE(uid != 0);
    fixture.manager.closeCurrentArea();
    fixture.lastRead->set("second", uid);
    fixture.manager.reload();

    REQUIRE(fixture.manager.areas()[0].unread > 0);
    REQUIRE(fixture.manager.areas()[1].unread == 0);
    REQUIRE(fixture.manager.areas()[2].unread > 0);

    // From the first area the cursor goes past the one that is read through,
    // and the key is not typed into the quick search on the way.
    fixture.state.areaCursor = 0;
    REQUIRE(area_list::handleEvent(fixture.state, Event::Character("/")));
    CHECK(fixture.state.areaCursor == 2);
    CHECK(fixture.state.areaSearch.empty());

    // And round the end of the list, back to the first: the last unread area is
    // not where the walking stops.
    REQUIRE(area_list::handleEvent(fixture.state, Event::Character("/")));
    CHECK(fixture.state.areaCursor == 0);
}

TEST_CASE("The slash leaves the cursor where it is when nothing is unread "
          "[arealist]") {
    // Passthrough areas hold nothing and can hold nothing unread: the key is
    // still the list's — it is swallowed rather than typed — and the cursor
    // stays on the area it was on.
    Fixture fixture({passthroughArea("one"), passthroughArea("two")});
    fixture.state.areaCursor = 1;

    REQUIRE(area_list::handleEvent(fixture.state, Event::Character("/")));
    CHECK(fixture.state.areaCursor == 1);
    CHECK(fixture.state.areaSearch.empty());
}

TEST_CASE("The area list draws the columns arealist_format asks for [arealist]") {
    using amberedit::config::AreaFieldKind;
    Fixture fixture({passthroughArea("one"), passthroughArea("two")});
    fixture.config.areaListFormatNarrow = {{{AreaFieldKind::Number, 3},
                                            {AreaFieldKind::Space, 1},
                                            {AreaFieldKind::Echoid, 0}}};
    fixture.state.width = 20;

    namespace term = amberedit::ui::term;
    term::Screen screen(fixture.state.width, fixture.state.height);
    term::render(screen, area_list::render(fixture.state));

    // The heading follows the format — no counts asked for, none drawn — and the
    // name takes what the numbered column leaves, margins on both sides apart.
    CHECK(rowText(screen, 0) == "   # Area           ");
    CHECK(rowText(screen, 2) == "   1 one            ");
    CHECK(rowText(screen, 3) == "   2 two            ");
}

TEST_CASE("The width the window has picks which of the two formats a row follows "
          "[arealist]") {
    using amberedit::config::AreaFieldKind;
    AreaConfig described = passthroughArea("one");
    described.description = "The first area";
    Fixture fixture({described});
    fixture.config.adaptiveUiThreshold = 30;
    fixture.config.areaListFormatNarrow = {{{AreaFieldKind::Echoid, 0}}};
    fixture.config.areaListFormatWide = {{{AreaFieldKind::Echoid, 8},
                                          {AreaFieldKind::Space, 1},
                                          {AreaFieldKind::Description, 0}}};

    namespace term = amberedit::ui::term;
    // Under the threshold the narrow format stands: the name and nothing beside
    // it, the window having no columns to spare.
    fixture.state.width = 29;
    term::Screen narrow(fixture.state.width, fixture.state.height);
    term::render(narrow, area_list::render(fixture.state));
    CHECK(rowText(narrow, 0) == " Area                        ");
    CHECK(rowText(narrow, 2) == " one                         ");

    // At it, the wide one — the same list, read again on the next frame, which
    // is all a dragged window does.
    fixture.state.width = 30;
    term::Screen wide(fixture.state.width, fixture.state.height);
    term::render(wide, area_list::render(fixture.state));
    CHECK(rowText(wide, 0) == " Area     Description         ");
    CHECK(rowText(wide, 2) == " one      The first area      ");
}

TEST_CASE("A format written on several lines draws an area on all of them "
          "[arealist]") {
    using amberedit::config::AreaFieldKind;
    AreaConfig first = passthroughArea("one");
    first.description = "The first area";
    AreaConfig second = passthroughArea("two");
    second.description = "The second area";
    Fixture fixture({first, second});
    // The name on one line and the description under it — what a narrow window
    // has no columns to put side by side.
    fixture.config.areaListFormatNarrow = {{{AreaFieldKind::Echoid, 0}},
                                           {{AreaFieldKind::Description, 0}}};
    fixture.state.width = 20;
    fixture.state.height = 9;  // seven lines: three whole rows, and one over

    namespace term = amberedit::ui::term;
    term::Screen screen(fixture.state.width, fixture.state.height);
    term::render(screen, area_list::render(fixture.state));

    // One heading row however tall a row is, over the line the row is read from
    // first.
    CHECK(rowText(screen, 0) == " Area               ");
    CHECK(rowText(screen, 2) == " one                ");
    CHECK(rowText(screen, 3) == " The first area     ");
    CHECK(rowText(screen, 4) == " two                ");
    CHECK(rowText(screen, 5) == " The second area    ");
    // Past the end of the list, and the line at the bottom no whole row fitted
    // in: a row is drawn whole or not at all.
    CHECK(rowText(screen, 6) == "                    ");
    CHECK(rowText(screen, 8) == "                    ");
}

TEST_CASE("A window too short for a whole row still shows an area [arealist]") {
    using amberedit::config::AreaFieldKind;
    AreaConfig first = passthroughArea("one");
    first.description = "The first area";
    Fixture fixture({first, passthroughArea("two")});
    fixture.config.areaListFormatNarrow = {{{AreaFieldKind::Echoid, 0}},
                                           {{AreaFieldKind::Description, 0}}};
    fixture.state.width = 20;
    fixture.state.height = 3;  // one line for the list, and a row wants two

    namespace term = amberedit::ui::term;
    term::Screen screen(fixture.state.width, fixture.state.height);
    term::render(screen, area_list::render(fixture.state));

    // There is always an area on the screen: as much of the first row as there
    // is room for, from the top down.
    CHECK(rowText(screen, 2) == " one                ");
}

TEST_CASE("A click on any line of a row is a click on that area [arealist]") {
    using amberedit::config::AreaFieldKind;
    Fixture fixture(numberedAreas(6));
    fixture.config.areaListFormatNarrow = {{{AreaFieldKind::Echoid, 0}},
                                           {{AreaFieldKind::Description, 0}}};
    fixture.state.width = 20;
    fixture.state.height = 10;

    namespace term = amberedit::ui::term;
    const auto clickAt = [](int y) {
        term::MouseEvent mouse;
        mouse.x = 2;
        mouse.y = y;
        mouse.button = term::MouseEvent::Button::Left;
        mouse.motion = term::MouseEvent::Motion::Pressed;
        return Event::Mouse(mouse);
    };

    // The description under a name is the same area as the name: the second
    // line of the second row is the second area, not the fourth.
    REQUIRE(area_list::handleEvent(fixture.state, clickAt(5)));
    CHECK(fixture.state.areaCursor == 1);
    // And the line above it, the same area again.
    fixture.state.errorMessage.clear();
    REQUIRE(area_list::handleEvent(fixture.state, clickAt(4)));
    CHECK(fixture.state.areaCursor == 1);
}

TEST_CASE("The selected area keeps its place on the screen when a row changes height "
          "[arealist]") {
    using amberedit::config::AreaFieldKind;
    Fixture fixture(numberedAreas(20));
    fixture.config.adaptiveUiThreshold = 30;
    // Two lines to a row in a narrow window and one in a wide one, which is
    // what the defaults do: a screen of twelve lines holds six areas or twelve.
    fixture.config.areaListFormatNarrow = {{{AreaFieldKind::Echoid, 0}},
                                           {{AreaFieldKind::Description, 0}}};
    fixture.config.areaListFormatWide = {{{AreaFieldKind::Echoid, 0}}};
    fixture.state.height = 14;  // twelve lines for the list

    namespace term = amberedit::ui::term;
    const auto draw = [&fixture] {
        term::Screen screen(fixture.state.width, fixture.state.height);
        term::render(screen, area_list::render(fixture.state));
    };

    // Narrow first, with the cursor on the bottom row of the six.
    fixture.state.width = 29;
    fixture.state.areaCursor = 10;
    draw();
    REQUIRE(fixture.state.areaOffset == 5);

    // Widened: the rows are half as tall, so twelve areas stand where six did.
    // The cursor was on the eleventh line of the list and it stays there — the
    // areas above it are counted again rather than kept.
    fixture.state.width = 30;
    draw();
    CHECK(fixture.state.areaCursor == 10);
    CHECK(fixture.state.areaOffset == 0);

    // And back: the same line again, which is the offset it started from.
    fixture.state.width = 29;
    draw();
    CHECK(fixture.state.areaCursor == 10);
    CHECK(fixture.state.areaOffset == 5);
}

TEST_CASE("The scrollbar beside tall rows counts areas, not lines [arealist]") {
    using amberedit::config::AreaFieldKind;
    Fixture fixture(numberedAreas(20));
    fixture.config.areaListFormatNarrow = {{{AreaFieldKind::Echoid, 0}},
                                           {{AreaFieldKind::Description, 0}}};
    fixture.config.areaListScrollbar = true;
    fixture.state.width = 20;
    fixture.state.height = 8;  // six lines: three areas of two lines each

    namespace term = amberedit::ui::term;
    term::Screen screen(fixture.state.width, fixture.state.height);
    term::render(screen, area_list::render(fixture.state));

    // Three areas of twenty: the thumb points at the first of them, and covers
    // both the lines that area is drawn on rather than one of them.
    CHECK(barColumn(screen, 2, 6) == "██││││");

    // Scrolled to the end, it stands at the bottom — again over the whole of
    // the row it points at.
    fixture.state.areaCursor = 19;
    REQUIRE(area_list::handleEvent(fixture.state, Event::End));
    term::render(screen, area_list::render(fixture.state));
    CHECK(barColumn(screen, 2, 6) == "││││██");
}

TEST_CASE("The description column is drawn quiet, the area's own words and all "
          "[arealist][squish]") {
    using amberedit::config::AreaFieldKind;
    namespace theme = amberedit::ui::theme;

    const TempSquishBase described;
    const TempSquishBase bare;
    const TempSquishBase under;
    AreaConfig speaks = squishArea("one", described.path());
    speaks.description = "The first area";
    // The cursor stands on the first row and a selected row keeps the
    // selection's colors throughout, so the two rows the colors are read off
    // are both below it.
    Fixture fixture({squishArea("zero", under.path()), speaks,
                     squishArea("two", bare.path())});
    fixture.config.areaListFormatNarrow = {{{AreaFieldKind::Echoid, 8},
                                            {AreaFieldKind::Space, 1},
                                            {AreaFieldKind::Description, 0}}};
    fixture.config.areaListFormatWide = fixture.config.areaListFormatNarrow;
    fixture.state.width = 30;
    fixture.state.height = 10;

    namespace term = amberedit::ui::term;
    term::Screen screen(fixture.state.width, fixture.state.height);
    term::render(screen, area_list::render(fixture.state));

    // The description column starts past the margin, the name and the gap.
    constexpr int kDescription = 1 + 8 + 1;
    REQUIRE(rowText(screen, 3).substr(0, 24) == " one      The first area");
    REQUIRE(rowText(screen, 4).substr(0, 24) == " two      no description");

    // The column is prose either way — what the area said about itself as much
    // as what the config stands in with where it said nothing — and quiet like
    // a kludge line.
    CHECK(screen.at(kDescription, 3).fg == theme::palette.dimmed);
    CHECK(screen.at(kDescription, 4).fg == theme::palette.dimmed);
    // Only that column is quiet: the names beside it are what the list is read
    // down, and they are drawn as ever.
    CHECK(screen.at(1, 3).fg != theme::palette.dimmed);
    CHECK(screen.at(1, 4).fg != theme::palette.dimmed);
}

TEST_CASE("The area list draws the scrollbar where the list does not fit [arealist]") {
    using amberedit::config::AreaFieldKind;
    Fixture fixture(numberedAreas(20));
    fixture.config.areaListFormatNarrow = {{{AreaFieldKind::Number, 3},
                                            {AreaFieldKind::Space, 1},
                                            {AreaFieldKind::Echoid, 0}}};
    fixture.config.areaListScrollbar = true;  // off by default
    fixture.state.width = 20;
    fixture.state.height = 8;  // six rows for the list

    namespace term = amberedit::ui::term;
    term::Screen screen(fixture.state.width, fixture.state.height);
    term::render(screen, area_list::render(fixture.state));

    // The bar stands in the rightmost column beside the rows, and the rows have
    // that column less to lay out in: the name column gives it up, where without
    // the bar it would run to the right margin.
    CHECK(rowText(screen, 2) == "   1 area1         █");
    CHECK(rowText(screen, 3) == "   2 area2         │");
    CHECK(rowText(screen, 7) == "   6 area6         │");
    // Six rows of twenty: the thumb is one row tall and stands at the top, the
    // list being scrolled to its first area.
    CHECK(barColumn(screen, 2, 6) == "█│││││");

    // Scrolled to the end, it stands at the bottom.
    fixture.state.areaCursor = 19;
    REQUIRE(area_list::handleEvent(fixture.state, Event::End));
    term::render(screen, area_list::render(fixture.state));
    CHECK(barColumn(screen, 2, 6) == "│││││█");
}

TEST_CASE("The area list draws no scrollbar for a list that fits [arealist]") {
    using amberedit::config::AreaFieldKind;
    Fixture fixture(numberedAreas(6));
    fixture.config.areaListScrollbar = true;
    // A row of one line, so that six lines are six areas: what fits is what the
    // bar is about here, and the default row stands two lines tall.
    fixture.config.areaListFormatNarrow = {{{AreaFieldKind::Echoid, 0}}};
    fixture.state.width = 20;
    fixture.state.height = 8;  // six rows for six areas

    namespace term = amberedit::ui::term;
    term::Screen screen(fixture.state.width, fixture.state.height);
    term::render(screen, area_list::render(fixture.state));

    // Nothing to scroll, so nothing is drawn and the rows keep the column.
    CHECK(barColumn(screen, 2, 6) == "      ");
}

TEST_CASE("arealist_scrollbar off leaves the rows the whole width [arealist]") {
    using amberedit::config::AreaFieldKind;
    Fixture fixture(numberedAreas(20));
    fixture.config.areaListFormatNarrow = {{{AreaFieldKind::Number, 3},
                                            {AreaFieldKind::Space, 1},
                                            {AreaFieldKind::Echoid, 0}}};
    fixture.config.areaListScrollbar = false;  // the default, said out loud
    fixture.state.width = 20;
    fixture.state.height = 8;

    namespace term = amberedit::ui::term;
    term::Screen screen(fixture.state.width, fixture.state.height);
    term::render(screen, area_list::render(fixture.state));

    CHECK(barColumn(screen, 2, 6) == "      ");
    CHECK(rowText(screen, 2) == "   1 area1          ");
}

TEST_CASE("A rescan reads the tosser config again [arealist]") {
    Fixture fixture({passthroughArea("one"), passthroughArea("two")});
    REQUIRE(fixture.manager.areas().size() == 2);

    fixture.areas.push_back(passthroughArea("three"));
    area_list::rescan(fixture.state);

    REQUIRE(fixture.manager.areas().size() == 3);
    CHECK(fixture.manager.areas()[2].config.tag == "three");
}

TEST_CASE("A rescan names each area on the modal as it reaches it [arealist]") {
    Fixture fixture({passthroughArea("one"), passthroughArea("two")});

    // What the shell's callback does with a terminal to draw on: every frame
    // drawn while the rescan runs names the area it is waiting for.
    std::vector<std::string> named;
    fixture.state.drawFrame = [&fixture, &named] {
        named.push_back(fixture.state.rescanArea);
    };

    area_list::rescan(fixture.state);

    CHECK(named == std::vector<std::string>{"one", "two"});
    // And it is cleared afterwards, so nothing is left named once the modal is
    // down.
    CHECK(fixture.state.rescanArea.empty());
}

TEST_CASE("A rescan keeps the cursor on the area it was on [arealist]") {
    Fixture fixture({passthroughArea("one"), passthroughArea("two")});
    fixture.state.areaCursor = 1;

    // The area the cursor is on is now the last of three rather than the second
    // of two: the cursor follows the area, not the position.
    fixture.areas.insert(fixture.areas.begin(), passthroughArea("zero"));
    area_list::rescan(fixture.state);

    REQUIRE(fixture.manager.areas().size() == 3);
    CHECK(fixture.state.areaCursor == 2);
    CHECK(fixture.manager.areas()[fixture.state.areaCursor].config.tag == "two");
}

TEST_CASE("A rescan puts the cursor back in bounds when its area is gone [arealist]") {
    Fixture fixture({passthroughArea("one"), passthroughArea("two")});
    fixture.state.areaCursor = 1;

    fixture.areas.pop_back();
    area_list::rescan(fixture.state);

    REQUIRE(fixture.manager.areas().size() == 1);
    CHECK(fixture.state.areaCursor == 0);
}

TEST_CASE("A rescan brings the totals and the unread counts up to date "
          "[arealist][squish]") {
    TempSquishBase base;
    Fixture fixture({squishArea("localnet", base.path())});

    REQUIRE(fixture.manager.areas().size() == 1);
    const uint32_t total = fixture.manager.areas()[0].total;
    REQUIRE(total > 2);
    // Nothing read yet, so every message counts as unread.
    CHECK(fixture.manager.areas()[0].unread == total);

    // The mark is a UID, so it has to be taken from the open base — which is
    // what a lastread file written by another reader would have held.
    amberedit::ports::IMsgBase* opened =
        fixture.manager.openArea(fixture.manager.areas()[0].config);
    REQUIRE(opened != nullptr);
    const uint32_t uid = opened->uidOf(total - 2);
    REQUIRE(uid != 0);
    fixture.manager.closeCurrentArea();
    fixture.lastRead->set(uid);

    // The counts are what the rescan is for: the mark moved under the
    // application, and only reading the base again says by how much.
    area_list::rescan(fixture.state);

    REQUIRE(fixture.manager.areas().size() == 1);
    CHECK(fixture.manager.areas()[0].total == total);
    CHECK(fixture.manager.areas()[0].unread == 2);
}

TEST_CASE("Entering an area with no base on disk makes one and reads it "
          "[arealist][create]") {
    // The row is dimmed — at startup there was nothing to open — and Enter on it
    // works all the same: what the tosser config declares is an area, and the
    // base under it is made on the way in.
    const amberedit::test::TempDir dir;
    Fixture fixture({squishArea("new.echo", dir.path("new-echo"))});

    REQUIRE(fixture.manager.areas().size() == 1);
    REQUIRE_FALSE(fixture.manager.areas()[0].isAvailable());

    REQUIRE(area_list::handleEvent(fixture.state, Event::Return));

    CHECK(fixture.state.errorMessage.empty());
    // An area with nothing in it opens in the reader, on blank rows, which is
    // the screen a first message is written from.
    CHECK(fixture.state.navigator.current() == amberedit::app::ScreenId::MessageRead);
    CHECK(fixture.state.messageCount == 0);
    // And the row is no longer the unavailable one it was drawn as.
    CHECK(fixture.manager.areas()[0].isAvailable());
}

TEST_CASE("An area that will not open says so in the error box [arealist][create]") {
    const amberedit::test::TempDir dir;
    // A regular file where the base's directory would have to be: nothing can
    // be opened there and nothing can be made there either.
    const std::string blocked = dir.path("a-file");
    {
        std::ofstream(blocked) << "not a directory";
    }
    Fixture fixture({squishArea("new.echo", blocked + "/new-echo")});

    REQUIRE(area_list::handleEvent(fixture.state, Event::Return));

    // The box says what went wrong, and the user is still on the area list —
    // nothing was half opened behind it.
    CHECK_FALSE(fixture.state.errorMessage.empty());
    CHECK(fixture.state.errorMessage.find("Cannot open the area:") == 0);
    CHECK(fixture.state.navigator.current() == amberedit::app::ScreenId::AreaList);

    // Acknowledging it puts the list back.
    amberedit::ui::error_dialog::handleEvent(fixture.state, Event::Return);
    CHECK(fixture.state.errorMessage.empty());
    CHECK(fixture.state.navigator.current() == amberedit::app::ScreenId::AreaList);
}

TEST_CASE("A passthrough area is entered and refused like any other "
          "[arealist][create]") {
    // There is no base on disk and none to make: the area is a name the tosser
    // routes through. Enter says that rather than doing nothing at all.
    Fixture fixture({passthroughArea("pass.through")});

    REQUIRE(area_list::handleEvent(fixture.state, Event::Return));

    CHECK_FALSE(fixture.state.errorMessage.empty());
    CHECK(fixture.state.navigator.current() == amberedit::app::ScreenId::AreaList);
}

TEST_CASE("The error box draws what went wrong and a button to dismiss it "
          "[arealist][create]") {
    namespace term = amberedit::ui::term;
    Fixture fixture({passthroughArea("one")});
    fixture.state.errorMessage = "Cannot open the area: no Squish base at /spool/new";

    term::Screen screen(fixture.state.width, fixture.state.height);
    term::render(screen, amberedit::ui::error_dialog::render(
                             fixture.state, area_list::render(fixture.state)));

    std::string drawn;
    for (int y = 0; y < screen.height(); ++y) drawn += rowText(screen, y);
    CHECK(drawn.find("Cannot open the area:") != std::string::npos);
    CHECK(drawn.find("OK") != std::string::npos);

    // A click on the button acknowledges it, the same as Enter does.
    const term::Box& box = fixture.state.errorOkBox;
    term::MouseEvent mouse;
    mouse.x = (box.x_min + box.x_max) / 2;
    mouse.y = (box.y_min + box.y_max) / 2;
    mouse.button = term::MouseEvent::Button::Left;
    mouse.motion = term::MouseEvent::Motion::Pressed;

    amberedit::ui::error_dialog::handleEvent(fixture.state, Event::Mouse(mouse));
    CHECK(fixture.state.errorMessage.empty());
}
