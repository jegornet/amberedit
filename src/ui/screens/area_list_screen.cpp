#include "ui/screens/area_list_screen.hpp"

#include <algorithm>
#include <exception>
#include <optional>
#include <string>

#include "ui/area_list_format.hpp"
#include "ui/event_util.hpp"
#include "ui/list_page.hpp"
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

void clampCursor(AppState& state) {
    const int total = static_cast<int>(state.manager.areas().size());
    if (total == 0) {
        state.areaCursor = 0;
        state.areaOffset = 0;
        return;
    }
    state.areaCursor = std::clamp(state.areaCursor, 0, total - 1);

    // Areas, not lines: a row two lines tall halves how many of them a screen
    // holds, and everything about where the list is scrolled to is counted in
    // areas.
    const int rows = state.areaListItems();
    state.areaOffset = std::min(state.areaOffset, state.areaCursor);
    if (state.areaCursor >= state.areaOffset + rows)
        state.areaOffset = state.areaCursor - rows + 1;
    state.areaOffset = std::clamp(state.areaOffset, 0, std::max(0, total - rows));
}

/// Keeps the selected area where it stands on the screen when a row's height
/// changes under it — which is what dragging the window across
/// `adaptive_ui_threshold` does where the two `arealist_format`s are not the
/// same number of lines tall.
///
/// What is held is the line the selected row starts on, not how many areas
/// stand above it: those two are the same thing only while a row is one line,
/// and it is the screen the user is looking at. So the line is worked out from
/// the height the offset was last settled against, and the areas above the
/// cursor counted again for the new one — a cursor halfway down a screen of
/// one-line rows is halfway down a screen of two-line rows afterwards, and one
/// on the bottom row stays on the bottom row.
///
/// `clampCursor()` has the last word, as ever: near the end of a short list
/// there may be no offset that puts the cursor back where it was, and then the
/// list stays full and the cursor moves instead.
void reflowOffset(AppState& state) {
    const int height = state.areaRowHeight();
    if (state.areaRowHeightShown == height) return;
    if (state.areaRowHeightShown > 0) {
        const int line = (state.areaCursor - state.areaOffset) * state.areaRowHeightShown;
        // To the nearest row rather than the one above: half a row either way is
        // the closest the new height can come to where the cursor stood.
        const int above =
            std::clamp((line + (height / 2)) / height, 0, state.areaListItems() - 1);
        state.areaOffset = state.areaCursor - above;
    }
    state.areaRowHeightShown = height;
    clampCursor(state);
}

void moveBy(AppState& state, int delta) {
    state.areaCursor += delta;
    clampCursor(state);
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

/// Puts the cursor on what the query points at. A query matching nothing
/// leaves the cursor alone — the input line says as much by turning red, and
/// backspace takes the user back to where they were still matching.
void applySearch(AppState& state) {
    if (const auto match = findAreaByPrefix(state.manager.areas(), state.areaSearch)) {
        state.areaCursor = *match;
        clampCursor(state);
    }
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

    if (message_list::enterArea(state, area)) {
        // An area that would not open at startup and opens now has had its base
        // made in between: the row saying it cannot be read is out of date, and
        // so are the two counts beside it.
        if (wasUnavailable) state.manager.refreshArea(area);
        return;
    }

    // Nothing was left half open — enterArea() goes on only once the base is
    // there — so saying why is the whole of what is left to do.
    const std::string reason = state.manager.lastError();
    state.errorMessage = "Cannot open the area: " +
                         (reason.empty() ? "the base could not be opened" : reason);
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
    for (int step = 1; step <= total; ++step) {
        const int index = (state.areaCursor + step) % total;
        const auto& entry = areas[static_cast<size_t>(index)];
        if (entry.isAvailable() && entry.unread > 0) return index;
    }
    return std::nullopt;
}

/// Asks for a rescan — what Ctrl-R does. The reading itself is the shell's: the
/// modal saying what is going on has to be on the screen before every base is
/// opened again, and that is what blocks in between.
void askRescan(AppState& state) {
    state.areaSearch.clear();
    state.rescanning = true;
}

/// Which area a click landed on, if it landed on one. Any line of a row is that
/// row — the description under a name is the same area as the name. Lines past
/// the end of the list, and the lines at the bottom no whole row fitted in, are
/// drawn blank and point at nothing.
std::optional<int> clickedArea(const AppState& state, const Event& event) {
    const auto click = leftClick(event);
    if (!click) return std::nullopt;

    const int line = click->y - kHeaderRows;
    if (line < 0) return std::nullopt;
    const int row = line / state.areaRowHeight();
    if (row >= state.areaListItems()) return std::nullopt;

    const int index = state.areaOffset + row;
    if (index >= static_cast<int>(state.manager.areas().size())) return std::nullopt;
    return index;
}

}  // namespace

Element render(AppState& state) {
    const auto& areas = state.manager.areas();

    // No title line: the area names are the screen's own heading. Nor is there a
    // menu button in the corner: beyond opening an area the list has two
    // commands, Ctrl-R and `/`, and neither is worth a row of furniture.
    if (areas.empty()) {
        return vbox({text(" The tosser config declares no areas."),
                     text(" Check general.tosser_config in the AmberEdit config.")});
    }

    // Where the cursor stands on the screen is kept across a change in how tall
    // a row is, which is what dragging the window across the threshold can do.
    // Before anything is laid out: the offset settled here is the one drawn.
    reflowOffset(state);

    const int visibleLines = state.areaListRows();
    const int rowHeight = state.areaRowHeight();
    const int visibleAreas = state.areaListItems();
    const int total = static_cast<int>(areas.size());
    // The bar the reader draws beside a message too long for the window, in the
    // rightmost column and beside the rows alone — the heading and the rule
    // above them span the whole width. Only where the list is longer than the
    // screen: a list that fits has nothing to scroll, exactly as the reader
    // shows no bar for a message that fits.
    const bool scrollbarShown = state.config.areaListScrollbar && total > visibleAreas;

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
    Element header = text(toRow(" " + area_format::header(layout))) |
                     color(theme::palette.tableHeader);
    if (searching) {
        // "▌" stands in for the cursor: the terminal's own is hidden for the
        // whole application, and an input line without one reads as a label.
        const bool matched = findAreaByPrefix(areas, state.areaSearch).has_value();
        header = text(truncateToWidth(" Area: " + state.areaSearch + "▌",
                                      std::max(1, state.width))) |
                 bold |
                 color(matched ? theme::palette.tableHeader : theme::palette.error);
    }

    auto separator = text(horizontalRule(state.width)) | color(theme::palette.separator);

    // One line of one area's row, drawn.
    //
    // Unavailable areas are not hidden: it matters that the user can see the
    // area exists in the config but cannot be read. The number a row is
    // shown under is its place in the list as it stands, sorted and all.
    //
    // A line is drawn in the runs the format cuts it into rather than as one
    // string: the description column is drawn quiet, as a kludge line is, and
    // the runs are what say where it stands. Everything else is what the joined
    // line did — the runs are cut to the window at the same right edge, and the
    // line is padded out here rather than only when it is selected, so every
    // line is the same width and the trailing margin belongs to it.
    const auto lineOf = [&](const app::AreaEntry& entry, int index,
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
        for (const auto& run : area_format::runs(entry, index + 1, columns,
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
        const int index = state.areaOffset + i;
        for (int line = 0;
             line < rowHeight && static_cast<int>(lines.size()) < visibleLines; ++line) {
            if (index >= total) {
                lines.push_back(text(""));
                continue;
            }
            lines.push_back(lineOf(areas[index], index, layout[line]));
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
    // `area ... endarea` block declares too, are what reload() throws for, and
    // it leaves the list as it was — so there is nothing to undo and, with no
    // status line to say it in, nothing to say. The cursor is settled below
    // either way.
    try {
        // Each area is named on the modal as it is reached, which is what the
        // frame drawn from in here puts on the screen. The shell is blocked
        // inside this call, so nothing else would draw one.
        state.manager.reload([&state](const std::string& areaTag) {
            state.rescanArea = areaTag;
            state.redraw();
        });
    } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch)
    }

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
    const int total = static_cast<int>(state.manager.areas().size());

    // Both commands come ahead of the quick search, which would otherwise take
    // the key for something typed. That is also what a layout binding a bare
    // letter here costs: the letter runs the command and stops being one an
    // area's name can be searched by.
    if (state.keys.is(event, KeyCommand::AreaListRescan)) {
        askRescan(state);
        return true;
    }
    // Down the list to the next area with something unread in it, and round the
    // end. It is swallowed even when there is nowhere to go — the answer to
    // "take me to the next unread area" when there is none is to stay put.
    if (state.keys.is(event, KeyCommand::AreaListNextUnread)) {
        state.areaSearch.clear();
        if (const auto target = nextUnread(state)) {
            state.areaCursor = *target;
            clampCursor(state);
        }
        return true;
    }

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
    if (const auto clicked = clickedArea(state, event)) {
        endSearch();
        state.areaCursor = *clicked;
        clampCursor(state);
        // The cursor is moved, shown there for the length of the click
        // animation, and only then is the area opened: the row the pointer
        // landed on is on the screen as the current one before it goes away.
        state.showClick();
        openSelected(state);
        return true;
    }
    // The wheel moves the cursor a line at a time, the same as ↑↓.
    if (const int wheel = wheelDelta(event); wheel != 0) {
        endSearch();
        moveBy(state, wheel);
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
        state.areaCursor =
            pageUpTarget(state.areaCursor, state.areaOffset, state.areaListItems());
        clampCursor(state);
        return true;
    }
    if (event == Event::PageDown || event == Event::Character(' ')) {
        endSearch();
        state.areaCursor = pageDownTarget(state.areaCursor, state.areaOffset,
                                          state.areaListItems(), total);
        clampCursor(state);
        return true;
    }
    if (event == Event::Home) {
        endSearch();
        state.areaCursor = 0;
        clampCursor(state);
        return true;
    }
    if (event == Event::End) {
        endSearch();
        state.areaCursor = total - 1;
        clampCursor(state);
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
        openSelected(state);
        return true;
    }
    return false;
}

}  // namespace amberedit::ui::screens::area_list
