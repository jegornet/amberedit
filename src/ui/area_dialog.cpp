#include "ui/area_dialog.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ui/dialog_frame.hpp"
#include "ui/event_util.hpp"
#include "ui/list_page.hpp"
#include "ui/quick_search.hpp"
#include "ui/text_layout.hpp"
#include "ui/theme.hpp"

namespace amberedit::ui::area_dialog {

using namespace term;

namespace {

/// The narrowest the box is drawn, whatever the tags measure: the title has to
/// fit inside it, and a box that shrank to the shortest tag in the list would
/// read as a stray column of text rather than as a dialog.
constexpr int kMinInner = 24;
/// The margin the box keeps from the edges of the window: a row above and
/// below, a column at each side.
constexpr int kMarginRows = 4;
constexpr int kMarginColumns = 2;

/// Rows the list may stand on — the window less the frame and that margin.
int visibleRows(const AppState& state) {
    return std::max(1, state.height - kMarginRows);
}

/// Keeps the cursor on the list and the list around the cursor.
void clampCursor(AppState& state, AppState::AreaPicker& picker) {
    const auto total = static_cast<int>(state.manager.areas().size());
    if (total == 0) {
        picker.cursor = 0;
        picker.offset = 0;
        return;
    }
    picker.cursor = std::clamp(picker.cursor, 0, total - 1);

    const int rows = std::min(visibleRows(state), total);
    picker.offset = std::min(picker.offset, picker.cursor);
    if (picker.cursor >= picker.offset + rows) picker.offset = picker.cursor - rows + 1;
    picker.offset = std::clamp(picker.offset, 0, std::max(0, total - rows));
}

void moveBy(AppState& state, AppState::AreaPicker& picker, int delta) {
    picker.cursor += delta;
    clampCursor(state, picker);
}

/// The character an event types into the quick search, if it types one — the
/// same rule the area list searches by: printable ASCII, the space excepted,
/// no area tag having one in it.
std::optional<char> searchInput(const Event& event) {
    if (!event.is_character() || event.input().size() != 1) return std::nullopt;
    const auto c = static_cast<unsigned char>(event.input()[0]);
    if (c <= ' ' || c > '~') return std::nullopt;
    return static_cast<char>(c);
}

/// How wide the box is inside its frame: the widest tag with a column of
/// margin at each side, within what the window has room for.
int innerWidth(const AppState& state) {
    int widest = kMinInner;
    for (const auto& entry : state.manager.areas()) {
        widest = std::max(widest, displayWidth(entry.config.tag) + (2 * kMarginColumns));
    }
    return std::min(widest, std::max(kMinInner, state.width - kMarginColumns));
}

/// What the dialog says it is asking for. The list is the same either way and
/// the title is the whole of what tells the four apart — which is why the two
/// that act as soon as an area is picked say so in the word a user would look
/// for afterwards.
std::string titleFor(AppState::AreaPicker::For purpose) {
    switch (purpose) {
        case AppState::AreaPicker::For::Forward: return " Forward to area ";
        case AppState::AreaPicker::For::Move: return " Move to area ";
        case AppState::AreaPicker::For::Copy: return " Copy to area ";
        case AppState::AreaPicker::For::Reply: break;
    }
    return " Reply in area ";
}

/// The top of the frame, with a label in the middle of it — the title, or the
/// search query while one is being typed, the way the area list gives its
/// heading row over to it.
Element titleBar(const std::string& label, int width, theme::Color tint) {
    const std::string shown = truncateToWidth(label, width);
    const int left = std::max(0, (width - displayWidth(shown)) / 2);
    const int right = std::max(0, width - left - displayWidth(shown));
    return hbox({text("╭" + horizontalRule(left)) | color(theme::palette.dialogBorder),
                 text(shown) | color(tint),
                 text(horizontalRule(right) + "╮") | color(theme::palette.dialogBorder)});
}

}  // namespace

Element render(AppState& state, Element background) {
    AppState::AreaPicker& picker = *state.areaPicker;
    const auto& areas = state.manager.areas();
    clampCursor(state, picker);

    const int inner = innerWidth(state);
    const auto total = static_cast<int>(areas.size());
    const int rows = std::min(visibleRows(state), std::max(1, total));

    // Which label the frame carries. A query that matches nothing turns red and
    // leaves the cursor where it was, as it does on the area list: erasing it
    // takes the user back to what was still matching.
    std::string label = titleFor(picker.purpose);
    theme::Color tint = theme::palette.dialogTitle;
    if (!picker.search.empty()) {
        // "▌" stands in for the cursor: the terminal's own is hidden for the
        // whole application, and an input line without one reads as a label.
        label = " Area: " + picker.search + "▌ ";
        tint = findAreaByPrefix(areas, picker.search) ? theme::palette.dialogTitle
                                                      : theme::palette.error;
    }

    const auto side = [] { return text("│") | color(theme::palette.dialogBorder); };
    Elements lines{titleBar(label, inner, tint)};

    // The room is reserved first: the boxes are written into while the frame is
    // laid out, and a vector that grew under them would leave the earlier rows
    // pointing at freed memory.
    picker.rows.clear();
    picker.rows.reserve(static_cast<size_t>(rows));

    for (int i = 0; i < rows; ++i) {
        const int index = picker.offset + i;
        if (index >= total) {
            lines.push_back(hbox(
                {side(), text(std::string(static_cast<size_t>(inner), ' ')), side()}));
            continue;
        }
        const auto& entry = areas[static_cast<size_t>(index)];
        // A column of margin at each side, so the fill under the current row
        // covers them too rather than starting a column in.
        const int room = std::max(1, inner - 2);
        const std::string row =
            " " + padRight(truncateToWidth(entry.config.tag, room), room);

        // An area that cannot be opened is dimmed rather than hidden, the way
        // the area list shows one: it matters that the user can see the area
        // exists in the config and cannot be written to.
        Element cell = text(padRight(row, inner));
        if (index == picker.cursor) {
            cell = std::move(cell) | bold | color(theme::palette.selectionText) |
                   bgcolor(theme::palette.selection);
        } else if (!entry.isAvailable()) {
            cell = std::move(cell) | color(theme::palette.dialogHint);
        } else {
            cell = std::move(cell) | color(theme::palette.dialogText);
        }

        picker.rows.push_back({index, {}});
        lines.push_back(
            hbox({side(), std::move(cell) | reflect(picker.rows.back().box), side()}));
    }
    lines.push_back(text("╰" + horizontalRule(inner) + "╯") |
                    color(theme::palette.dialogBorder));

    // dialog::surface() wipes the screen behind the box and lays the dialog's
    // own fill down in its place, so the message underneath neither shows
    // through it nor colors it.
    return dbox(
        {std::move(background), dialog::surface(vbox(std::move(lines))) | center});
}

Outcome handleEvent(AppState& state, const Event& event) {
    AppState::AreaPicker& picker = *state.areaPicker;
    const auto& areas = state.manager.areas();
    const auto total = static_cast<int>(areas.size());
    if (total == 0) {
        state.areaPicker.reset();
        return Outcome::Dismissed;
    }

    // Picking the area under the cursor. A passthrough or an unreadable base has
    // nowhere to put the message, and the row says so by being dimmed — so the
    // key is swallowed rather than answered.
    const auto pick = [&picker, &areas]() -> Outcome {
        return areas[static_cast<size_t>(picker.cursor)].isAvailable() ? Outcome::Picked
                                                                       : Outcome::Ignored;
    };

    // A click picks the row it landed on, without selecting it first: pointing
    // at an area and pressing is one gesture, not two.
    if (const auto click = leftClick(event)) {
        for (const auto& row : picker.rows) {
            if (!row.box.Contain(click->x, click->y)) continue;
            // The area is taken off the row before anything else: showing the
            // click draws a frame, and render() rebuilds the rows every time it
            // does — which leaves nothing behind `row`.
            picker.cursor = row.index;
            clampCursor(state, picker);
            // Selected first and picked after, so that the row the pointer
            // landed on is on screen as the current one for the length of the
            // click animation before the dialog goes away.
            const Outcome outcome = pick();
            if (outcome == Outcome::Picked) state.showClick();
            return outcome;
        }
        // Anywhere else, inside the box or outside it: swallowed, as every
        // other event is while the dialog is modal.
        return Outcome::Ignored;
    }

    // Typing a name is how the search starts, so the letters are claimed before
    // anything that would rather have them.
    if (const auto typed = searchInput(event)) {
        picker.search += *typed;
        if (const auto match = findAreaByPrefix(areas, picker.search)) {
            picker.cursor = *match;
            clampCursor(state, picker);
        }
        return Outcome::Ignored;
    }
    if (event == Event::Backspace && !picker.search.empty()) {
        // An emptied query puts the title back and leaves the cursor on whatever
        // it last found: erasing a search is not undoing it.
        picker.search.pop_back();
        if (const auto match = findAreaByPrefix(areas, picker.search)) {
            picker.cursor = *match;
            clampCursor(state, picker);
        }
        return Outcome::Ignored;
    }
    // Esc closes the search rather than the dialog while something is being
    // typed: that is what the key most plainly means there.
    if (event == Event::Escape && !picker.search.empty()) {
        picker.search.clear();
        return Outcome::Ignored;
    }
    // Every other key ends the search — once the cursor is being moved by hand
    // or an area is being picked, the query has said what it had to say.
    picker.search.clear();

    if (const int wheel = wheelDelta(event); wheel != 0) {
        moveBy(state, picker, wheel);
        return Outcome::Ignored;
    }
    if (event == Event::ArrowUp) {
        moveBy(state, picker, -1);
        return Outcome::Ignored;
    }
    if (event == Event::ArrowDown) {
        moveBy(state, picker, 1);
        return Outcome::Ignored;
    }
    if (event == Event::PageUp) {
        picker.cursor = pageUpTarget(picker.cursor, picker.offset, visibleRows(state));
        clampCursor(state, picker);
        return Outcome::Ignored;
    }
    if (event == Event::PageDown) {
        picker.cursor =
            pageDownTarget(picker.cursor, picker.offset, visibleRows(state), total);
        clampCursor(state, picker);
        return Outcome::Ignored;
    }
    if (event == Event::Home) {
        picker.cursor = 0;
        clampCursor(state, picker);
        return Outcome::Ignored;
    }
    if (event == Event::End) {
        picker.cursor = total - 1;
        clampCursor(state, picker);
        return Outcome::Ignored;
    }
    if (event == Event::Return || event == Event::ArrowRight) {
        clampCursor(state, picker);
        return pick();
    }
    if (event == Event::Escape || event == Event::Backspace) {
        state.areaPicker.reset();
        return Outcome::Dismissed;
    }
    return Outcome::Ignored;
}

}  // namespace amberedit::ui::area_dialog
