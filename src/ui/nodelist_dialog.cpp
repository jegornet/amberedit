#include "ui/nodelist_dialog.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "i18n/i18n.hpp"
#include "nodelist/nodelist_format.hpp"
#include "ui/dialog_frame.hpp"
#include "ui/event_util.hpp"
#include "ui/input_field.hpp"
#include "ui/list_page.hpp"
#include "ui/scrollbar.hpp"
#include "ui/text_layout.hpp"
#include "ui/theme.hpp"

namespace amberedit::ui::nodelist_dialog {

using namespace term;

namespace {

/// How wide the box stands inside its frame, and the narrowest it is squeezed
/// to in a window with less than that.
///
/// A width, not a measurement of what is in it — the same rule the import and
/// export boxes are built on. A box measured against the longest name in the
/// nodelist would be a different width for every nodelist, and jumping about it
/// would have the columns moving under the cursor. Seventy-four columns is the
/// three columns of a row at their full widths, and leaves the whole box inside
/// the eighty a terminal has always had.
constexpr int kInnerWidth = 74;
constexpr int kMinInner = 40;
/// The frame itself, a column on each side.
constexpr int kFrame = 2;

/// Rows of the box that are not the list: the title, the two lines about the
/// node under the cursor, the rule under them, and the bottom of the frame —
/// which carries the nodelist's name rather than a row of its own.
constexpr int kChromeRows = 5;
/// And the rows it keeps clear of the top and bottom of the window.
constexpr int kWindowMargin = 2;

/// What the two columns that are always there take. The address is as wide as
/// `2:5020/9999.9999` and a little over; the sysop is as wide as a name that
/// does not have to be cut. What is left over is the station's, and there is a
/// station column only where something is — see `columnsFor()`.
constexpr int kAddressColumn = 22;
constexpr int kSysopColumn = 25;

/// Settles how big the box is, once — and again only where the window itself
/// has changed size, exactly as the import and export boxes do it.
void fitBox(const AppState& state, AppState::NodelistView& view) {
    if (view.layoutWidth == state.width && view.layoutHeight == state.height) return;
    view.layoutWidth = state.width;
    view.layoutHeight = state.height;
    view.inner = std::min(kInnerWidth, std::max(kMinInner, state.width - kFrame));
    view.rows = std::max(1, state.height - kChromeRows - kWindowMargin);
}

struct Columns {
    int address{0};
    int sysop{0};
    int system{0};
};

/// The columns for a box `inner` columns wide.
///
/// The address and the sysop are what the box is for and are given their full
/// widths; **the system name is what the room is left over from, and it goes
/// away entirely where there is none**. It is dropped rather than squeezed
/// because the two beside it are the answer to both of the questions a nodelist
/// is opened with — who is at this address, and where is this sysop — and three
/// columns of a station's name is not a name, it is an ellipsis in a column
/// that the sysop would rather have had.
Columns columnsFor(int inner) {
    // A column of margin on the left, which is where the rows start.
    const int room = std::max(1, inner - 1);
    Columns columns;
    columns.address = std::min(room, kAddressColumn);

    const int left = room - columns.address;
    columns.system = left - kSysopColumn;
    if (columns.system > 0) {
        columns.sysop = kSysopColumn;
    } else {
        // Not enough for the address and a whole sysop's name, so the station
        // goes and its room goes to the name.
        columns.sysop = std::max(0, left);
        columns.system = 0;
    }
    return columns;
}

/// Where a lookup lands, and whether it landed on anything.
struct Landing {
    int index{0};
    bool found{true};
};

/// The place `query` names, starting from the node at `from`.
///
/// `next` asks for the one after that rather than the first — what Enter does,
/// so that a surname several sysops share, or a net with forty nodes in it, is
/// walked through by pressing it again.
Landing lookupIn(const nodelist::NodelistDb& db, const std::string& query, int from,
                 bool next) {
    const auto total = static_cast<int>(db.size());
    if (query.empty() || total == 0) return {from, true};

    // An address and a name are told apart by the text itself: what reads as
    // the beginning of an address is one, and nothing else can be, no sysop
    // being called `2:240`.
    if (const auto prefix = nodelist::AddressPrefix::parse(query)) {
        const auto range = db.findRange(*prefix);
        if (range.first == range.second) {
            // Nothing there, so the cursor goes where it would have been: the
            // neighbours are the answer to "is this address in the nodelist".
            return {std::clamp(static_cast<int>(range.first), 0, total - 1), false};
        }
        const auto first = static_cast<int>(range.first);
        const auto last = static_cast<int>(range.second);
        if (!next) return {first, true};
        // The next node inside the run, and round to its start at the end of
        // it: `2:240` steps through the net a press at a time.
        const int after = from + 1;
        return {after > first && after < last ? after : first, true};
    }

    const std::vector<size_t> matches = db.findBySysop(query);
    if (matches.empty()) return {from, false};
    if (!next) return {static_cast<int>(matches.front()), true};
    for (size_t match : matches) {
        if (static_cast<int>(match) > from) return {static_cast<int>(match), true};
    }
    // Past the last one, so round to the first: a search that stopped dead at
    // the end would leave the user to work out why pressing Enter again does
    // nothing.
    return {static_cast<int>(matches.front()), true};
}

void clampCursor(AppState::NodelistView& view, int total, int rows) {
    if (total == 0) {
        view.cursor = 0;
        view.offset = 0;
        return;
    }
    view.cursor = std::clamp(view.cursor, 0, total - 1);
    const int shown = std::min(rows, total);
    view.offset = std::min(view.offset, view.cursor);
    if (view.cursor >= view.offset + shown) view.offset = view.cursor - shown + 1;
    view.offset = std::clamp(view.offset, 0, std::max(0, total - shown));
}

/// Puts the cursor on `index` with the run around it on the screen, rather than
/// scrolled hard to the top: a node is worth as much for its neighbours as for
/// itself, and a jump that put the answer on the first row would show only what
/// comes after it.
void centreOn(AppState::NodelistView& view, int index, int total, int rows) {
    view.cursor = index;
    view.offset = index - (rows / 3);
    clampCursor(view, total, rows);
}

/// One of the two lines at the head of the box: `left` from the first column,
/// `right` against the last, and never less than a space between them.
///
/// The left is what gives way when the two will not both fit. It is the longer
/// and the more forgiving of the pair — a name or a system cut short is still
/// read for what it is, where a location or a set of flags cut at the front
/// would be read as another one.
std::string headLine(const std::string& left, const std::string& right, int width) {
    const int rightRoom = std::max(1, width - 2);
    const std::string tail = truncateToWidth(right, rightRoom);
    const int room = std::max(1, width - 1 - displayWidth(tail) - 1);
    return " " + padRight(truncateToWidth(left, room), room) + " " + tail;
}

/// The two lines at the head of the box: everything the node under the cursor's
/// own line holds, laid out as the line itself is — who is there over where the
/// node is, and what to call it over how to reach it.
std::pair<std::string, std::string> detailOf(const nodelist::NodeEntry& entry,
                                             const domain::FtnAddress& address,
                                             int width) {
    std::string who = entry.sysop;
    if (!entry.system.empty()) {
        if (!who.empty()) who += ", ";
        who += entry.system;
    }

    // Everything the line holds past the sysop, joined back up with commas
    // exactly as the nodelist wrote it, so that what is on the screen can be
    // read against the file it came out of. A field the line left empty is
    // left out rather than written as a comma against nothing: the fields are
    // named by what they hold here, not by where they stand.
    std::string rest = entry.phone;
    const auto append = [&rest](const std::string& part) {
        if (part.empty()) return;
        if (!rest.empty()) rest += ",";
        rest += part;
    };
    if (entry.speed != 0) append(std::to_string(entry.speed));
    append(entry.flags);

    return {headLine(who, entry.location, width),
            headLine(address.toString(), rest, width)};
}

/// Whether the box shows what its lookup found rather than the whole nodelist.
///
/// Only the one that looks a name up does. Somebody who typed a name is asking
/// which node is theirs, and the nodes standing around the answer say nothing
/// about that; somebody who typed an address is asking who is there, and the
/// neighbours are half of the answer.
bool showsMatches(AppState::NodelistView::Purpose purpose) {
    return purpose == AppState::NodelistView::Purpose::PickAddress ||
           purpose == AppState::NodelistView::Purpose::PickCarbonCopy;
}

/// How many rows the list has.
int listSize(const AppState& state, const AppState::NodelistView& view) {
    if (view.listMatches) return static_cast<int>(view.matches.size());
    return state.nodelistDb ? static_cast<int>(state.nodelistDb->size()) : 0;
}

/// The node a row of the list names.
size_t nodeAt(const AppState::NodelistView& view, int row) {
    if (!view.listMatches) return static_cast<size_t>(row);
    return view.matches[static_cast<size_t>(row)];
}

/// The nodes a lookup finds, in the order the box that filters by it shows
/// them: an address has no order but its own, and a name is answered closest
/// first.
std::vector<size_t> matchesFor(const nodelist::NodelistDb& db, const std::string& query) {
    if (const auto prefix = nodelist::AddressPrefix::parse(query)) {
        const auto range = db.findRange(*prefix);
        std::vector<size_t> found;
        found.reserve(range.second - range.first);
        for (size_t i = range.first; i < range.second; ++i) found.push_back(i);
        return found;
    }
    return db.findBySysop(query, 0, nodelist::NodelistDb::SysopOrder::Relevance);
}

/// Runs the lookup again on a box that shows what it finds, and puts the cursor
/// at the top of the answer.
///
/// An empty line is the whole nodelist rather than nothing: in a box that is
/// there to pick from, "no lookup" can only mean "all of them" — which is not
/// the rule the browsing box goes by, where an empty line is a search field
/// nobody has typed into yet and moves no cursor at all.
void refilter(const AppState& state, AppState::NodelistView& view) {
    view.matches.clear();
    view.listMatches = !view.lookup.empty() && state.nodelistDb;
    if (view.listMatches) view.matches = matchesFor(*state.nodelistDb, view.lookup);
    view.found = !view.listMatches || !view.matches.empty();
    view.cursor = 0;
    view.offset = 0;
}

/// What the bottom of the frame says: which nodelist the node under the cursor
/// came from, by the name of the file rather than by the line the config wrote
/// — `Z2DAILY.225` is what the user has, `~/ftn/nodelist/Z2DAILY.999` is how
/// they asked for it.
/// Where the list is empty but the nodelist is not — a lookup that found
/// nothing — it says nothing at all: the frame names the file a node came from,
/// and there is no node. The red Lookup line above is what says so.
std::string sourceLabel(const AppState& state, const AppState::NodelistView& view,
                        int total) {
    if (!state.nodelistDb || state.nodelistDb->empty()) {
        return state.nodelistProblem.empty()
                   ? _("no nodelist — append nodelist and nodelist_db lines to "
                       "the config")
                   : state.nodelistProblem;
    }
    if (total == 0) return {};
    const auto& sources = state.nodelistDb->sources();
    const size_t at = state.nodelistDb->sourceAt(nodeAt(view, view.cursor));
    if (at >= sources.size()) return {};
    const nodelist::SourceState& source = sources[at];
    const std::string& name = source.path.empty() ? source.spec : source.path;
    const size_t cut = name.find_last_of('/');
    return cut == std::string::npos ? name : name.substr(cut + 1);
}

}  // namespace

namespace {

/// Opens the box on `lookup`, for whatever it was opened to do.
void openWith(AppState& state, AppState::NodelistView::Purpose purpose,
              const std::string& lookup) {
    // Read once and kept, here or wherever asked for it first: a compiled
    // nodelist is a few megabytes, and it is written at startup and does not
    // change while AmberEdit runs.
    static_cast<void>(state.nodelist());

    AppState::NodelistView view;
    view.purpose = purpose;
    view.lookup = lookup;
    // The first character typed replaces it: what is in the line was put there
    // by whatever opened the box, and is an answer already given rather than
    // the beginning of the next question.
    view.seeded = !lookup.empty();

    // The lookup is run here rather than left for the first frame: the box's
    // size is what says how many rows there are to centre the answer in, and it
    // is settled by the window, which is known now.
    fitBox(state, view);
    if (showsMatches(purpose)) {
        refilter(state, view);
    } else if (state.nodelistDb && !state.nodelistDb->empty()) {
        const auto total = static_cast<int>(state.nodelistDb->size());
        const Landing landing = lookupIn(*state.nodelistDb, view.lookup, 0, false);
        view.found = landing.found;
        centreOn(view, landing.index, total, view.rows);
    }
    state.nodelistView = std::move(view);
}

}  // namespace

void open(AppState& state) {
    // On whoever wrote the message being read. It is what a nodelist is opened
    // for nine times in ten, and a box that opened on the first node of zone 1
    // would be asking the user to type what the screen already knows.
    std::string lookup;
    if (state.readHeader && state.readHeader->origAddr.isValid()) {
        const domain::FtnAddress& sender = state.readHeader->origAddr;
        lookup = sender.toString();

        // A point no nodelist here lists opens on the node it hangs off, and
        // the line says so: what is on the screen is that node, and a line
        // still naming the point would be describing something the box is not
        // showing. Every other address is its own answer and the line is left
        // exactly as the message wrote it.
        if (const nodelist::NodelistDb* db = state.nodelist(); db != nullptr) {
            if (const auto at = db->findOrBoss(sender)) {
                lookup = db->addressAt(*at).toString();
            }
        }
    }
    openWith(state, AppState::NodelistView::Purpose::Browse, lookup);
}

void openFor(AppState& state, AppState::NodelistView::Purpose purpose,
             const std::string& lookup) {
    openWith(state, purpose, lookup);
}

std::optional<nodelist::NodeEntry> currentNode(const AppState& state) {
    if (!state.nodelistView || !state.nodelistDb) return std::nullopt;
    const AppState::NodelistView& view = *state.nodelistView;
    if (view.cursor < 0 || view.cursor >= listSize(state, view)) return std::nullopt;
    return state.nodelistDb->entry(nodeAt(view, view.cursor));
}

Element render(AppState& state, Element background) {
    AppState::NodelistView& view = *state.nodelistView;

    // Null where the config names no nodelist, or where the one it names would
    // not open. The box comes up either way — the frame is where it says so —
    // so every row below asks whether there is one.
    const nodelist::NodelistDb* db = state.nodelistDb ? &*state.nodelistDb : nullptr;
    fitBox(state, view);
    const int inner = view.inner;
    const int total = listSize(state, view);
    clampCursor(view, total, view.rows);

    // What the node under the cursor's own line holds, at the head of the box
    // and marked off from the list by a rule: the rows below are a table and
    // these two are not, and a detail line among them would be read as one of
    // them.
    std::string first;
    std::string second;
    if (total != 0) {
        const size_t node = nodeAt(view, view.cursor);
        std::tie(first, second) = detailOf(db->entry(node), db->addressAt(node), inner);
    }

    Elements lines{
        // "▌" stands in for the cursor: the terminal's own is hidden for the
        // whole application, and an input line without one reads as a label.
        dialog::titleBar(i18n::format(_(" Lookup: {0}▌ "), {view.lookup}), inner,
                         view.found ? theme::palette.dialogTitle : theme::palette.error),
        dialog::line(first, inner, theme::palette.dialogLabel),
        dialog::line(second, inner, theme::palette.dialogText),
        dialog::divider(inner),
    };

    // The bar the reader draws beside a message too long for the window, in the
    // rightmost column of the box and with the rows running up to it — and only
    // where the list is longer than the box, exactly as the reader shows it only
    // for a message that does not fit.
    const bool scrollbarShown = total > view.rows;
    const int listWidth = scrollbarShown ? std::max(1, inner - 1) : inner;
    const scrollbar::Thumb thumb = scrollbar::thumbOf(view.rows, total, view.offset);
    const auto framedRow = [&](Element content, int row) {
        if (!scrollbarShown) return dialog::framed(std::move(content));
        return dialog::framed(hbox({std::move(content), scrollbar::cell(row, thumb)}));
    };

    const Columns columns = columnsFor(listWidth);
    // The room is reserved first: the boxes are written into while the frame is
    // laid out, and a vector that grew under them would leave the earlier rows
    // pointing at freed memory.
    view.rowBoxes.clear();
    view.rowBoxes.reserve(static_cast<size_t>(view.rows));

    for (int i = 0; i < view.rows; ++i) {
        const int at = view.offset + i;
        if (at >= total) {
            lines.push_back(
                framedRow(text(std::string(static_cast<size_t>(listWidth), ' ')), i));
            continue;
        }
        const size_t index = nodeAt(view, at);
        const auto entry = db->entry(index);
        // Each column but the last is padded to its width, which is what puts
        // the gap between them; the last is only truncated, the row itself
        // being padded to the frame below.
        std::string row = " " + padRight(truncateToWidth(db->addressAt(index).toString(),
                                                         columns.address),
                                         columns.address);
        if (columns.sysop > 0) {
            row +=
                columns.system > 0
                    ? padRight(truncateToWidth(entry.sysop, columns.sysop), columns.sysop)
                    : truncateToWidth(entry.sysop, columns.sysop);
        }
        if (columns.system > 0) row += truncateToWidth(entry.system, columns.system);

        Element cell = text(padRight(row, listWidth));
        if (at == view.cursor) {
            cell = std::move(cell) | bold | color(theme::palette.selectionText) |
                   bgcolor(theme::palette.selection);
        } else {
            cell = std::move(cell) | color(theme::palette.dialogText);
        }
        view.rowBoxes.push_back({at, {}});
        lines.push_back(
            framedRow(std::move(cell) | reflect(view.rowBoxes.back().box), i));
    }

    lines.push_back(dialog::footerBar(sourceLabel(state, view, total), inner));
    return dbox(
        {std::move(background), dialog::surface(vbox(std::move(lines))) | center});
}

Outcome handleEvent(AppState& state, const Event& event) {
    AppState::NodelistView& view = *state.nodelistView;
    const nodelist::NodelistDb* db = state.nodelistDb ? &*state.nodelistDb : nullptr;
    const int total = listSize(state, view);

    // Answers the lookup as it now stands: the box that shows what it finds
    // gathers that again, and the one that browses moves its cursor to it.
    const auto seek = [&state, db, &view, total](bool next) -> void {
        if (showsMatches(view.purpose)) {
            refilter(state, view);
            return;
        }
        if (db == nullptr || total == 0) return;
        const Landing landing = lookupIn(*db, view.lookup, view.cursor, next);
        view.found = landing.found;
        if (landing.found || !next) centreOn(view, landing.index, total, view.rows);
    };
    const auto moveBy = [&view, total](int delta) {
        view.cursor += delta;
        clampCursor(view, total, view.rows);
    };

    if (const auto click = leftClick(event)) {
        for (const auto& row : view.rowBoxes) {
            if (!row.box.Contain(click->x, click->y)) continue;
            view.cursor = row.index;
            clampCursor(view, total, view.rows);
            return Outcome::Ignored;
        }
        return Outcome::Ignored;
    }
    if (const int wheel = wheelDelta(event); wheel != 0) {
        moveBy(wheel);
        return Outcome::Ignored;
    }

    // The keys that move about come before the ones that type, so that a list
    // can be walked while a lookup stands in the line — the query is where the
    // cursor last jumped from and not a filter, so leaving it there costs
    // nothing.
    if (event == Event::ArrowUp) {
        moveBy(-1);
        return Outcome::Ignored;
    }
    if (event == Event::ArrowDown) {
        moveBy(1);
        return Outcome::Ignored;
    }
    if (event == Event::PageUp) {
        view.cursor = pageUpTarget(view.cursor, view.offset, view.rows);
        clampCursor(view, total, view.rows);
        return Outcome::Ignored;
    }
    if (event == Event::PageDown) {
        view.cursor = pageDownTarget(view.cursor, view.offset, view.rows, total);
        clampCursor(view, total, view.rows);
        return Outcome::Ignored;
    }
    if (event == Event::Home) {
        view.cursor = 0;
        clampCursor(view, total, view.rows);
        return Outcome::Ignored;
    }
    if (event == Event::End) {
        view.cursor = std::max(0, total - 1);
        clampCursor(view, total, view.rows);
        return Outcome::Ignored;
    }
    // Enter picks the node under the cursor where the box was opened to pick
    // one, which is what the compose screen opens it for. Browsing, there is
    // nothing to pick and it means "the next one this finds" — which is what
    // makes a surname several sysops share, or a net with forty nodes in it,
    // worth typing.
    if (event == Event::Return) {
        if (view.purpose != AppState::NodelistView::Purpose::Browse) {
            // The box is left standing: what was picked is read off it by
            // whoever asked, and putting it away is theirs to do.
            return total == 0 ? Outcome::Ignored : Outcome::Picked;
        }
        seek(/*next=*/true);
        return Outcome::Ignored;
    }
    // Backspace edits the line and nothing else. On an empty one it does
    // nothing at all rather than closing the box: the two are a keystroke apart
    // while a lookup is being cleared to type another, and Esc is the way out.
    if (event == Event::Backspace) {
        if (view.lookup.empty()) return Outcome::Ignored;
        // Erasing is editing what is there, so what is there is the user's from
        // now on and the next character typed adds to it.
        view.seeded = false;
        view.lookup.erase(prevChar(view.lookup, view.lookup.size()));
        seek(/*next=*/false);
        return Outcome::Ignored;
    }
    // Esc, and whatever key opened it: whichever the hand reaches for puts the
    // box away. Ahead of the line being typed into — a binding on a bare letter
    // would otherwise be typed into the lookup instead of closing the box, which
    // is the same order every screen answers its commands in.
    if (event == Event::Escape || state.keys.is(event, Command::ReaderNodelist)) {
        state.nodelistView.reset();
        return Outcome::Ignored;
    }
    // Every printable character goes into the line, the space among them: a
    // sysop's name has one in it, and this is a field being typed into rather
    // than the letter-at-a-time quick search the lists have. A chord is not
    // typing: what Ctrl or Alt is held for is a binding, here or nowhere.
    if (event.is_character() && !event.input().empty() && !event.ctrl() && !event.alt() &&
        static_cast<unsigned char>(event.input().front()) >= ' ') {
        // The first one over the address the box opened on takes the whole line
        // with it, and only that first one: what stood there was an answer
        // already given, and adding a letter to the end of it would look up
        // nothing at all.
        if (view.seeded) {
            view.lookup.clear();
            view.seeded = false;
        }
        view.lookup += event.input();
        seek(/*next=*/false);
        return Outcome::Ignored;
    }
    return Outcome::Ignored;
}

}  // namespace amberedit::ui::nodelist_dialog
