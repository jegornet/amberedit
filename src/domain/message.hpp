#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "domain/ftn_address.hpp"

namespace amberedit::domain {

/// Whether the line is a tearline: three hyphens on their own, or followed by
/// a space and the name of the program that wrote the message (FTS-0004).
[[nodiscard]] bool isTearline(std::string_view line);

/// Whether the line is an origin line (FTS-0004).
///
/// Only the prefix is checked. What stands in the trailing parentheses is
/// deliberately not parsed: it may be a plain 4D address, one with a domain
/// such as `(2:382/736@fidonet)`, or the network name alongside it as in
/// `(Fidonet 2:382/736)`. None of that changes what the line is.
[[nodiscard]] bool isOriginLine(std::string_view line);

/// When the message was written. Stored exactly as it appears in the base,
/// with no conversion to local time: FTN stamps carry no reliable time zone.
struct MessageDate {
    uint16_t year{0};  ///< full year, e.g. 2024
    uint8_t month{0};  ///< 1..12
    uint8_t day{0};    ///< 1..31
    uint8_t hour{0};
    uint8_t minute{0};
    uint8_t second{0};

    [[nodiscard]] bool isValid() const { return year != 0 && month != 0 && day != 0; }

    /// The stamp written out as a strftime format asks — the one way a date
    /// reaches a screen or a message. What the config's
    /// `reader_datetime_format`, `template_date_format` and
    /// `template_time_format` name; empty for a stamp that is no date, so that
    /// a message carrying none shows nothing rather than a row of zeroes.
    ///
    /// The fields go to strftime as the base stores them, in no time zone: an
    /// FTN stamp carries none, so `%Z` says nothing here. The weekday and day
    /// of the year `%a` and `%j` need are worked out from the date itself
    /// rather than through mktime(), which would answer in the local zone —
    /// one the stamp was never in.
    ///
    /// What comes back is trimmed at both ends. A specifier that writes
    /// nothing leaves the space beside it behind — `%z` for a message stating
    /// no zone, which the default format asks for — and a stamp ending in a
    /// blank would give every column measured off it a column of nothing.
    ///
    /// `%z` is the one thing the stamp cannot answer for itself, so it is
    /// answered for it: `zone` is written where it stands, and nothing at all
    /// where the caller passes none. It never reaches strftime, which would
    /// take `%z` off `struct tm` and say "+0000" — this machine's idea of a
    /// zone the message was never in. What goes in there is the offset the
    /// message's own TZUTC states; see `MessageHeader::utcOffset`.
    [[nodiscard]] std::string format(const std::string& spec,
                                     std::string_view zone = {}) const;
};

/// The FTS-0001 attribute bits, as smapi defines them in msgapi.h. Spelled out
/// here rather than included, because domain/ knows nothing of smapi — the
/// values are FTS-0001's and fixed for good.
///
/// Named constants rather than an enum: they are a bit field, and a message —
/// one being read or one being written — carries them as a single word.
namespace attr {

inline constexpr uint32_t kPrivate = 0x0001u;
inline constexpr uint32_t kCrash = 0x0002u;
inline constexpr uint32_t kRead = 0x0004u;
inline constexpr uint32_t kSent = 0x0008u;
inline constexpr uint32_t kFile = 0x0010u;
inline constexpr uint32_t kInTransit = 0x0020u;
inline constexpr uint32_t kOrphan = 0x0040u;
inline constexpr uint32_t kKillSent = 0x0080u;
inline constexpr uint32_t kLocal = 0x0100u;
inline constexpr uint32_t kHold = 0x0200u;
inline constexpr uint32_t kDirect = 0x0400u;
inline constexpr uint32_t kFileRequest = 0x0800u;
inline constexpr uint32_t kReceiptRequest = 0x1000u;
inline constexpr uint32_t kIsReceipt = 0x2000u;
inline constexpr uint32_t kAuditRequest = 0x4000u;
inline constexpr uint32_t kUpdateRequest = 0x8000u;
inline constexpr uint32_t kScanned = 0x00010000u;
inline constexpr uint32_t kImmediate = 0x00040000u;

}  // namespace attr

/// Message header. Every text field is already converted to UTF-8.
struct MessageHeader {
    uint32_t number{0};  ///< message number within the area, 1-based
    std::string from;
    std::string to;
    std::string subject;
    /// When the message was written.
    MessageDate date;
    /// When it arrived here. Squish and JAM keep this apart from the written
    /// stamp; a Fido *.msg header has only one date, and smapi hands the same
    /// one back for both.
    MessageDate arrivalDate;
    /// The offset from UTC the message's TZUTC control line states, written the
    /// way `%z` writes one — "+0300", "-0330" — and empty where the message
    /// carries no TZUTC, or carries one that is not an offset.
    ///
    /// It belongs to `date` and to nothing else: TZUTC says which clock the
    /// message was written by, and when it arrived here is this system's own
    /// clock, which the message has nothing to say about. So `%z` in a reader
    /// format writes this beside the written stamp and writes nothing beside
    /// the arrival stamp.
    std::string utcOffset;
    FtnAddress origAddr;
    FtnAddress destAddr;
    uint32_t attributes{0};

    /// The charset the names and the subject above were decoded out of — the
    /// message's own CHRS kludge, or the area's `default_charset` where it
    /// states none. `MessageBody::charset` is the same answer read off the same
    /// message; it is here as well because a search over an area folds case by
    /// the charset and must not have to read every body to learn it.
    ///
    /// Empty only where there was no message to read.
    std::string charset;

    /// Whether the message has been read here — the mark the base itself keeps
    /// on it, which every FTN reader writes and reads: JAM's `TimesRead`,
    /// Squish's `MSGSEEN` bit, the `times_read` word of a Fido *.msg.
    ///
    /// Not `isRead()` below, which is FTS-0001's `MSGREAD` — "this netmail was
    /// received by the node it was addressed to", a thing the *network* says
    /// about the message and nothing to do with whether anybody here has looked
    /// at it. Nor the area list's unread count, which is a position: how many
    /// messages stand after the lastread mark. This is per message and it
    /// outlives the mark.
    ///
    /// A field of its own rather than another bit in `attributes`, because two
    /// of the three formats have no attribute for it at all — they count reads
    /// — and because a reader's mark on a message is not one of the message's
    /// own attributes: the attributes dialog would otherwise offer to clear it,
    /// and "zap all attribs" would.
    bool seen{false};

    [[nodiscard]] bool isPrivate() const { return (attributes & attr::kPrivate) != 0; }
    [[nodiscard]] bool isRead() const { return (attributes & attr::kRead) != 0; }
};

/// Where a message sits in its thread: what it answers, and what answers it.
///
/// Message numbers, not the UIDs the bases keep these links in — a number is
/// what the reader shows and what the user can go to. A link to a message that
/// has since been deleted is left out rather than pointed at nothing.
struct MessageThread {
    /// The message this one answers, or 0 when it answers none in this area.
    uint32_t replyTo{0};
    /// The messages answering this one, in the order the base keeps them.
    std::vector<uint32_t> replies;

    [[nodiscard]] bool empty() const { return replyTo == 0 && replies.empty(); }
};

/// A message being written, as a base is asked to store it.
///
/// The text is UTF-8 like everything else above the message-base port, and the
/// adapter converts it into `charset` on the way to disk — which is the same
/// charset the CHRS control line in `kludges` names, since the two have to
/// agree or the message would be unreadable to everybody but us.
struct MessageDraft {
    std::string from;
    std::string to;
    std::string subject;
    FtnAddress origAddr;
    FtnAddress destAddr;
    /// Netmail is private and addressed to a node; echomail is neither.
    bool netmail{false};

    /// The attribute bits the message is stored with — `attr::kLocal` for one
    /// written here, `attr::kPrivate` in netmail, and whatever else was asked
    /// for. A base stores what it is given rather than deciding for itself:
    /// these are the author's attributes, and the compose screen is where they
    /// are set.
    uint32_t attributes{0};

    /// The control lines, without their leading ^A and in the order they go
    /// out. The base decides where they end up: apart from the text in Squish
    /// and JAM, ahead of it in Fido *.msg.
    std::vector<std::string> kludges;
    /// The visible text, the tearline and origin line included. Lines, not one
    /// string: the hard carriage returns FTS-0001 asks for are the adapter's to
    /// write, and nothing above it should have to remember that.
    std::vector<std::string> lines;

    std::string charset{"CP866"};
    /// Minutes east of UTC where the message was written, for the base's own
    /// stamp. The TZUTC control line says the same thing to the network.
    int utcOffsetMinutes{0};

    /// When the message was written, where that is not now: a message copied or
    /// moved into another area was written when it says it was, and a stamp
    /// made up at the copying would date somebody else's words by our clock.
    /// Invalid — which is what a message being typed leaves it — means now, and
    /// the base is what reads the clock.
    ///
    /// Only the written stamp: when the message reached the base it is going
    /// into is that base's own business, and it is reaching it now.
    MessageDate written;
};

/// Whether the message was written here and has not gone out yet: `MSGLOCAL`
/// set with `MSGSENT` clear. There is no bit saying so — it is the absence of
/// one — which is why it is worth a name of its own. This is what the virtual
/// `Uns` attribute the screens show stands on; see messageAttributes().
[[nodiscard]] bool isUnsent(uint32_t attributes);
[[nodiscard]] bool isUnsent(const MessageHeader& header);

/// The attributes set in `attributes`, in the short forms FTN readers show them
/// in — `Rcv`, `Pvt`, `Loc` and the rest — ready to be joined with spaces.
///
/// Empty when nothing is set, so a message carrying no attributes shows no
/// brackets rather than an empty pair. The order is fixed rather than following
/// the bit order: what became of the message reads first, then whether it is
/// private, then the rest.
///
/// **`Uns` is a virtual attribute**: alone among these it stands for no bit, and
/// is shown exactly when `Loc` is set and `Snt` is not — see isUnsent(). It
/// exists in the UI only. Nothing stores it, nothing reads it back, and the
/// attributes dialog has no checkbox for it: setting or clearing `Snt` and `Loc`
/// is what makes it come and go. It is worked out from the same attributes as
/// the rest, so a message says the same thing about itself wherever it is shown:
/// the compose screen writes one carrying `[Uns Loc]` and the reader shows it
/// carrying `[Uns Loc]`.
[[nodiscard]] std::vector<std::string> messageAttributes(uint32_t attributes);

/// The same for a message that has been read back out of a base.
[[nodiscard]] std::vector<std::string> messageAttributes(const MessageHeader& header);

/// The bit `name` stands for, read the other way round: the short forms above,
/// matched without regard to case, so a config may write `k/s` for what the
/// screens show as `K/s`. Nullopt for a word that names no attribute.
///
/// `Uns` is one of those words. It stands for no bit — it is `Loc` set with
/// `Snt` clear, worked out and never stored — so there is nothing here for it to
/// answer; whoever asks is told what it is not, and the two attributes it is
/// made of are written instead.
[[nodiscard]] std::optional<uint32_t> messageAttributeBit(std::string_view name);

/// Every short form there is, in the order messageAttributes() writes them —
/// what a config error lists when it has been given a word that is not one of
/// them. `Uns` is not among them, for the reason above.
[[nodiscard]] std::vector<std::string> messageAttributeNames();

/// One line of a message body, in UTF-8.
struct MessageLine {
    std::string text;
    /// True for service lines: ^A kludges, and the SEEN-BY:/PATH: routing lines
    /// that carry no ^A but are service data all the same.
    bool kludge{false};
    /// True for the tearline and origin line closing the message. See
    /// markTrailer() for why this is a flag rather than a test on the text.
    bool trailer{false};
};

/// Flags the tearline and origin line that close a message.
///
/// Only the block at the very end qualifies, which is why this cannot be
/// decided one line at a time: authors use `---` mid-message as a separator
/// often enough that flagging every occurrence would be wrong. The walk starts
/// at the last line and stops as soon as it meets something that is neither a
/// tearline nor an origin. Kludges and blank lines are stepped over, since
/// SEEN-BY and PATH sit after the origin.
void markTrailer(std::vector<MessageLine>& lines);

/// Message body, kept as the sequence of lines the base stores. Order matters
/// and is not rearranged: MSGID and friends come before the text, SEEN-BY and
/// PATH after the origin line, exactly as the base has them.
struct MessageBody {
    std::vector<MessageLine> lines;
    std::string charset;  ///< what the base actually used
    std::string origin;   ///< the " * Origin: ..." line, if there was one

    /// The visible text, service lines dropped, joined with newlines.
    [[nodiscard]] std::string text() const;

    /// The service lines, in the order the base stores them.
    [[nodiscard]] std::vector<std::string> kludges() const;
};

/// One labelled value of the service report a base gives about a message —
/// what `i` shows in the reader.
struct MessageInfoField {
    std::string label;
    std::string value;
    /// Whether the value is text out of the message itself — a name, a subject,
    /// a subfield — and so in the charset the message was written in rather
    /// than in the plain ASCII everything else here is. It is what tells the
    /// adapter which values to convert; the numbers, the offsets and the
    /// attributes are the base's own and are the same bytes in every charset.
    bool text{false};
};

/// One block of that report: a heading, the values under it, and — where the
/// block is about a stored record rather than about a computed value — the
/// record itself, to be shown as a hexdump.
struct MessageInfoBlock {
    /// The heading over the block, empty for the one that opens the report.
    std::string title;
    std::vector<MessageInfoField> fields;
    /// The bytes exactly as the base holds them. Empty where there are none to
    /// show, which is what makes a block of fields alone.
    std::string bytes;
};

/// What one message base holds about one message: the header fields as they
/// are stored, the records around them, and the bytes those records are made
/// of. Every format answers this its own way — there is nothing in common
/// between a Squish frame and a JAM subfield worth pretending there is — so
/// only the shape of the report is shared, not its fields.
struct MessageInfo {
    /// What the report is of, as its first line says: "Squish message 85 of
    /// …". Empty for a message that could not be read at all, which is what
    /// tells an empty report from a failed one.
    std::string title;
    std::vector<MessageInfoBlock> blocks;

    [[nodiscard]] bool empty() const { return title.empty() && blocks.empty(); }
};

}  // namespace amberedit::domain
