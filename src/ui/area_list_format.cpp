#include "ui/area_list_format.hpp"

#include <algorithm>
#include <string>

#include "ui/text_layout.hpp"

namespace amberedit::ui::area_format {
namespace {

using config::AreaFieldKind;

/// Whether the field holds a number, and so stands against the right edge of
/// its column with the heading over it doing the same. Everything else is text
/// and reads from the left.
bool isNumeric(AreaFieldKind kind) {
    return kind == AreaFieldKind::Number || kind == AreaFieldKind::Total ||
           kind == AreaFieldKind::Unread;
}

/// What stands over a column. The star has none: a one-character label would
/// say less about it than the stars underneath already do.
std::string headingOf(AreaFieldKind kind) {
    switch (kind) {
        case AreaFieldKind::Number: return "#";
        case AreaFieldKind::Echoid: return "Area";
        case AreaFieldKind::Description: return "Description";
        case AreaFieldKind::Group: return "Grp";
        case AreaFieldKind::Total: return "Msgs";
        case AreaFieldKind::Unread: return "New";
        case AreaFieldKind::UnreadFlag: return "";
        case AreaFieldKind::Space: return " ";
    }
    return "";
}

/// A heading cut to its column rather than truncated to it: an ellipsis in
/// "Msgs" would spend a column of a narrow one saying nothing. The rows below
/// are truncated as ever — what a row holds is the user's text and worth the
/// mark, and a heading is furniture.
std::string fitHeading(const std::string& heading, int width) {
    if (displayWidth(heading) > width) return substrByWidth(heading, 0, width);
    return heading;
}

std::string cellText(const app::AreaEntry& entry, int ordinal, const Column& column,
                     const std::string& descriptionDefault) {
    switch (column.kind) {
        case AreaFieldKind::Number:
            return countText(static_cast<uint64_t>(std::max(0, ordinal)), column.width);
        case AreaFieldKind::Echoid:
            return truncateToWidth(entry.config.tag, column.width);
        case AreaFieldKind::Description:
            // An area nothing describes shows what `arealist_description_default`
            // stands in with — "no description" by default, and the empty string
            // where the config asks for the blank column instead.
            return truncateToWidth(entry.config.description.empty()
                                       ? descriptionDefault
                                       : entry.config.description,
                                   column.width);
        case AreaFieldKind::Group:
            // areas.bbs has no notion of groups, so there the column is simply
            // blank for every area.
            return truncateToWidth(entry.config.group, column.width);
        case AreaFieldKind::Total:
            return entry.isAvailable() ? countText(entry.total, column.width) : "—";
        case AreaFieldKind::Unread:
            return entry.isAvailable() ? countText(entry.unread, column.width) : "—";
        case AreaFieldKind::UnreadFlag:
            return entry.isAvailable() && entry.unread > 0 ? "*" : "";
        case AreaFieldKind::Space: return "";
    }
    return "";
}

}  // namespace

Line layoutLine(const config::AreaListLine& fields, int width) {
    int fixed = 0;
    int flexible = 0;
    for (const auto& field : fields) {
        if (field.width > 0) {
            fixed += field.width;
        } else {
            ++flexible;
        }
    }

    const int spare = std::max(0, width - fixed);
    const int each = flexible > 0 ? spare / flexible : 0;
    int odd = flexible > 0 ? spare % flexible : 0;

    Line columns;
    columns.reserve(fields.size());
    for (const auto& field : fields) {
        if (field.width > 0) {
            columns.push_back(Column{field.kind, field.width});
            continue;
        }
        // The columns that do not divide evenly go to the fields written
        // first: a row is read from the left, and that is where the width is
        // most use.
        columns.push_back(Column{field.kind, each + (odd > 0 ? 1 : 0)});
        if (odd > 0) --odd;
    }
    return columns;
}

Layout layout(const config::AreaListFormat& format, int width) {
    // A format always has a line, so a layout always has one to draw the row's
    // top from — an empty one would be a row of no lines at all, and there is
    // nothing the screen could do with that.
    if (format.empty()) return Layout{Line{}};

    Layout lines;
    lines.reserve(format.size());
    for (const auto& fields : format) lines.push_back(layoutLine(fields, width));
    return lines;
}

std::string header(const Layout& layout) {
    std::string line;
    if (layout.empty()) return line;
    for (const auto& column : layout.front()) {
        if (column.width <= 0) continue;
        const std::string heading = fitHeading(headingOf(column.kind), column.width);
        line += isNumeric(column.kind) ? padLeft(heading, column.width)
                                       : padRight(heading, column.width);
    }
    return line;
}

std::vector<Run> runs(const app::AreaEntry& entry, int ordinal, const Line& columns,
                      const std::string& descriptionDefault) {
    std::vector<Run> pieces;
    for (const auto& column : columns) {
        if (column.width <= 0) continue;
        const std::string cell = cellText(entry, ordinal, column, descriptionDefault);
        // The description column, whatever it holds: what the area says about
        // itself is drawn as quiet as what the config stands in with where it
        // says nothing. The column is prose either way, and the fields beside it
        // are what the list is read down.
        const bool dimmed = column.kind == AreaFieldKind::Description;
        std::string drawn = isNumeric(column.kind) ? padLeft(cell, column.width)
                                                   : padRight(cell, column.width);
        // A run is only ever cut where the color changes, so a row with no
        // description in it is the one run the whole row used to be.
        if (!pieces.empty() && pieces.back().dimmed == dimmed) {
            pieces.back().text += drawn;
        } else {
            pieces.push_back(Run{std::move(drawn), dimmed});
        }
    }
    return pieces;
}

std::string row(const app::AreaEntry& entry, int ordinal, const Line& columns,
                const std::string& descriptionDefault) {
    std::string line;
    for (const auto& run : runs(entry, ordinal, columns, descriptionDefault)) {
        line += run.text;
    }
    return line;
}

std::string countText(uint64_t value, int width) {
    if (width <= 0) return {};

    std::string plain = std::to_string(value);
    if (static_cast<int>(plain.size()) <= width) return plain;

    // Thousands, then millions, then milliards: each step drops the digits
    // below it rather than rounding them, so what is shown is never more than
    // what is there.
    static const char kSuffixes[] = {'k', 'M', 'G', 'T'};
    uint64_t scaled = value;
    for (const char suffix : kSuffixes) {
        scaled /= 1000;
        // "0M" for seventeen thousand messages would be a wrong answer rather
        // than a shorter one: past the last step that still has a digit, the
        // column has nothing true left to say.
        if (scaled == 0) break;
        const std::string scaledText = std::to_string(scaled) + suffix;
        if (static_cast<int>(scaledText.size()) <= width) return scaledText;
    }
    // Not even "9k" fits. The column still has to say that there is something
    // there rather than lie about how much.
    return "+";
}

}  // namespace amberedit::ui::area_format
