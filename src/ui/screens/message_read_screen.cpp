#include "ui/screens/message_read_screen.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "app/export_file.hpp"
#include "app/message_builder.hpp"
#include "app/message_search.hpp"
#include "config/text_util.hpp"
#include "encoding/text_search.hpp"
#include "ui/back_button.hpp"
#include "ui/event_util.hpp"
#include "ui/export_dialog.hpp"
#include "ui/export_mode_dialog.hpp"
#include "ui/find_dialog.hpp"
#include "ui/info_dialog.hpp"
#include "ui/menu_button.hpp"
#include "ui/menu_dialog.hpp"
#include "ui/nodelist_dialog.hpp"
#include "ui/screens/compose_screen.hpp"
#include "ui/screens/message_list_screen.hpp"
#include "ui/scrollbar.hpp"
#include "ui/text_layout.hpp"
#include "ui/theme.hpp"

namespace amberedit::ui::screens::message_read {

using namespace term;

namespace {

/// " From : ", " To   : ", " Subj : " — the label column, indent included,
/// since the screen carries no outer margin.
constexpr int kLabelWidth = 8;
constexpr int kBackWidth = back_button::kWidth;
constexpr int kMenuWidth = menu_button::kWidth;
/// A column of margin at the right end of the header block, so the addresses —
/// and the attributes that line up under them — stand off the edge of the window
/// rather than running into it. The rules and the body still span the full
/// width; this is the header table's own margin.
constexpr int kRightPad = 1;
constexpr int kMinNameWidth = 8;
/// XMSG stores 35 characters of a name at most, so a wider column could never
/// be filled.
constexpr int kMaxNameWidth = 36;

/// Column widths of the header table. The right-hand column lines up under
/// itself across the four rows, so it is measured once for the whole block
/// rather than per row.
struct HeaderLayout {
    /// The column down the left, which holds a name on the From and To rows,
    /// the stamps on the Date row, and the beginning of the subject.
    int name{0};
    /// The column beside it, as wide as the widest thing that goes in it — an
    /// address or the attributes.
    int address{0};
    /// What the Subj row has to itself: everything right of the label, which is
    /// wider than the two columns together whenever the names stop at
    /// kMaxNameWidth and the window leaves the rest over.
    int subject{0};
};

/// Works the columns out from the window and from how wide what stands beside
/// the names actually is — most addresses are around 13 characters, and
/// reserving room for the widest one FTN allows would take it from the names
/// for nothing.
///
/// `stampsWidth` is what the Date and Recd rows would like the left column to
/// be: a stamp stands there too, and it is not a name. The column stops at the
/// width of the name field FTS-0001 fixes, which no name can outgrow — but a
/// date format can, `reader_datetime_format` being free to ask for anything, and
/// capping a stamp at a name's width would cut it at every window size rather
/// than only in a narrow one. So a stamp may widen the column, and the window is
/// still what decides: what there is no room for is cut back here as it always
/// was.
HeaderLayout headerLayout(int width, int addressWidth, int stampsWidth) {
    HeaderLayout layout;
    layout.address = addressWidth;

    const auto nameRoom = [&] { return width - kLabelWidth - layout.address; };
    // A window too narrow for both drops the column at the right: who the
    // message is from and to is what the rows are for.
    if (nameRoom() < kMinNameWidth) layout.address = 0;

    layout.name = std::clamp(nameRoom(), 1, std::max(kMaxNameWidth, stampsWidth));
    layout.subject = std::max(1, width - kLabelWidth);
    return layout;
}

Element headerLabel(const std::string& label) {
    return text(" " + padRight(label, 4) + " : ") | bold | color(theme::palette.header);
}

/// A stretch of text with what a search found in it lit up: the theme's `found`
/// as a fill, with the screen's own background written on it, so that an
/// occurrence is seen at a glance whatever color the text around it was in — a
/// quote, a link, a header field or the message's own BBS codes.
///
/// `found` holds byte ranges of `shown` and nothing else is asked of it, which
/// is what lets one function paint a header cell and a body row alike.
Element painted(const std::string& shown, theme::Color tint,
                const std::vector<encoding::TextMatch>& found) {
    if (found.empty()) return text(shown) | color(tint);

    Elements runs;
    size_t at = 0;
    for (const auto& match : found) {
        if (match.begin > at) {
            runs.push_back(text(shown.substr(at, match.begin - at)) | color(tint));
        }
        runs.push_back(text(shown.substr(match.begin, match.end - match.begin)) | bold |
                       color(theme::palette.background) | bgcolor(theme::palette.found));
        at = match.end;
    }
    if (at < shown.size()) runs.push_back(text(shown.substr(at)) | color(tint));
    return hbox(std::move(runs));
}

/// A column of the header table, `width` wide whatever is in it, so that the
/// rows stay aligned.
///
/// `search`, where the message on screen is the one a search landed on, lights
/// what it found — the cell as it is *drawn*, so a name cut off at the column's
/// edge is lit as far as it is shown and no further.
Element headerCell(const std::string& value, int width, theme::Color tint,
                   const encoding::TextSearch* search) {
    const std::string shown = truncateToWidth(value, width);
    const int room = std::max(0, width - displayWidth(shown));
    const std::string pad(static_cast<size_t>(room), ' ');
    if (search == nullptr || search->empty()) return text(shown + pad) | color(tint);
    // The padding is drawn beside the cell rather than inside it: a query
    // ending in a space would otherwise be found in the blank a short name
    // leaves behind.
    return hbox({painted(shown, tint, search->findAll(shown)), text(pad)});
}

/// One row of the header table: a name down the left and, where the block has
/// a second column, an address hard against it. Nothing stands between the two:
/// the name column is padded to its width, so where it ends is where the
/// address begins, and a column of blank would only push the addresses in off
/// the margin they already stand on.
Element headerRow(const std::string& label, const std::string& name, bool ownName,
                  const std::string& address, const HeaderLayout& layout,
                  const encoding::TextSearch* search) {
    Elements cells{
        headerLabel(label),
        headerCell(name, layout.name,
                   ownName ? theme::palette.ownName : theme::palette.header, search)};
    if (layout.address > 0) {
        cells.push_back(
            headerCell(address, layout.address, theme::palette.header, search));
    }
    return hbox(std::move(cells));
}

/// The Subj row, which runs across the whole block: a subject is one long thing
/// rather than two, nothing lines up under it, and cutting it at the columns
/// beside it would drop the end of a subject a wide window has room for.
Element subjectRow(const std::string& subject, const HeaderLayout& layout,
                   const encoding::TextSearch* search) {
    return hbox({headerLabel("Subj"),
                 headerCell(subject, layout.subject, theme::palette.header, search)});
}

/// The Date row: when the message was written, down the left where the names
/// stand, and the attributes in the column the addresses stand in above.
Element dateRow(const std::string& written, const std::string& attributes,
                const HeaderLayout& layout) {
    Elements cells{headerLabel("Date"),
                   headerCell(written, layout.name, theme::palette.header, nullptr)};
    if (layout.address > 0) {
        cells.push_back(
            headerCell(attributes, layout.address, theme::palette.header, nullptr));
    }
    return hbox(std::move(cells));
}

/// The Recd row under it: when the message arrived here, which
/// `show_recd_date` decides whether the block carries at all.
///
/// A row of its own rather than a second stamp beside the written one: the two
/// are the same thing said of two different clocks, and the only way to tell
/// which was which was that one came second. The label is what says it now, so
/// the row is drawn like the Date row above it, in the block's own color: the
/// two are a pair of stamps and read as one.
///
/// Nothing goes in the column beside it: the attributes on the Date row above are
/// message's, said once.
Element recdRow(const std::string& arrived, const HeaderLayout& layout) {
    return hbox({headerLabel("Recd"),
                 headerCell(arrived, layout.name, theme::palette.header, nullptr)});
}

/// What the base says about the message, in brackets, or nothing at all where
/// it says nothing — not even an empty pair of them.
std::string attributesOf(const domain::MessageHeader& header) {
    const std::vector<std::string> names = domain::messageAttributes(header);
    if (names.empty()) return {};

    std::string trailing = "[";
    for (size_t i = 0; i < names.size(); ++i) {
        trailing += (i == 0 ? "" : " ") + names[i];
    }
    return trailing + "]";
}

/// The header table, and where its right-hand column starts — which is what the
/// location standing in the rule below it lines itself up with.
struct HeaderBlock {
    Elements rows;
    int column{0};
};

/// The header block over the message: who it is from and to, what it is about,
/// and when it was written and when it arrived.
///
/// Four rows whatever the window — the names down the left with the addresses
/// beside them, the subject across both columns, and the Date row carrying the
/// written stamp on the left and the attributes under the addresses — and a fifth,
/// the Recd row, where `show_recd_date` asks for one. The same block the editor
/// draws over a message being written, so that a message read and a message
/// written line up field for field.
HeaderBlock headerBlock(const AppState& state, const domain::MessageHeader& header,
                        const encoding::TextSearch* search) {
    const std::string fromAddr =
        header.origAddr.isValid() ? header.origAddr.toString() : "";
    // Outside netmail the destination address addresses nobody, so showing it
    // would only invite the reader to draw conclusions from noise.
    const std::string toAddr =
        state.currentArea.hasAddressedRecipient() && header.destAddr.isValid()
            ? header.destAddr.toString()
            : "";

    // The Date row carries when the message was written and the Recd row under
    // it when it arrived here. A message written locally never arrived, so that
    // stamp is simply absent rather than repeated from the other — the row is
    // still there, since `show_recd_date` is what says whether the block has
    // one and the message on the screen is not.
    //
    // Only the written stamp is given a zone: `%z` in the format writes the
    // offset the message's TZUTC states, and TZUTC is about the clock the
    // message was written by. When it arrived here was read off this system's
    // clock, which the message says nothing about, so `%z` writes nothing
    // there rather than borrowing an offset that is not its own.
    const std::string writtenStamp =
        header.date.format(state.config.readerDateTimeFormat, header.utcOffset);
    const std::string arrivedStamp =
        header.arrivalDate.format(state.config.readerDateTimeFormat);
    const std::string trailing = attributesOf(header);

    // The one column beside the names holds an address on the From and To rows
    // and the attributes on the Date row, so it is as wide as the widest of them.
    // Sizing it on the addresses alone would leave a message no room for its
    // attributes, the row would overflow the window and the columns would
    // silently stop lining up.
    const int column =
        std::max({displayWidth(fromAddr), displayWidth(toAddr), displayWidth(trailing)});
    // What the widest stamp in the left column takes, which the column may be
    // widened to where the window allows it — see headerLayout(). The two stand
    // on rows of their own, so it is the wider of them and not their sum.
    const bool recd = state.recdRowShown();
    const int stamps =
        std::max(displayWidth(writtenStamp), recd ? displayWidth(arrivedStamp) : 0);
    const HeaderLayout layout = headerLayout(state.width - kRightPad, column, stamps);
    Elements rows{
        headerRow("From", header.from, state.isOwnName(header.from), fromAddr, layout,
                  search),
        headerRow("To", header.to, state.isOwnName(header.to), toAddr, layout, search),
        subjectRow(header.subject, layout, search),
        dateRow(writtenStamp, trailing, layout),
    };
    if (recd) rows.push_back(recdRow(arrivedStamp, layout));
    return {std::move(rows), kLabelWidth + layout.name};
}

/// Where the message was written, as the nodelist gives it for the address it
/// was written from — or nothing, which is what a message from an address no
/// nodelist holds gets, and what every message gets where the config names no
/// nodelist or turns this off.
///
/// A point no nodelist here lists is answered for by the node it hangs off —
/// see `NodelistDb::findOrBoss`. A pointlist is a separate file that many
/// systems never compile, and where the point itself is not listed its boss's
/// town is the true thing about it: a point is that node's own client.
///
/// Looked up on every frame rather than kept beside the message: the compiled
/// nodelist is in memory, and the lookup is a binary search over it.
std::string senderLocation(AppState& state) {
    if (!state.config.showLocation || !state.readHeader) return {};
    if (!state.readHeader->origAddr.isValid()) return {};

    const nodelist::NodelistDb* db = state.nodelist();
    if (db == nullptr) return {};
    const auto at = db->findOrBoss(state.readHeader->origAddr);
    if (!at) return {};
    return db->entry(*at).location;
}

/// How much message there is, in the shortest way of saying it: bytes under a
/// kilobyte, kilobytes under a megabyte, megabytes above that. Rounded to the
/// unit rather than written out to the byte — it is there to be taken in at a
/// glance, and nobody reads a message by its exact length.
///
/// A count of bytes is written bare: it is the number itself, and the `k` and
/// the `M` above it are there to say that the number is no longer that.
std::string sizeText(size_t bytes) {
    constexpr size_t kKilo = 1024;
    constexpr size_t kMega = kKilo * kKilo;
    if (bytes < kKilo) return std::to_string(bytes);
    const size_t kilobytes = (bytes + kKilo / 2) / kKilo;
    if (kilobytes < kKilo) return std::to_string(kilobytes) + "k";
    return std::to_string((bytes + kMega / 2) / kMega) + "M";
}

/// The size of the message on screen, as the rule closing the header block says
/// it — or nothing, which is what the config turns it off gets and what a
/// screen with no message on it gets.
///
/// Every line the message holds is counted, service lines included, with the
/// newline ending it: what is measured is the message, not the part of it this
/// reader happens to be showing, so showing the kludges or hiding them leaves
/// the number where it was. It is the UTF-8 the base was decoded into and not
/// the bytes on disk — above `ports::IMsgBase` those do not exist, and the same
/// message stored in CP866 takes fewer of them.
std::string messageSize(const AppState& state) {
    if (!state.config.readerShowMessageSize || !state.readHeader || !state.readBody) {
        return {};
    }
    size_t bytes = 0;
    for (const auto& line : state.readBody->lines) bytes += line.text.size() + 1;
    return sizeText(bytes);
}

/// The rule closing the header block, with what stands in it: the size of the
/// message at the left end, where the header rows begin, and the sender's
/// location under the addresses above it — which is where the eye is already
/// looking for something about whoever wrote it.
///
/// Neither costs a row of its own: the rule is there either way, and each is a
/// word. Both are in the kludges' color for the reason the kludges are — they
/// are there to be glanced at, and they are not the message. A window with no
/// room left for one gets that much of the plain rule back.
Element closingRule(const std::string& size, const std::string& location, int column,
                    int width) {
    Elements cells;
    int left = width;

    // The blanks marking the size off are taken out of the rule rather than
    // added to the row, so the rule still spans the window exactly and the
    // location keeps the column it lines up in.
    const int sizeRoom = size.empty() ? 0 : displayWidth(size) + 2;
    if (sizeRoom > 0 && sizeRoom < width) {
        cells.push_back(text(" " + size + " ") | color(theme::palette.kludge));
        left -= sizeRoom;
    }

    // What is left of the rule is measured from where the size ended, since the
    // location stands in a column of the window and not in one of the rule.
    const int before = std::max(0, column - 1 - (width - left));
    const int room = std::max(0, left - before - 2);
    const std::string shown =
        location.empty() ? std::string{} : truncateToWidth(location, room);
    if (shown.empty()) {
        cells.push_back(text(horizontalRule(left)) | color(theme::palette.separator));
        return hbox(std::move(cells));
    }

    const int after = std::max(0, left - before - displayWidth(shown) - 2);
    cells.push_back(text(horizontalRule(before) + " ") | color(theme::palette.separator));
    cells.push_back(text(shown) | color(theme::palette.kludge));
    cells.push_back(text(" " + horizontalRule(after)) | color(theme::palette.separator));
    return hbox(std::move(cells));
}

/// How one stretch of a body line is drawn: its color and its emphasis.
struct RunStyle {
    bool link{false};
    /// The style code covering it, or '\0' for plain text.
    char marker{'\0'};
    /// The BBS color codes in force over it, or a plain one where the line
    /// carries none and the theme answers for the whole of it.
    bbs::Color coded;
    /// Whether a search landed on this message and found these very bytes.
    bool found{false};

    // Written out because a defaulted operator== is C++20 and this is C++17.
    bool operator==(const RunStyle& other) const {
        return link == other.link && marker == other.marker && coded == other.coded &&
               found == other.found;
    }
};

/// A body line drawn in `base`, with the links in it picked out, the colors its
/// BBS codes asked for laid down, and — where the config asks for style codes —
/// the emphasised phrases marked, each with the emphasis its marker names.
/// Lines with none of the three stay a single text element, which is nearly all
/// of them.
///
/// The three are worked out per byte and then gathered back into runs, rather
/// than each splitting the line in turn: a link, a phrase and a color can start
/// and end wherever they like, and only the bytes know which of them they are
/// under.
Element bodyLine(const AppState::DisplayLine& source, theme::Color base,
                 const AppState& state) {
    const std::string& line = source.text;
    const auto links = findLinks(line);
    std::vector<StyleSpan> spans;
    // The area's own answer: markup in one echo is punctuation in another.
    if (state.areaConfig.styleCodes) spans = findStyleSpans(line);
    if (links.empty() && spans.empty() && source.colorRuns.empty() &&
        source.found.empty()) {
        return text(line) | color(base);
    }

    std::vector<RunStyle> styles(line.size());
    for (const auto& [begin, end] : links) {
        for (size_t i = begin; i < end; ++i) styles[i].link = true;
    }
    for (const auto& span : spans) {
        // An address is written the way it is written: a marker inside one is
        // part of it, not markup around it.
        bool overLink = false;
        for (size_t i = span.begin; i < span.end; ++i) overLink |= styles[i].link;
        if (overLink) continue;
        for (size_t i = span.begin; i < span.end; ++i) styles[i].marker = span.marker;
    }
    // A code colors everything after it, so each run reaches to the next one or
    // to the end of the row.
    for (const auto& run : source.colorRuns) {
        for (size_t i = run.begin; i < line.size(); ++i) styles[i].coded = run.color;
    }
    for (const auto& match : source.found) {
        for (size_t i = match.begin; i < match.end && i < line.size(); ++i) {
            styles[i].found = true;
        }
    }

    Elements runs;
    for (size_t at = 0; at < line.size();) {
        size_t end = at + 1;
        while (end < line.size() && styles[end] == styles[at]) ++end;

        // A link keeps the link color wherever it stands, a message's own color
        // answers for the rest, and the theme answers where the message said
        // nothing — the same order the quote colors are already under.
        const bbs::Color coded = styles[at].coded;
        const theme::Color fg = styles[at].found  ? theme::palette.background
                                : styles[at].link ? theme::palette.link
                                : coded.fg >= 0   ? bbs::paletteColor(coded.fg)
                                                  : base;

        Element run = text(line.substr(at, end - at)) | color(fg);
        // What a search found wins over the message's own colors outright: the
        // point of it is to be seen from across a long message, and a fill the
        // message could paint over would not be.
        if (styles[at].found) {
            run = std::move(run) | bold | bgcolor(theme::palette.found);
        } else if (coded.bg >= 0) {
            run = std::move(run) | bgcolor(bbs::paletteColor(coded.bg));
        }
        if (styles[at].link && state.underlineLinks) run = std::move(run) | underlined;
        switch (styles[at].marker) {
            case '_': run = std::move(run) | underlined; break;
            case '*': run = std::move(run) | bold; break;
            case '/': run = std::move(run) | italic; break;
            case '#': run = std::move(run) | inverted; break;
            default: break;
        }
        runs.push_back(std::move(run));
        at = end;
    }
    return hbox(std::move(runs));
}

/// What stands in place of a twit's text until the reader is asked for it. One
/// line, and it names the key: a message hidden with no way back would be a
/// message the reader had decided about on the user's behalf.
constexpr char kTwitNotice[] = "This is a twit message. Press Space to view it";

/// The message the reader is to land on when it is sent to `number`: that one,
/// or — where `twit_mode` walks past it — the first one from there on in
/// `step`'s direction that it does not walk past. Zero when there is none,
/// which is the end of the area as far as reading is concerned.
uint32_t unskipped(AppState& state, uint32_t number, int step) {
    if (state.base == nullptr) return 0;
    while (number >= 1 && number <= state.messageCount) {
        if (!state.twitSkipped(state.base->header(number))) return number;
        number = static_cast<uint32_t>(static_cast<int64_t>(number) + step);
    }
    return 0;
}

int maxScroll(const AppState& state) {
    return std::max(0, static_cast<int>(state.readLines.size()) - state.readRows());
}

void scrollBy(AppState& state, int delta) {
    state.readScroll = std::clamp(state.readScroll + delta, 0, maxScroll(state));
}

/// Follows the thread up: to the message this one answers. A message that
/// answers none is where the marker beside its number says so already, and the
/// key does nothing.
void followUp(AppState& state) {
    if (state.readThread.replyTo == 0) return;
    goToMessage(state, state.readThread.replyTo);
}

/// Follows the thread down: to the message answering this one, or — where
/// several do — to the list of them, since only the user knows which.
void followDown(AppState& state) {
    const auto& replies = state.readThread.replies;
    if (replies.empty()) return;
    if (replies.size() == 1) {
        goToMessage(state, replies.front());
        return;
    }

    state.replyChoices.clear();
    state.replyChoice = 0;
    for (const uint32_t number : replies) {
        const domain::MessageHeader header = state.base->header(number);
        state.replyChoices.push_back(
            {number,
             header.from,
             header.origAddr.isValid() ? header.origAddr.toString() : "",
             header.date.format(state.config.readerDateTimeFormat, header.utcOffset),
             {}});
    }
}

/// Asks what is to become of the message on screen before asking where it is to
/// go — the first half of what `m` and the Forward button do here. The three
/// answers are a message of one's own carrying this one, this message moved into
/// another area, and this message copied there; the area dialog follows
/// whichever is given, and the shell is what puts it up.
///
/// It opens on Forward, which is the one that writes nothing by itself: the
/// other two act the moment an area is picked, and Move takes the message out of
/// the area being read.
void askForward(AppState& state) {
    if (!state.readHeader) return;
    if (state.manager.areas().empty()) return;

    state.forwardPicker = AppState::ForwardPicker{};
}

/// Exports the message on screen, asking first where there is anything to ask
/// about: a message carrying uuencoded files is two things at once, and which of
/// them is wanted is not something to guess at. Where it carries none there is
/// no question, and the export dialog opens as it always has.
///
/// The files are decoded here rather than in the box that asks about them —
/// whether there is a question at all is exactly what the decoding answers.
void askExport(AppState& state) {
    if (!state.readHeader || !state.readBody) return;

    std::vector<app::UueFile> files = app::uueFiles(*state.readBody);
    if (files.empty()) {
        export_dialog::open(state);
        return;
    }
    export_mode_dialog::open(state, std::move(files));
}

/// Where the message on screen was written from, as far as it says.
///
/// Not always the header's address: JAM keeps no address subfields in an echo
/// area — the format is explicit that echomail is broadcast and that they would
/// only repeat the AKA — so a message read there carries none, and its MSGID is
/// what names the system it came from. Asking the message rather than the base
/// is what keeps "is this yours?" from being answered "no" for every message in
/// such an area, one's own included.
domain::FtnAddress senderAddress(const AppState& state) {
    if (!state.readHeader) return {};
    if (state.readHeader->origAddr.isValid()) return state.readHeader->origAddr;
    if (!state.readBody) return {};

    // "2:5020/1042 5f3ac2e1": the address, a space, and the serial number.
    const std::string msgid = app::msgidOf(*state.readBody);
    const size_t space = msgid.find(' ');
    if (space == std::string::npos) return {};
    if (const auto parsed = domain::FtnAddress::parse(msgid.substr(0, space))) {
        return *parsed;
    }
    return {};
}

/// Opens the editor on the message on screen, over the message itself — what
/// `c`, F2 and the menu's Change all ask for.
///
/// Two of the three ways in are asked about first, and both questions are about
/// the same thing: what the change would be doing to somebody else's copy. A
/// message that is not ours is one we are writing in another person's name, and
/// the answer to yes carries the template's notice saying so; one of ours that
/// has already gone out is out of our hands, and changing it here changes
/// nothing anywhere else. Our own message that has not gone anywhere is nobody
/// else's business, and the editor opens on it at once.
void askToChange(AppState& state) {
    if (!state.readHeader || !state.readBody) return;

    if (!state.config.isOwnAddress(senderAddress(state))) {
        state.confirm = AppState::Confirm::ChangeForeignMessage;
        state.confirmChoice = AppState::ConfirmChoice::Yes;
        return;
    }
    if ((state.readHeader->attributes & domain::attr::kSent) != 0) {
        state.confirm = AppState::Confirm::ChangeSentMessage;
        state.confirmChoice = AppState::ConfirmChoice::Yes;
        return;
    }
    compose::startChange(state, /*notice=*/false);
}

/// Moves to a neighbouring message without going back to the list. Past the
/// last message and before the first there is none, and then the area itself is
/// left where `reader_edge_exit` asks for it: an area read to its end is where
/// the next area is wanted, and a key that does nothing says nothing about why.
/// An empty area has no message either way round, so both keys leave it — and
/// with no first message to stand before, there is no mark to take off it.
void switchMessage(AppState& state, int delta) {
    int target = state.messageCursor + delta;
    // The twits `twit_mode` walks past are walked past here, in the direction
    // the key is going: → over a run of them lands on the first message after
    // it, ← on the first before it. A run reaching the end of the area is the
    // end of the area — there is nothing further to read that way, which is
    // exactly what walking off it means, and `reader_edge_exit` answers for it
    // below as it does for the last message itself.
    if (target >= 0 && target < static_cast<int>(state.messageCount)) {
        const uint32_t landed =
            unskipped(state, static_cast<uint32_t>(target) + 1, delta > 0 ? 1 : -1);
        target = landed == 0 ? (delta > 0 ? static_cast<int>(state.messageCount) : -1)
                             : static_cast<int>(landed) - 1;
    }
    if (target < 0 || target >= static_cast<int>(state.messageCount)) {
        if (state.config.edgeExit) {
            // Which end was walked off says something about the reading, and
            // the two ends do not say the same thing. Off the front the reader
            // has asked for the message before the first one: it is standing
            // before the area rather than in it, so the mark comes off and the
            // area is unread whole again — the three messages just walked back
            // through are three unread messages in the area list. Off the back
            // there is nothing to say; the last message has been read and the
            // mark sits on it already. Esc leaves from either end without
            // moving anywhere, and leaves the mark on the message on screen.
            if (target < 0 && state.messageCount > 0) state.manager.markUnread();
            message_list::leaveArea(state);
            // Whatever else was typed by then goes with it. → is held down to
            // walk through an area, and on the area list underneath it opens
            // the area under the cursor — which reopens on the message just
            // left, at the end, ready to be walked off again. Without this the
            // repeats already in the terminal would bounce between the two
            // screens after the key was let go.
            state.discardTypeahead = true;
        }
        return;
    }

    state.messageCursor = target;
    // The list cursor has to stay consistent: going back should land the user
    // on the message they were reading. Where the list will be scrolled to is
    // not decided here — it is centred on this message when it is opened.
    message_list::ensureHeaders(state);

    loadMessage(state, static_cast<uint32_t>(target + 1));
}

/// Opens the list of messages on the one being read, so that it lands where the
/// reader is rather than wherever it was left last time. Centred on it, too:
/// reading walks the list downwards, so the message being read is at the bottom
/// edge of the window by the time the list is asked for, and what is worth
/// seeing beside it is what comes next.
void openList(AppState& state) {
    // An empty area has no list to open — and going there would leave the one
    // screen a first message can be written from.
    if (state.messageCount == 0) return;
    if (state.readHeader) {
        state.messageCursor = static_cast<int>(state.readHeader->number) - 1;
    }
    message_list::centerCursor(state);
    message_list::ensureHeaders(state);
    state.navigator.push(app::ScreenId::MessageList);
}

/// Whether a menu command can be run on what is in front of the user.
///
/// Everything that acts on the message needs a message to act on, and an empty
/// area has none — it opens the reader on blank rows, where writing the first
/// message is the whole of what is on offer. New is that message; the nodelist
/// is about somebody else's system and opens whatever is on the screen, saying
/// along its own bottom edge when there is no nodelist to show.
bool commandEnabled(const AppState& state, config::MenuCommand command) {
    switch (command) {
        case config::MenuCommand::Reply:
        case config::MenuCommand::ReplyTo:
        case config::MenuCommand::Forward:
        case config::MenuCommand::Change:
        case config::MenuCommand::Info:
        case config::MenuCommand::Export:
        case config::MenuCommand::Find:
        case config::MenuCommand::List: return state.messageCount > 0;
        default: return true;
    }
}

}  // namespace

void openMenu(AppState& state) {
    std::vector<AppState::MenuView::Item> items;
    items.reserve(state.config.readerMenu.size());
    for (const config::MenuCommand command : state.config.readerMenu) {
        items.push_back({command, commandEnabled(state, command), {}});
    }
    menu_dialog::open(state, std::move(items));
}

void runMenuCommand(AppState& state, config::MenuCommand command) {
    switch (command) {
        case config::MenuCommand::Reply: compose::startReply(state); break;
        case config::MenuCommand::ReplyTo:
            askArea(state, AppState::AreaPicker::For::Reply);
            break;
        case config::MenuCommand::Forward: askForward(state); break;
        case config::MenuCommand::New: compose::startNew(state); break;
        case config::MenuCommand::Change: askToChange(state); break;
        case config::MenuCommand::Info: info_dialog::open(state); break;
        case config::MenuCommand::Export: askExport(state); break;
        case config::MenuCommand::Find: find_dialog::open(state); break;
        case config::MenuCommand::Nodelist: nodelist_dialog::open(state); break;
        case config::MenuCommand::List: openList(state); break;
        // The editor's own two, which `reader_menu` cannot name.
        case config::MenuCommand::Save:
        case config::MenuCommand::Import: break;
    }
}

bool loadMessage(AppState& state, uint32_t msgNumber) {
    state.readHeader.reset();
    state.readBody.reset();
    state.readThread = {};
    state.readLines.clear();
    state.readScroll = 0;
    state.readLayoutWidth = 0;
    // Whatever was asked to be shown after all was asked for that message. The
    // next one is somebody else's, and it is hidden again until it is asked for
    // in its turn.
    state.twitRevealed = false;
    // And what a search lit belongs to the message it found. Every way to
    // another message comes through here, so this is the one place the
    // highlight has to be taken off — findMessage() puts it back on after the
    // message it landed on has been loaded.
    state.findHighlight.clear();

    if (state.base == nullptr || msgNumber == 0 || msgNumber > state.messageCount) {
        return false;
    }

    state.readHeader = state.base->header(msgNumber);
    state.readBody = state.base->body(msgNumber);
    state.readThread = state.base->thread(msgNumber);

    const std::string error = state.base->lastError();

    // Opening a message is what "read" means here, so the mark moves with it
    // rather than waiting for the area to be left: a reader killed mid-area
    // should still come back where it was.
    state.manager.markRead(msgNumber);

    // And the mark on the message itself, which is a different thing entirely:
    // the one above is a position in the area, this is JAM's TimesRead or
    // Squish's MSGSEEN on this one message, and every FTN reader keeps it.
    //
    // Made whatever `highlight_unread` says, that being about what this screen
    // paints and not about what the base holds: the mark is the message's, and
    // GoldED reading the same area afterwards goes by it. Asked for only where
    // the message was not marked already, so reading back over an area takes no
    // lock and writes nothing; a base that cannot be written says so in
    // lastError() and nothing is made of it — the message is on the screen
    // either way, and there is nothing the user could do about it.
    if (state.readHeader && !state.readHeader->seen && state.base->markSeen(msgNumber)) {
        state.readHeader->seen = true;
        // The list's window is a copy, so the row behind this screen would go on
        // showing the message unread until the window happened to be re-read.
        const int cached = static_cast<int>(msgNumber) - 1 - state.headersStart;
        if (cached >= 0 && cached < static_cast<int>(state.headers.size())) {
            state.headers[static_cast<size_t>(cached)].seen = true;
        }
    }

    relayout(state);
    return error.empty();
}

void showEmptyArea(AppState& state) {
    state.readHeader.reset();
    state.readBody.reset();
    // The thread the last message read was part of names messages of the area
    // it was in. Left standing, its markers would be drawn beside an empty
    // area's title, pointing at numbers that are not there.
    state.readThread = {};
    state.readThreadLinks.clear();
    state.readLines.clear();
    state.readScroll = 0;
    // relayout() has no body to work from and leaves this alone, so it is
    // cleared here rather than left over from the area before.
    state.scrollbarShown = false;
    state.messageCursor = 0;
}

void openMessage(AppState& state, uint32_t number) {
    if (number == 0 || number > state.messageCount) {
        // Nothing to skip past and nothing to open — loadMessage() clears the
        // reader and says so, which is what a number naming no message means
        // wherever it comes from.
        loadMessage(state, number);
        return;
    }

    // Forward first, which is the direction reading runs in, and back from
    // where it was asked for when everything after it is a twit: the message
    // picked out of a list is a place in the area, and the reader has to land
    // somewhere in it.
    uint32_t target = unskipped(state, number, 1);
    if (target == 0) target = unskipped(state, number, -1);
    // Every message here is one to walk past, and there is nowhere to walk to.
    // The one asked for is shown, its text behind the notice — which is what
    // `blank` would have done with it, and what the setting says to fall back
    // to.
    if (target == 0) target = number;

    state.messageCursor = static_cast<int>(target) - 1;
    message_list::ensureHeaders(state);
    loadMessage(state, target);
}

void goToMessage(AppState& state, uint32_t number) {
    if (state.base == nullptr || number == 0 || number > state.messageCount) return;

    // The list's cursor follows, so that going back to it lands on the message
    // the reader is showing rather than on wherever it was left. Where the list
    // is scrolled to follows from that when it opens, not from here.
    state.messageCursor = static_cast<int>(number) - 1;
    message_list::ensureHeaders(state);
    loadMessage(state, number);
}

namespace {

/// Puts the reader where the occurrence is: the row the first of them stands
/// on, a third of the way down the viewport so there is something above it to
/// read it against. A match that is already on the first page moves nothing, and
/// a message found by its header alone has no row to go to and opens at the top.
void scrollToMatch(AppState& state) {
    const int rows = std::max(1, state.readRows());
    for (size_t i = 0; i < state.readLines.size(); ++i) {
        if (state.readLines[i].found.empty()) continue;
        state.readScroll =
            std::clamp(static_cast<int>(i) - (rows / 3), 0, maxScroll(state));
        return;
    }
    state.readScroll = 0;
}

}  // namespace

bool findMessage(AppState& state, const std::string& query, app::SearchScope scope) {
    if (state.base == nullptr || state.messageCount == 0) return false;

    const std::string wanted(config::text::trim(query));
    if (wanted.empty()) return false;

    // A search starts on the message in front of the user. The same one made
    // again starts on the one after it, which is what makes the dialog answered
    // twice walk from occurrence to occurrence — and only where the reader is
    // still standing on what that search found, since a query typed again after
    // walking somewhere else is a fresh search from wherever the user now is.
    uint32_t from = state.readHeader ? state.readHeader->number
                                     : static_cast<uint32_t>(state.messageCursor) + 1;
    const AppState::LastFind& last = state.lastFind;
    const bool again = last.message != 0 && last.message == from &&
                       last.query == wanted && last.scope == scope &&
                       last.areaTag == state.currentArea.tag &&
                       last.areaPath == state.currentArea.path;
    if (again) ++from;
    if (from == 0) from = 1;

    // The charset is the message's own and is set again for each of them; most
    // areas never change it, and setting the one it already holds costs nothing.
    encoding::TextSearch search(wanted, state.areaConfig.defaultCharset);

    for (uint32_t number = from; number <= state.messageCount; ++number) {
        const domain::MessageHeader header = state.base->header(number);
        // What `twit_mode` walks past is walked past here as well: `ignore`
        // passes over every twit, `skip` over the ones not addressed to the
        // user. `blank` and `kill` are not navigation — a twit they hide is
        // found like any other message and opened behind the notice, which is
        // exactly what the reader shows of it.
        if (state.twitSkipped(header)) continue;

        search.setCharset(header.charset.empty() ? state.areaConfig.defaultCharset
                                                 : header.charset);
        bool hit = app::matchesHeader(search, header);
        if (!hit && scope == app::SearchScope::HeaderAndText) {
            hit = app::matchesBody(search, state.base->body(number));
        }
        if (!hit) continue;

        state.lastFind = {wanted, scope, state.currentArea.tag, state.currentArea.path,
                          number};
        goToMessage(state, number);
        // After the message is on the screen: loadMessage() takes the highlight
        // off, this message being the one it belongs to now.
        state.findHighlight = wanted;
        state.readLayoutWidth = 0;  // the window is the same size; the layout is not
        relayout(state);
        scrollToMatch(state);
        return true;
    }
    return false;
}

void deleteMessage(AppState& state) {
    if (state.base == nullptr || !state.readHeader) return;

    const uint32_t number = state.readHeader->number;
    if (!state.base->remove(number)) return;

    // The area is one message shorter, and the area list is counting the old
    // number until it is told to look again.
    state.manager.refreshArea(state.currentArea);
    state.messageCount = state.base->count();
    state.headers.clear();
    state.headersStart = 0;

    if (state.messageCount == 0) {
        // That was the last of them. The reader stays where it is, on blank
        // rows — the screen a first message is written from.
        showEmptyArea(state);
        return;
    }

    // Everything after it moved up one, so the number that named it now names
    // what followed it — except at the end of the area, where nothing did.
    const uint32_t next = std::min(number, state.messageCount);
    state.messageCursor = static_cast<int>(next) - 1;
    message_list::ensureHeaders(state);
    loadMessage(state, next);
}

void askArea(AppState& state, AppState::AreaPicker::For purpose) {
    if (!state.readHeader) return;
    if (state.manager.areas().empty()) return;

    AppState::AreaPicker picker;
    picker.purpose = purpose;
    // Where a reply is usually written, from `reply_to_area`. Only the reply:
    // the other three carry the message itself somewhere, and there is no one
    // area a config could name for that. A tag naming no area in the list
    // leaves the cursor at the top, which is where it has always started.
    // Off the settings of the area being read rather than off the file's own:
    // an area group may name where that echo's answers belong, which is the one
    // place the setting is worth stating per echo.
    if (purpose == AppState::AreaPicker::For::Reply &&
        !state.areaConfig.replyToArea.empty()) {
        const auto& areas = state.manager.areas();
        for (size_t i = 0; i < areas.size(); ++i) {
            if (!config::text::iequals(areas[i].config.tag, state.areaConfig.replyToArea))
                continue;
            picker.cursor = static_cast<int>(i);
            break;
        }
    }
    state.areaPicker = picker;
}

namespace {

/// Writes a message into another area and comes back to the one being read.
///
/// One base is open at a time, so the target's takes the place of it and the
/// reader's own is opened again straight after — the same swap a reply moved
/// into another area makes, and for the same reason. Nothing on the screen comes
/// off the base while it is away: the header and the body being read are copies,
/// as are the list's headers, so the reader is left exactly as it was.
///
/// false means there is nothing further to do with the message here: either it
/// did not go in — the area would not open, or its base refused it — or the area
/// being read will not open again, which ends on the area list. Both answers are
/// the same to a move: a message that is not somewhere else is not one to take
/// out of here.
bool storeInto(AppState& state, const domain::AreaConfig& target,
               const domain::MessageDraft& draft) {
    const domain::AreaConfig source = state.currentArea;

    // The source's base is closed by opening another, so nothing may be left
    // pointing at it in between.
    state.base = nullptr;
    uint32_t written = 0;
    if (ports::IMsgBase* into = state.manager.openArea(target)) {
        written = into->write(draft);
        // The area list counts it while the base is still open — one message
        // more in an area nobody has read, so one unread more as well.
        if (written != 0) state.manager.refreshArea(target);
    }

    // Back where the user was, whether or not the message went in.
    state.base = state.manager.openArea(source);
    if (state.base == nullptr) {
        // The area that was open a moment ago will not open again: there is
        // nothing left underneath to come back to, and nothing to delete from.
        message_list::leaveArea(state);
        return false;
    }
    return written != 0;
}

/// The message itself into another area — `takeOut` saying whether it stays in
/// this one as well, which is the whole difference between Copy and Move.
void passOn(AppState& state, const domain::AreaConfig& target, bool takeOut) {
    if (state.base == nullptr || !state.readHeader || !state.readBody) return;

    // Tag and path together are what name an area here, as they do everywhere
    // else. Moving a message into the area it is already in has nothing to do —
    // it would be written again and the original deleted, which is a message
    // renumbered and nothing else; copying it there is a second copy, which is
    // what was asked for, and needs no swap since that base is the open one.
    const bool here =
        target.tag == state.currentArea.tag && target.path == state.currentArea.path;
    if (here && takeOut) return;

    // The message exactly as the base holds it. Whether it is addressed to
    // anybody is the one thing it cannot answer for itself, and that is decided
    // by the area it is going into rather than by the one it came from.
    const domain::MessageDraft draft =
        app::copyOf(*state.readHeader, *state.readBody, target.hasAddressedRecipient());
    if (here) {
        if (state.base->write(draft) == 0) return;
        state.manager.refreshArea(state.currentArea);
        state.messageCount = state.base->count();
        state.headers.clear();
        state.headersStart = 0;
        return;
    }

    if (!storeInto(state, target, draft)) return;
    // Only once it is somewhere else. A message taken out of here on the
    // strength of a write that failed would be a message gone from both areas.
    if (takeOut) deleteMessage(state);
}

}  // namespace

void copyMessage(AppState& state, const domain::AreaConfig& target) {
    passOn(state, target, /*takeOut=*/false);
}

void moveMessage(AppState& state, const domain::AreaConfig& target) {
    passOn(state, target, /*takeOut=*/true);
}

namespace {

/// What is to be lit in the message on screen, or nothing where nothing is.
///
/// Folded by the charset the *message* declares — its CHRS kludge, or the area's
/// `default_charset` where it carries none — which is what the search that
/// landed here was folded by, so the two agree on what an occurrence is. See
/// `encoding::TextSearch`.
std::optional<encoding::TextSearch> highlight(const AppState& state) {
    if (state.findHighlight.empty()) return std::nullopt;
    std::string charset = state.readBody ? state.readBody->charset : std::string{};
    if (charset.empty() && state.readHeader) charset = state.readHeader->charset;
    if (charset.empty()) charset = state.areaConfig.defaultCharset;
    return encoding::TextSearch(state.findHighlight, charset);
}

/// The occurrences in one line of the message, cut up between the rows the
/// window broke it into.
///
/// The same walk `bbs::runsForRows()` makes over the color runs, and for the
/// same reason: the rows are `wrapText`'s own substrings of the line, in order,
/// so walking forward from the end of one lands on the next. An occurrence a
/// break falls inside is lit on both rows — a break the window happened to make
/// must not take the highlight off the words it fell between.
std::vector<std::vector<encoding::TextMatch>> foundForRows(
    const std::string& source, const std::vector<encoding::TextMatch>& found,
    const std::vector<std::string>& rows) {
    std::vector<std::vector<encoding::TextMatch>> perRow(rows.size());
    if (found.empty()) return perRow;

    size_t at = 0;
    for (size_t row = 0; row < rows.size(); ++row) {
        const size_t begin = source.find(rows[row], at);
        // Cannot happen — the rows are the line's own substrings — but a row
        // that is somehow not in it is drawn plainly rather than lit from
        // whatever offset a wrong answer would have given.
        if (begin == std::string::npos) break;
        const size_t end = begin + rows[row].size();
        for (const auto& match : found) {
            const size_t from = std::max(match.begin, begin);
            const size_t to = std::min(match.end, end);
            if (from < to) perRow[row].push_back({from - begin, to - begin});
        }
        at = end;
    }
    return perRow;
}

/// Wraps a body into readLines at the given width. The body is passed in
/// rather than read back off the state: this way the function does not depend
/// on the optional being engaged, which is the caller's business.
void wrapBody(AppState& state, const domain::MessageBody& body, int width) {
    state.readLines.clear();
    const std::optional<encoding::TextSearch> search = highlight(state);

    for (const auto& line : body.lines) {
        if (line.kludge && !state.showKludges) continue;

        // The codes come out before anything measures or reads the line: they
        // are three bytes and no columns, so a line wrapped with them in it
        // wraps early, and a quote marker behind one would not be found at all.
        // A kludge is left as it stands — it is service data shown verbatim,
        // and in the one color service data is shown in.
        //
        // One line at a time, and nothing carried between them: a color reaches
        // the end of the line the message wrote and stops there, so the quote
        // colors, the trailer and the kludges are the reader's own again on
        // every new line.
        bbs::CodedLine coded;
        const bool colored = state.areaConfig.bbsCodesRenegade && !line.kludge;
        if (colored) coded = bbs::stripRenegade(line.text);
        // Named apart from `text()`, the element the screens are built out of.
        const std::string& shown = colored ? coded.text : line.text;

        // Quoting is read off the source line: a wrapped continuation no longer
        // carries the markers but is still part of the same quote.
        const int depth = line.kludge ? 0 : quoteDepth(shown);

        // wrapText yields nothing for an empty string, but a blank line inside a
        // message is content and has to survive.
        if (shown.empty()) {
            state.readLines.push_back(
                {std::string{}, line.kludge, 0, line.trailer, {}, {}});
            continue;
        }

        // A search reads the text and not the service data, which is the same
        // rule that decided whether the message was found at all — and a MSGID
        // lit up would be a highlight on a line the reader hides.
        const bool searched = search && !search->empty() && !line.kludge;

        std::vector<std::string> rows = wrapText(shown, width);
        std::vector<std::vector<bbs::ColorRun>> runs;
        if (colored) runs = bbs::runsForRows(coded, rows);
        std::vector<std::vector<encoding::TextMatch>> found;
        if (searched) found = foundForRows(shown, search->findAll(shown), rows);
        for (size_t i = 0; i < rows.size(); ++i) {
            state.readLines.push_back(
                {std::move(rows[i]), line.kludge, depth, line.trailer,
                 colored ? std::move(runs[i]) : std::vector<bbs::ColorRun>{},
                 searched ? std::move(found[i]) : std::vector<encoding::TextMatch>{}});
        }
    }
}

}  // namespace

void relayout(AppState& state) {
    if (!state.readBody) return;
    // The body spans the window minus the one-column margins the screen sits in.
    // The terminal can be resized mid-session, so this runs on every frame and
    // re-lays out as soon as the width actually changes.
    const int available = std::max(1, state.width);
    if (available == state.readLayoutWidth) return;

    // A twit's text is not laid out at all: what stands in its place is the one
    // line saying so, and the message itself is not wrapped until it is asked
    // for. There is nothing to scroll and so nothing for the scrollbar to say.
    if (state.twitHidden()) {
        state.readLines.assign(
            1, AppState::DisplayLine{kTwitNotice, false, 0, false, {}, {}});
        state.scrollbarShown = false;
        state.readLayoutWidth = available;
        state.readScroll = 0;
        return;
    }

    // Two passes, because the scrollbar and whether it is needed depend on each
    // other: it takes the rightmost column, and that column can be what pushes
    // the body past the end of the screen. Laying out at the full width first
    // means a message that fits keeps it, and one that does not is re-wrapped a
    // column narrower — which can only make it longer, so the answer does not
    // flip back.
    const domain::MessageBody& body = *state.readBody;
    wrapBody(state, body, available);
    state.scrollbarShown = state.showScrollbar &&
                           static_cast<int>(state.readLines.size()) > state.readRows();
    if (state.scrollbarShown) wrapBody(state, body, std::max(1, available - 1));

    state.readLayoutWidth = available;
    state.readScroll = std::clamp(state.readScroll, 0, maxScroll(state));
}

namespace {

/// Turns the kludges on or off and re-lays out the body around them.
void toggleKludges(AppState& state) {
    state.showKludges = !state.showKludges;
    state.readLayoutWidth = 0;  // force relayout(): the width has not changed
    relayout(state);
    state.readScroll = 0;
}

/// Shows the twit on screen after all — what Space does where the notice
/// stands. The body has to be laid out, the notice having stood in place of it.
void revealTwit(AppState& state) {
    state.twitRevealed = true;
    state.readLayoutWidth = 0;  // the window is the same size; the layout is not
    relayout(state);
    state.readScroll = 0;
}

/// Shows or hides the scrollbar. The body has to be re-wrapped: the bar takes
/// the rightmost column away from the text.
void toggleScrollbar(AppState& state) {
    state.showScrollbar = !state.showScrollbar;
    state.readLayoutWidth = 0;  // the window is the same size; the layout is not
    relayout(state);
}

}  // namespace

Element render(AppState& state) {
    // An empty area opens here too, on no message at all. The rows are then
    // drawn blank rather than replaced by a notice: this is the screen a first
    // message is written from, and it should look like the one it will be read
    // on when somebody answers it.
    const domain::MessageHeader nothing;
    const auto& header = state.readHeader ? *state.readHeader : nothing;
    const bool empty = state.messageCount == 0;

    // The AKA the area is presented under, where the tosser config states one:
    // fidoconfig's -a, squish.cfg's -p. areas.bbs has no way to say it, so
    // there the parentheses are simply left out.
    const std::string aka = state.currentArea.address.isValid()
                                ? " (" + state.currentArea.address.toString() + ")"
                                : "";

    // The echo the message itself says it was posted to — its `AREA:` line —
    // where that is not the area it is being read in. An area collecting
    // messages from elsewhere (a dupe area, a carbon copy, a bad-message area)
    // shows nothing else about where each of them belongs, and it is where an
    // answer to this one goes: `areareplydirect` follows the same line.
    //
    // Said whatever that setting is: it is a fact about the message, and a
    // reader who has turned the following off is the more likely to want to
    // know where it came from.
    const std::string posted =
        state.readBody ? app::areaTagOf(*state.readBody) : std::string{};
    const std::string from =
        !posted.empty() && !config::text::iequals(posted, state.currentArea.tag)
            ? " from " + posted
            : "";

    // Built as a string rather than an element: the Back button, when it is
    // shown, takes the corner the title used to start in, and what is left
    // decides how much of the title fits.
    const std::string titleText = " " + state.currentArea.tag + from + aka +
                                  (empty ? " empty"
                                         : " " + std::to_string(header.number) + "/" +
                                               std::to_string(state.messageCount));

    // "-10 +12 +15": this message answers the tenth, and the twelfth and
    // fifteenth answer it. The links come from the base's own thread fields,
    // and a message that is in no thread shows nothing at all.
    struct Marker {
        std::string text;
        uint32_t number;
    };
    std::vector<Marker> markers;
    if (state.readThread.replyTo != 0) {
        markers.push_back(
            {" -" + std::to_string(state.readThread.replyTo), state.readThread.replyTo});
    }
    for (const uint32_t reply : state.readThread.replies) {
        markers.push_back({" +" + std::to_string(reply), reply});
    }

    // What a search landed here found, lit in the header block as it is in the
    // body. Built per frame rather than kept: folding a handful of characters
    // costs less than another field to keep in step with the message on screen.
    const std::optional<encoding::TextSearch> search = highlight(state);
    const HeaderBlock block = headerBlock(state, header, search ? &*search : nullptr);
    const std::string location = senderLocation(state);
    const std::string size = messageSize(state);

    const int viewportHeight = state.readRows();
    Elements bodyLines;
    const int totalLines = static_cast<int>(state.readLines.size());
    const int firstLine = std::clamp(state.readScroll, 0, std::max(0, totalLines));
    const int lastLine = std::min(totalLines, firstLine + viewportHeight);

    for (int i = firstLine; i < lastLine; ++i) {
        const auto& source = state.readLines[i];
        if (source.kludge) {
            // Service lines render darker, so they read as service data wherever in
            // the message they happen to sit. Nothing in them is a link worth
            // pointing at — a MSGID is not an address anyone follows.
            bodyLines.push_back(text(source.text) | color(theme::palette.kludge));
            continue;
        }

        // Whatever the line would be without its links: they color the address
        // alone, so a link in a quote leaves the quote around it intact.
        const theme::Color base =
            source.trailer ? theme::palette.trailer
            : source.quoteDepth > 0
                ? (source.quoteDepth % 2 == 1 ? theme::palette.quoteOdd
                                              : theme::palette.quoteEven)
                : theme::palette.text;

        bodyLines.push_back(bodyLine(source, base, state));
    }
    while (static_cast<int>(bodyLines.size()) < viewportHeight)
        bodyLines.push_back(text(""));

    Element viewport = vbox(std::move(bodyLines)) | color(theme::palette.text);
    if (state.scrollbarShown) {
        // The body was wrapped a column narrower, so the bar fills the rightmost
        // one exactly, with the text running up to it.
        viewport = hbox({std::move(viewport) | flex,
                         scrollbar::bar(viewportHeight, totalLines, state.readScroll)});
    }

    // Rules rather than blank lines: they mark off the title and the header
    // block without costing any extra height.
    auto rule = [&] {
        return text(horizontalRule(state.width)) | color(theme::palette.separator);
    };

    // The two corners the title row shares: the way back on the left and the way
    // into the menu on the right, each two rows tall and each costing the title
    // its five columns.
    const bool back = state.backButtonShown();
    const bool menu = state.readerMenuShown();
    const int titleRoom =
        std::max(1, state.width - (back ? kBackWidth : 0) - (menu ? kMenuWidth : 0));
    const std::string titleShown = truncateToWidth(titleText, titleRoom);

    // Where the message sits in its thread, beside the number that names it:
    // what it answers with a minus, what answers it with a plus. Service data
    // like the kludges, and drawn in their color for the same reason — it is
    // there to be glanced at, not read.
    //
    // One element per marker, each remembering where it landed, so that a
    // click can go to the message it names. The room is reserved first: the
    // boxes are written into while the frame is laid out, and a vector that
    // grew under them would leave the earlier ones pointing at freed memory.
    Elements titleCells{text(titleShown) | bold | color(theme::palette.tableHeader)};
    state.readThreadLinks.clear();
    state.readThreadLinks.reserve(markers.size());

    int titleLeft = titleRoom - displayWidth(titleShown);
    for (const auto& marker : markers) {
        const int width = displayWidth(marker.text);
        if (width > titleLeft) break;  // the window has no room for the rest
        titleLeft -= width;

        state.readThreadLinks.push_back({marker.number, {}});
        // A click on the marker is shown on the marker itself, in the moment
        // between the press and the message it names being loaded.
        const bool pressed =
            state.isPressed(AppState::Pressed::ThreadLink, marker.number);
        titleCells.push_back(
            text(marker.text) |
            color(pressed ? theme::palette.animatedButtonText : theme::palette.kludge) |
            reflect(state.readThreadLinks.back().box));
    }
    auto title = hbox(std::move(titleCells));

    // The two rows the corners take, with the title on the first and the rule
    // closing the block on the second. Each button starts or ends a column clear
    // of the rule, so it reads as a thing standing beside it rather than a piece
    // of it, and what is left over is what the rule is drawn across.
    const bool pressedBack = state.isPressed(AppState::Pressed::Back);
    const bool pressedMenu = state.isPressed(AppState::Pressed::MenuButton);
    const int ruleWidth = std::max(
        0, state.width - (back ? kBackWidth + 1 : 0) - (menu ? kMenuWidth + 1 : 0));

    Elements titleRow;
    Elements ruleRow;
    if (back) {
        titleRow.push_back(back_button::topRow(pressedBack));
        ruleRow.push_back(back_button::bottomRow(pressedBack));
    }
    titleRow.push_back(std::move(title));
    ruleRow.push_back(
        text((back ? " " : "") + horizontalRule(ruleWidth) + (menu ? " " : "")) |
        color(theme::palette.separator));
    if (menu) {
        // The filler is a child of this row rather than of the title beside it:
        // an hbox asks its children for the room they need and does not carry
        // their appetite for more up to its own parent, so a filler buried
        // inside one would grow into nothing.
        titleRow.push_back(filler());
        titleRow.push_back(menu_button::topRow(pressedMenu));
        ruleRow.push_back(menu_button::bottomRow(pressedMenu));
    }

    Elements content;
    if (back || menu) {
        content.push_back(hbox(std::move(titleRow)));
        content.push_back(hbox(std::move(ruleRow)));
    } else {
        content.push_back(std::move(titleRow.front()));
        content.push_back(rule());
    }
    content.insert(content.end(), block.rows.begin(), block.rows.end());
    content.push_back(closingRule(size, location, block.column, state.width));
    content.push_back(viewport);

    return vbox(std::move(content));
}

bool handleEvent(AppState& state, const Event& event) {
    // The back button, where it is shown, goes the same way Esc does here —
    // out of the area, not back a screen.
    if (state.backButtonShown() && back_button::clicked(event)) {
        state.showClick(AppState::Pressed::Back);
        message_list::leaveArea(state);
        return true;
    }
    // The menu button in the other corner, which puts the menu up over this
    // screen. What it holds is decided as it opens, on the message that is in
    // front of the user now.
    if (state.readerMenuShown() && menu_button::clicked(event, state.width)) {
        state.showClick(AppState::Pressed::MenuButton);
        openMenu(state);
        return true;
    }
    // A click on one of the thread markers goes to the message it names.
    if (const auto click = leftClick(event)) {
        for (const auto& link : state.readThreadLinks) {
            if (link.number == 0 || !link.box.Contain(click->x, click->y)) continue;
            // The number is taken out of the marker before anything else:
            // showing the click draws a frame, and render() builds the markers
            // afresh every time it does — which leaves nothing behind `link`.
            const uint32_t number = link.number;
            state.showClick(AppState::Pressed::ThreadLink, number);
            goToMessage(state, number);
            return true;
        }
    }
    // The wheel scrolls the body a line at a time, the same as ↑↓.
    if (const int wheel = wheelDelta(event); wheel != 0) {
        scrollBy(state, wheel);
        return true;
    }
    // Moving between messages is the arrow keys' job alone. Everything else on
    // this screen stays inside the message being read, so paging never carries
    // the reader off the end of it.
    if (event == Event::ArrowRight) {
        switchMessage(state, 1);
        return true;
    }
    if (event == Event::ArrowLeft) {
        switchMessage(state, -1);
        return true;
    }
    // Replying and writing anew. Both belong to the reader alone: a reply is a
    // reply to the message on screen, and the other screens leave the keys free.
    if (state.keys.is(event, KeyCommand::ReaderReply)) {
        compose::startReply(state);
        return true;
    }
    if (state.keys.is(event, KeyCommand::ReaderNew)) {
        compose::startNew(state);
        return true;
    }
    // Writing the message on screen again, over the one in the base. Not in the
    // menu unless the config asks for it: writing over a message that is already
    // in a base is a rare thing to want and a bad thing to do by accident.
    if (state.keys.is(event, KeyCommand::ReaderChange)) {
        askToChange(state);
        return true;
    }
    // Writing the message out, the dialog asking where and under what name —
    // and, where the message carries uuencoded files, asking first whether it is
    // those that are wanted. Not in the default menu: it is a thing done now and
    // then.
    if (state.keys.is(event, KeyCommand::ReaderExport)) {
        askExport(state);
        return true;
    }
    // Answering in another area: the dialog asks which, and what follows is the
    // ordinary reply, quote and all, written where it says.
    if (state.keys.is(event, KeyCommand::ReaderReplyTo)) {
        askArea(state, AppState::AreaPicker::For::Reply);
        return true;
    }
    // Passing the message on into another area, having asked first in what
    // sense: in a message of one's own carrying it, or by putting this very
    // message there — with or without taking it out of here. The area dialog
    // follows whichever was answered.
    if (state.keys.is(event, KeyCommand::ReaderForward)) {
        askForward(state);
        return true;
    }
    // Up and down the thread. The markers beside the message number say what
    // these two would do, and a click on one does it as well.
    if (state.keys.is(event, KeyCommand::ReaderThreadUp)) {
        followUp(state);
        return true;
    }
    if (state.keys.is(event, KeyCommand::ReaderThreadDown)) {
        followDown(state);
        return true;
    }
    // Deleting is asked about first: the base has no way back from it, and the
    // key sits among ones that only move about.
    if (state.keys.is(event, KeyCommand::ReaderDelete)) {
        if (!state.readHeader) return true;
        state.confirm = AppState::Confirm::DeleteMessage;
        state.confirmChoice = AppState::ConfirmChoice::Yes;
        return true;
    }
    // About the storage rather than about the message: what the base holds,
    // record by record, down to the bytes. Not in the menu unless the config
    // asks for it — it answers a question most readers never ask, and the ones
    // who do know the key.
    if (state.keys.is(event, KeyCommand::ReaderInfo)) {
        info_dialog::open(state);
        return true;
    }
    // Looking for a message in the area — the dialog asks what for, and the
    // shell hands its answer back to findMessage() above.
    if (state.keys.is(event, KeyCommand::ReaderFind)) {
        find_dialog::open(state);
        return true;
    }
    // The nodelist, on whoever wrote the message on screen.
    if (state.keys.is(event, KeyCommand::ReaderNodelist)) {
        nodelist_dialog::open(state);
        return true;
    }
    if (state.keys.is(event, KeyCommand::ReaderKludges)) {
        toggleKludges(state);
        return true;
    }
    if (state.keys.is(event, KeyCommand::ReaderScrollbar)) {
        toggleScrollbar(state);
        return true;
    }
    if (state.keys.is(event, KeyCommand::ReaderList)) {
        openList(state);
        return true;
    }
    if (event == Event::ArrowDown) {
        scrollBy(state, 1);
        return true;
    }
    if (event == Event::ArrowUp) {
        scrollBy(state, -1);
        return true;
    }
    // Space on a twit shows it after all, which is what the notice standing in
    // place of its text says to do. Only while the notice is there: once the
    // message is on the screen, Space is the page key it is everywhere else,
    // and PageDown never means anything but paging.
    if (event == Event::Character(' ') && state.twitHidden()) {
        revealTwit(state);
        return true;
    }
    if (event == Event::PageDown || event == Event::Character(' ')) {
        scrollBy(state, state.readRows());
        return true;
    }
    // Shift+Space. It arrives at all only because the terminal was asked to
    // report modified keys when the screen was opened; which of the two forms it
    // uses to say so is the input layer's business rather than this one's.
    if (event == Event::PageUp ||
        (event.shift() && event.is_character() && event.character() == " ")) {
        scrollBy(state, -state.readRows());
        return true;
    }
    if (event == Event::Home) {
        state.readScroll = 0;
        return true;
    }
    if (event == Event::End) {
        state.readScroll = maxScroll(state);
        return true;
    }
    // Going back is Esc/Backspace only: the arrow keys are taken by message
    // navigation here. The reader is where an area is entered, so leaving it
    // leaves the area.
    if (event == Event::Escape || event == Event::Backspace) {
        message_list::leaveArea(state);
        return true;
    }
    return false;
}

}  // namespace amberedit::ui::screens::message_read
