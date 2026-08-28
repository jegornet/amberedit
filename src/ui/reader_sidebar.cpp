#include "ui/reader_sidebar.hpp"

#include <algorithm>
#include <cstdint>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "ui/event_util.hpp"
#include "ui/message_marks.hpp"
#include "ui/msg_list_format.hpp"
#include "ui/screens/message_list_screen.hpp"
#include "ui/text_layout.hpp"
#include "ui/theme.hpp"

namespace amberedit::ui::reader_sidebar {

using namespace term;

namespace {

/// The panel carries its own margin on both sides, so that the highlight on the
/// current row covers them rather than starting a column in. The rule beside it
/// is a column of its own and no part of a row.
constexpr int kIndent = 1;
constexpr int kRightPad = 1;

/// What the thread is drawn with under `reader_sidebar_content tree`: a fork
/// where more answers to the same message follow the one on the row, a corner
/// where it is the last of them, and the bar or the blank each becomes on the
/// lines below. Two columns apiece, so how deep a row stands is what its
/// drawing is wide.
constexpr const char* kFork = "├─";
constexpr const char* kCorner = "└─";
constexpr const char* kBar = "│ ";
constexpr const char* kBlank = "  ";

/// Which message the panel draws its bar on: the one the reader is showing, and
/// not `messageCursor`. They are the same wherever the reading went through this
/// screen — but a message written and then read back lands in the reader
/// straight from the compose screen, and what the panel is for is saying which
/// message is on the screen beside it.
///
/// Nothing to do with `AppState::marks`, which is what the user marked and what
/// the `m` column draws a star for.
uint32_t currentMessage(const AppState& state) {
    return state.readHeader ? state.readHeader->number : 0;
}

/// How many rows the panel has to show: every message of the area where it is
/// showing the area, and the rows of the thread where it is showing that.
int rowCount(const AppState& state) {
    return state.readerSidebarIsTree() ? static_cast<int>(state.readerSidebarTree.size())
                                       : static_cast<int>(state.messageCount);
}

/// How far the panel may be scrolled: the last offset that still has its bottom
/// row on it.
int lastOffset(const AppState& state) {
    return std::max(0, rowCount(state) - state.readerSidebarItems());
}

/// Which of `numbers` are worth a row of their own: those naming a message of
/// this area, and those the tree does not hold already. Both halves are the
/// base talking — a link may name a message that has since been deleted, or one
/// standing higher up the tree, and a thread whose links point in a circle would
/// otherwise be drawn for ever.
std::vector<uint32_t> drawable(const AppState& state,
                               const std::vector<uint32_t>& numbers,
                               const std::set<uint32_t>& seen) {
    std::vector<uint32_t> kept;
    for (const uint32_t number : numbers) {
        if (number == 0 || number > state.messageCount) continue;
        if (seen.count(number) != 0) continue;
        if (std::find(kept.begin(), kept.end(), number) != kept.end()) continue;
        kept.push_back(number);
    }
    return kept;
}

/// One message and everything under it the tree still has room for, in the
/// order the rows stand: the message itself, and then each of its answers with
/// its own answers under it, so that the panel reads down the thread.
///
/// `depth` is how far under the top of the tree this message stands and
/// `deepest` where the drawing stops — the level `reader_sidebar_tree_levels_down`
/// puts under the message being read. `path` is the way down to that message,
/// from the top of the tree to the message itself: a link the base holds only
/// one way round would otherwise leave the tree drawn without the message it
/// was drawn for, so wherever the descent stands on that path the next step of
/// it is among the answers whether the base names it or not.
void pushSubtree(const AppState& state, uint32_t number, int depth, int deepest,
                 const std::string& branch, const std::string& under,
                 const std::vector<uint32_t>& path, std::set<uint32_t>& seen,
                 std::vector<AppState::SidebarTreeRow>& rows) {
    if (!seen.insert(number).second) return;

    AppState::SidebarTreeRow row;
    row.number = number;
    row.branch = branch;
    row.under = under;
    row.header = state.base->header(number);
    rows.push_back(std::move(row));
    if (depth >= deepest) return;

    std::vector<uint32_t> shown =
        drawable(state, state.base->thread(number).replies, seen);
    if (depth + 1 < static_cast<int>(path.size()) &&
        path[static_cast<size_t>(depth)] == number) {
        const uint32_t next = path[static_cast<size_t>(depth) + 1];
        if (std::find(shown.begin(), shown.end(), next) == shown.end()) {
            shown.push_back(next);
        }
    }

    for (size_t i = 0; i < shown.size(); ++i) {
        const bool last = i + 1 == shown.size();
        pushSubtree(state, shown[i], depth + 1, deepest, under + (last ? kCorner : kFork),
                    under + (last ? kBlank : kBar), path, seen, rows);
    }
}

/// The way from `number` up to the top of the tree drawn around it and back
/// down: at most `reader_sidebar_tree_levels_up` messages answered one after
/// another, ending at `number` itself, so `front()` is the message the drawing
/// begins at and the size less one is how far under it the reading stands.
///
/// The thread is climbed only as far as it goes: a message with nothing above
/// it is the top of its own tree, and so is one whose links point back at
/// something already on the way up, a circle being no thread to climb.
std::vector<uint32_t> pathUp(const AppState& state, uint32_t number) {
    std::vector<uint32_t> path{number};
    std::set<uint32_t> climbed{number};
    for (int level = 0; level < state.config.readerSidebarTreeLevelsUp; ++level) {
        const uint32_t above = state.base->thread(path.back()).replyTo;
        if (above == 0 || above > state.messageCount) break;
        if (!climbed.insert(above).second) break;
        path.push_back(above);
    }
    std::reverse(path.begin(), path.end());
    return path;
}

/// The thread around `number`, in the rows the panel draws it as:
/// `reader_sidebar_tree_levels_up` levels of what it answers stand above it,
/// and everything under the message that reaches stands below, cut off
/// `reader_sidebar_tree_levels_down` levels under the message itself.
///
/// So the two settings at one each — which is what a config saying nothing gets
/// — is the message in its own place: what it answers at the top, that
/// message's other answers beside it, and the answers to each of those under
/// them. What the thread holds further up or further out than that is not
/// drawn: this is the thread as far as it bears on where the reading is, and a
/// panel is a strip beside a message rather than a second view of the area.
///
/// Read off the base rather than off `AppState::readThread`, so that what is
/// drawn is the thread of the message asked about rather than of whichever
/// message the reader last finished loading.
std::vector<AppState::SidebarTreeRow> treeAround(const AppState& state, uint32_t number) {
    std::vector<AppState::SidebarTreeRow> rows;
    if (state.base == nullptr || number == 0 || number > state.messageCount) return rows;

    const std::vector<uint32_t> path = pathUp(state, number);
    // Counted from the top of the tree and not from the message: the levels
    // below are the message's own, so a message with nothing above it is drawn
    // with the same depth beneath it as one buried in a thread.
    const int deepest =
        static_cast<int>(path.size()) - 1 + state.config.readerSidebarTreeLevelsDown;

    std::set<uint32_t> seen;
    pushSubtree(state, path.front(), 0, deepest, "", "", path, seen, rows);
    return rows;
}

/// Builds the tree around `number` and opens the panel at the top of it.
///
/// Only where the panel is up: a tree is a read of the base for every message in
/// it, and a window too narrow to show one would pay for it every time the
/// reading moved on. `render()` asks again where the tree it has was built
/// around another message, which is what builds one for a window dragged out to
/// where the panel fits.
void followTree(AppState& state, uint32_t number) {
    state.readerSidebarTree.clear();
    state.readerSidebarTreeFor = 0;
    state.readerSidebarOffset = 0;
    if (!state.readerSidebarShown()) return;

    state.readerSidebarTree = treeAround(state, number);
    state.readerSidebarTreeFor = number;

    // A thread is drawn whole and from its top, being the size it is rather than
    // a window on an area: only where it will not fit on the screen does the
    // panel open around the message being read instead.
    const int rows = state.readerSidebarItems();
    for (size_t i = 0; i < state.readerSidebarTree.size(); ++i) {
        if (state.readerSidebarTree[i].number != number) continue;
        if (const auto index = static_cast<int>(i); index >= rows) {
            state.readerSidebarOffset =
                std::clamp(index - (rows / 2), 0, lastOffset(state));
        }
        break;
    }
}

/// The messages the panel is about to draw, in the order they stand in — what
/// the Date column is measured against and what the rows are then drawn from,
/// gathered once so that the two cannot disagree.
///
/// A tree carries its own headers, its messages standing anywhere in the area.
/// Down the area they come from the window the screens share, and one it has not
/// reached yet stands as a row with no header in it: the row comes out blank,
/// and the next frame has it.
std::vector<msg_format::Row> visibleRows(const AppState& state) {
    const int total = rowCount(state);
    const int shown = state.readerSidebarItems();

    std::vector<msg_format::Row> rows;
    rows.reserve(static_cast<size_t>(std::max(0, shown)));
    for (int i = 0; i < shown; ++i) {
        const int index = state.readerSidebarOffset + i;
        if (index >= total) break;

        msg_format::Row row;
        if (state.readerSidebarIsTree()) {
            const AppState::SidebarTreeRow& node =
                state.readerSidebarTree[static_cast<size_t>(index)];
            row.number = static_cast<int>(node.number);
            row.header = &node.header;
        } else {
            row.number = index + 1;
            row.header =
                screens::message_list::headerAt(state, static_cast<uint32_t>(row.number));
        }
        row.marked = marks::isMarked(state, static_cast<uint32_t>(row.number));
        if (row.header != nullptr) {
            row.fromIsOwn = state.isOwnName(row.header->from);
            row.toIsOwn = state.isOwnName(row.header->to);
        }
        rows.push_back(row);
    }
    return rows;
}

/// The columns the thread takes in front of the messages: what the deepest row
/// of it comes to, measured over the whole tree rather than over the rows on the
/// screen, so that the columns beside it stand in one place and scrolling the
/// panel does not shuffle them. Nought where the panel is showing the area,
/// which has no tree in it.
int treeWidth(const AppState& state) {
    if (!state.readerSidebarIsTree()) return 0;
    int width = 0;
    for (const AppState::SidebarTreeRow& node : state.readerSidebarTree) {
        width = std::max(width, displayWidth(node.branch));
        width = std::max(width, displayWidth(node.under));
    }
    return width;
}

/// The thread as it is drawn in front of the `i`th row on the screen, on that
/// row's `line`th line, in the `width` columns every row of this tree leaves for
/// it. The corner stands on the line the message is read from and the bar under
/// it on the rest, so a row two lines tall is one message hanging from one fork.
std::string branchOf(const AppState& state, int i, int line, int width) {
    if (width <= 0) return "";
    const int index = state.readerSidebarOffset + i;
    if (index < 0 || index >= static_cast<int>(state.readerSidebarTree.size())) return "";

    const AppState::SidebarTreeRow& node =
        state.readerSidebarTree[static_cast<size_t>(index)];
    const std::string& drawn = line == 0 ? node.branch : node.under;
    // Padded out to the width of the deepest row rather than drawn as it stands:
    // the message then begins in the same column whatever depth it hangs at.
    const int pad = width - displayWidth(drawn);
    return pad > 0 ? drawn + std::string(static_cast<size_t>(pad), ' ') : drawn;
}

}  // namespace

void follow(AppState& state, uint32_t number) {
    if (state.readerSidebarIsTree()) {
        followTree(state, number);
        return;
    }

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
    if (index >= rowCount(state)) return 0;
    // A row of the tree is the message it was drawn for, wherever in the area
    // that message stands; a row of the area is its place in it.
    if (state.readerSidebarIsTree()) {
        return state.readerSidebarTree[static_cast<size_t>(index)].number;
    }
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
    const uint32_t current = currentMessage(state);

    // A window resized under the panel can carry the message being read off it
    // with nothing having been asked for, so the panel goes back to it. Only
    // then: where the geometry is what it was, the offset is the user's — the
    // wheel scrolls the panel and the reader stays where it is.
    //
    // A tree built around another message is the second reason to go back, and
    // the one that has nothing to do with the geometry: the panel is up in a
    // window that was too narrow for it when the message was opened, and this is
    // where the thread it wants is read.
    const bool stale =
        state.readerSidebarIsTree() && state.readerSidebarTreeFor != current;
    if (state.readerSidebarItemsShown != items || stale) {
        follow(state, current);
        state.readerSidebarItemsShown = items;
    }
    state.readerSidebarOffset =
        std::clamp(state.readerSidebarOffset, 0, lastOffset(state));

    // The headers under the rows about to be drawn, where those rows are the
    // area's. The list screen is scrolled somewhere else entirely and asks for
    // its own when it opens; a tree holds the headers it draws from itself.
    if (!state.readerSidebarIsTree()) {
        screens::message_list::ensureHeaders(state, state.readerSidebarOffset, items);
    }

    const std::vector<msg_format::Row> shown = visibleRows(state);
    const int rowWidth = std::max(0, width - kRightPad);
    const int branchWidth = treeWidth(state);
    const msg_format::Layout layout = msg_format::layout(
        state.config.readerSidebarFormat, std::max(0, rowWidth - kIndent - branchWidth),
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
            // Every line of the current row carries the bar: a highlight
            // stopping halfway down a row would read as two messages, one of
            // them chosen.
            //
            // Current and not selected: the panel says which message is on the
            // screen beside it, and the keyboard is in the reader. Its bar is
            // `reader_sidebar_msglist_selected` for that reason — quieter than
            // the one the lists choose a row with.
            const bool selected = static_cast<uint32_t>(row.number) == current;
            const bool unsent = row.header != nullptr && domain::isUnsent(*row.header);
            const bool unread = state.config.highlightUnread && row.header != nullptr &&
                                !row.header->seen;
            const msg_format::Paint paint = selected ? msg_format::Paint::Current
                                            : unsent ? msg_format::Paint::Unsent
                                            : unread ? msg_format::Paint::Unread
                                                     : msg_format::Paint::None;
            lines.push_back(msg_format::drawLine(row, layout[line], width, paint,
                                                 branchOf(state, i, line, branchWidth)));
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
