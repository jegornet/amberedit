#include "ui/msg_list_format.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include "i18n/i18n.hpp"
#include "ui/text_layout.hpp"
#include "ui/theme.hpp"

namespace amberedit::ui::msg_format {
namespace {

using config::MsgFieldKind;

/// Whether the field holds a number, and so stands against the right edge of
/// its column with the heading over it doing the same. Everything else is text
/// and reads from the left, the stamp included — a Date column cut down to the
/// day would otherwise drift away from its heading.
bool isNumeric(MsgFieldKind kind) {
    return kind == MsgFieldKind::Number;
}

/// What stands over a column.
std::string headingOf(MsgFieldKind kind) {
    switch (kind) {
        case MsgFieldKind::Number: return C_("message list column", "#");
        // Nothing stands over the mark column, as nothing stands over the area
        // list's unread marker: a star needs no word to say what it is, and a
        // heading over one column would only be a letter cut out of a word.
        case MsgFieldKind::Marked: return "";
        case MsgFieldKind::From: return C_("message list column", "From");
        case MsgFieldKind::To: return C_("message list column", "To");
        case MsgFieldKind::Subject: return C_("message list column", "Subject");
        case MsgFieldKind::Date: return C_("message list column", "Date");
        case MsgFieldKind::Space: return " ";
    }
    return "";
}

/// A heading cut to its column rather than truncated to it: an ellipsis in
/// "Date" would spend a column of a narrow one saying nothing. The rows below
/// are truncated as ever — what a row holds is the message's own text and worth
/// the mark, and a heading is furniture.
std::string fitHeading(const std::string& heading, int width) {
    if (displayWidth(heading) > width) return substrByWidth(heading, 0, width);
    return heading;
}

/// Whether the field takes what its line has left over rather than a width of
/// its own. `kAutoWidth` is not one of these: it is a width the field works out,
/// not a share of what is going.
bool isFlexible(const config::MsgListField& field) {
    return field.width == 0;
}

/// Whether the field is a Date column that works its own width out from the
/// stamps in it, which is the pass between the fixed widths and the shares.
bool isMeasuredDate(const config::MsgListField& field) {
    return field.kind == MsgFieldKind::Date && field.width == config::kAutoWidth;
}

/// The number column is never narrower than this, however few messages the area
/// holds: a handful of them should still read as a column rather than as a
/// stray digit against the left edge.
constexpr int kMinNumberWidth = 3;

/// What a field written with no width of its own stands at, where that is a
/// number this pass can answer. The Date column's is not — it is settled
/// against what the first pass leaves — so it comes back as nothing here.
int fixedWidthOf(const config::MsgListField& field, uint32_t messageCount) {
    if (field.width > 0) return field.width;
    if (field.width != config::kAutoWidth) return 0;
    // The number column is as wide as the highest number that can go in it, so
    // an area of a hundred thousand messages gets six columns and one of twelve
    // gets the floor above.
    if (field.kind == MsgFieldKind::Number) {
        return std::max(kMinNumberWidth, digitWidth(static_cast<int64_t>(messageCount)));
    }
    return 0;
}

std::string cellText(const Row& row, const Column& column) {
    if (row.header == nullptr) return {};
    switch (column.kind) {
        case MsgFieldKind::Number:
            return truncateToWidth(std::to_string(std::max(0, row.number)), column.width);
        case MsgFieldKind::Marked: return row.marked ? "*" : "";
        case MsgFieldKind::From: return truncateToWidth(row.header->from, column.width);
        case MsgFieldKind::To: return truncateToWidth(row.header->to, column.width);
        case MsgFieldKind::Subject:
            return truncateToWidth(row.header->subject, column.width);
        case MsgFieldKind::Date: return fitDate(stampOf(row, column), column.width);
        case MsgFieldKind::Space: return "";
    }
    return "";
}

Ink inkOf(const Row& row, MsgFieldKind kind) {
    switch (kind) {
        // The subject is the one column that is prose rather than a fact about
        // the message, exactly as the description is in the area list, and the
        // numbers, names and stamps beside it are what the eye goes down the
        // list for.
        case MsgFieldKind::Subject: return Ink::Dimmed;
        case MsgFieldKind::From: return row.fromIsOwn ? Ink::OwnName : Ink::Plain;
        case MsgFieldKind::To: return row.toIsOwn ? Ink::OwnName : Ink::Plain;
        default: return Ink::Plain;
    }
}

}  // namespace

std::string stampOf(const Row& row, const Column& column) {
    if (row.header == nullptr) return {};
    return row.header->date.format(column.format, row.header->utcOffset);
}

std::string fitDate(const std::string& stamp, int width) {
    if (width <= 0) return {};
    if (displayWidth(stamp) <= width) return stamp;

    std::string cut = stamp;
    while (true) {
        const size_t space = cut.find_last_of(' ');
        if (space == std::string::npos) break;
        cut.erase(space);
        if (displayWidth(cut) <= width) return cut;
    }
    return truncateToWidth(cut, width);
}

Line layoutLine(const config::MsgListLine& fields, int width, uint32_t messageCount,
                const std::vector<Row>& shown, const std::string& defaultDateFormat) {
    // What each field is written with, settled before anything is measured: the
    // format the field named, or the reader's where it named none. A Date column
    // is measured and drawn through the same string this way, and nothing below
    // has a default left to remember.
    std::vector<std::string> formats;
    formats.reserve(fields.size());
    for (const auto& field : fields) {
        if (field.kind != MsgFieldKind::Date) {
            formats.emplace_back();
        } else {
            formats.push_back(field.dateFormat.empty() ? defaultDateFormat
                                                       : field.dateFormat);
        }
    }

    int fixed = 0;
    int dates = 0;
    int flexible = 0;
    for (const auto& field : fields) {
        if (isFlexible(field)) {
            ++flexible;
        } else if (isMeasuredDate(field)) {
            ++dates;
        } else {
            fixed += fixedWidthOf(field, messageCount);
        }
    }

    int spare = std::max(0, width - fixed);

    // The Date columns' own pass. It stands between the fixed widths and the
    // shares because what they do not use is worth more to the fields written
    // `0` than an empty five columns of stamp would be. Each is measured through
    // its own format — two of them may show quite different stamps — but the
    // room is shared equally, so neither can crowd the other out.
    std::vector<int> dateWidths(fields.size(), 0);
    if (dates > 0) {
        const int cap = spare / dates;
        const int heading = std::min(cap, displayWidth(headingOf(MsgFieldKind::Date)));
        for (size_t at = 0; at < fields.size(); ++at) {
            if (!isMeasuredDate(fields[at])) continue;
            int needed = heading;
            for (const auto& row : shown) {
                const Column measured{MsgFieldKind::Date, cap, formats[at]};
                needed =
                    std::max(needed, displayWidth(fitDate(stampOf(row, measured), cap)));
            }
            dateWidths[at] = std::min(cap, needed);
            spare -= dateWidths[at];
        }
    }

    const int each = flexible > 0 ? spare / flexible : 0;
    int odd = flexible > 0 ? spare % flexible : 0;

    Line columns;
    columns.reserve(fields.size());
    for (size_t at = 0; at < fields.size(); ++at) {
        const auto& field = fields[at];
        if (isFlexible(field)) {
            // The columns that do not divide evenly go to the fields written
            // first: a row is read from the left, and that is where the width
            // is most use.
            columns.push_back(Column{field.kind, each + (odd > 0 ? 1 : 0), formats[at]});
            if (odd > 0) --odd;
        } else if (isMeasuredDate(field)) {
            columns.push_back(Column{field.kind, dateWidths[at], formats[at]});
        } else {
            columns.push_back(
                Column{field.kind, fixedWidthOf(field, messageCount), formats[at]});
        }
    }
    return columns;
}

Layout layout(const config::MsgListFormat& format, int width, uint32_t messageCount,
              const std::vector<Row>& shown, const std::string& defaultDateFormat) {
    // A format always has a line, so a layout always has one to draw the row's
    // top from — an empty one would be a row of no lines at all, and there is
    // nothing the screen could do with that.
    if (format.empty()) return Layout{Line{}};

    Layout lines;
    lines.reserve(format.size());
    for (const auto& fields : format) {
        lines.push_back(
            layoutLine(fields, width, messageCount, shown, defaultDateFormat));
    }
    return lines;
}

std::string header(const Layout& layout) {
    std::string text;
    if (layout.empty()) return text;
    for (const auto& column : layout.front()) {
        if (column.width <= 0) continue;
        const std::string heading = fitHeading(headingOf(column.kind), column.width);
        text += isNumeric(column.kind) ? padLeft(heading, column.width)
                                       : padRight(heading, column.width);
    }
    return text;
}

std::vector<Run> runs(const Row& row, const Line& columns) {
    std::vector<Run> pieces;
    for (const auto& column : columns) {
        if (column.width <= 0) continue;
        const std::string cell = cellText(row, column);
        const Ink ink = inkOf(row, column.kind);
        std::string drawn = isNumeric(column.kind) ? padLeft(cell, column.width)
                                                   : padRight(cell, column.width);
        // A run is only ever cut where the color changes, so a line of nothing
        // but plain columns is the one run the whole line used to be.
        if (!pieces.empty() && pieces.back().ink == ink) {
            pieces.back().text += drawn;
        } else {
            pieces.push_back(Run{std::move(drawn), ink});
        }
    }
    return pieces;
}

std::string line(const Row& row, const Line& columns) {
    std::string text;
    for (const auto& run : runs(row, columns)) text += run.text;
    return text;
}

term::Element drawLine(const Row& row, const Line& columns, int width, Paint paint,
                       const std::string& prefix) {
    using namespace term;

    // A highlighted row is painted whole, so its cells are left plain and the
    // decorator below covers them together. An unsent message is marked cell by
    // cell instead: the color has to reach the blanks the fields leave as well,
    // and those are pieces of the line like any other.
    const bool highlighted = paint == Paint::Selected || paint == Paint::Current;
    const auto styled = [&](std::string piece, Ink ink) {
        Element cell = text(std::move(piece));
        if (highlighted) return cell;
        if (paint == Paint::Unsent) return std::move(cell) | color(theme::palette.unsent);
        switch (ink) {
            // Elsewhere a cell is left in the row's own color rather than being
            // repainted with the default: the row then reads exactly as it did
            // before, the marked cells aside. That is what lets the unread color
            // be painted over the row below while a name of the user's own keeps
            // `own_name` — the two are about different things, one about the
            // message and one about that one name, and there is room on the row
            // to say both.
            case Ink::Dimmed: return std::move(cell) | color(theme::palette.dimmed);
            case Ink::OwnName: return std::move(cell) | color(theme::palette.ownName);
            case Ink::Plain: break;
        }
        return cell;
    };

    Elements cells;
    int drawn = 0;
    const auto push = [&](const std::string& piece, Ink ink) {
        const std::string fitted = substrByWidth(piece, 0, width - drawn);
        if (fitted.empty()) return;
        drawn += displayWidth(fitted);
        cells.push_back(styled(fitted, ink));
    };

    // The margin on the left is the line's own, so that a highlight covers it.
    // What the fields leave, and the margin on the right, are the one blank
    // piece closing the line.
    push(" ", Ink::Plain);
    push(prefix, Ink::Dimmed);
    for (const auto& run : runs(row, columns)) push(run.text, run.ink);
    if (drawn < width) {
        push(std::string(static_cast<size_t>(width - drawn), ' '), Ink::Plain);
    }

    Element drawing = hbox(std::move(cells));
    if (highlighted) {
        // The same bar either way, in a quieter fill where the keyboard is
        // elsewhere. `selection_text` on both: every theme picks something
        // near-white there, and both fills are dark enough to carry it.
        const theme::Color fill = paint == Paint::Selected
                                      ? theme::palette.selection
                                      : theme::palette.readerSidebarMsglistSelected;
        return std::move(drawing) | bold | color(theme::palette.selectionText) |
               bgcolor(fill);
    }
    // A message nobody has read yet takes the color across the whole row, the
    // number and the date included: a message is unread, not a column of it. A
    // cell with a color of its own keeps it, `Painted` drawing the child after
    // filling the box.
    if (paint == Paint::Unread)
        return std::move(drawing) | color(theme::palette.msglistUnread);
    return drawing;
}

}  // namespace amberedit::ui::msg_format
