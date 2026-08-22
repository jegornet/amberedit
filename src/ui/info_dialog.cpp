#include "ui/info_dialog.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "ui/dialog_frame.hpp"
#include "ui/event_util.hpp"
#include "ui/text_layout.hpp"
#include "ui/theme.hpp"

namespace amberedit::ui::info_dialog {

using namespace term;

namespace {

/// How wide the box is at most. Eighty columns is what a hexdump of sixteen
/// bytes to the row takes with its offset and its text column, which is the
/// form every FTN tool has printed one in; wider would only stretch the fields
/// beside it.
constexpr int kMaxWidth = 80;

/// The label column of a field, as the reports are written: `Msgbase`, eight
/// spaces, and the colon. Every block lines up on it, so a value can be read
/// down the box rather than hunted for on each row.
///
/// A block whose labels do not fit — JAM's subfields, which carry the number
/// and the name both — widens its own column rather than pushing one row's
/// colon out of line with the rest of its block. The cap is what keeps a
/// label nobody expected from taking the width the values need.
constexpr int kLabelWidth = 15;
constexpr int kMaxLabelWidth = 26;

/// A row of a dump: the offset, three spaces, the bytes in groups of four with
/// two spaces between the groups, two spaces, and the text column.
constexpr int kOffsetWidth = 4;
constexpr int kGroup = 4;

int dumpRowWidth(int perRow) {
    const int groups = perRow / kGroup;
    const int bytes = (groups * ((kGroup * 3) - 1)) + ((groups - 1) * 2);
    return kOffsetWidth + 3 + bytes + 2 + perRow;
}

/// How many bytes a dump row carries in `columns` columns: sixteen where there
/// is room, and then eight and four. Below that there is nothing left to drop —
/// the row is truncated like any other, which is the honest answer for a window
/// that narrow.
int bytesPerRow(int columns) {
    for (const int perRow : {16, 8, 4}) {
        if (dumpRowWidth(perRow) <= columns) return perRow;
    }
    return 4;
}

constexpr char kHexDigits[] = "0123456789ABCDEF";

std::string hexByte(unsigned char value) {
    return std::string{kHexDigits[value >> 4], kHexDigits[value & 0xfu]};
}

/// The offset down the left of a dump. Four digits are enough for any block
/// that reaches here: what the drivers hand over is capped well under 64K.
std::string hexOffset(size_t at) {
    std::string out;
    for (int shift = (kOffsetWidth - 1) * 4; shift >= 0; shift -= 4) {
        out += kHexDigits[(at >> shift) & 0xfu];
    }
    return out;
}

/// The column beside the bytes: what is printable ASCII shown as itself and
/// everything else as a dot.
///
/// Deliberately not decoded through the message's charset. What is being looked
/// at here is the bytes, and a column that turned some of them into letters and
/// left the rest as dots would be neither the bytes nor the text — while a
/// message in one charset read on a terminal in another would put a third
/// reading on the screen.
char printable(unsigned char value) {
    return value >= 0x20 && value < 0x7f ? static_cast<char>(value) : '.';
}

std::string dumpRow(const std::string& bytes, size_t at, int perRow) {
    std::string row = hexOffset(at) + "   ";
    std::string shown;
    for (int i = 0; i < perRow; ++i) {
        if (i != 0) row += (i % kGroup == 0) ? "  " : " ";
        const size_t offset = at + static_cast<size_t>(i);
        if (offset >= bytes.size()) {
            // The last row of a dump, padded so that its text column stands
            // under the one above it.
            row += "  ";
            continue;
        }
        const auto value = static_cast<unsigned char>(bytes[offset]);
        row += hexByte(value);
        shown += printable(value);
    }
    return row + "  " + shown;
}

/// Lays the report out into rows for a box `columns` columns wide.
void layout(AppState::InfoView& view, int columns) {
    view.lines.clear();
    const auto row = [&view, columns](const std::string& text, bool heading) {
        view.lines.push_back({truncateToWidth(text, columns), heading});
    };

    if (!view.report.title.empty()) row(view.report.title, true);
    for (const auto& block : view.report.blocks) {
        // A blank line between the blocks, and none above the first: what
        // separates them is the only thing that makes them blocks.
        if (!view.lines.empty()) row({}, false);
        if (!block.title.empty()) row(block.title, true);

        int labels = kLabelWidth;
        for (const auto& field : block.fields) {
            labels =
                std::max(labels, std::min(kMaxLabelWidth, displayWidth(field.label)));
        }
        for (const auto& field : block.fields) {
            const std::string label = padRight(field.label, labels) + ":";
            row(field.value.empty() ? label : label + " " + field.value, false);
        }
        if (block.bytes.empty()) continue;

        const int perRow = bytesPerRow(columns);
        for (size_t at = 0; at < block.bytes.size(); at += static_cast<size_t>(perRow)) {
            row(dumpRow(block.bytes, at, perRow), false);
        }
    }
    // A message the base would not read at all. Saying so is the whole of what
    // there is to say: the box was asked for, so it opens either way.
    if (view.lines.empty()) row("this base says nothing about the message", false);
}

/// One side of the frame, and one of the two bars closing it — the same frame
/// the replies dialog draws, with a label in the middle of the top side.
Element side() {
    return text("│") | color(theme::palette.dialogBorder);
}

Element bar(int width, const std::string& left, const std::string& right,
            const std::string& label) {
    const int room = std::max(0, width - displayWidth(label));
    const int before = room / 2;
    return hbox(
        {text(left + horizontalRule(before)) | color(theme::palette.dialogBorder),
         text(label) | color(theme::palette.dialogTitle),
         text(horizontalRule(room - before) + right) | color(theme::palette.dialogBorder)});
}

}  // namespace

void open(AppState& state) {
    if (state.base == nullptr || !state.readHeader) return;

    AppState::InfoView view;
    view.report = state.base->info(state.readHeader->number);
    state.infoView = std::move(view);
}

Element render(AppState& state, Element background) {
    AppState::InfoView& view = *state.infoView;

    // The window where it is narrower than the box would be — a box wider than
    // the screen would lose the frame down its right-hand side. The two columns
    // of frame and the space inside each of them come off the top, and what is
    // left is what the report is laid out to.
    const int width = std::max(4, std::min(kMaxWidth, state.width));
    const int inner = width - 2;
    const int columns = std::max(1, inner - 2);
    if (view.layoutWidth != columns) {
        layout(view, columns);
        view.layoutWidth = columns;
    }

    const auto total = static_cast<int>(view.lines.size());
    view.rows = std::max(1, std::min(total, state.height - 2));
    view.scroll = std::clamp(view.scroll, 0, std::max(0, total - view.rows));

    Elements lines{bar(inner, "╭", "╮", " Message info ")};
    for (int i = 0; i < view.rows; ++i) {
        const int at = view.scroll + i;
        const bool heading = at < total && view.lines[static_cast<size_t>(at)].heading;
        const std::string content =
            at < total ? view.lines[static_cast<size_t>(at)].text : std::string{};

        Element row =
            text(" " + padRight(content, columns) + " ") |
            color(heading ? theme::palette.dialogLabel : theme::palette.dialogText);
        if (heading) row = std::move(row) | bold;
        lines.push_back(hbox({side(), std::move(row), side()}));
    }
    // Where in the report the box has got to, on the bottom side of the frame —
    // there being no room for a scrollbar beside a dump that is already as wide
    // as the box. Nothing at all where it all fits: then there is nowhere else
    // to be.
    std::string position;
    if (total > view.rows) {
        position = " " + std::to_string(view.scroll + 1) + "-" +
                   std::to_string(view.scroll + view.rows) + "/" + std::to_string(total) +
                   " ";
    }
    lines.push_back(bar(inner, "╰", "╯", position));

    return dbox(
        {std::move(background), dialog::surface(vbox(std::move(lines))) | center});
}

void handleEvent(AppState& state, const Event& event) {
    AppState::InfoView& view = *state.infoView;
    const int total = static_cast<int>(view.lines.size());
    const int last = std::max(0, total - view.rows);
    const auto scrollBy = [&view, last](int delta) {
        view.scroll = std::clamp(view.scroll + delta, 0, last);
    };

    // A click anywhere puts the box away. There is nothing in it to point at —
    // it shows and does not ask — so a click on it can only mean "done".
    if (leftClick(event)) {
        state.infoView.reset();
        return;
    }
    if (const int wheel = wheelDelta(event); wheel != 0) {
        scrollBy(wheel);
        return;
    }
    if (event == Event::ArrowDown) {
        scrollBy(1);
        return;
    }
    if (event == Event::ArrowUp) {
        scrollBy(-1);
        return;
    }
    if (event == Event::PageDown || event == Event::Character(' ')) {
        scrollBy(view.rows);
        return;
    }
    if (event == Event::PageUp) {
        scrollBy(-view.rows);
        return;
    }
    if (event == Event::Home) {
        view.scroll = 0;
        return;
    }
    if (event == Event::End) {
        view.scroll = last;
        return;
    }
    // The same keys the reader moves inside a message with, and whatever key
    // opened this to close it again — every one of them, so that whichever the
    // hand reaches for puts the box away.
    if (event == Event::Escape || event == Event::Backspace || event == Event::Return ||
        state.keys.is(event, KeyCommand::ReaderInfo)) {
        state.infoView.reset();
    }
}

}  // namespace amberedit::ui::info_dialog
