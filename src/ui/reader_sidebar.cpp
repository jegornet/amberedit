#include "ui/reader_sidebar.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "ui/event_util.hpp"
#include "ui/msg_list_format.hpp"
#include "ui/screens/message_list_screen.hpp"
#include "ui/theme.hpp"

namespace amberedit::ui::reader_sidebar {

using namespace term;

namespace {

/// The panel carries its own margin on both sides, so that the highlight on the
/// marked row covers them rather than starting a column in. The rule beside it
/// is a column of its own and no part of a row.
constexpr int kIndent = 1;
constexpr int kRightPad = 1;

/// Which message the panel marks: the one the reader is showing, and not
/// `messageCursor`. They are the same wherever the reading went through this
/// screen — but a message written and then read back lands in the reader
/// straight from the compose screen, and what the panel is for is saying which
/// message is on the screen beside it.
uint32_t markedMessage(const AppState& state) {
    return state.readHeader ? state.readHeader->number : 0;
}

/// How far the panel may be scrolled: the last offset that still has the bottom
/// row of the area on it.
int lastOffset(const AppState& state) {
    return std::max(0, static_cast<int>(state.messageCount) - state.readerSidebarItems());
}

/// The messages the panel is about to draw, in the order they stand in — what
/// the Date column is measured against and what the rows are then drawn from,
/// gathered once so that the two cannot disagree.
///
/// A header the window has not reached yet stands as a row with none in it: the
/// row comes out blank, and the next frame has it.
std::vector<msg_format::Row> visibleRows(const AppState& state) {
    const int total = static_cast<int>(state.messageCount);
    const int shown = state.readerSidebarItems();

    std::vector<msg_format::Row> rows;
    rows.reserve(static_cast<size_t>(std::max(0, shown)));
    for (int i = 0; i < shown; ++i) {
        const int index = state.readerSidebarOffset + i;
        if (index >= total) break;

        msg_format::Row row;
        row.number = index + 1;
        row.header =
            screens::message_list::headerAt(state, static_cast<uint32_t>(row.number));
        if (row.header != nullptr) {
            row.fromIsOwn = state.isOwnName(row.header->from);
            row.toIsOwn = state.isOwnName(row.header->to);
        }
        rows.push_back(row);
    }
    return rows;
}

}  // namespace

void follow(AppState& state, uint32_t number) {
    const int total = static_cast<int>(state.messageCount);
    if (total <= 0 || number == 0) {
        state.readerSidebarOffset = 0;
        return;
    }

    const int rows = state.readerSidebarItems();
    const int index = static_cast<int>(number) - 1;
    int offset = state.readerSidebarOffset;
    // A message a row off either edge is the reading walking on, and the panel
    // scrolls the one row that puts it back on — the rows around it are the ones
    // that were already there. Anywhere else is a jump: a thread followed, a
    // search landed, an area opened at its lastread mark, and the panel opens
    // around where it landed rather than pinning it to an edge with nothing
    // beyond it.
    if (index >= offset - 1 && index <= offset + rows) {
        offset = std::clamp(offset, index - rows + 1, index);
    } else {
        offset = index - (rows / 2);
    }
    state.readerSidebarOffset = std::clamp(offset, 0, lastOffset(state));
}

uint32_t clickedMessage(const AppState& state, const Event& event) {
    if (!state.readerSidebarShown()) return 0;
    const auto click = leftClick(event);
    if (!click) return 0;
    // The rule closing the panel off is the reader's edge rather than the
    // panel's last column: a click on it has landed between the two and picks
    // neither.
    const int left = state.readerSidebarLeft();
    if (click->x < left || click->x >= left + state.readerSidebarWidth()) return 0;
    if (click->y < 0) return 0;

    const int row = click->y / state.readerSidebarRowHeight();
    if (row >= state.readerSidebarItems()) return 0;

    const int index = state.readerSidebarOffset + row;
    if (index >= static_cast<int>(state.messageCount)) return 0;
    return static_cast<uint32_t>(index) + 1;
}

bool wheeled(AppState& state, const Event& event) {
    if (!state.readerSidebarShown()) return false;
    const auto mouse = mouseOf(event);
    const int left = state.readerSidebarLeft();
    if (!mouse || mouse->x < left || mouse->x >= left + state.readerSidebarWidth())
        return false;

    const int wheel = wheelDelta(event);
    if (wheel == 0) return false;
    if (const int steps = state.wheelSteps(wheel, state.readerSidebarRowHeight());
        steps != 0) {
        state.readerSidebarOffset =
            std::clamp(state.readerSidebarOffset + steps, 0, lastOffset(state));
    }
    // A notch the throttle swallowed is handled all the same: the wheel was
    // turned at the panel, and the panel is what answers it rather than the
    // text beside it.
    return true;
}

Element render(AppState& state) {
    const int width = state.readerSidebarWidth();
    const int height = std::max(1, state.height);
    const int items = state.readerSidebarItems();
    const int rowHeight = state.readerSidebarRowHeight();
    const uint32_t marked = markedMessage(state);

    // A window resized under the panel can carry the message being read off it
    // with nothing having been asked for, so the panel goes back to it. Only
    // then: where the geometry is what it was, the offset is the user's — the
    // wheel scrolls the panel and the reader stays where it is.
    if (state.readerSidebarItemsShown != items) {
        follow(state, marked);
        state.readerSidebarItemsShown = items;
    }
    state.readerSidebarOffset =
        std::clamp(state.readerSidebarOffset, 0, lastOffset(state));

    // The headers under the rows about to be drawn. The list screen is scrolled
    // somewhere else entirely and asks for its own when it opens.
    screens::message_list::ensureHeaders(state, state.readerSidebarOffset, items);

    const std::vector<msg_format::Row> shown = visibleRows(state);
    const int rowWidth = std::max(0, width - kRightPad);
    const msg_format::Layout layout =
        msg_format::layout(state.config.readerSidebarFormat, rowWidth - kIndent,
                           state.messageCount, shown, state.config.readerDateTimeFormat);

    Elements lines;
    lines.reserve(static_cast<size_t>(height));
    for (int i = 0; i < items; ++i) {
        for (int line = 0; line < rowHeight && static_cast<int>(lines.size()) < height;
             ++line) {
            if (i >= static_cast<int>(shown.size())) {
                lines.push_back(text(std::string(static_cast<size_t>(width), ' ')));
                continue;
            }
            const msg_format::Row& row = shown[static_cast<size_t>(i)];
            // Every line of the marked row is marked: a highlight stopping
            // halfway down a row would read as two messages, one of them chosen.
            //
            // Marked and not selected: the panel says which message is on the
            // screen beside it, and the keyboard is in the reader. Its bar is
            // `reader_sidebar_msglist_selected` for that reason — quieter than
            // the one the lists choose a row with.
            const bool selected = static_cast<uint32_t>(row.number) == marked;
            const bool unsent = row.header != nullptr && domain::isUnsent(*row.header);
            const bool unread = state.config.highlightUnread && row.header != nullptr &&
                                !row.header->seen;
            const msg_format::Paint paint = selected ? msg_format::Paint::Marked
                                            : unsent ? msg_format::Paint::Unsent
                                            : unread ? msg_format::Paint::Unread
                                                     : msg_format::Paint::None;
            lines.push_back(msg_format::drawLine(row, layout[line], width, paint));
        }
    }
    // The lines at the bottom no whole row fitted in, drawn blank. A row is
    // drawn whole or not at all: half of one would read as a message shown, and
    // the lines cut off it are the ones the format put lowest because they
    // matter least.
    while (static_cast<int>(lines.size()) < height) {
        lines.push_back(text(std::string(static_cast<size_t>(width), ' ')));
    }

    // The rule down the side, in the color every other separator on the screen
    // is drawn in: the panel and the message are two things beside one another,
    // and the line is what says so. It stands between them, so which hand it is
    // on is `reader_sidebar_position` read the other way round.
    Elements rule;
    rule.reserve(static_cast<size_t>(height));
    for (int i = 0; i < height; ++i) rule.push_back(text("│"));

    Element panel = vbox(std::move(lines));
    Element edge = vbox(std::move(rule)) | color(theme::palette.separator);
    if (state.readerSidebarOnLeft()) {
        return hbox({std::move(panel), std::move(edge)});
    }
    return hbox({std::move(edge), std::move(panel)});
}

}  // namespace amberedit::ui::reader_sidebar
