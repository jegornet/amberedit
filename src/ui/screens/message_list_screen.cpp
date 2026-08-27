#include "ui/screens/message_list_screen.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>

#include "ui/back_button.hpp"
#include "ui/event_util.hpp"
#include "ui/goto_field.hpp"
#include "ui/input_field.hpp"
#include "ui/list_page.hpp"
#include "ui/message_marks.hpp"
#include "ui/msg_list_format.hpp"
#include "ui/screens/message_read_screen.hpp"
#include "ui/scrollbar.hpp"
#include "ui/text_layout.hpp"
#include "ui/theme.hpp"

namespace amberedit::ui::screens::message_list {

using namespace term;

namespace {

/// The screen has no outer margin, so a row carries its own on both sides —
/// that way the highlight on the current row covers them too, rather than
/// starting a column in. The rules span the full width instead.
constexpr int kIndent = 1;
constexpr int kRightPad = 1;
/// What a row holds is `msglist_format`'s — which fields, in what order and how
/// wide — and `ui/msg_list_format.*` is what lays a window's width out among
/// them. Nothing about the columns is decided here.
///
/// Rows the table stands under: the title, the column headings and the rule.
/// Nothing stands under the rows, so they run to the bottom of the window.
/// What a click at a given row means is worked out from this, so it has to
/// match what render() puts above the rows.
constexpr int kHeaderRows = 3;

void clampCursor(AppState& state) {
    const int total = static_cast<int>(state.messageCount);
    if (total == 0) {
        state.messageCursor = 0;
        state.messageOffset = 0;
        return;
    }
    state.messageCursor = std::clamp(state.messageCursor, 0, total - 1);

    // Messages, not lines: a row two lines tall halves how many of them a
    // screen holds, and everything about where the list is scrolled to is
    // counted in messages.
    const int rows = state.messageListItems();
    state.messageOffset = std::min(state.messageOffset, state.messageCursor);
    if (state.messageCursor >= state.messageOffset + rows)
        state.messageOffset = state.messageCursor - rows + 1;
    state.messageOffset = std::clamp(state.messageOffset, 0, std::max(0, total - rows));
}

/// Keeps the selected message where it stands on the screen when a row's
/// height changes under it — what dragging the window across
/// `adaptive_ui_threshold` does where the two `msglist_format`s are not the
/// same number of lines tall. The area list's `reflowOffset()` word for word,
/// and for the same reasons: what is held is the line the selected row starts
/// on, and the messages above the cursor are counted again for the new height.
void reflowOffset(AppState& state) {
    const int height = state.msgRowHeight();
    if (state.msgRowHeightShown == height) return;
    if (state.msgRowHeightShown > 0) {
        const int line =
            (state.messageCursor - state.messageOffset) * state.msgRowHeightShown;
        // To the nearest row rather than the one above: half a row either way
        // is the closest the new height can come to where the cursor stood.
        const int above =
            std::clamp((line + (height / 2)) / height, 0, state.messageListItems() - 1);
        state.messageOffset = state.messageCursor - above;
    }
    state.msgRowHeightShown = height;
    clampCursor(state);
}

void moveBy(AppState& state, int delta) {
    state.messageCursor += delta;
    clampCursor(state);
    ensureHeaders(state);
}

/// Opens the message under the cursor — what Enter does, and what a click on a
/// row does once it has put the cursor there. The list sits on top of the
/// reader it was opened from, so picking a message drops back to it rather
/// than stacking a second reader.
///
/// Through `openMessage` rather than `loadMessage`: a row picked out of a list
/// is a place in the area, and where `twit_mode` walks past what stands there
/// the reader lands on the first message from it that is worth reading.
void openSelected(AppState& state) {
    message_read::openMessage(state, static_cast<uint32_t>(state.messageCursor + 1));
    state.navigator.pop();
}

/// Takes the twits out of the area, which is the whole of what `twit_mode kill`
/// does: the messages the `twit` lines cover are deleted as the area is opened,
/// and nothing is asked first — the setting is the answer, given once in the
/// config rather than message by message.
///
/// Here rather than in the reader, and once rather than as each is reached, so
/// that everything downstream sees an area with no twits in it: the numbers, the
/// list, the thread markers and the counts all agree, where deleting one message
/// at a time would renumber the area under whatever was reading it.
///
/// Backwards, for the same reason: deleting a message moves every number after
/// it, and a sweep that ran forwards would step over the message that moved up
/// into the place of the one it had just removed.
void killTwits(AppState& state) {
    if (state.areaConfig.twitMode != config::TwitMode::Kill) return;
    if (state.base == nullptr) return;

    bool removed = false;
    for (uint32_t number = state.base->count(); number >= 1; --number) {
        if (!state.areaConfig.isTwit(state.base->header(number))) continue;
        removed = state.base->remove(number).has_value() || removed;
    }
    // A base that will not be written is not worth saying anything about: the
    // messages are still there, and `twitHidden` keeps them behind the notice
    // rather than putting them on the screen unasked.
    if (removed) state.manager.refreshArea(state.currentArea);
}

/// Which message a click landed on, if it landed on one. Any line of a row is
/// that row — the subject under a name is the same message as the name. Lines
/// past the end of the area, and the lines at the bottom no whole row fitted
/// in, are drawn blank and point at nothing.
std::optional<int> clickedMessage(const AppState& state, const Event& event) {
    const auto click = leftClick(event);
    if (!click) return std::nullopt;

    const int line = click->y - kHeaderRows;
    if (line < 0) return std::nullopt;
    const int row = line / state.msgRowHeight();
    if (row >= state.messageListItems()) return std::nullopt;

    const int index = state.messageOffset + row;
    if (index >= static_cast<int>(state.messageCount)) return std::nullopt;
    return index;
}

/// The messages this frame is about to draw, in the order they stand in — what
/// the Date columns are measured against and what the lines are then drawn from,
/// gathered once so that the two cannot disagree.
///
/// A header the window has not reached yet stands as a row with none in it: the
/// line comes out blank, and `ensureHeaders()` will have it by the next frame.
std::vector<msg_format::Row> visibleRows(const AppState& state) {
    const int total = static_cast<int>(state.messageCount);
    const int shown = state.messageListItems();

    std::vector<msg_format::Row> rows;
    rows.reserve(static_cast<size_t>(std::max(0, shown)));
    for (int i = 0; i < shown; ++i) {
        const int index = state.messageOffset + i;
        if (index >= total) break;

        msg_format::Row row;
        row.number = index + 1;
        row.header = headerAt(state, static_cast<uint32_t>(row.number));
        row.marked = marks::isMarked(state, static_cast<uint32_t>(row.number));
        if (row.header != nullptr) {
            row.fromIsOwn = state.isOwnName(row.header->from);
            row.toIsOwn = state.isOwnName(row.header->to);
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

/// Back down to the reader. It still shows its own message, so the cursor goes
/// back onto it — otherwise the arrow keys there would carry on from wherever
/// the list was left.
void returnToReader(AppState& state) {
    // Whatever was half typed is left behind with the screen it was typed on:
    // the list comes back up on the message being read, not on a number nobody
    // finished.
    state.listGoto.clear();
    if (state.readHeader) {
        state.messageCursor = static_cast<int>(state.readHeader->number) - 1;
        clampCursor(state);
        ensureHeaders(state);
    }
    state.navigator.pop();
}

/// Goes to the message the typed number names, and puts the title back whether
/// or not there was one: a number naming no message is answered by the field
/// closing on it, which is the list saying it has nowhere to go. Nothing else is
/// said, exactly as in the reader — there is no room on this screen for more,
/// and a box over a mistyped digit is more than the mistake is worth.
///
/// What going means is `msglist_goto_field_opens`: on, the message is opened,
/// the field being the quick way to it; off, the cursor lands on the row and
/// the list stays up, and the row is then opened by Enter on it like any other.
///
/// Either way the cursor is centred first, `centerCursor()` being what a jump
/// across the area asks for — the row landed on has the area either side of it,
/// where the least scrolling that shows it would leave it against the top or
/// the bottom edge. It is what the list comes back up on where a message opened
/// from here is left by the way that returns to it.
void applyGoto(AppState& state) {
    const uint32_t number = goto_field::numberOf(state.listGoto);
    state.listGoto.clear();
    if (number == 0 || number > state.messageCount) return;

    state.messageCursor = static_cast<int>(number) - 1;
    centerCursor(state);
    ensureHeaders(state);
    if (state.config.messageListGotoFieldOpens) openSelected(state);
}

}  // namespace

void centerCursor(AppState& state) {
    const int total = static_cast<int>(state.messageCount);
    if (total == 0) {
        state.messageCursor = 0;
        state.messageOffset = 0;
        return;
    }

    const int rows = state.messageListItems();
    state.messageCursor = std::clamp(state.messageCursor, 0, total - 1);
    // Half a screen above the cursor, clamped at both ends: the list never
    // scrolls past the first or the last message to keep the cursor centred,
    // and an area that fits on one screen does not scroll at all.
    state.messageOffset =
        std::clamp(state.messageCursor - rows / 2, 0, std::max(0, total - rows));
}

void ensureHeaders(AppState& state) {
    ensureHeaders(state, state.messageOffset, state.messageListItems());
}

void ensureHeaders(AppState& state, int first, int count) {
    if (state.base == nullptr || state.messageCount == 0) {
        state.headers.clear();
        state.headersStart = 0;
        return;
    }

    const int total = static_cast<int>(state.messageCount);
    const int loaded = static_cast<int>(state.headers.size());
    const int wanted = std::max(1, count);
    const int from = std::clamp(first, 0, std::max(0, total - 1));
    const int windowEnd = std::min(total, from + wanted);

    // The window still covers the visible part — nothing to re-read from disk.
    if (!state.headers.empty() && from >= state.headersStart &&
        windowEnd <= state.headersStart + loaded) {
        return;
    }

    // Grab a margin on both sides so that scrolling does not hit the disk on
    // every single row.
    const int window = std::max(120, wanted * 4);
    const int start = std::max(0, from - (window / 2));
    const int limit = std::min(window, total - start);

    state.headers.clear();
    state.headers.reserve(static_cast<size_t>(std::max(0, limit)));
    for (int i = 0; i < limit; ++i) {
        // Messages are indexed 1-based throughout IMsgBase.
        state.headers.push_back(state.base->header(static_cast<uint32_t>(start + i + 1)));
    }
    state.headersStart = start;
}

const domain::MessageHeader* headerAt(const AppState& state, uint32_t msgNumber) {
    const int index = static_cast<int>(msgNumber) - 1 - state.headersStart;
    if (index < 0 || index >= static_cast<int>(state.headers.size())) return nullptr;
    return &state.headers[static_cast<size_t>(index)];
}

tl::expected<void, ErrorPtr> enterArea(AppState& state, const domain::AreaConfig& area) {
    auto opened = state.manager.openArea(area);
    if (!opened) return tl::make_unexpected(std::move(opened).error());

    ports::IMsgBase* base = *opened;
    state.base = base;
    // The area and the settings it is read under, together: setCurrentArea() is
    // the one place either is assigned. The twits are read off those settings,
    // so this stands before the sweep below.
    state.setCurrentArea(area);
    killTwits(state);
    state.messageCount = base->count();
    state.headers.clear();
    state.headersStart = 0;

    const uint32_t start = state.manager.startingMessage(area, state.messageCount);
    state.messageCursor = start == 0 ? 0 : static_cast<int>(start) - 1;
    centerCursor(state);
    ensureHeaders(state);

    // Entering an area drops straight into reading, empty or not: an empty area
    // opens the reader on nothing at all — blank header rows, no body — which
    // is where a first message is written from. A list of no messages would
    // only be a dead end.
    if (state.messageCount == 0) {
        message_read::showEmptyArea(state);
        state.navigator.push(app::ScreenId::MessageRead);
        return {};
    }
    // Where the mark leaves the reading is a place in the area like any other,
    // so a twit standing there is walked past exactly as it is when the list
    // names one.
    message_read::openMessage(state, start);
    // The cursor may have moved off the message the mark named, and the list is
    // still scrolled to where that one was.
    centerCursor(state);
    ensureHeaders(state);
    state.navigator.push(app::ScreenId::MessageRead);
    return {};
}

void leaveArea(AppState& state) {
    // What the area holds now, taken off the base still open on it, before that
    // base is closed: a tosser delivering into an area while it is being read
    // leaves the row underneath counting what was there when the list was built,
    // and the mark has moved wherever the reading went. Both counts are read
    // again here rather than a rescan being asked for: it is this one area's
    // own base, and every other row stands as it was.
    state.manager.refreshArea(state.currentArea);
    state.base = nullptr;
    state.headers.clear();
    state.headersStart = 0;
    state.readHeader.reset();
    state.readBody.reset();
    state.readLines.clear();
    // The reader's sidebar was scrolled to somewhere in the area being left,
    // and the next area is a different area: it opens at its own lastread mark,
    // which is a jump from the top rather than from wherever this one ended.
    state.readerSidebarOffset = 0;
    // The marks are the area's: they are UIDs of messages in the base about to
    // be closed, and the next area's numbering knows nothing of them. Marking is
    // done to gather a handful of messages and then act on them, so a set that
    // outlived the area would be a set nobody could see to unmake.
    state.marks.clear();
    state.manager.closeCurrentArea();
    // Whatever depth the area was left from, the way out is the area list.
    state.navigator.reset();
}

Element render(AppState& state) {
    // The reader's title, word for word: the tag, the AKA the area is presented
    // under, then which message of how many. The two screens show the same area
    // and moving between them should not restate it differently. The AKA is
    // absent only when the tosser config states none and the AmberEdit config has no
    // address either.
    const std::string aka = state.currentArea.address.isValid()
                                ? " (" + state.currentArea.address.toString() + ")"
                                : "";
    // Which message of how many, the pair the title ends in. The list is only
    // ever opened from the reader, on a message: an area with none opens the
    // reader on nothing instead, and `l` there says so rather than bringing up
    // a table of no rows.
    const std::string numbers = std::to_string(state.messageCursor + 1) + "/" +
                                std::to_string(state.messageCount);

    // The number typed to be taken somewhere stands where that pair does, the
    // reader's rule on the reader's title: the title says either where the
    // cursor is or where it is being sent, never both. Empty means nothing is
    // being typed; see `AppState::listGoto`.
    const bool goingTo = !state.listGoto.empty();

    // The space in front of the pair is the title's own and stays the title's
    // while the field stands in the pair's columns — what sets the box off from
    // the area's name is the space that set the pair off from it.
    const std::string titleText =
        " " + state.currentArea.tag + aka + (goingTo ? " " : " " + numbers);

    // Where the cursor stands on the screen is kept across a change in how tall
    // a row is, which is what dragging the window across the threshold can do.
    // Before anything is laid out: the offset settled here is the one drawn.
    reflowOffset(state);

    const int visibleLines = state.messageListRows();
    const int rowHeight = state.msgRowHeight();
    const int visibleMessages = state.messageListItems();
    const int messageCount = static_cast<int>(state.messageCount);
    // The bar the reader draws beside a message too long for the window, in the
    // rightmost column and beside the rows alone — the title, the headings and
    // the rule above them span the whole width. Only where the area is longer
    // than the screen: a list that fits has nothing to scroll, exactly as the
    // reader shows no bar for a message that fits.
    const bool scrollbarShown =
        state.config.messageListScrollbar && messageCount > visibleMessages;
    // What the columns are laid out in: the bar's column is taken off the rows
    // before they are divided up, so the text runs up to it rather than under it.
    const int listWidth = std::max(0, state.width - (scrollbarShown ? 1 : 0));
    const int rowWidth = std::max(0, listWidth - kRightPad);

    // The messages this frame draws, gathered before the columns are worked out:
    // how wide the Date column stands is a question about the stamps on the
    // screen, and these are them.
    const std::vector<msg_format::Row> shown = visibleRows(state);
    const msg_format::Layout layout =
        msg_format::layout(state.messageListFormat(), rowWidth - kIndent,
                           state.messageCount, shown, state.config.readerDateTimeFormat);

    // A format whose fixed widths come to more than the window holds is cut off
    // at its right edge rather than pushing the row past it: what a field asks
    // for is the user's, and how much there is to give is the window's.
    const auto toRow = [rowWidth](const std::string& line) {
        return padRight(substrByWidth(line, 0, rowWidth), rowWidth) + " ";
    };

    // The button's second row lands on the headings rather than on the rule —
    // that is where the list's second row is. The headings are not moved over
    // for it, only covered: shifting them would take every column out of line
    // with the rows below, and what goes under the button is the '#' over the
    // number column, which the numbers under it say plainly enough.
    // The button's label lights up while a click on it is being shown, and the
    // row it is on is laid out well before the row under it.
    const bool pressedBack = state.isPressed(AppState::Pressed::Back);

    const std::string headings = toRow(" " + msg_format::header(layout));
    Element header = text(headings) | color(theme::palette.tableHeader);
    if (state.backButtonShown() && displayWidth(headings) > back_button::kWidth) {
        const size_t rest = displayWidth(headings) - back_button::kWidth;
        header = hbox({back_button::bottomRow(pressedBack),
                       text(substrByWidth(headings, back_button::kWidth, rest)) |
                           color(theme::palette.tableHeader)});
    }

    auto separator = text(horizontalRule(state.width)) | color(theme::palette.separator);

    // One line of one message's row, drawn — the runs the format cut it into,
    // and what the screen has to say about the message on top of them. How it is
    // painted is `msg_format::drawLine()`'s, shared with the reader's sidebar so
    // that a message reads the same in both.
    const auto lineOf = [&](const msg_format::Row& row, const msg_format::Line& columns) {
        // Every line of the selected row is drawn selected: a highlight
        // stopping halfway down a row would read as two messages, one of them
        // chosen.
        const bool selected = row.number - 1 == state.messageCursor;
        // A message of one's own still waiting to go out is marked across the
        // whole row. A message nobody has read yet is marked by the mark the
        // base itself keeps on it — not the area list's unread count, which is
        // a position and says how many messages stand after the lastread mark.
        const bool unsent = row.header != nullptr && domain::isUnsent(*row.header);
        const bool unread =
            state.config.highlightUnread && row.header != nullptr && !row.header->seen;

        const msg_format::Paint paint = selected ? msg_format::Paint::Selected
                                        : unsent ? msg_format::Paint::Unsent
                                        : unread ? msg_format::Paint::Unread
                                                 : msg_format::Paint::None;
        return msg_format::drawLine(row, columns, rowWidth + 1, paint);
    };

    Elements lines;
    lines.reserve(static_cast<size_t>(visibleLines));
    for (int i = 0; i < visibleMessages; ++i) {
        for (int line = 0;
             line < rowHeight && static_cast<int>(lines.size()) < visibleLines; ++line) {
            if (i >= static_cast<int>(shown.size())) {
                lines.push_back(text(""));
                continue;
            }
            lines.push_back(lineOf(shown[static_cast<size_t>(i)], layout[line]));
        }
    }
    // The lines at the bottom that no whole row fitted in. A row is drawn whole
    // or not at all: half of one would read as a message shown, and the fields
    // cut off it are the ones the format put lowest because they matter least.
    //
    // The window too short for even one whole row is the one exception, and the
    // count above is what makes it one: there is always a message on the screen,
    // so as much of the first row as there is room for is drawn.
    while (static_cast<int>(lines.size()) < visibleLines) lines.push_back(text(""));

    const int titleRoom =
        std::max(1, state.width - (state.backButtonShown() ? back_button::kWidth : 0));

    // What is being typed, drawn exactly as the reader draws it: `inputField()`
    // on the fill a focused field carries, the digits in `focused_text`, the
    // room left underscored and the cursor an inverted cell — the box asking
    // for a number reads the same on both screens showing the area.
    //
    // It is the pair's own columns and no more, fixed at that whatever is typed
    // into it: a longer number scrolls sideways under the cursor, which
    // `inputField()` does itself, and a window too narrow for the whole box
    // narrows it rather than pushing the Back button off the row.
    int gotoWidth = 0;
    Element gotoField;
    if (goingTo) {
        gotoWidth = std::max(1, std::min(displayWidth(numbers), titleRoom));
        gotoField = inputField(state.listGoto, state.listGoto.size(), gotoWidth, true,
                               theme::palette.focusedText,
                               fieldFiller(theme::palette.inputFiller)) |
                    bgcolor(theme::palette.focusedField);
    }

    // The field keeps its columns and the area's name gives them up, as in the
    // reader: what is being typed is the one thing on the row the user is
    // looking at, and a name cut short still says which area this is.
    const int titleWidth = std::max(goingTo ? 0 : 1, titleRoom - gotoWidth);
    Element title = text(truncateToWidth(titleText, static_cast<size_t>(titleWidth))) |
                    bold | color(theme::palette.tableHeader);
    if (goingTo) title = hbox({std::move(title), std::move(gotoField)});
    if (state.backButtonShown()) {
        title = hbox({back_button::topRow(pressedBack), std::move(title)});
    }

    Element table = vbox(std::move(lines));
    if (scrollbarShown) {
        // The rows were laid out a column narrower, so the bar fills the
        // rightmost one exactly, with the table running up to it.
        //
        // Built a cell at a time rather than by `bar()`: the thumb is worked out
        // against the messages on the screen, which is what is being scrolled,
        // and then drawn down the lines each of them takes — so a row two lines
        // tall has two lines of thumb, and the bar says where in the area the
        // screen is rather than how many lines that came to.
        const scrollbar::Thumb thumb =
            scrollbar::thumbOf(visibleMessages, messageCount, state.messageOffset);
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

    return vbox({std::move(title), std::move(header), separator, std::move(table)});
}

bool handleEvent(AppState& state, const Event& event) {
    // The back button, where it is shown. It goes back to the message being
    // read, the same way Esc does here.
    if (state.backButtonShown() && back_button::clicked(event)) {
        state.showClick(AppState::Pressed::Back);
        returnToReader(state);
        return true;
    }
    // A click opens the message it landed on, cursor and all: the row under the
    // pointer is the one meant, wherever the cursor happened to be.
    if (const auto clicked = clickedMessage(state, event)) {
        // A row picked with the pointer is the row meant, and the number half
        // typed is put away with it — the click said where to go.
        state.listGoto.clear();
        state.messageCursor = *clicked;
        clampCursor(state);
        ensureHeaders(state);
        // The cursor is moved, shown there for the length of the click
        // animation, and only then is the message opened: the row the pointer
        // landed on is on the screen as the current one before it goes away.
        state.showClick();
        openSelected(state);
        return true;
    }
    // While a message number is being typed, the keys that build it are the
    // field's: the digits, Enter to go there, Esc to think better of it, and
    // Backspace taking them back one at a time — an emptied field is a closed
    // one, so the last Backspace puts the title back the way Esc does. All four
    // mean something else on this screen with no field up, and the field has
    // them while it is up.
    //
    // Every other key closes the field and is then answered as it always is:
    // once something else is being asked for, the number has said what it had
    // to say. That covers the wheel below as it covers ↑↓ — the cursor being
    // moved by hand is the number given up on.
    if (!state.listGoto.empty()) {
        if (const auto digit = goto_field::digitOf(event)) {
            if (state.listGoto.size() < goto_field::kDigits) state.listGoto += *digit;
            return true;
        }
        if (event == Event::Return) {
            applyGoto(state);
            return true;
        }
        if (event == Event::Escape) {
            state.listGoto.clear();
            return true;
        }
        if (event == Event::Backspace) {
            state.listGoto.pop_back();
            return true;
        }
        state.listGoto.clear();
    }

    // Marking the message under the cursor, which is the whole of what this
    // screen does besides move about in the area. Space says the same thing as
    // the key the layout binds, and is not itself bindable: the keys that move
    // about are fixed everywhere, and this is the one screen where Space is
    // worth more than a second PgDn — a list is where a run of messages is
    // picked out, and the marking hand is on the space bar.
    if (state.messageCount > 0 && (state.keys.is(event, Command::MessageListMarkToggle) ||
                                   event == Event::Character(' '))) {
        marks::toggle(state, static_cast<uint32_t>(state.messageCursor + 1));
        return true;
    }

    // A digit is how the field opens — after the command above, so a layout that
    // binds a digit to it keeps the digit: a key made into a command stops being
    // one the list can be sent anywhere by.
    if (state.messageCount > 0) {
        if (const auto digit = goto_field::digitOf(event)) {
            state.listGoto = std::string(1, *digit);
            return true;
        }
    }

    // The wheel moves the cursor a line at a time, the same as ↑↓ — through
    // `list_wheel_throttle`, which where a row stands more than one line tall
    // spends a row's worth of notches on each message, so that a flick moves
    // the list as far as it moves the text in the reader. A notch it swallows
    // is still handled: the wheel was turned at the list, and the list is what
    // answers it.
    if (const int wheel = wheelDelta(event); wheel != 0) {
        if (const int steps = state.wheelSteps(wheel, state.msgRowHeight()); steps != 0)
            moveBy(state, steps);
        return true;
    }
    if (event == Event::ArrowUp) {
        moveBy(state, -1);
        return true;
    }
    if (event == Event::ArrowDown) {
        moveBy(state, 1);
        return true;
    }
    if (event == Event::PageUp) {
        state.messageCursor = pageUpTarget(state.messageCursor, state.messageOffset,
                                           state.messageListItems());
        clampCursor(state);
        ensureHeaders(state);
        return true;
    }
    if (event == Event::PageDown) {
        state.messageCursor = pageDownTarget(state.messageCursor, state.messageOffset,
                                             state.messageListItems(),
                                             static_cast<int>(state.messageCount));
        clampCursor(state);
        ensureHeaders(state);
        return true;
    }
    if (event == Event::Home) {
        state.messageCursor = 0;
        clampCursor(state);
        ensureHeaders(state);
        return true;
    }
    if (event == Event::End) {
        state.messageCursor = static_cast<int>(state.messageCount) - 1;
        clampCursor(state);
        ensureHeaders(state);
        return true;
    }
    if (event == Event::Return || event == Event::ArrowRight) {
        if (state.messageCount == 0) return true;
        openSelected(state);
        return true;
    }
    if (event == Event::Escape || event == Event::ArrowLeft ||
        event == Event::Backspace) {
        returnToReader(state);
        return true;
    }
    return false;
}

}  // namespace amberedit::ui::screens::message_list
