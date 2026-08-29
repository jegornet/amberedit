#include <doctest/doctest.h>

#include <algorithm>
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
#include "test_strings.hpp"
#include "ui/app_state.hpp"
#include "ui/error_dialog.hpp"
#include "ui/keys.hpp"
#include "ui/menu_dialog.hpp"
#include "ui/screens/area_list_screen.hpp"
#include "ui/screens/message_read_screen.hpp"
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
namespace message_read = amberedit::ui::screens::message_read;

namespace {

/// An area source the test can change between reloads, which is what a rescan
/// is for: the tosser config is read again, not only the bases.
class MutableAreaSource final : public amberedit::ports::IAreaConfigSource {
public:
    explicit MutableAreaSource(std::vector<AreaConfig>* areas) : areas_(areas) {}
    tl::expected<std::vector<AreaConfig>, amberedit::ErrorPtr> loadAreas() override {
        return *areas_;
    }

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
    /// Written down under the area's own name as well as in the one mark the
    /// areas nobody has named share: reading in one area must not answer for
    /// the next, and these bases are copies of one another — the UID the reader
    /// puts down names a message in every one of them.
    void setLastRead(const AreaConfig& area, uint32_t uid) override {
        uid_ = uid;
        perArea_[area.tag] = uid;
    }

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

/// A notch of the wheel, up or down. Where the pointer stands is not asked
/// after: the list moves its cursor wherever in it the wheel was turned.
Event wheel(bool down) {
    amberedit::ui::term::MouseEvent mouse;
    mouse.button = down ? amberedit::ui::term::MouseEvent::Button::WheelDown
                        : amberedit::ui::term::MouseEvent::Button::WheelUp;
    mouse.motion = amberedit::ui::term::MouseEvent::Motion::Pressed;
    mouse.x = 0;
    mouse.y = 0;
    return Event::Mouse(mouse);
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
        static_cast<void>(manager.reload());
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

/// One drawn row with the padding taken off it, which is what the eye reads a
/// row as.
std::string trimmed(const amberedit::ui::term::Screen& screen, int y) {
    std::string row = rowText(screen, y);
    while (!row.empty() && row.back() == ' ') row.pop_back();
    return row;
}

/// Marks an area read to its last message and reads the counts again with the
/// mark in place — which is what the unread-only filter is read against.
///
/// A mark is a UID, so it has to be taken from the base itself: the bases these
/// tests build are copies of one another, and a number would name a different
/// message in each of them.
void markToEnd(Fixture& fixture, const std::string& tag) {
    const auto& areas = fixture.manager.areas();
    const auto found = std::find_if(areas.begin(), areas.end(),
                                    [&tag](const amberedit::app::AreaEntry& entry) {
                                        return entry.config.tag == tag;
                                    });
    REQUIRE(found != areas.end());
    const AreaConfig area = found->config;
    const uint32_t total = found->total;
    REQUIRE(total > 0);

    amberedit::ports::IMsgBase* opened =
        amberedit::test::valueOf(fixture.manager.openArea(area));
    REQUIRE(opened != nullptr);
    const uint32_t uid = opened->uidOf(total);
    REQUIRE(uid != 0);
    fixture.manager.closeCurrentArea();
    fixture.lastRead->set(tag, uid);
    static_cast<void>(fixture.manager.reload());
}

/// Puts a mark of its own on every area, at the front of it, so that all of
/// them stand unread and none of them answers for another.
///
/// The stub keeps one mark for the areas it has not been told about by name,
/// and the bases these tests are built on are copies of one another — reading
/// in one of them would otherwise leave the rest looking read to the same
/// point, the same UID naming a message in each.
void markAllUnread(Fixture& fixture) {
    for (const auto& entry : fixture.manager.areas())
        fixture.lastRead->set(entry.config.tag, 0);
    static_cast<void>(fixture.manager.reload());
}

/// Opens the area at `index` the way the user opens one, and leaves the reader
/// standing on the message the area's mark puts it on.
void enter(Fixture& fixture, int index) {
    fixture.state.areaCursor = index;
    REQUIRE(area_list::handleEvent(fixture.state, Event::Return));
    REQUIRE(fixture.state.navigator.current() == amberedit::app::ScreenId::MessageRead);
    REQUIRE(fixture.state.errorMessage.empty());
}

/// Ctrl-U, which is what `arealist.toggle_unread` runs by default.
Event toggleUnread() {
    return Event::Character("u", true, false, false);
}

/// The three areas the filter's tests are built on: copies of one base, with
/// the middle one read to its end, so that two of the three have something
/// unread and one has not.
std::vector<AreaConfig> threeAreas(const TempSquishBase& first,
                                   const TempSquishBase& second,
                                   const TempSquishBase& third) {
    return {squishArea("first", first.path()), squishArea("second", second.path()),
            squishArea("third", third.path())};
}

}  // namespace

TEST_CASE(
    "Ctrl-R asks for a rescan rather than typing into the quick search "
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

TEST_CASE(
    "A letter a layout has made a command stops typing into the search "
    "[arealist][keys]") {
    Fixture fixture({passthroughArea("one"), passthroughArea("two")});
    fixture.state.keys = amberedit::test::valueOf(
        amberedit::ui::KeyMap::parse("t arealist.rescan\n", "keys"));

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

TEST_CASE("An external utility is asked for from the area list [arealist][keys]") {
    Fixture fixture({passthroughArea("one"), passthroughArea("two")});
    fixture.config.externUtils[2] = {"Files", {"/usr/bin/mc"}};
    fixture.state.keys = amberedit::test::valueOf(
        amberedit::ui::KeyMap::parse("Alt-F3 arealist.extern_util2\n", "keys"));

    // Asked for and not run: a screen has no terminal to hand over, so what the
    // key leaves behind is the slot for `runApp()` to answer on the next pass.
    REQUIRE(area_list::handleEvent(
        fixture.state, Event::Named(amberedit::ui::term::Event::Name::F3, false, true)));
    REQUIRE(fixture.state.externUtilRequested);
    CHECK(*fixture.state.externUtilRequested == 2);

    // The slot is the digit alone: the screen the key was pressed on decides
    // which command ran and nothing about which program does.
    fixture.state.externUtilRequested.reset();
    fixture.state.keys = amberedit::test::valueOf(
        amberedit::ui::KeyMap::parse("t arealist.extern_util2\n", "keys"));
    REQUIRE(area_list::handleEvent(fixture.state, Event::Character("t")));
    CHECK(fixture.state.externUtilRequested == 2);
    // And a letter bound to one stops being a letter the search can be typed
    // with, as any other command's letter does.
    CHECK(fixture.state.areaSearch.empty());
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
    amberedit::ports::IMsgBase* opened =
        amberedit::test::valueOf(fixture.manager.openArea(middle));
    REQUIRE(opened != nullptr);
    const uint32_t uid = opened->uidOf(total);
    REQUIRE(uid != 0);
    fixture.manager.closeCurrentArea();
    fixture.lastRead->set("second", uid);
    static_cast<void>(fixture.manager.reload());

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

TEST_CASE(
    "The slash leaves the cursor where it is when nothing is unread "
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

TEST_CASE("The wheel spends a row's worth of notches on a two-line row [arealist]") {
    using amberedit::config::AreaFieldKind;
    Fixture fixture(numberedAreas(20));
    fixture.state.height = 24;
    // Two lines to the row, whatever the window: the name on one and the
    // description under it, which is what the narrow default holds.
    fixture.config.areaListFormatNarrow = {{{AreaFieldKind::Echoid, 0}},
                                           {{AreaFieldKind::Description, 0}}};
    fixture.config.areaListFormatWide = fixture.config.areaListFormatNarrow;

    // A clock the test turns itself, as the message list's own test does.
    amberedit::ui::Millis now = 1000;
    fixture.state.monotonicMs = [&now] { return now; };
    const auto notch = [&fixture, &now](bool down, amberedit::ui::Millis after) {
        now += after;
        REQUIRE(area_list::handleEvent(fixture.state, wheel(down)));
    };

    fixture.state.areaCursor = 0;
    for (int i = 0; i < 6; ++i) notch(true, 20);
    CHECK(fixture.state.areaCursor == 3);

    // Slower than the window, and every notch moves an area again.
    for (int i = 0; i < 3; ++i) notch(true, 500);
    CHECK(fixture.state.areaCursor == 6);
}

TEST_CASE("A notch the throttle swallows still ends the quick search [arealist]") {
    using amberedit::config::AreaFieldKind;
    Fixture fixture({passthroughArea("one"), passthroughArea("two")});
    fixture.config.areaListFormatNarrow = {{{AreaFieldKind::Echoid, 0}},
                                           {{AreaFieldKind::Description, 0}}};
    fixture.config.areaListFormatWide = fixture.config.areaListFormatNarrow;
    amberedit::ui::Millis now = 1000;
    fixture.state.monotonicMs = [&now] { return now; };

    // The first notch of the run moves and ends the search; the second is
    // swallowed, and a search typed in the meantime ends on it all the same —
    // the wheel has been turned, whatever the list did about it.
    REQUIRE(area_list::handleEvent(fixture.state, wheel(true)));
    REQUIRE(area_list::handleEvent(fixture.state, Event::Character("t")));
    REQUIRE(fixture.state.areaSearch == "t");

    now += 20;
    REQUIRE(area_list::handleEvent(fixture.state, wheel(true)));
    CHECK(fixture.state.areaSearch.empty());
}

TEST_CASE("The area list draws the columns arealist_format asks for [arealist]") {
    using amberedit::config::AreaFieldKind;
    Fixture fixture({passthroughArea("one"), passthroughArea("two")});
    fixture.config.areaListFormatNarrow = {{{AreaFieldKind::Number, 3},
                                            {AreaFieldKind::Space, 1},
                                            {AreaFieldKind::Echoid, 0}}};
    fixture.state.width = 20;
    // The corner is not what this is about: a menu button would take the
    // right-hand end of the heading row, and what is being read here is the
    // format under it.
    fixture.config.arealistMenu.clear();

    namespace term = amberedit::ui::term;
    term::Screen screen(fixture.state.width, fixture.state.height);
    term::render(screen, area_list::render(fixture.state));

    // The heading follows the format — no counts asked for, none drawn — and the
    // name takes what the numbered column leaves, margins on both sides apart.
    CHECK(rowText(screen, 0) == "   # Area           ");
    CHECK(rowText(screen, 2) == "   1 one            ");
    CHECK(rowText(screen, 3) == "   2 two            ");
}

TEST_CASE(
    "The width the window has picks which of the two formats a row follows "
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
    // Which format a row follows is the question; the corner would only take
    // columns off the heading in the narrow window and not in the wide one,
    // which is one difference too many to read this through.
    fixture.config.arealistMenu.clear();

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

TEST_CASE(
    "A format written on several lines draws an area on all of them "
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
    // The heading row is read here too, so the corner is left off it.
    fixture.config.arealistMenu.clear();

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

TEST_CASE(
    "The selected area keeps its place on the screen when a row changes height "
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

TEST_CASE(
    "The description column is drawn quiet, the area's own words and all "
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
    Fixture fixture(
        {squishArea("zero", under.path()), speaks, squishArea("two", bare.path())});
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

TEST_CASE(
    "A rescan brings the totals and the unread counts up to date "
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
    amberedit::ports::IMsgBase* opened = amberedit::test::valueOf(
        fixture.manager.openArea(fixture.manager.areas()[0].config));
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

TEST_CASE(
    "Entering an area with no base on disk makes one and reads it "
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

TEST_CASE(
    "A passthrough area is entered and refused like any other "
    "[arealist][create]") {
    // There is no base on disk and none to make: the area is a name the tosser
    // routes through. Enter says that rather than doing nothing at all.
    Fixture fixture({passthroughArea("pass.through")});

    REQUIRE(area_list::handleEvent(fixture.state, Event::Return));

    CHECK_FALSE(fixture.state.errorMessage.empty());
    CHECK(fixture.state.navigator.current() == amberedit::app::ScreenId::AreaList);
}

TEST_CASE(
    "The error box draws what went wrong and a button to dismiss it "
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

TEST_CASE("Ctrl-U leaves only the areas with something unread [arealist][squish]") {
    using amberedit::config::AreaFieldKind;
    const TempSquishBase first;
    const TempSquishBase second;
    const TempSquishBase third;
    Fixture fixture(threeAreas(first, second, third));
    markToEnd(fixture, "second");
    REQUIRE(fixture.manager.areas()[1].unread == 0);

    fixture.config.areaListFormatNarrow = {{{AreaFieldKind::Echoid, 0}}};
    fixture.config.areaListFormatWide = fixture.config.areaListFormatNarrow;
    fixture.state.width = 20;
    fixture.state.height = 8;

    namespace term = amberedit::ui::term;
    term::Screen screen(fixture.state.width, fixture.state.height);
    term::render(screen, area_list::render(fixture.state));

    // All three of them to begin with: the filter is off unless the config or
    // the key says otherwise.
    REQUIRE_FALSE(fixture.state.areaUnreadOnly);
    REQUIRE(trimmed(screen, 2) == " first");
    REQUIRE(trimmed(screen, 3) == " second");
    REQUIRE(trimmed(screen, 4) == " third");

    REQUIRE(area_list::handleEvent(fixture.state, toggleUnread()));
    CHECK(fixture.state.areaUnreadOnly);
    term::render(screen, area_list::render(fixture.state));

    // The area that is read through is off the list, and the one under it has
    // come up into its place — numbered as the row it now stands on.
    CHECK(trimmed(screen, 2) == " first");
    CHECK(trimmed(screen, 3) == " third");
    CHECK(trimmed(screen, 4).empty());

    // And the key that took them off puts them back: the areas themselves, their
    // order and their counts were never touched.
    REQUIRE(area_list::handleEvent(fixture.state, toggleUnread()));
    CHECK_FALSE(fixture.state.areaUnreadOnly);
    term::render(screen, area_list::render(fixture.state));
    CHECK(trimmed(screen, 3) == " second");
}

TEST_CASE("The rows of the filtered list are numbered as they stand [arealist][squish]") {
    using amberedit::config::AreaFieldKind;
    const TempSquishBase first;
    const TempSquishBase second;
    const TempSquishBase third;
    Fixture fixture(threeAreas(first, second, third));
    markToEnd(fixture, "second");

    fixture.config.areaListFormatNarrow = {{{AreaFieldKind::Number, 3},
                                            {AreaFieldKind::Space, 1},
                                            {AreaFieldKind::Echoid, 0}}};
    fixture.config.areaListFormatWide = fixture.config.areaListFormatNarrow;
    fixture.state.width = 20;
    fixture.state.height = 8;
    fixture.state.areaUnreadOnly = true;

    namespace term = amberedit::ui::term;
    term::Screen screen(fixture.state.width, fixture.state.height);
    term::render(screen, area_list::render(fixture.state));

    // The column is read down the screen, so it counts the rows that are on it:
    // a list numbered 1, 3 would be saying something about an area that is not
    // there to be looked at.
    CHECK(trimmed(screen, 2) == "   1 first");
    CHECK(trimmed(screen, 3) == "   2 third");
}

TEST_CASE(
    "The filter keeps the cursor on its area, or on the one after it "
    "[arealist][squish]") {
    const TempSquishBase first;
    const TempSquishBase second;
    const TempSquishBase third;
    Fixture fixture(threeAreas(first, second, third));
    markToEnd(fixture, "second");

    // An area still on the list keeps the cursor, whichever row that has become.
    fixture.state.areaCursor = 2;
    REQUIRE(area_list::handleEvent(fixture.state, toggleUnread()));
    CHECK(fixture.state.areaCursor == 2);
    REQUIRE(area_list::handleEvent(fixture.state, toggleUnread()));
    CHECK(fixture.state.areaCursor == 2);

    // The area the filter takes off the list leaves the cursor on the one that
    // has come up under it, rather than back at the top.
    fixture.state.areaCursor = 1;
    REQUIRE(area_list::handleEvent(fixture.state, toggleUnread()));
    CHECK(fixture.state.areaCursor == 2);
}

TEST_CASE("Moving about counts the rows the filter leaves [arealist][squish]") {
    const TempSquishBase first;
    const TempSquishBase second;
    const TempSquishBase third;
    Fixture fixture(threeAreas(first, second, third));
    markToEnd(fixture, "second");
    fixture.state.areaUnreadOnly = true;
    fixture.state.areaCursor = 0;

    // Down from the first row is the next row of the list and not the next area
    // of the config: the one in between is not on the screen to be stopped on.
    REQUIRE(area_list::handleEvent(fixture.state, Event::ArrowDown));
    CHECK(fixture.state.areaCursor == 2);
    REQUIRE(area_list::handleEvent(fixture.state, Event::ArrowDown));
    CHECK(fixture.state.areaCursor == 2);
    REQUIRE(area_list::handleEvent(fixture.state, Event::ArrowUp));
    CHECK(fixture.state.areaCursor == 0);

    // End and Home are the last and the first of them, the same way.
    REQUIRE(area_list::handleEvent(fixture.state, Event::End));
    CHECK(fixture.state.areaCursor == 2);
    REQUIRE(area_list::handleEvent(fixture.state, Event::Home));
    CHECK(fixture.state.areaCursor == 0);
}

TEST_CASE("The quick search looks only at the rows on the list [arealist][squish]") {
    const TempSquishBase first;
    const TempSquishBase second;
    const TempSquishBase third;
    Fixture fixture(threeAreas(first, second, third));
    markToEnd(fixture, "second");
    fixture.state.areaUnreadOnly = true;
    fixture.state.areaCursor = 0;

    // "second" is not on the screen, so the query that names it finds nothing
    // and the cursor stays where the user put it — which is what the input line
    // says by turning red.
    REQUIRE(area_list::handleEvent(fixture.state, Event::Character("s")));
    CHECK(fixture.state.areaSearch == "s");
    CHECK(fixture.state.areaCursor == 0);

    // A row that is on it is found as ever.
    REQUIRE(area_list::handleEvent(fixture.state, Event::Backspace));
    REQUIRE(area_list::handleEvent(fixture.state, Event::Character("t")));
    CHECK(fixture.state.areaCursor == 2);
}

TEST_CASE("With nothing unread the list says so in the middle [arealist][squish]") {
    const TempSquishBase only;
    Fixture fixture({squishArea("localnet", only.path())});
    markToEnd(fixture, "localnet");
    REQUIRE(fixture.manager.areas()[0].unread == 0);

    fixture.state.width = 40;
    fixture.state.height = 10;
    fixture.state.areaUnreadOnly = true;

    namespace term = amberedit::ui::term;
    term::Screen screen(fixture.state.width, fixture.state.height);
    term::render(screen, area_list::render(fixture.state));

    // Under the list's own heading and rule, which stay: the filter is a way of
    // looking at the list rather than another screen.
    CHECK_FALSE(trimmed(screen, 0).empty());
    std::string body;
    for (int y = 2; y < fixture.state.height; ++y) body += trimmed(screen, y);
    CHECK_MESSAGE(amberedit::test::contains(body, "Every area has been read."), body);
    // And no row of an area anywhere on it.
    CHECK_MESSAGE(!amberedit::test::contains(body, "localnet"), body);

    // Ctrl-U is what it is still there to answer, and it brings the areas back.
    REQUIRE(area_list::handleEvent(fixture.state, toggleUnread()));
    term::render(screen, area_list::render(fixture.state));
    CHECK_MESSAGE(amberedit::test::contains(rowText(screen, 2), "localnet"),
                  rowText(screen, 2));
}

TEST_CASE("arealist_unread_only is the mode the list opens in [arealist]") {
    Fixture fixture({passthroughArea("one")});
    // Off unless the config says otherwise.
    CHECK_FALSE(fixture.state.areaUnreadOnly);

    AppConfig opens = Fixture::unsorted();
    opens.areaListUnreadOnly = true;
    const AppState started(fixture.manager, opens);
    CHECK(started.areaUnreadOnly);
}

TEST_CASE(
    "An area read to its end leaves the unread-only list under the cursor "
    "[arealist][squish]") {
    using amberedit::config::AreaFieldKind;
    const TempSquishBase first;
    const TempSquishBase second;
    const TempSquishBase third;
    Fixture fixture(threeAreas(first, second, third));
    markToEnd(fixture, "second");

    fixture.config.areaListFormatNarrow = {{{AreaFieldKind::Echoid, 0}}};
    fixture.config.areaListFormatWide = fixture.config.areaListFormatNarrow;
    fixture.state.width = 20;
    fixture.state.height = 8;
    fixture.state.areaUnreadOnly = true;
    fixture.state.areaCursor = 0;

    namespace term = amberedit::ui::term;
    term::Screen screen(fixture.state.width, fixture.state.height);
    term::render(screen, area_list::render(fixture.state));
    REQUIRE(trimmed(screen, 2) == " first");
    REQUIRE(fixture.state.areaCursor == 0);

    // Read through, as leaving the area in the reader would leave it: the row
    // goes, and the cursor comes back to a screen the area it named is no
    // longer on. The frame is what settles it, not the next keystroke — the
    // list has to be drawn with a row under the cursor.
    markToEnd(fixture, "first");
    term::render(screen, area_list::render(fixture.state));

    CHECK(trimmed(screen, 2) == " third");
    CHECK(trimmed(screen, 3).empty());
    CHECK(fixture.state.areaCursor == 2);
    CHECK(fixture.state.areaOffset == 0);
}

namespace {

/// A left-button press where the pointer is put, which is the whole of what the
/// corner answers: the release would arrive with the menu already up.
Event pressAt(int x, int y) {
    amberedit::ui::term::MouseEvent mouse;
    mouse.x = x;
    mouse.y = y;
    mouse.button = amberedit::ui::term::MouseEvent::Button::Left;
    mouse.motion = amberedit::ui::term::MouseEvent::Motion::Pressed;
    return Event::Mouse(mouse);
}

/// A press in the middle of the menu button, which stands in the two rows the
/// column headings and the rule under them take.
Event pressOnCorner(const AppState& state) {
    return pressAt(state.width - 3, 0);
}

/// That command's button, or nothing where the menu that is up does not hold
/// it.
const AppState::MenuView::Item* buttonFor(const Fixture& fixture,
                                          amberedit::config::Command command) {
    if (!fixture.state.menuView) return nullptr;
    for (const auto& item : fixture.state.menuView->items) {
        if (item.command == command) return &item;
    }
    return nullptr;
}

}  // namespace

TEST_CASE("The corner opens the list's own menu [arealist][menu]") {
    using amberedit::config::Command;
    Fixture fixture({passthroughArea("one"), passthroughArea("two")});
    fixture.state.width = 40;  // narrower than the threshold, so the corner is up
    REQUIRE(fixture.state.arealistMenuShown());

    namespace term = amberedit::ui::term;
    term::Screen screen(fixture.state.width, fixture.state.height);
    term::render(screen, area_list::render(fixture.state));
    // The button hangs from the top-right corner over the headings, and the
    // rule under them stops a column short of it.
    CHECK(rowText(screen, 0).find("│ ≡ │") != std::string::npos);
    CHECK(rowText(screen, 1).find("└───┘") != std::string::npos);

    // The quick search takes the heading row rather than a line of its own, and
    // the corner stands over that too: the query is cut to the room left beside
    // it, as the headings are.
    REQUIRE(area_list::handleEvent(fixture.state, Event::Character("t")));
    term::render(screen, area_list::render(fixture.state));
    CHECK(rowText(screen, 0).find("Area: t") != std::string::npos);
    CHECK(rowText(screen, 0).find("│ ≡ │") != std::string::npos);

    REQUIRE(area_list::handleEvent(fixture.state, pressOnCorner(fixture.state)));
    REQUIRE(fixture.state.menuView);
    // Rescanning and the filter by default, and nothing else: walking to the
    // next unread area is the cursor's work, and `/` does it without a button.
    CHECK(buttonFor(fixture, Command::AreaListRescan) != nullptr);
    CHECK(buttonFor(fixture, Command::AreaListToggleUnread) != nullptr);
    CHECK(buttonFor(fixture, Command::AreaListNextUnread) == nullptr);

    // And it is the same box the reader and the editor open, drawn over the
    // list: a column of framed buttons, each under the glyph its command
    // carries.
    term::render(screen, amberedit::ui::menu_dialog::render(
                             fixture.state, area_list::render(fixture.state)));
    std::string drawn;
    for (int y = 0; y < screen.height(); ++y) drawn += rowText(screen, y);
    CHECK(drawn.find("⟳ Rescan") != std::string::npos);
    CHECK(drawn.find("✱ Toggle unread") != std::string::npos);
}

TEST_CASE("A button of the list's menu does what its key does [arealist][menu]") {
    using amberedit::config::Command;
    Fixture fixture({passthroughArea("one"), passthroughArea("two")});
    fixture.state.width = 40;

    REQUIRE(area_list::handleEvent(fixture.state, pressOnCorner(fixture.state)));
    REQUIRE(fixture.state.menuView);
    const auto* filter = buttonFor(fixture, Command::AreaListToggleUnread);
    REQUIRE(filter != nullptr);
    REQUIRE(filter->enabled);

    // The box is put away first, as the shell puts it away, and the screen
    // underneath is asked afterwards.
    fixture.state.menuView.reset();
    area_list::runMenuCommand(fixture.state, Command::AreaListToggleUnread);
    CHECK(fixture.state.areaUnreadOnly);

    // And the rescan asks the same way Ctrl-R asks: the modal goes up, and the
    // reading itself is the shell's.
    area_list::runMenuCommand(fixture.state, Command::AreaListRescan);
    CHECK(fixture.state.rescanning);
}

TEST_CASE(
    "The walk to the next unread area is offered but not given "
    "[arealist][menu][squish]") {
    using amberedit::config::Command;
    const TempSquishBase first;
    const TempSquishBase second;
    const TempSquishBase third;
    Fixture fixture(threeAreas(first, second, third));
    markToEnd(fixture, "second");
    fixture.state.width = 40;

    // Not in the default menu: a menu that has to be opened again for every
    // step is a poor way to walk a list. Written down, it is there.
    fixture.config.arealistMenu = {Command::AreaListNextUnread};
    area_list::openMenu(fixture.state);
    const auto* button = buttonFor(fixture, Command::AreaListNextUnread);
    REQUIRE(button != nullptr);
    REQUIRE(button->enabled);

    fixture.state.menuView.reset();
    area_list::runMenuCommand(fixture.state, Command::AreaListNextUnread);
    // The cursor is on "first"; "second" is read through, so the next area with
    // something unread in it is "third"...
    CHECK(fixture.state.areaCursor == 2);
    area_list::runMenuCommand(fixture.state, Command::AreaListNextUnread);
    CHECK(fixture.state.areaCursor == 0);  // ...and then round the end of the list

    // Nothing unread anywhere is nowhere to go: the button is drawn quietly,
    // and pressing it all the same leaves the cursor where it stands.
    markToEnd(fixture, "first");
    markToEnd(fixture, "third");
    area_list::openMenu(fixture.state);
    const auto* dead = buttonFor(fixture, Command::AreaListNextUnread);
    REQUIRE(dead != nullptr);
    CHECK_FALSE(dead->enabled);

    fixture.state.menuView.reset();
    fixture.state.areaCursor = 2;
    area_list::runMenuCommand(fixture.state, Command::AreaListNextUnread);
    CHECK(fixture.state.areaCursor == 2);
}

TEST_CASE("menu_button off leaves the headings the whole row [arealist][menu]") {
    using amberedit::config::AreaFieldKind;
    using amberedit::config::Visibility;
    Fixture fixture({passthroughArea("one")});
    fixture.config.areaListFormatNarrow = {{{AreaFieldKind::Echoid, 0}}};
    fixture.state.width = 20;
    fixture.config.menuButton = Visibility::Off;
    CHECK_FALSE(fixture.state.arealistMenuShown());

    namespace term = amberedit::ui::term;
    term::Screen screen(fixture.state.width, fixture.state.height);
    term::render(screen, area_list::render(fixture.state));
    CHECK(rowText(screen, 0) == " Area               ");
    const int rows = fixture.state.areaListRows();

    // The corner costs no row — it stands in the two the headings and the rule
    // already take — so what turning it on takes is the right-hand end of those
    // two rows and nothing else.
    fixture.config.menuButton = Visibility::On;
    CHECK(fixture.state.arealistMenuShown());
    term::render(screen, area_list::render(fixture.state));
    CHECK(rowText(screen, 0) == " Area          │ ≡ │");
    CHECK(rowText(screen, 2) == " one                ");
    CHECK(fixture.state.areaListRows() == rows);

    // A menu with nothing in it is no menu either: the corner would open a box
    // with nothing in it to press.
    fixture.config.arealistMenu.clear();
    CHECK_FALSE(fixture.state.arealistMenuShown());
    term::render(screen, area_list::render(fixture.state));
    CHECK(rowText(screen, 0) == " Area               ");
}

TEST_CASE("A config declaring no areas has no corner to click [arealist][menu]") {
    Fixture fixture({});
    fixture.state.width = 40;
    REQUIRE(fixture.manager.areas().empty());
    REQUIRE(fixture.state.arealistMenuShown());

    // The screen is two lines saying the config declares nothing, and no
    // headings for the button to stand over — so the corner is not drawn there,
    // and a press on it is not answered either.
    namespace term = amberedit::ui::term;
    term::Screen screen(fixture.state.width, fixture.state.height);
    term::render(screen, area_list::render(fixture.state));
    CHECK(rowText(screen, 0).find("≡") == std::string::npos);

    CHECK_FALSE(area_list::handleEvent(fixture.state, pressOnCorner(fixture.state)));
    CHECK_FALSE(fixture.state.menuView);
}

TEST_CASE("reader_edge next_unread_area walks off an area into the next unread one "
          "[arealist][messageread][squish]") {
    using amberedit::app::ScreenId;
    const TempSquishBase first;
    const TempSquishBase second;
    const TempSquishBase third;
    Fixture fixture(threeAreas(first, second, third));
    markAllUnread(fixture);
    // The first area read to its end, which is where → has nowhere left to go:
    // the mark on the newest message opens the reader on it.
    markToEnd(fixture, "first");
    fixture.config.edgeBehavior = amberedit::config::EdgeBehavior::NextUnreadArea;

    enter(fixture, 0);
    REQUIRE(fixture.state.currentArea.tag == "first");
    REQUIRE(fixture.state.messageCursor ==
            static_cast<int>(fixture.state.messageCount) - 1);

    REQUIRE(message_read::handleEvent(fixture.state, Event::ArrowRight));

    // Still reading, and reading the next area with something unread in it —
    // the list is passed through rather than stopped on.
    CHECK(fixture.state.navigator.current() == ScreenId::MessageRead);
    CHECK(fixture.state.currentArea.tag == "second");
    CHECK(fixture.state.base != nullptr);
    CHECK(fixture.state.errorMessage.empty());
    // The list underneath stands on the area being read, so Esc comes back to
    // the right row.
    CHECK(fixture.state.areaCursor == 1);
    // The repeats already in the terminal are not spent walking through an area
    // nobody has looked at yet.
    CHECK(fixture.state.discardTypeahead);
}

TEST_CASE("The next unread area is looked for round the end of the list "
          "[arealist][messageread][squish]") {
    using amberedit::app::ScreenId;
    const TempSquishBase first;
    const TempSquishBase second;
    const TempSquishBase third;
    Fixture fixture(threeAreas(first, second, third));
    markAllUnread(fixture);
    markToEnd(fixture, "second");
    markToEnd(fixture, "third");
    fixture.config.edgeBehavior = amberedit::config::EdgeBehavior::NextUnreadArea;

    // From the bottom of the list, with the only unread area above it.
    enter(fixture, 2);
    REQUIRE(fixture.state.currentArea.tag == "third");

    REQUIRE(message_read::handleEvent(fixture.state, Event::ArrowRight));

    CHECK(fixture.state.navigator.current() == ScreenId::MessageRead);
    CHECK(fixture.state.currentArea.tag == "first");
    CHECK(fixture.state.areaCursor == 0);
}

TEST_CASE("With nothing unread anywhere next_unread_area takes the next area on the "
          "list [arealist][messageread][squish]") {
    using amberedit::app::ScreenId;
    const TempSquishBase first;
    const TempSquishBase second;
    const TempSquishBase third;
    Fixture fixture(threeAreas(first, second, third));
    markAllUnread(fixture);
    markToEnd(fixture, "first");
    markToEnd(fixture, "second");
    markToEnd(fixture, "third");
    fixture.config.edgeBehavior = amberedit::config::EdgeBehavior::NextUnreadArea;

    enter(fixture, 0);
    REQUIRE(message_read::handleEvent(fixture.state, Event::ArrowRight));

    // Nothing is unread, so what is left is the order of the list itself.
    CHECK(fixture.state.navigator.current() == ScreenId::MessageRead);
    CHECK(fixture.state.currentArea.tag == "second");
    CHECK(fixture.state.areaCursor == 1);

    // And the bottom of the list is the bottom of the reading: it does not go
    // round to the top the way the search for an unread area does.
    fixture.state.areaCursor = 2;
    REQUIRE(area_list::handleEvent(fixture.state, Event::Return));
    REQUIRE(fixture.state.currentArea.tag == "third");
    REQUIRE(message_read::handleEvent(fixture.state, Event::ArrowRight));
    CHECK(fixture.state.navigator.current() == ScreenId::AreaList);
    CHECK(fixture.state.base == nullptr);
}

TEST_CASE("next_unread_only leaves for the list when nothing is unread "
          "[arealist][messageread][squish]") {
    using amberedit::app::ScreenId;
    const TempSquishBase first;
    const TempSquishBase second;
    const TempSquishBase third;
    Fixture fixture(threeAreas(first, second, third));
    markAllUnread(fixture);
    markToEnd(fixture, "first");
    markToEnd(fixture, "third");
    fixture.config.edgeBehavior = amberedit::config::EdgeBehavior::NextUnreadOnly;

    // There is one unread area left, so this one is walked into.
    enter(fixture, 0);
    REQUIRE(message_read::handleEvent(fixture.state, Event::ArrowRight));
    REQUIRE(fixture.state.navigator.current() == ScreenId::MessageRead);
    REQUIRE(fixture.state.currentArea.tag == "second");

    // Read to its end in turn, and now nothing is unread anywhere: this is
    // where the two answers differ, and this one stops on the list.
    fixture.state.messageCursor = static_cast<int>(fixture.state.messageCount) - 1;
    message_read::openMessage(fixture.state, fixture.state.messageCount);
    REQUIRE(message_read::handleEvent(fixture.state, Event::ArrowRight));

    CHECK(fixture.state.navigator.current() == ScreenId::AreaList);
    CHECK(fixture.state.base == nullptr);
    CHECK(fixture.state.errorMessage.empty());
}

TEST_CASE("exit_set_to_next_unread stops on the list with the cursor on the next "
          "unread area [arealist][messageread][squish]") {
    using amberedit::app::ScreenId;
    const TempSquishBase first;
    const TempSquishBase second;
    const TempSquishBase third;
    Fixture fixture(threeAreas(first, second, third));
    markAllUnread(fixture);
    markToEnd(fixture, "first");
    markToEnd(fixture, "second");
    fixture.config.edgeBehavior = amberedit::config::EdgeBehavior::ExitSetToNextUnread;

    enter(fixture, 0);
    REQUIRE(fixture.state.currentArea.tag == "first");

    REQUIRE(message_read::handleEvent(fixture.state, Event::ArrowRight));

    // The area is left, as `exit` leaves it — nothing is opened, and Enter is
    // still the reader's to press.
    CHECK(fixture.state.navigator.current() == ScreenId::AreaList);
    CHECK(fixture.state.base == nullptr);
    CHECK(fixture.state.errorMessage.empty());
    // What the setting adds is where the cursor stands: the second area has
    // been read to its end as well, so the next unread one is the third.
    CHECK(fixture.state.areaCursor == 2);
    CHECK(fixture.state.discardTypeahead);

    // And Enter there reads it, which is the whole point of the cursor being
    // put where it is.
    REQUIRE(area_list::handleEvent(fixture.state, Event::Return));
    CHECK(fixture.state.currentArea.tag == "third");
}

TEST_CASE("With nothing unread exit_set_to_next_unread points at the next area on the "
          "list [arealist][messageread][squish]") {
    using amberedit::app::ScreenId;
    const TempSquishBase first;
    const TempSquishBase second;
    const TempSquishBase third;
    Fixture fixture(threeAreas(first, second, third));
    markAllUnread(fixture);
    markToEnd(fixture, "first");
    markToEnd(fixture, "second");
    markToEnd(fixture, "third");
    fixture.config.edgeBehavior = amberedit::config::EdgeBehavior::ExitSetToNextUnread;

    enter(fixture, 0);
    REQUIRE(message_read::handleEvent(fixture.state, Event::ArrowRight));

    // Nothing is unread, so what is left is the order of the list itself.
    CHECK(fixture.state.navigator.current() == ScreenId::AreaList);
    CHECK(fixture.state.areaCursor == 1);

    // The bottom of the list is the bottom of the reading here too: the cursor
    // stays on the area just read rather than going round to the top.
    enter(fixture, 2);
    REQUIRE(fixture.state.currentArea.tag == "third");
    REQUIRE(message_read::handleEvent(fixture.state, Event::ArrowRight));
    CHECK(fixture.state.navigator.current() == ScreenId::AreaList);
    CHECK(fixture.state.areaCursor == 2);
}

TEST_CASE("Walking off the front of an area goes to the list whatever reader_edge says "
          "[arealist][messageread][squish]") {
    using amberedit::app::ScreenId;
    const TempSquishBase first;
    const TempSquishBase second;
    const TempSquishBase third;
    Fixture fixture(threeAreas(first, second, third));
    markAllUnread(fixture);
    fixture.config.edgeBehavior = amberedit::config::EdgeBehavior::NextUnreadArea;

    // An unread area opens on its first message, which is where ← walks off.
    enter(fixture, 1);
    REQUIRE(fixture.state.currentArea.tag == "second");
    REQUIRE(fixture.state.messageCursor == 0);

    REQUIRE(message_read::handleEvent(fixture.state, Event::ArrowLeft));

    // ← is reading backwards past the front of the area, and it has just made
    // that area unread whole again: taken on to "the next unread area" it would
    // land straight back in this one.
    CHECK(fixture.state.navigator.current() == ScreenId::AreaList);
    CHECK(fixture.state.base == nullptr);
    CHECK(fixture.state.areaCursor == 1);

    // The same holds for the cursor `exit_set_to_next_unread` moves: pointed at
    // "the next unread area" it would be pointed back at the one just left.
    fixture.config.edgeBehavior = amberedit::config::EdgeBehavior::ExitSetToNextUnread;
    enter(fixture, 1);
    REQUIRE(fixture.state.messageCursor == 0);
    REQUIRE(message_read::handleEvent(fixture.state, Event::ArrowLeft));
    CHECK(fixture.state.navigator.current() == ScreenId::AreaList);
    CHECK(fixture.state.areaCursor == 1);
}
