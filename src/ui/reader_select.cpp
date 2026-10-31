#include "ui/reader_select.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ui/event_util.hpp"
#include "ui/header_format.hpp"
#include "ui/screens/compose_screen.hpp"
#include "ui/text_editor.hpp"
#include "ui/text_layout.hpp"

namespace amberedit::ui::reader_select {

using screens::compose::kFromAddr;
using screens::compose::kFromName;
using screens::compose::kSubject;
using screens::compose::kToAddr;
using screens::compose::kToName;
using term::Event;
using term::MouseEvent;

namespace {

/// Whether the cursor may stand in `field`: a stop of the block as the frame
/// drew it.
bool fieldShown(const AppState& state, int field) {
    const auto& stops = state.readSelect.stops;
    return field >= 0 && static_cast<size_t>(field) < stops.size() &&
           stops[static_cast<size_t>(field)].has_value();
}

/// The field after the cursor's in `step`'s direction, as the block is read, or
/// -1 where that is off either end — into the text, the ring closing there.
int fieldAfter(const AppState& state, int step) {
    const std::vector<int> order = header_format::stopsInOrder(state.readSelect.stops);
    int at = -1;
    for (size_t i = 0; i < order.size(); ++i) {
        if (order[i] == state.readSelect.field) at = static_cast<int>(i);
    }
    const int next = at + step;
    if (next < 0 || next >= static_cast<int>(order.size())) return -1;
    return order[static_cast<size_t>(next)];
}

/// The field under the cursor's as the block is drawn, -1 being the text — the
/// editor's `fieldBelow()`, with no button in the way.
int fieldBelow(const AppState& state) {
    const auto& select = state.readSelect;
    if (!fieldShown(state, select.field)) return -1;
    return header_format::stopToward(select.stops,
                                     *select.stops[static_cast<size_t>(select.field)], 1);
}

/// The field over it, or -1 where nothing stands over it.
int fieldAbove(const AppState& state) {
    const auto& select = state.readSelect;
    if (!fieldShown(state, select.field)) return -1;
    return header_format::stopToward(select.stops,
                                     *select.stops[static_cast<size_t>(select.field)], -1);
}

/// The first field read in the block and the last, or -1 with none drawn.
int firstField(const AppState& state) {
    const std::vector<int> order = header_format::stopsInOrder(state.readSelect.stops);
    return order.empty() ? -1 : order.front();
}

int lastField(const AppState& state) {
    const std::vector<int> order = header_format::stopsInOrder(state.readSelect.stops);
    return order.empty() ? -1 : order.back();
}

/// The field ↑ goes onto off the first row of the text: the one nearest the
/// left edge on the lowest line of the block holding one, or -1 with none drawn.
int fieldOverText(const AppState& state) {
    return header_format::stopToward(
        state.readSelect.stops, header_format::Stop{state.readerHeaderRows(), 0, 1}, -1);
}

int lastRow(const AppState& state) {
    return static_cast<int>(state.readSelect.text.lines.size()) - 1;
}

/// Keeps the row the cursor is on in the window.
void scrollToCursor(AppState& state) {
    const int row = state.readSelect.text.row;
    const int rows = state.readRows();
    state.readScroll = std::min(state.readScroll, row);
    if (row >= state.readScroll + rows) state.readScroll = row - rows + 1;
    const int most = std::max(0, static_cast<int>(state.readLines.size()) - rows);
    state.readScroll = std::clamp(state.readScroll, 0, most);
}

/// The cursor onto row `row`, in the column it stood in on the row it leaves.
void toRow(AppState& state, int row) {
    TextBuffer& text = state.readSelect.text;
    const int column = displayWidth(text.line().substr(0, text.col));
    text.row = std::clamp(row, 0, lastRow(state));
    text.col = offsetAtColumn(text.line(), 0, column);
}

/// The cursor into a header field, at the end of what it holds — where the
/// editor's cursor lands on a field as well.
void toField(AppState& state, int field) {
    auto& select = state.readSelect;
    select.inHeader = true;
    select.field = field;
    select.fieldCursor = fieldValue(state, field).size();
    select.fieldAnchor.reset();
}

/// The cursor down into the text, at the start of the first row on screen:
/// whatever the window shows under the header is what is below it.
void toText(AppState& state) {
    auto& select = state.readSelect;
    select.inHeader = false;
    select.fieldAnchor.reset();
    select.text.anchor.reset();
    select.text.row = std::clamp(state.readScroll, 0, lastRow(state));
    select.text.col = 0;
}

/// What is selected onto the clipboard `clipboard` names, the reader's own copy
/// going the same way. `quiet` leaves the "Copied" box out, for the copy a
/// selection makes by itself.
void copy(AppState& state, bool quiet) {
    std::string text = selectedText(state);
    if (text.empty()) return;
    if (state.config.clipboardInEffect() == config::ClipboardMode::Internal) {
        state.clipboard = std::move(text);
        state.copiedNotice = state.config.clipboardCopyIndicate && !quiet;
    } else {
        state.clipboardCopyRequested = std::move(text);
        state.clipboardCopyQuiet = quiet;
    }
}

/// Shift with an arrow, Home or End — what a selection is made with — or the
/// bare key while the anchor is held.
bool extendsSelection(const AppState& state, const Event& event) {
    if (event.ctrl() || event.alt() || event.kind() != Event::Kind::Key) return false;
    if (!event.shift() && !state.readSelect.anchored) return false;
    switch (event.name()) {
        case Event::Name::ArrowLeft:
        case Event::Name::ArrowRight:
        case Event::Name::ArrowUp:
        case Event::Name::ArrowDown:
        case Event::Name::Home:
        case Event::Name::End: return true;
        default: return false;
    }
}

/// The ends of a line, from the keys and from the editor's chords for them.
bool isLineStart(const AppState& state, const Event& event) {
    return event == Event::Home || state.keys.is(event, Command::ComposeLineStart);
}
bool isLineEnd(const AppState& state, const Event& event) {
    return event == Event::End || state.keys.is(event, Command::ComposeLineEnd);
}

/// A key in a header field. A field is one line, so Up and Down with Shift are
/// its two ends and nothing with Shift crosses into the field beside it — the
/// editor's rule.
void headerKey(AppState& state, const Event& event) {
    auto& select = state.readSelect;
    const std::string value = fieldValue(state, select.field);
    size_t& cursor = select.fieldCursor;
    cursor = std::min(cursor, value.size());

    if (extendsSelection(state, event)) {
        if (!select.fieldAnchor) select.fieldAnchor = cursor;
        switch (event.name()) {
            case Event::Name::ArrowLeft: cursor = prevChar(value, cursor); break;
            case Event::Name::ArrowRight:
                cursor = std::min(value.size(), cursor + charLen(value, cursor));
                break;
            case Event::Name::ArrowUp:
            case Event::Name::Home: cursor = 0; break;
            default: cursor = value.size(); break;
        }
        if (state.config.clipboardCopyOnSelect == config::CopyOnSelect::Always) {
            copy(state, /*quiet=*/true);
        }
        return;
    }
    // The anchor held stays where it was put down for every motion inside the
    // field; Tab takes the cursor out of it, and a selection is one field's.
    if (select.anchored && event != Event::Tab && event != Event::TabReverse) {
        if (!select.fieldAnchor) select.fieldAnchor = cursor;
    } else {
        select.anchored = false;
        select.fieldAnchor.reset();
    }

    if (event == Event::ArrowDown) {
        const int below = fieldBelow(state);
        if (below < 0) {
            toText(state);
        } else {
            toField(state, below);
        }
        return;
    }
    if (event == Event::ArrowUp) {
        const int above = fieldAbove(state);
        if (above >= 0) toField(state, above);
        return;
    }
    if (event == Event::Tab || event == Event::TabReverse) {
        const int next = fieldAfter(state, event == Event::Tab ? 1 : -1);
        if (next < 0) {
            toText(state);
        } else {
            toField(state, next);
        }
        return;
    }
    if (state.keys.is(event, Command::ComposeWordLeft)) {
        cursor = wordStartBefore(value, cursor).value_or(0);
        return;
    }
    if (state.keys.is(event, Command::ComposeWordRight)) {
        cursor = wordEndAfter(value, cursor).value_or(value.size());
        return;
    }
    // Fields standing side by side, and the arrows cross between them at the
    // edge they share, as in the editor.
    if (event == Event::ArrowLeft) {
        const int beside = fieldShown(state, select.field)
                               ? header_format::stopBeside(select.stops, select.field, -1)
                               : -1;
        if (cursor == 0 && beside >= 0) {
            toField(state, beside);
            return;
        }
        cursor = prevChar(value, cursor);
        return;
    }
    if (event == Event::ArrowRight) {
        const int beside = fieldShown(state, select.field)
                               ? header_format::stopBeside(select.stops, select.field, 1)
                               : -1;
        if (cursor == value.size() && beside >= 0) {
            toField(state, beside);
            select.fieldCursor = 0;
            return;
        }
        cursor = std::min(value.size(), cursor + charLen(value, cursor));
        return;
    }
    if (isLineStart(state, event)) {
        cursor = 0;
        return;
    }
    if (isLineEnd(state, event)) cursor = value.size();
}

/// A key in the text, which moves the cursor over the rows as they are laid
/// out: the rows are what is on the screen, and a row the window broke off a
/// line is a row an arrow goes down onto like any other.
void textKey(AppState& state, const Event& event) {
    TextBuffer& text = state.readSelect.text;

    if (extendsSelection(state, event)) {
        if (!text.anchor) text.anchor = text.cursor();
        switch (event.name()) {
            case Event::Name::ArrowLeft: moveLeft(text); break;
            case Event::Name::ArrowRight: moveRight(text); break;
            case Event::Name::ArrowUp: toRow(state, text.row - 1); break;
            case Event::Name::ArrowDown: toRow(state, text.row + 1); break;
            case Event::Name::Home: moveToLineStart(text); break;
            default: moveToLineEnd(text); break;
        }
        scrollToCursor(state);
        if (state.config.clipboardCopyOnSelect == config::CopyOnSelect::Always) {
            copy(state, /*quiet=*/true);
        }
        return;
    }
    // The anchor held stays down for the word, page and Ctrl-Home/End keys too;
    // Tab takes the cursor up into the header and lifts it.
    if (state.readSelect.anchored && event != Event::Tab && event != Event::TabReverse) {
        if (!text.anchor) text.anchor = text.cursor();
    } else {
        state.readSelect.anchored = false;
        text.anchor.reset();
    }

    if (event == Event::Tab || event == Event::TabReverse ||
        (event == Event::ArrowUp && text.row == 0)) {
        // Up off the first row of the message is the header over it.
        const int field = event == Event::Tab          ? firstField(state)
                          : event == Event::TabReverse ? lastField(state)
                                                       : fieldOverText(state);
        if (field >= 0) toField(state, field);
        return;
    }
    if (state.keys.is(event, Command::ComposeWordLeft)) {
        moveWordLeft(text);
    } else if (state.keys.is(event, Command::ComposeWordRight)) {
        moveWordRight(text);
    } else if (event == Event::ArrowLeft) {
        moveLeft(text);
    } else if (event == Event::ArrowRight) {
        moveRight(text);
    } else if (event == Event::ArrowUp) {
        toRow(state, text.row - 1);
    } else if (event == Event::ArrowDown) {
        toRow(state, text.row + 1);
    } else if (event == Event::PageUp) {
        toRow(state, text.row - state.readRows());
    } else if (event == Event::PageDown) {
        toRow(state, text.row + state.readRows());
    } else if (event == Event::Named(Event::Name::Home, /*ctrl=*/true)) {
        text.row = 0;
        text.col = 0;
    } else if (event == Event::Named(Event::Name::End, /*ctrl=*/true)) {
        text.row = lastRow(state);
        text.col = text.line().size();
    } else if (isLineStart(state, event)) {
        moveToLineStart(text);
    } else if (isLineEnd(state, event)) {
        moveToLineEnd(text);
    } else {
        return;
    }
    scrollToCursor(state);
}

/// The row of the viewport `y` stands on, counted from the top of it: -1 over
/// it and the number of rows under it.
int viewportRowAt(const AppState& state, int y) {
    const auto& rows = state.readSelect.textRows;
    if (rows.empty() || y < rows.front().box.y_min) return -1;
    for (size_t i = 0; i < rows.size(); ++i) {
        if (y >= rows[i].box.y_min && y <= rows[i].box.y_max) return static_cast<int>(i);
    }
    return static_cast<int>(rows.size());
}

/// The cursor under the pointer in the text, on row `on` of the viewport —
/// which may be a row past either end of it, the window then scrolling a row.
void pointTo(AppState& state, int on, int x) {
    auto& select = state.readSelect;
    if (select.textRows.empty()) return;
    TextBuffer& text = select.text;
    text.row = std::clamp(state.readScroll + on, 0, lastRow(state));
    // Where the row was drawn, where it was drawn at all: the cursor's own row
    // is the one drawn scrolled, and so the one whose origin is not its start.
    const int shown = text.row - state.readScroll;
    size_t origin = 0;
    int left = select.textRows.front().box.x_min;
    if (shown >= 0 && shown < static_cast<int>(select.textRows.size())) {
        origin = select.textRows[static_cast<size_t>(shown)].origin;
        left = select.textRows[static_cast<size_t>(shown)].box.x_min;
    }
    text.col = offsetAtColumn(text.line(), origin, std::max(0, x - left));
    scrollToCursor(state);
}

/// A field under the pointer, or -1.
int fieldAt(const AppState& state, int x, int y) {
    const auto& spots = state.readSelect.fieldSpots;
    for (size_t i = 0; i < spots.size(); ++i) {
        if (spots[i].box.Contain(x, y) && fieldShown(state, static_cast<int>(i))) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

/// Answers a press that is the second or the third of a run on one cell by
/// selecting the word under the pointer or the whole line, as the editor does —
/// in a field the line is the field, and in the text it is the line the
/// message wrote, every row the window broke it into. The characters and not
/// the break after them. Says whether it was.
bool multiClick(AppState& state, const MouseEvent& click) {
    auto& select = state.readSelect;
    const int count = countClick(select.clicks, click.x, click.y, state.monotonicMs());
    if (count < 2) return false;

    if (select.inHeader) {
        const std::string value = fieldValue(state, select.field);
        const auto [begin, end] = count == 2 ? wordAround(value, select.fieldCursor)
                                             : std::pair<size_t, size_t>{0, value.size()};
        select.fieldAnchor = begin;
        select.fieldCursor = end;
    } else {
        TextBuffer& text = select.text;
        if (count == 2) {
            const auto [begin, end] = wordAround(text.line(), text.col);
            text.anchor = TextPoint{text.row, begin};
            text.col = end;
        } else {
            const auto continued = [&state](int row) {
                return row >= 0 && static_cast<size_t>(row) < state.readLines.size() &&
                       state.readLines[static_cast<size_t>(row)].continues;
            };
            int first = text.row;
            while (continued(first - 1)) --first;
            int last = text.row;
            while (last < lastRow(state) && continued(last)) ++last;
            text.anchor = TextPoint{first, 0};
            text.row = last;
            text.col = text.line().size();
        }
        scrollToCursor(state);
    }
    select.drag = AppState::ReaderSelect::Drag::None;
    if (state.config.clipboardCopyOnSelect != config::CopyOnSelect::Off) {
        copy(state, /*quiet=*/true);
    }
    return true;
}

/// A press: the cursor to where it points, and a drag begun from there.
bool press(AppState& state, const MouseEvent& click) {
    auto& select = state.readSelect;
    // What was selected before the cursor moves, for a press inside it to hold.
    const bool wasInHeader = select.inHeader;
    const int wasField = select.field;
    const FieldSelection inField =
        wasInHeader ? fieldSelection(state, wasField) : FieldSelection{};
    const auto inText = wasInHeader ? std::nullopt : selectionOf(select.text);
    select.heldField.reset();
    select.heldText.reset();

    select.anchored = false;
    if (const int field = fieldAt(state, click.x, click.y); field >= 0) {
        select.text.anchor.reset();
        select.inHeader = true;
        select.field = field;
        const auto& spot = select.fieldSpots[static_cast<size_t>(field)];
        select.fieldCursor = offsetAtColumn(fieldValue(state, field), spot.origin,
                                            click.x - spot.box.x_min);
        if (!multiClick(state, click)) {
            select.fieldAnchor = select.fieldCursor;
            select.drag = AppState::ReaderSelect::Drag::Field;
            using Held = HeldSelection<size_t>;
            if (wasInHeader && wasField == field && !inField.empty() &&
                Held::covers(inField.begin, inField.end, select.fieldCursor)) {
                select.heldField = Held{inField.begin, inField.end, select.fieldCursor};
            }
        }
        return true;
    }
    const int on = viewportRowAt(state, click.y);
    if (on < 0 || on >= static_cast<int>(select.textRows.size()) ||
        click.x < select.textRows.front().box.x_min) {
        select.clicks = {};
        return false;
    }
    select.inHeader = false;
    select.fieldAnchor.reset();
    pointTo(state, on, click.x);
    if (!multiClick(state, click)) {
        select.text.anchor = select.text.cursor();
        select.drag = AppState::ReaderSelect::Drag::Text;
        using Held = HeldSelection<TextPoint>;
        if (inText && Held::covers(inText->first, inText->second, select.text.cursor())) {
            select.heldText = Held{inText->first, inText->second, select.text.cursor()};
        }
    }
    return true;
}

/// The mode as nothing has touched it. Where the fields stand is the last
/// frame's and not the mode's, so it stays.
void reset(AppState::ReaderSelect& select) {
    header_format::Stops stops = std::move(select.stops);
    select = AppState::ReaderSelect{};
    select.stops = std::move(stops);
}

}  // namespace

void start(AppState& state) {
    if (!state.readHeader) return;
    auto& select = state.readSelect;
    reset(select);
    select.on = true;
    sync(state);
    select.text.row = std::clamp(state.readScroll, 0, lastRow(state));
    select.text.col = 0;
}

void stop(AppState& state) { reset(state.readSelect); }

void finish(AppState& state) {
    if (hasSelection(state)) copy(state, /*quiet=*/false);
    stop(state);
}

void sync(AppState& state) {
    auto& select = state.readSelect;
    if (!select.on) return;
    TextBuffer& text = select.text;
    text.lines.clear();
    for (const auto& line : state.readLines) text.lines.push_back(line.text);
    // The buffer is written against one that always has a line, as the editor's
    // is: a message with no text in it still has a row the cursor stands on.
    if (text.lines.empty()) text.lines.emplace_back();
    text.row = std::clamp(text.row, 0, lastRow(state));
    text.col = std::min(text.col, text.line().size());
    // Held to a character boundary: the row under it may not be the row it was
    // counted against.
    while (text.col > 0 && text.col < text.line().size() &&
           (static_cast<unsigned char>(text.line()[text.col]) & 0xC0) == 0x80) {
        --text.col;
    }
    text.anchor.reset();
    select.anchored = false;
}

std::string fieldValue(const AppState& state, int field) {
    if (!state.readHeader) return {};
    const domain::MessageHeader& header = *state.readHeader;
    switch (field) {
        case kFromName: return header.from;
        case kFromAddr: return header.origAddr.isValid() ? header.origAddr.toString() : "";
        case kToName: return header.to;
        case kToAddr:
            // Outside netmail the destination addresses nobody, and the block
            // leaves it out.
            return state.currentArea.hasAddressedRecipient() && header.destAddr.isValid()
                       ? header.destAddr.toString()
                       : "";
        case kSubject: return header.subject;
        default: return {};
    }
}

FieldSelection fieldSelection(const AppState& state, int field) {
    const auto& select = state.readSelect;
    if (!select.on || !select.inHeader || field != select.field || !select.fieldAnchor) {
        return {};
    }
    const size_t size = fieldValue(state, field).size();
    const size_t anchor = std::min(*select.fieldAnchor, size);
    const size_t cursor = std::min(select.fieldCursor, size);
    return {std::min(anchor, cursor), std::max(anchor, cursor)};
}

FieldSelection rowSelection(const AppState& state, int row) {
    const auto& select = state.readSelect;
    if (!select.on || select.inHeader) return {};
    const auto range = selectionOf(select.text);
    if (!range || row < 0 || row > lastRow(state)) return {};
    const auto& [from, to] = *range;
    if (row < from.row || row > to.row) return {};
    const size_t size = select.text.lines[static_cast<size_t>(row)].size();
    const size_t begin = row == from.row ? from.col : 0;
    // A row the selection runs on past takes its break in, which is drawn as the
    // rest of the row selected.
    const size_t end = row == to.row ? to.col : size + 1;
    if (begin >= end) return {};
    return {begin, end};
}

bool hasSelection(const AppState& state) {
    const auto& select = state.readSelect;
    if (!select.on) return false;
    if (select.inHeader) return !fieldSelection(state, select.field).empty();
    return selectionOf(select.text).has_value();
}

std::string selectedText(const AppState& state) {
    const auto& select = state.readSelect;
    if (!hasSelection(state)) return {};
    if (select.inHeader) {
        const FieldSelection chosen = fieldSelection(state, select.field);
        return fieldValue(state, select.field).substr(chosen.begin, chosen.end - chosen.begin);
    }
    const auto [from, to] = *selectionOf(select.text);
    std::string out;
    for (int row = from.row; row <= to.row; ++row) {
        const std::string& line = select.text.lines[static_cast<size_t>(row)];
        const size_t begin = row == from.row ? std::min(from.col, line.size()) : 0;
        const size_t end = row == to.row ? std::min(to.col, line.size()) : line.size();
        if (end > begin) out += line.substr(begin, end - begin);
        if (row == to.row) break;
        // What stands between this row and the next: the blanks the window broke
        // the line at, or the line break the message wrote.
        const auto at = static_cast<size_t>(row);
        if (at < state.readLines.size() && state.readLines[at].continues) {
            out += state.readLines[at].joiner;
        } else {
            out += '\n';
        }
    }
    return out;
}

bool dragged(AppState& state, const Event& event) {
    auto& select = state.readSelect;
    const auto mouse = mouseOf(event);
    if (!select.on || !mouse || mouse->button != MouseEvent::Button::Left) return false;
    // The pointer back in the window with the button still down carries the drag
    // on; a button or the hint bar is still clicked.
    const auto overDragged = [&] {
        if (select.drag == AppState::ReaderSelect::Drag::Field) {
            return select.fieldSpots[static_cast<size_t>(select.field)].box.Contain(mouse->x,
                                                                                    mouse->y);
        }
        return std::any_of(select.textRows.begin(), select.textRows.end(),
                           [&](const auto& spot) { return spot.box.Contain(mouse->x, mouse->y); });
    };
    const bool pressed = mouse->motion == MouseEvent::Motion::Pressed;
    if (pressed && (!mouse->resumed || select.drag == AppState::ReaderSelect::Drag::None ||
                    !overDragged())) {
        // A press begins afresh, whatever the last drag left behind: a release
        // that went to a box standing over the screen never reached here.
        select.drag = AppState::ReaderSelect::Drag::None;
        return false;
    }
    if (select.drag == AppState::ReaderSelect::Drag::None) return false;

    if (select.drag == AppState::ReaderSelect::Drag::Field) {
        const auto& spot = select.fieldSpots[static_cast<size_t>(select.field)];
        if (select.inHeader && spot.box.x_max >= spot.box.x_min) {
            const int column =
                std::clamp(mouse->x, spot.box.x_min, spot.box.x_max + 1) - spot.box.x_min;
            select.fieldCursor =
                offsetAtColumn(fieldValue(state, select.field), spot.origin, column);
            if (select.heldField) {
                const auto [anchor, cursor] = select.heldField->endsFor(select.fieldCursor);
                select.fieldAnchor = anchor;
                select.fieldCursor = cursor;
            }
        }
    } else if (!select.inHeader) {
        pointTo(state, viewportRowAt(state, mouse->y), mouse->x);
        if (select.heldText) {
            const auto [anchor, cursor] = select.heldText->endsFor(select.text.cursor());
            select.text.anchor = anchor;
            select.text.row = cursor.row;
            select.text.col = cursor.col;
            scrollToCursor(state);
        }
    }

    if (mouse->motion == MouseEvent::Motion::Released) {
        select.drag = AppState::ReaderSelect::Drag::None;
        // Once, as the button is let go, where `clipboard_copy_on_select` asks
        // — and the mode stays on: nobody asked for the copy.
        if (state.config.clipboardCopyOnSelect != config::CopyOnSelect::Off) {
            copy(state, /*quiet=*/true);
        }
    }
    return true;
}

bool handleEvent(AppState& state, const Event& event) {
    auto& select = state.readSelect;

    // Copying is what the mode is for, and copying ends it. With nothing
    // selected there is nothing to copy, and the mode stays.
    if (event == Event::Return || state.keys.is(event, Command::ReaderCopy)) {
        if (hasSelection(state)) {
            copy(state, /*quiet=*/false);
            stop(state);
        }
        return true;
    }
    if (event == Event::Escape || state.keys.is(event, Command::ReaderSelect)) {
        stop(state);
        return true;
    }
    // Space puts the anchor down where the cursor stands, and lifts it again
    // with what it selected left standing: the next bare motion drops that.
    if (event == Event::Character(' ')) {
        select.anchored = !select.anchored;
        if (select.anchored) {
            if (select.inHeader) {
                if (!select.fieldAnchor) select.fieldAnchor = select.fieldCursor;
            } else if (!select.text.anchor) {
                select.text.anchor = select.text.cursor();
            }
        }
        return true;
    }

    if (const auto click = leftClick(event)) {
        static_cast<void>(press(state, *click));
        return true;
    }
    // The wheel scrolls the message and carries the cursor along where the
    // window would leave it behind, as the editor's does.
    if (const int wheel = wheelDelta(event); wheel != 0) {
        const int most =
            std::max(0, static_cast<int>(state.readLines.size()) - state.readRows());
        state.readScroll = std::clamp(state.readScroll + wheel, 0, most);
        if (!select.inHeader) {
            const int top = state.readScroll;
            const int bottom = top + state.readRows() - 1;
            const std::optional<TextPoint> anchor = select.text.anchor;
            if (select.text.row < top) toRow(state, top);
            if (select.text.row > bottom) toRow(state, bottom);
            select.text.anchor = anchor;
        }
        return true;
    }
    if (event.is_mouse()) return true;

    if (select.inHeader) {
        headerKey(state, event);
    } else {
        textKey(state, event);
    }
    return true;
}

}  // namespace amberedit::ui::reader_select
