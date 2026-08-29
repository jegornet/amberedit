#include "ui/screens/area_list_screen.hpp"

#include <algorithm>
#include <exception>
#include <optional>
#include <string>
#include <vector>

#include "i18n/i18n.hpp"
#include "ui/area_list_format.hpp"
#include "ui/event_util.hpp"
#include "ui/extern_util.hpp"
#include "ui/list_page.hpp"
#include "ui/menu_button.hpp"
#include "ui/menu_dialog.hpp"
#include "ui/quick_search.hpp"
#include "ui/screens/message_list_screen.hpp"
#include "ui/scrollbar.hpp"
#include "ui/text_layout.hpp"
#include "ui/theme.hpp"

namespace amberedit::ui::screens::area_list {

using namespace term;

namespace {

/// What each row holds is `arealist_format`'s — which fields, in what order and
/// how wide — and `ui/area_list_format.*` is what lays a window's width out
/// among them. Nothing about the columns is decided here.
///
/// The screen has no outer margin, so a row carries its own on both sides —
/// that way the highlight on the current row covers them too, rather than
/// starting a column in. The rules span the full width instead.
constexpr int kIndent = 1;
constexpr int kRightPad = 1;
/// Rows the table stands under: the headings (or the search line in their
/// place) and the rule. What a click at a given row means is worked out from
/// this, so it has to match what render() puts above the rows.
constexpr int kHeaderRows = 2;

/// Whether an area has anything unread in it.
///
/// The star column's own test (`AreaFieldKind::UnreadFlag`), and what both `/`
/// and the unread-only filter are read against: an area that cannot be read has
/// no unread messages to offer, whatever count it was left with.
bool hasUnread(const app::AreaEntry& entry) {
    return entry.isAvailable() && entry.unread > 0;
}

/// The areas the list is showing, as places in the manager's own list: every
/// one of them, or — under `AppState::areaUnreadOnly` — only those with
/// something unread.
///
/// Worked out afresh wherever it is wanted rather than settled once and kept.
/// The counts move only while an area is being read, which is a screen away
/// from this one, so nothing can change under a cursor walking down the list;
/// and a filtered list held beside the cursor would be one more thing every
/// path back here had to remember to rebuild.
std::vector<int> shownAreas(const AppState& state) {
    const auto& areas = state.manager.areas();
    std::vector<int> shown;
    shown.reserve(areas.size());
    for (size_t i = 0; i < areas.size(); ++i) {
        if (!state.areaUnreadOnly || hasUnread(areas[i]))
            shown.push_back(static_cast<int>(i));
    }
    return shown;
}

/// Which row of the list the cursor's area stands on.
///
/// Where the filter has taken that area off the list — an area read to its end
/// while the filter was on — it is the nearest row after it: the cursor is left
/// on the area that has come up under it rather than pulled back to the top.
int rowOf(const std::vector<int>& shown, int cursor) {
    if (shown.empty()) return 0;
    const auto at = std::lower_bound(shown.begin(), shown.end(), cursor);
    if (at == shown.end()) return static_cast<int>(shown.size()) - 1;
    return static_cast<int>(at - shown.begin());
}

/// Puts the cursor on a row of the list and settles the offset around it.
///
/// `areaCursor` names an area of the manager's list and `areaOffset` a row of
/// the screen, which is the whole of what the filter changes: with nothing to
/// show there is no row for the cursor to be on, and the area it was left on is
/// kept rather than reset — it is what the list comes back to when the filter
/// goes off again.
void putOnRow(AppState& state, const std::vector<int>& shown, int row) {
    const int total = static_cast<int>(state.manager.areas().size());
    if (shown.empty()) {
        state.areaCursor = std::clamp(state.areaCursor, 0, std::max(0, total - 1));
        state.areaOffset = 0;
        return;
    }
    const int rows = state.areaListItems();
    const int last = static_cast<int>(shown.size()) - 1;
    row = std::clamp(row, 0, last);
    state.areaCursor = shown[static_cast<size_t>(row)];

    // Areas, not lines: a row two lines tall halves how many of them a screen
    // holds, and everything about where the list is scrolled to is counted in
    // areas.
    state.areaOffset = std::min(state.areaOffset, row);
    if (row >= state.areaOffset + rows) state.areaOffset = row - rows + 1;
    state.areaOffset = std::clamp(state.areaOffset, 0, std::max(0, last + 1 - rows));
}

void clampCursor(AppState& state) {
    const std::vector<int> shown = shownAreas(state);
    putOnRow(state, shown, rowOf(shown, state.areaCursor));
}

/// Keeps the selected area where it stands on the screen when a row's height
/// changes under it — which is what dragging the window across
/// `adaptive_ui_threshold` does where the two `arealist_format`s are not the
/// same number of lines tall.
///
/// What is held is the line the selected row starts on, not how many rows
/// stand above it: those two are the same thing only while a row is one line,
/// and it is the screen the user is looking at. So the line is worked out from
/// the height the offset was last settled against, and the areas above the
/// cursor counted again for the new one — a cursor halfway down a screen of
/// one-line rows is halfway down a screen of two-line rows afterwards, and one
/// on the bottom row stays on the bottom row.
///
/// The `putOnRow()` call after it in `render()` has the last word, as ever:
/// near the end of a short list there may be no offset that puts the cursor
/// back where it was, and then the list stays full and the cursor moves
/// instead.
void reflowOffset(AppState& state, const std::vector<int>& shown) {
    const int height = state.areaRowHeight();
    if (state.areaRowHeightShown == height) return;
    if (state.areaRowHeightShown > 0) {
        const int row = rowOf(shown, state.areaCursor);
        const int line = (row - state.areaOffset) * state.areaRowHeightShown;
        // To the nearest row rather than the one above: half a row either way is
        // the closest the new height can come to where the cursor stood.
        const int above =
            std::clamp((line + (height / 2)) / height, 0, state.areaListItems() - 1);
        state.areaOffset = row - above;
    }
    state.areaRowHeightShown = height;
}

void moveBy(AppState& state, int delta) {
    const std::vector<int> shown = shownAreas(state);
    putOnRow(state, shown, rowOf(shown, state.areaCursor) + delta);
}

/// The character an event types into the quick search, if it types one.
///
/// Printable ASCII except the space, which pages. Anything wider than a byte is
/// left alone too — an area tag is ASCII in practice, and the search compares it
/// as ASCII.
///
/// The commands this screen answers are asked before this, so a letter a layout
/// has made one of them never gets here. By default that is the slash alone, and
/// no area tag holds one.
std::optional<char> searchInput(const Event& event) {
    if (!event.is_character() || event.input().size() != 1) return std::nullopt;
    if (event.ctrl() || event.alt()) return std::nullopt;
    const auto c = static_cast<unsigned char>(event.input()[0]);
    if (c <= ' ' || c > '~') return std::nullopt;
    return static_cast<char>(c);
}

/// Which row the query points at: the first one on the list whose tag begins
/// with it, and nothing where none does.
///
/// The rows the list is showing and not the areas there are: under the
/// unread-only filter an area that is not on the screen is not one the cursor
/// can be put on, so a query only it matches finds nothing and the line turns
/// red — which is the truth about what is in front of the user.
std::optional<int> searchRow(const AppState& state, const std::vector<int>& shown) {
    if (state.areaSearch.empty()) return std::nullopt;
    const auto& areas = state.manager.areas();
    for (size_t row = 0; row < shown.size(); ++row) {
        const app::AreaEntry& entry = areas[static_cast<size_t>(shown[row])];
        if (startsWithIgnoreCase(entry.config.tag, state.areaSearch))
            return static_cast<int>(row);
    }
    return std::nullopt;
}

/// Puts the cursor on what the query points at. A query matching nothing
/// leaves the cursor alone — the input line says as much by turning red, and
/// backspace takes the user back to where they were still matching.
void applySearch(AppState& state) {
    const std::vector<int> shown = shownAreas(state);
    if (const auto row = searchRow(state, shown)) putOnRow(state, shown, *row);
}

/// Enters the area under the cursor — what Enter does, and what a click on a
/// row does once it has put the cursor there.
///
/// An area drawn dimmed is tried like any other rather than refused out of
/// hand: what was found at startup is most often a base no tosser has written
/// yet, and entering it is what makes one (`AreaManager::openArea()`). Only
/// once that has failed too is there anything to say, and then it is said in
/// the box — the row was dimmed, but Enter on it had every reason to work.
void openSelected(AppState& state) {
    // Copied out of the list rather than referred to: opening the area brings
    // the entry it came from up to date behind us.
    const app::AreaEntry& entry = state.manager.areas()[state.areaCursor];
    const domain::AreaConfig area = entry.config;
    const bool wasUnavailable = !entry.isAvailable();

    const auto entered = message_list::enterArea(state, area);
    if (entered) {
        // An area that would not open at startup and opens now has had its base
        // made in between: the row saying it cannot be read is out of date, and
        // so are the two counts beside it.
        if (wasUnavailable) state.manager.refreshArea(area);
        return;
    }

    // Nothing was left half open — enterArea() goes on only once the base is
    // there — so saying why is the whole of what is left to do.
    const std::string why = entered.error()->message().empty()
                                ? std::string(_("the base could not be opened"))
                                : entered.error()->message();
    state.errorMessage = i18n::format(_("Cannot open the area: {0}"), {why});
}

/// The next area holding unread messages, starting below the cursor and going
/// round the end of the list — what `/` goes to. It is the star column's
/// own test (`AreaFieldKind::UnreadFlag`): an area that cannot be read has no
/// unread messages to offer, whatever count it was left with.
///
/// The cursor's own area is the last place looked at rather than the first, so
/// that holding the key walks the unread areas one after another instead of
/// standing still on the one already reached. Nothing found anywhere — the
/// cursor's area included — is `nullopt`, and the cursor stays where it is.
std::optional<int> nextUnread(const AppState& state) {
    const auto& areas = state.manager.areas();
    const int total = static_cast<int>(areas.size());
    // A config declaring no areas at all: there is no list to walk, and the step
    // below would be counted against a length of nothing.
    if (total == 0) return std::nullopt;
    for (int step = 1; step <= total; ++step) {
        const int index = (state.areaCursor + step) % total;
        const auto& entry = areas[static_cast<size_t>(index)];
        if (entry.isAvailable() && entry.unread > 0) return index;
    }
    return std::nullopt;
}

/// The next area on the list below the cursor, whether it holds anything unread
/// or not — where `reader_edge next_unread_area` goes once there is nothing
/// unread left anywhere.
///
/// It does not go round the end: the bottom of the list is the bottom of the
/// reading, and there the reader is left on the list rather than sent back to
/// the top of it. Areas that would not open are passed over — this is a move
/// nobody asked for by name, and making a base for one of them is not something
/// walking off the end of another area should do.
std::optional<int> nextInList(const AppState& state) {
    const auto& areas = state.manager.areas();
    const int total = static_cast<int>(areas.size());
    for (int index = state.areaCursor + 1; index < total; ++index) {
        if (areas[static_cast<size_t>(index)].isAvailable()) return index;
    }
    return std::nullopt;
}

/// Turns the unread-only filter on or off — what `arealist.toggle_unread` does.
///
/// The cursor keeps the area it was on wherever that area is still on the list,
/// and lands on the one after it where turning the filter on has just taken it
/// off; either way the row it stands on has moved under it, so the offset is
/// settled again around it. The search ends on it as on every other command:
/// what was typed was typed against the other list.
void toggleUnreadOnly(AppState& state) {
    state.areaSearch.clear();
    state.areaUnreadOnly = !state.areaUnreadOnly;
    clampCursor(state);
}

/// Puts the cursor on the next area with something unread in it — what `/`
/// does. Nowhere to go is nowhere to go: the cursor stays where it is, the
/// answer to "take me to the next unread area" when there is none being to stay
/// put. The search ends either way, as it does on every other command.
void goToNextUnread(AppState& state) {
    state.areaSearch.clear();
    if (const auto target = nextUnread(state)) {
        state.areaCursor = *target;
        clampCursor(state);
    }
}

/// Asks for a rescan — what Ctrl-R does. The reading itself is the shell's: the
/// modal saying what is going on has to be on the screen before every base is
/// opened again, and that is what blocks in between.
void askRescan(AppState& state) {
    state.areaSearch.clear();
    state.rescanning = true;
}

/// Which row a click landed on, if it landed on one. Any line of a row is that
/// row — the description under a name is the same area as the name. Lines past
/// the end of the list, and the lines at the bottom no whole row fitted in, are
/// drawn blank and point at nothing.
std::optional<int> clickedRow(const AppState& state, const Event& event,
                              const std::vector<int>& shown) {
    const auto click = leftClick(event);
    if (!click) return std::nullopt;

    const int line = click->y - kHeaderRows;
    if (line < 0) return std::nullopt;
    const int row = line / state.areaRowHeight();
    if (row >= state.areaListItems()) return std::nullopt;

    const int at = state.areaOffset + row;
    if (at >= static_cast<int>(shown.size())) return std::nullopt;
    return at;
}

/// Whether the list's own menu offers that button. Only the walk to the next
/// unread area can be dead: with nothing unread anywhere there is nowhere for it
/// to go, and a button that would leave the cursor where it stands says so by
/// being drawn quietly. Rescanning and the filter are answered whatever the list
/// holds — a list with nothing unread on it is exactly where the filter is
/// turned off again — and a utility is a program, which has nothing to do with
/// what is on the screen.
bool commandEnabled(const AppState& state, Command command) {
    if (command == Command::AreaListNextUnread) return nextUnread(state).has_value();
    return true;
}

}  // namespace

void openNextArea(AppState& state, config::EdgeBehavior behavior) {
    // The area just read is off the reader's screen and its counts have been
    // read again by then, so an area read to its end is no longer one with
    // anything unread in it and the search below passes over it.
    auto target = nextUnread(state);
    if (!target && behavior == config::EdgeBehavior::NextUnreadArea) {
        target = nextInList(state);
    }
    // Nowhere to go: the list is what the reader was left on, and that is where
    // it stays. `next_unread_only` with nothing unread anywhere is exactly this,
    // and so is the bottom of the list under either of the two.
    if (!target) return;

    // The cursor is moved first because that is what opens an area: it is the
    // same step Enter takes on the list, error box and all, so an area that
    // will not open says so there rather than half-opening under the reader.
    state.areaCursor = *target;
    openSelected(state);
}

void openMenu(AppState& state) {
    std::vector<AppState::MenuView::Item> items;
    items.reserve(state.config.arealistMenu.size());
    for (const Command command : state.config.arealistMenu) {
        items.push_back({command, commandEnabled(state, command), {}});
    }
    menu_dialog::open(state, std::move(items));
}

void runMenuCommand(AppState& state, Command command) {
    switch (command) {
        // Rescanning asks the same way Ctrl-R asks: the modal has to be on the
        // screen before the bases are opened again, and the shell is what runs
        // in between.
        case Command::AreaListRescan: askRescan(state); break;
        case Command::AreaListToggleUnread: toggleUnreadOnly(state); break;
        // Refused rather than merely drawn dim, for the reason the editor's
        // Import is: a button the menu dimmed can still be walked onto and
        // pressed, and there is nowhere to walk to.
        case Command::AreaListNextUnread:
            if (commandEnabled(state, command)) goToNextUnread(state);
            break;
        // The ten utilities, which are one case rather than ten. Nothing else
        // reaches here: moving about the list is the cursor's, and
        // `arealist_menu` can name no other command.
        default: static_cast<void>(extern_util::run(state, command)); break;
    }
}

Element render(AppState& state) {
    const auto& areas = state.manager.areas();

    // No title line: the area names are the screen's own heading, and the menu
    // button stands over them where it is shown at all. Nothing is said here
    // about a config that declares no areas: the two lines below are the whole
    // screen, and a corner over them would be furniture around an error.
    if (areas.empty()) {
        return vbox({text(_(" The tosser config declares no areas.")),
                     text(_(" Check general.tosser_config in the AmberEdit config."))});
    }

    // Which areas are on the list at all — every one of them, or those with
    // something unread. Worked out before anything is laid out: how long the
    // list is decides the scrollbar, and which rows are drawn is this.
    const std::vector<int> shown = shownAreas(state);

    // Where the cursor stands on the screen is kept across a change in how tall
    // a row is, which is what dragging the window across the threshold can do.
    // Before anything is laid out: the offset settled here is the one drawn.
    reflowOffset(state, shown);
    // And onto a row of the list as it now stands, on every frame rather than
    // only when a key has been pressed: an area read to its end leaves the
    // unread-only list of its own accord, so the cursor can arrive back on this
    // screen naming an area that is no longer a row of it.
    putOnRow(state, shown, rowOf(shown, state.areaCursor));

    const int visibleLines = state.areaListRows();
    const int rowHeight = state.areaRowHeight();
    const int visibleAreas = state.areaListItems();
    const int total = static_cast<int>(shown.size());
    // The bar the reader draws beside a message too long for the window, in the
    // rightmost column and beside the rows alone — the heading and the rule
    // above them span the whole width. Only where the list is longer than the
    // screen: a list that fits has nothing to scroll, exactly as the reader
    // shows no bar for a message that fits.
    const bool scrollbarShown = state.config.areaListScrollbar && total > visibleAreas;

    // The way into the list's own menu, in the top-right corner. It costs no
    // row: the two it stands in are the heading and the rule under it, which
    // are there either way. What it costs is the right-hand end of the heading,
    // which is furniture — the rows below it run their full width, and the
    // scrollbar's column is theirs alone.
    const bool menu = state.arealistMenuShown();
    const int headerRoom = std::max(1, state.width - (menu ? menu_button::kWidth : 0));

    // The columns as this window can afford them, worked out once and used for
    // the heading and every row alike. The bar's column is taken off the rows
    // before they are laid out, so the text runs up to it rather than under it.
    const int listWidth = std::max(0, state.width - (scrollbarShown ? 1 : 0));
    const int rowWidth = std::max(0, listWidth - kRightPad);
    const area_format::Layout layout =
        area_format::layout(state.areaListFormat(), rowWidth - kIndent);

    // A format whose fixed widths come to more than the window holds is cut off
    // at its right edge rather than pushing the row past it: what a field asks
    // for is the user's, and how much there is to give is the window's.
    const auto toRow = [rowWidth](const std::string& line) {
        return padRight(substrByWidth(line, 0, rowWidth), rowWidth) + " ";
    };

    // The search takes the heading row rather than a line of its own: the table
    // keeps its height, so the rows do not shift under the cursor as soon as a
    // letter is typed. The heading is furniture and the query is not.
    const bool searching = !state.areaSearch.empty();
    Element header =
        text(substrByWidth(toRow(" " + area_format::header(layout)), 0, headerRoom)) |
        color(theme::palette.tableHeader);
    if (searching) {
        // "▌" stands in for the cursor: the terminal's own is hidden for the
        // whole application, and an input line without one reads as a label.
        const bool matched = searchRow(state, shown).has_value();
        header = text(truncateToWidth(i18n::format(_(" Area: {0}▌"), {state.areaSearch}),
                                      headerRoom)) |
                 bold |
                 color(matched ? theme::palette.tableHeader : theme::palette.error);
    }

    // The rule stops a column short of the button, so the box reads as a thing
    // standing beside it rather than a piece of it — the same column the reader
    // and the editor leave between the two.
    const int ruleWidth = std::max(0, state.width - (menu ? menu_button::kWidth + 1 : 0));
    Element separator = text(horizontalRule(ruleWidth) + (menu ? " " : "")) |
                        color(theme::palette.separator);
    if (menu) {
        // The filler is what holds the button against the right edge whatever
        // the heading came to, and it is a child of this row rather than of the
        // heading: an hbox does not carry a child's appetite for room up to its
        // own parent. Both rows are told of the press together, so the button
        // lights up as one thing rather than as the half the pointer landed on.
        const bool pressed = state.isPressed(AppState::Pressed::MenuButton);
        header = hbox({std::move(header), filler(), menu_button::topRow(pressed)});
        separator = hbox({std::move(separator), menu_button::bottomRow(pressed)});
    }

    // The unread-only filter with nothing left to show. It is said in the middle
    // of the screen and under the list's own heading and rule, because the
    // filter is a way of looking at the list rather than another screen: the key
    // that turned it on is what turns it off again, and the hint bar names it.
    //
    // An area list this can be reached from is never empty — the empty one is
    // answered above — so what stands here is always the filter and never the
    // config.
    if (shown.empty()) {
        Element notice =
            text(_("Every area has been read.")) | color(theme::palette.dimmed) | center;
        return vbox({header, separator, std::move(notice) | flex});
    }

    // One line of one area's row, drawn.
    //
    // Unavailable areas are not hidden: it matters that the user can see the
    // area exists in the config but cannot be read. The number a row is shown
    // under is its place in the list as it stands — sorted, and with the areas
    // the unread-only filter leaves out left out of the counting: the column is
    // read down the screen, and a list numbered 3, 7, 12 would be saying
    // something about areas that are not on it.
    //
    // A line is drawn in the runs the format cuts it into rather than as one
    // string: the description column is drawn quiet, as a kludge line is, and
    // the runs are what say where it stands. Everything else is what the joined
    // line did — the runs are cut to the window at the same right edge, and the
    // line is padded out here rather than only when it is selected, so every
    // line is the same width and the trailing margin belongs to it.
    const auto lineOf = [&](const app::AreaEntry& entry, int index, int row,
                            const area_format::Line& columns) {
        // Every line of the selected area's row is drawn selected: the row is
        // what the cursor is on, and a highlight stopping halfway down it would
        // read as two areas, one of them chosen.
        const bool selected = index == state.areaCursor;
        Elements cells;
        int drawn = 0;
        // The row under the cursor keeps the selection's colors throughout: it
        // is marked out already, and the quiet color on the selection's
        // background would be the one thing on the row that could not be read.
        const auto styled = [&](std::string piece, bool dimmed) {
            Element cell = text(std::move(piece));
            if (selected) {
                return std::move(cell) | bold | color(theme::palette.selectionText) |
                       bgcolor(theme::palette.selection);
            }
            if (!entry.isAvailable() || dimmed) {
                return std::move(cell) | color(theme::palette.dimmed);
            }
            return cell;
        };
        const auto push = [&](const std::string& piece, bool dimmed) {
            const std::string fitted = substrByWidth(piece, 0, rowWidth - drawn);
            if (fitted.empty()) return;
            drawn += displayWidth(fitted);
            cells.push_back(styled(fitted, dimmed));
        };

        // The margin on the left is the row's own, so that the highlight covers
        // it. What the fields left of the width, and the margin on the right,
        // are the one blank piece closing the line.
        push(" ", false);
        for (const auto& run : area_format::runs(entry, row + 1, columns,
                                                 state.config.areaDescriptionDefault)) {
            push(run.text, run.dimmed);
        }
        cells.push_back(
            styled(std::string(static_cast<size_t>(rowWidth - drawn) + 1, ' '), false));
        return hbox(std::move(cells));
    };

    Elements lines;
    lines.reserve(static_cast<size_t>(visibleLines));
    for (int i = 0; i < visibleAreas; ++i) {
        const int row = state.areaOffset + i;
        for (int line = 0;
             line < rowHeight && static_cast<int>(lines.size()) < visibleLines; ++line) {
            if (row >= total) {
                lines.push_back(text(""));
                continue;
            }
            const int index = shown[static_cast<size_t>(row)];
            lines.push_back(
                lineOf(areas[static_cast<size_t>(index)], index, row, layout[line]));
        }
    }
    // The lines at the bottom that no whole row fitted in. A row is drawn whole
    // or not at all: half of one would read as an area shown, and the fields cut
    // off it are the ones the format put lowest because they matter least.
    //
    // The window too short for even one whole row is the one exception, and the
    // count above is what makes it one: there is always an area on the screen,
    // so as much of the first row as there is room for is drawn.
    while (static_cast<int>(lines.size()) < visibleLines) lines.push_back(text(""));

    Element table = vbox(std::move(lines));
    if (scrollbarShown) {
        // The rows were laid out a column narrower, so the bar fills the
        // rightmost one exactly, with the table running up to it.
        //
        // Built a cell at a time rather than by `bar()`: the thumb is worked out
        // against the areas on the screen, which is what is being scrolled, and
        // then drawn down the lines each of them takes — so a row two lines tall
        // has two lines of thumb, and the bar says where in the list the screen
        // is rather than how many lines that came to. The lines under the last
        // whole row are track: there is nothing there to point at.
        const scrollbar::Thumb thumb =
            scrollbar::thumbOf(visibleAreas, total, state.areaOffset);
        Elements cells;
        cells.reserve(static_cast<size_t>(visibleLines));
        for (int i = 0; static_cast<int>(cells.size()) < visibleLines; ++i) {
            for (int line = 0;
                 line < rowHeight && static_cast<int>(cells.size()) < visibleLines;
                 ++line) {
                cells.push_back(scrollbar::cell(i, thumb));
            }
        }
        table = hbox({std::move(table) | flex, vbox(std::move(cells))});
    }

    return vbox({header, separator, std::move(table)});
}

void rescan(AppState& state) {
    // Which area the cursor is on, rather than where in the list it sits: the
    // config is read again and the list sorted again, so afterwards the same
    // index may well name a different area. Tag and path together are what
    // names one, the same pair the unread counts are matched on.
    std::string tag;
    std::string path;
    const auto& before = state.manager.areas();
    if (state.areaCursor >= 0 && state.areaCursor < static_cast<int>(before.size())) {
        tag = before[state.areaCursor].config.tag;
        path = before[state.areaCursor].config.path;
    }

    // An unreadable tosser config, or one that has come to declare a tag an
    // `area ... endarea` block declares too, are what reload() fails for, and it
    // leaves the list as it was — so there is nothing to undo and, with no
    // status line to say it in, nothing to say. The cursor is settled below
    // either way.
    //
    // Each area is named on the modal as it is reached, which is what the frame
    // drawn from in here puts on the screen. The shell is blocked inside this
    // call, so nothing else would draw one.
    static_cast<void>(state.manager.reload([&state](const std::string& areaTag) {
        state.rescanArea = areaTag;
        state.redraw();
    }));

    const auto& areas = state.manager.areas();
    if (!tag.empty()) {
        const auto found = std::find_if(
            areas.begin(), areas.end(), [&tag, &path](const app::AreaEntry& entry) {
                return entry.config.tag == tag && entry.config.path == path;
            });
        if (found != areas.end())
            state.areaCursor = static_cast<int>(found - areas.begin());
    }
    clampCursor(state);

    // Nothing is being read any more. It is also what the next rescan's first
    // frame shows, before it has an area to name: the tosser config itself.
    state.rescanArea.clear();
}

bool handleEvent(AppState& state, const Event& event) {
    // The menu button in the top-right corner, before anything else a click
    // could mean: it stands over the column headings, and what it holds is
    // decided as it opens, on the list as it stands now. A config that declares
    // no areas has no headings for it to stand over — that screen is two lines
    // of prose — so there is no corner to click there either.
    if (state.arealistMenuShown() && !state.manager.areas().empty() &&
        menu_button::clicked(event, state.width)) {
        state.showClick(AppState::Pressed::MenuButton);
        openMenu(state);
        return true;
    }

    // The rows the list is showing, which everything that moves about in it is
    // counted in. Read once here rather than in each of the handlers below: the
    // list cannot change while one key is being answered.
    const std::vector<int> shown = shownAreas(state);
    const int total = static_cast<int>(shown.size());

    // The three commands come ahead of the quick search, which would otherwise
    // take the key for something typed. That is also what a layout binding a
    // bare letter here costs: the letter runs the command and stops being one an
    // area's name can be searched by.
    if (state.keys.is(event, Command::AreaListRescan)) {
        askRescan(state);
        return true;
    }
    // Between the whole list and the areas with something unread in them. It is
    // answered whatever the list holds — an empty one is exactly where somebody
    // wants the filter off again.
    if (state.keys.is(event, Command::AreaListToggleUnread)) {
        toggleUnreadOnly(state);
        return true;
    }
    // Down the list to the next area with something unread in it, and round the
    // end. It is swallowed even when there is nowhere to go — the answer to
    // "take me to the next unread area" when there is none is to stay put.
    if (state.keys.is(event, Command::AreaListNextUnread)) {
        goToNextUnread(state);
        return true;
    }

    // The external utilities, ahead of the quick search for the same reason:
    // a letter bound to one is a letter an area's name can no longer be
    // searched by.
    if (extern_util::handleKey(state, event, CommandScreen::AreaList)) return true;

    // Typing a name is how the search starts, so the letters are claimed before
    // anything that would rather have them — which is why the list has no g/G
    // for top and bottom the way the other screens do. Home and End do that job
    // here, and every letter is free to be the first one of an area's name.
    if (total > 0) {
        if (const auto typed = searchInput(event)) {
            state.areaSearch += *typed;
            applySearch(state);
            return true;
        }
    }
    if (event == Event::Backspace && !state.areaSearch.empty()) {
        // An emptied query puts the heading back and leaves the cursor on
        // whatever it last found: erasing a search is not undoing it.
        state.areaSearch.pop_back();
        applySearch(state);
        return true;
    }
    // Esc closes the search rather than the application: while something is
    // being typed, that is what the key most plainly means.
    if (event == Event::Escape && !state.areaSearch.empty()) {
        state.areaSearch.clear();
        return true;
    }
    // Every other key ends the search — once the cursor is being moved by hand
    // or an area is being opened, the query has said what it had to say.
    const auto endSearch = [&state] { state.areaSearch.clear(); };

    // A click enters the area it landed on, cursor and all: the row under the
    // pointer is the one meant, wherever the cursor happened to be.
    if (const auto clicked = clickedRow(state, event, shown)) {
        endSearch();
        putOnRow(state, shown, *clicked);
        // The cursor is moved, shown there for the length of the click
        // animation, and only then is the area opened: the row the pointer
        // landed on is on the screen as the current one before it goes away.
        state.showClick();
        openSelected(state);
        return true;
    }
    // The wheel moves the cursor a line at a time, the same as ↑↓ — through
    // `list_wheel_throttle`, which where a row stands more than one line tall
    // spends a row's worth of notches on each area. The search ends on the
    // notch that was swallowed as much as on the one that moved: either way the
    // wheel has been turned, and the query has said what it had to say.
    if (const int wheel = wheelDelta(event); wheel != 0) {
        endSearch();
        if (const int steps = state.wheelSteps(wheel, state.areaRowHeight()); steps != 0)
            moveBy(state, steps);
        return true;
    }
    if (event == Event::ArrowUp) {
        endSearch();
        moveBy(state, -1);
        return true;
    }
    if (event == Event::ArrowDown) {
        endSearch();
        moveBy(state, 1);
        return true;
    }
    if (event == Event::PageUp) {
        endSearch();
        putOnRow(state, shown,
                 pageUpTarget(rowOf(shown, state.areaCursor), state.areaOffset,
                              state.areaListItems()));
        return true;
    }
    if (event == Event::PageDown || event == Event::Character(' ')) {
        endSearch();
        putOnRow(state, shown,
                 pageDownTarget(rowOf(shown, state.areaCursor), state.areaOffset,
                                state.areaListItems(), total));
        return true;
    }
    if (event == Event::Home) {
        endSearch();
        putOnRow(state, shown, 0);
        return true;
    }
    if (event == Event::End) {
        endSearch();
        putOnRow(state, shown, total - 1);
        return true;
    }
    // The area list is the root screen, so there is nowhere to go back to:
    // Esc here means leaving the application, and that is worth confirming.
    if (event == Event::Escape) {
        state.confirm = AppState::Confirm::Quit;
        state.confirmChoice =
            AppState::ConfirmChoice::Yes;  // opens on Yes, as AppState documents
        return true;
    }
    if (event == Event::Return || event == Event::ArrowRight) {
        if (total == 0) return true;
        endSearch();
        // Onto a row first: what Enter opens is the row the cursor is drawn on,
        // and an area the filter has taken off the list is not one of them.
        putOnRow(state, shown, rowOf(shown, state.areaCursor));
        openSelected(state);
        return true;
    }
    return false;
}

}  // namespace amberedit::ui::screens::area_list
