#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "config/app_config.hpp"
#include "domain/message.hpp"
#include "ui/term/element.hpp"

namespace amberedit::ui::msg_format {

/// One field of `msglist_format` with the width this window gives it — the
/// message list's half of what `ui/area_list_format.*` is for the area list,
/// and laid out on the same terms: the format says how wide a field wants to
/// be, the window says how much there is, and `layout()` is where the two meet.
/// The heading and the rows are built from the same list, which is what keeps a
/// column's heading over its column.
struct Column {
    config::MsgFieldKind kind{config::MsgFieldKind::Subject};
    int width{0};
    /// What a Date column writes its stamp with, settled: the format the field
    /// named, or `reader_datetime_format` where it named none. `layoutLine()`
    /// is where the two meet, so everything downstream of it has one format to
    /// read and no default to remember. Empty on every other column.
    std::string format;
};

/// One line of a row, laid out: the columns on it, in the order the format
/// wrote them.
using Line = std::vector<Column>;

/// A whole row, laid out — a line per line of the format, and always at least
/// one, so `front()` is the row's top line whatever was written.
using Layout = std::vector<Line>;

/// One message as the table draws it: the header it is drawn from and its number
/// in the area, counted from one. The stamp is not carried here — a row has as
/// many stamps as the format has Date columns, and each of those writes its own
/// from the header's date.
///
/// Whether a name is the user's own is settled by the screen, which knows what
/// the area is signed with; nothing here compares names. Whether the message is
/// marked is settled there too — a mark is a UID in `AppState::marks`, and
/// nothing here knows what a UID is.
struct Row {
    const domain::MessageHeader* header{nullptr};
    int number{0};
    bool fromIsOwn{false};
    bool toIsOwn{false};
    /// Whether the user has marked this message, which the `m` column draws a
    /// star for.
    bool marked{false};
};

/// The stamp `column` writes for `row`, whole — before the column has had a
/// chance to cut it, and empty for a row whose header has not been read yet.
/// The zone is the message's own, as everywhere else a written stamp is shown.
[[nodiscard]] std::string stampOf(const Row& row, const Column& column);

/// The stamp for a Date column, cut to the room it has: a datetime too wide for
/// its column drops its trailing parts one at a time, at the spaces —
/// "15 Aug 26 20:28 +0200" to "15 Aug 26 20:28" to "15 Aug 26" to "15 Aug" to
/// "15" — rather than being cut mid-word into something that reads as a
/// different date. The format is the column's own and may put its parts in any
/// order, so what goes first is whatever the user chose to lead with; only when
/// even that first part will not fit is the stamp cut where the width falls.
std::string fitDate(const std::string& stamp, int width);

/// Shares `width` columns out among one line's fields, in three passes, which
/// is the one way this differs from the area list's simpler two:
///
///  1. The fields written with a width of their own keep it, and the number
///     column — written with none — takes as many columns as the highest number
///     in the area needs, and never fewer than three: a handful of messages
///     should still read as a column rather than as a stray digit against the
///     left edge.
///  2. Each Date column written with no width of its own takes what the stamps
///     in it come to and no more: a stamp is cut at the spaces, so a column of
///     fourteen would show nine of "30 Jul 26" and stand five empty. Two Date
///     columns are measured apart, each through its own format, and share the
///     room the pass has between them. The heading is a floor where there is
///     room for it — a column of "15" under a truncated "Date" says less than
///     the two spare columns are worth. What they leave goes on to the next pass
///     rather than standing blank.
///  3. What is left is split equally between the fields written `0`, the first
///     of them taking the odd column.
///
/// `shown` is the rows on the screen and no others — the stamps are measured as
/// they will be drawn, and cutting the column down to what they need cannot cut
/// any of them further, so the widths do not chase each other from one frame to
/// the next. A window too narrow for the first two passes leaves the flexible
/// fields nothing; a field of no width is simply not drawn, which is what keeps
/// a row inside its window however the format was written.
/// `defaultDateFormat` is `reader_datetime_format`: what a Date column written
/// without a format of its own is drawn with, and the one place the reader's
/// format and the list's meet.
Line layoutLine(const config::MsgListLine& fields, int width, uint32_t messageCount,
                const std::vector<Row>& shown, const std::string& defaultDateFormat);

/// The whole row laid out in `width` columns, a line at a time. Each line is
/// measured on its own: the fixed fields on one line are not what the flexible
/// fields on the next have to share, so a subject given `0` on a line of its own
/// takes the whole width whatever stands above it.
Layout layout(const config::MsgListFormat& format, int width, uint32_t messageCount,
              const std::vector<Row>& shown, const std::string& defaultDateFormat);

/// The heading row over `layout`'s top line. There is one heading row however
/// tall a row is: it stands over the line the row is read from first, and the
/// lines under it are read by what they hold.
std::string header(const Layout& layout);

/// What a run of a row is drawn in beyond whatever the row itself is painted.
///
/// The screen decides what those colors are and when a row's own outranks a
/// run's; this only says which part of the row is which.
enum class Ink {
    Plain,    ///< the row's own color, whatever the row has
    Dimmed,   ///< the subject: prose rather than a fact about the message
    OwnName,  ///< a From or To that names the user themselves
};

/// One run of a row that is drawn in one color: the text as it stands in the
/// row, padding and all, and what it is drawn in. A row is cut into runs only
/// where the color changes, so a line of nothing but plain columns is a single
/// run.
struct Run {
    std::string text;
    Ink ink{Ink::Plain};
};

/// The line `columns` lays out, for `row`, in the runs it is drawn in. `line()`
/// is these joined back together, which is what a caller drawing the whole of
/// it in one color wants.
std::vector<Run> runs(const Row& row, const Line& columns);

/// One line of the row for `row` — the line being the one `columns` lays out,
/// which for a format written on one line is the whole row.
std::string line(const Row& row, const Line& columns);

/// What a whole drawn line is painted in, over and above the `Ink`s its runs
/// carry — the four things a screen may have to say about the message the row
/// is for.
///
/// They are ranked rather than combined, and the ranking is the point: the
/// cases coincide constantly, and a row painted half one way and half another
/// would read as neither. A highlighted row outranks everything and takes its
/// cells with it — a name picked out in another color would fight the highlight
/// rather than add to it. A message that has not gone out outranks one nobody
/// has read, an unsent message being unread by definition and the one of the two
/// worth doing something about.
///
/// `Current` is `Selected` said quietly, and the two are never on one screen: a
/// list the keyboard is in draws the row Enter would act on, and a panel it is
/// not in draws the row that happens to be on the screen beside it. Two bars of
/// the same loudness would leave the eye to work out which of them was which.
///
/// None of them is about a message the user has *marked*: that is the `m`
/// column's star and no bar at all, a mark being something the reader chose and
/// a row's paint being where the message stands.
enum class Paint {
    None,      ///< whatever the runs say and nothing more
    Unread,    ///< nobody has read this message yet
    Unsent,    ///< written here and still waiting to go out
    Current,   ///< the row a panel shows the reader's message on, quietly
    Selected,  ///< the row the cursor stands on
};

/// One line of `row`, drawn as `columns` lays it out, in exactly `width`
/// columns: a blank column down the left, `prefix`, the runs, and blanks out to
/// the right. The margins belong to the line rather than to the screen around
/// it, so that a highlight covers them too rather than starting a column in.
///
/// The table and the reader's sidebar both draw their rows through this, so a
/// message reads the same in the panel as it does in the list — `columns` is
/// what differs between them, being what each window's format laid out.
///
/// `prefix` is the sidebar's thread, drawn in front of the message it belongs
/// to and empty everywhere else. It is part of the line rather than a column
/// beside it, so a highlighted row carries it like everything else on the row,
/// and it is drawn `Dimmed`: it says where the message stands and nothing about
/// the message. Whoever passes one leaves room for it in `columns`, this only
/// cutting the line at `width` as it does for every other piece.
[[nodiscard]] term::Element drawLine(const Row& row, const Line& columns, int width,
                                     Paint paint, const std::string& prefix = "");

}  // namespace amberedit::ui::msg_format
