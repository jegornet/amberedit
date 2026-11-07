#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "config/app_config.hpp"

namespace amberedit::ui::header_format {

/// The word a label draws, in the interface's language. `{From}` in a format is
/// this and nothing the format wrote: a word written into a config would stay
/// the language it was written in.
[[nodiscard]] const char* labelText(config::HeaderLabel label);

/// The widest of the five labels, measured in the interface's language — what a
/// label written with no width stands at, so that the labels line up down the
/// block in a language whose words are a different length. Never under four:
/// the English labels are that wide, and a language with shorter words has no
/// business narrowing the block.
[[nodiscard]] int labelWidth();

/// One piece of a line, laid out: what it shows and the columns it stands in.
struct Cell {
    config::HeaderFieldKind kind{config::HeaderFieldKind::Literal};
    config::HeaderLabel label{config::HeaderLabel::From};
    /// A literal's text, or a stamp's strftime format — empty there for
    /// `reader_datetime_format`.
    std::string text;
    /// The column it begins in, counted from the left of the block.
    int x{0};
    /// How many columns it stands in. Zero is a piece the window had no room
    /// for, which is drawn as nothing at all.
    int width{0};
};

using Line = std::vector<Cell>;
using Layout = std::vector<Line>;

/// How wide a stamp written at `kAutoWidth` would like to be — the stamp it
/// shows, measured. Asked only of `Written` and `Arrived`.
using Measure = std::function<int(const config::HeaderField&)>;

/// The most a field written `0` may be given where nothing typed stands after it
/// on its line, or 0 for no limit. The editor's: a box wider than what it will
/// take offers room the base has none for, and with nothing after it on the line
/// nothing moves for its being narrower. The reader passes none.
using Cap = std::function<int(config::HeaderFieldKind)>;

/// Lays a header format out across `width` columns, a line at a time as the
/// lists are: the fixed widths first — literals as wide as they are written,
/// labels at their width or `labelWidth()`, fields at theirs — then the stamps
/// at what they come to, each held to an even share of what is left, and then
/// the fields written `0` share the rest, the odd column going to the first.
/// A window too narrow for the fixed widths gives the shares nothing, and what
/// runs past the right edge is cut there.
[[nodiscard]] Layout layout(const config::HeaderFormat& format, int width,
                            const Measure& measure, const Cap& cap = {});

/// Where the sender's location stands in the rule under the block: the column
/// the second field of the line holding the sender's name begins in — the
/// addresses, in every format worth writing — or nothing where that line holds
/// one field only, which the rule answers by putting the location at its right
/// end.
[[nodiscard]] std::optional<int> locationColumn(const Layout& layout);

/// Where a stop of the block stands: its line and the columns it spans. What the
/// cursor walks is the block as this window draws it, in the editor and in the
/// reader's selection mode alike.
struct Stop {
    int line{0};
    int left{0};
    int right{0};
};

/// Every stop by its number, or nothing for one the block does not draw.
using Stops = std::vector<std::optional<Stop>>;

/// The stops as the block is read: a line at a time from the top, along each
/// line from the left, whatever line the format puts each on.
[[nodiscard]] std::vector<int> stopsInOrder(const Stops& stops);

/// The stop nearest `from` on the nearest line in `step`'s direction that has
/// one, or -1 where no line that way does:
///
/// - the nearest line holding a stop at all, so a line of stamps alone is
///   stepped over;
/// - on it, the stop sharing the most columns with `from`;
/// - sharing none, the one standing nearest beside it;
/// - and between two equally near, the one further left.
[[nodiscard]] int stopToward(const Stops& stops, const Stop& from, int step);

/// The stop beside `current` on its own line — `step` +1 the next to the right,
/// -1 the next to the left — or -1 where none stands that way.
[[nodiscard]] int stopBeside(const Stops& stops, int current, int step);

}  // namespace amberedit::ui::header_format
