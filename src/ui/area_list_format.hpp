#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "app/area_manager.hpp"
#include "config/app_config.hpp"

namespace amberedit::ui::area_format {

/// One field of `arealist_format` with the width this window gives it.
///
/// The format says how wide a field wants to be; the window says how much
/// there is. `layout()` is where the two meet, and everything drawn afterwards
/// works from these — the heading and the rows are laid out by the same list,
/// which is what keeps a column's heading over its column.
struct Column {
    config::AreaFieldKind kind{config::AreaFieldKind::Echoid};
    int width{0};
};

/// One line of a row, laid out: the columns on it, in the order the format
/// wrote them.
using Line = std::vector<Column>;

/// A whole row, laid out — a line per line of the format, and always at least
/// one, so `front()` is the row's top line whatever was written.
using Layout = std::vector<Line>;

/// Shares `width` columns out among one line's fields: those written with a
/// width of their own keep it, and what is left over is split equally between
/// the ones written `0`, the first of them taking the odd column. A window too
/// narrow for the fixed widths leaves the flexible fields nothing — a field of
/// no width is simply not drawn, which is what keeps a row inside its window
/// however the format was written.
Line layoutLine(const config::AreaListLine& fields, int width);

/// The whole row laid out in `width` columns, a line at a time. Each line is
/// measured on its own: the fixed fields on one line are not what the flexible
/// fields on the next have to share, so a name given `0` on a line of its own
/// takes the whole width whatever stands under it.
Layout layout(const config::AreaListFormat& format, int width);

/// The heading row over `layout`'s top line. There is one heading row however
/// tall a row is: it stands over the line the row is read from first, and the
/// lines under it are read by what they hold — a description needs no label,
/// and a second row of furniture would cost the list an area.
std::string header(const Layout& layout);

/// One run of a row that is drawn in one color: the text as it stands in the
/// row, padding and all, and whether it is drawn quiet.
///
/// The description column is what is quiet, and the whole of it — what the area
/// says about itself as much as what the config stands in with where it says
/// nothing. It is the one column that is prose rather than a fact about the
/// area, and the names and the counts are what the eye goes down the list for.
/// The runs are where the screen learns which part of the row that is: a row is
/// cut into them only where the color changes, so a row with no description in
/// it is a single run.
struct Run {
    std::string text;
    bool dimmed{false};
};

/// The line `columns` lays out, for `entry`, in the runs it is drawn in.
/// `row()` is these joined back together, which is what a screen drawing the
/// line in one color wants.
std::vector<Run> runs(const app::AreaEntry& entry, int ordinal, const Line& columns,
                      const std::string& descriptionDefault);

/// One line of the row for `entry`, standing `ordinal` in the list, counted
/// from one — the line being the one `columns` lays out, which for a format
/// written on one line is the whole row.
/// An area that would not open shows an em dash where its counts would be: it
/// stays in the list, and nothing about it has been counted.
///
/// `descriptionDefault` — `arealist_description_default` — is what the
/// description column shows for an area nothing describes. Empty leaves that
/// column blank, which is what the setting is written empty for.
std::string row(const app::AreaEntry& entry, int ordinal, const Line& columns,
                const std::string& descriptionDefault);

/// A count written to fit `width` columns: the number itself where it fits,
/// then thousands as `17k`, millions as `1M` and so on, and a bare `+` when
/// even that is too wide. What is dropped is precision rather than the column,
/// so the fields beside it stay where the heading says they are.
std::string countText(uint64_t value, int width);

}  // namespace amberedit::ui::area_format
