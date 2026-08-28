#pragma once

#include <ctime>
#include <string>
#include <string_view>
#include <vector>

#include "app/compose_prefill.hpp"
#include "config/app_config.hpp"
#include "domain/area.hpp"
#include "domain/message.hpp"

namespace amberedit::app {

/// Everything the two steps of writing a message need to know: what is being
/// answered, in which area, and when.
struct BuildRequest {
    const config::AppConfig& config;
    /// The area the message is being written into — not necessarily the one it
    /// answers, which is what `movedFrom` says.
    const domain::AreaConfig& area;
    /// The header as the compose screen left it.
    ComposeFields fields;
    /// The message being answered or forwarded — `fields.forward` tells the two
    /// apart; null for a new one, which is neither.
    const domain::MessageHeader* original{nullptr};
    const domain::MessageBody* originalBody{nullptr};
    /// The area that message was read in, when what is being written is going
    /// into a different one — a reply moved, or a forward. Null when the two
    /// are the same area. It is what @oecho names, and what the template's
    /// @moved lines are written for.
    const domain::AreaConfig* originalArea{nullptr};
    /// When the message is being written, and how far the clock here stands
    /// from UTC. Passed in rather than read off the clock so that what goes
    /// into a message is decided in one place and can be tested.
    std::time_t now{0};
    int utcOffsetMinutes{0};
    /// Whether the control lines go into the quote or the forward along with the
    /// text. It follows the reader's own `k`: what the message being answered
    /// looks like on screen is what is carried into the answer, so that somebody
    /// who turned the kludges on to point at one can quote it. Off, which is how
    /// the reader stands by default, only the visible text is carried.
    bool kludgesShown{false};
    /// Control lines to write beside the ones every message carries — today the
    /// `CC:` kludges `compose_cc_list hidden` asks for, which name the
    /// recipients a copy went to and are meant to be read by a program rather
    /// than by a person. Without their leading ^A, as `MessageDraft::kludges`
    /// wants them.
    std::vector<std::string> extraKludges;
};

/// The text the editor opens on: the template expanded against this message,
/// the quoted original in it where the template asks for one.
struct StartingText {
    std::vector<std::string> lines;
    /// Which line the cursor starts on, from the template's @position.
    int cursorLine{0};
    /// Why the template was not used, empty when it was. The message is still
    /// begun — a template that cannot be read is worth saying out loud, but
    /// not worth refusing to write over.
    std::string error;
};

[[nodiscard]] StartingText startingText(const BuildRequest& request);

/// The message as it will be stored: the control lines FTS-0009, FTS-4008,
/// FTS-5003 and FSC-0004 ask for, the text as edited, and the tearline and
/// origin line closing it.
[[nodiscard]] domain::MessageDraft buildDraft(const BuildRequest& request,
                                              const std::vector<std::string>& text);

/// The message as the base already holds it, ready to be written into another
/// area — what the reader's Move and Copy hand to the base they are going into.
///
/// Nothing is made up and nothing is left out. The header fields, the attributes,
/// control lines standing either side of the text and the text between them are
/// the message's own, and so is the date it was written on: this is the same
/// message in another area, which is the whole of what a move and a copy
/// promise. The MSGID goes with it for that reason — it is what the network
/// tells two messages apart by, and a copy that invented one would be a second
/// message wearing the first one's words.
///
/// `netmail` is the kind of the area it is going into, which is the one thing
/// the message cannot answer for itself: a destination address means a recipient
/// in netmail and nothing at all in an echo.
[[nodiscard]] domain::MessageDraft copyOf(const domain::MessageHeader& header,
                                          const domain::MessageBody& body, bool netmail);

/// What a message being changed keeps of what the base already holds: the
/// service lines, which are none of the editor's business, and the charset the
/// message is written in.
///
/// The control lines are split where the text begins because that is where the
/// formats split them: MSGID and its like stand before the message, SEEN-BY and
/// PATH after the origin, and a message put back together the other way round
/// would be one no tosser could read. What the editor shows is the text between
/// them, and these two are put back around it when it is stored.
struct PreservedLines {
    /// The control lines standing before the text, without their leading ^A —
    /// as `MessageDraft::kludges` wants them.
    std::vector<std::string> kludges;
    /// The service lines standing after it — Via, SEEN-BY, PATH — with the ^A
    /// they are stored with, since they go back into the text they came from.
    std::vector<std::string> trailing;
    /// What the base wrote the message in, so that it is written back in the
    /// same charset the CHRS line among the kludges still names. A message that
    /// declared none was read in the area's own default, which an area group may
    /// have decided — and that is the charset it goes back in, `compose_charset`
    /// having no say over a message that was written once already.
    std::string charset;
};

/// Takes those out of a message that has been read.
[[nodiscard]] PreservedLines preservedLines(const domain::MessageBody& body);

/// What says when a changed message was written and by which system: the clock
/// here, the zone it stands in, and the address this area is presented under
/// (`ownAddress()`). All three are passed in rather than read off the machine,
/// so that what goes into a message is decided in one place and can be tested.
struct ChangeStamp {
    std::time_t now{0};
    int utcOffsetMinutes{0};
    std::string address;
};

/// The message as changed: the header fields and the text as the editor leaves
/// them, everything else as the base already had it.
///
/// No tearline is added and no REPLY: this message has been written once
/// already, it still answers whatever it answered, and the whole point of
/// putting it back where it was is that it is the same message in the base.
/// Three control lines are the exception, and `stamp` is what they are made of,
/// because all three describe the writing rather than the message:
///
/// - **MSGID** is made afresh, naming this system and this moment. What went
///   out under the old one is not what the message now says, and a MSGID is
///   what a network tells two messages apart by. Where `stamp.address` is empty
///   — a config naming no address, in an area presented under none — the
///   message's own From address stands in, and with neither the old MSGID is
///   left alone rather than written without an address in it.
/// - **TZUTC** says which clock the stamp is on, and the base dates a changed
///   message by the clock here.
///
/// A message that carried neither is given both — anything written here carries
/// them — and everything else it carried it carries still.
[[nodiscard]] domain::MessageDraft buildChange(const ComposeFields& fields,
                                               const PreservedLines& kept,
                                               const std::vector<std::string>& text,
                                               const ChangeStamp& stamp);

/// The template's @Changed lines, expanded for this message — the notice that
/// goes at the head of a message being changed by somebody other than whoever
/// wrote it. Empty where the template names none, or cannot be read: a notice
/// nobody wrote is not worth refusing the change over.
[[nodiscard]] std::vector<std::string> changeNotice(const BuildRequest& request);

/// The clock here, as a stamp of the kind a message carries: the fields the
/// local time shows, in no time zone of their own — which is what an FTN stamp
/// is. What a message being written is stamped with, and so what the editor's
/// Date row shows over one.
[[nodiscard]] domain::MessageDate localStamp(std::time_t when);

// --- the pieces, exposed for the tests -------------------------------------

/// The serial number of a MSGID: the time the message was written, as a 32-bit
/// unsigned count of seconds, in the eight hexadecimal digits FTS-0009 asks
/// for. Unique per system for far longer than the three years the standard
/// requires, so long as no two messages are written in the same second.
[[nodiscard]] std::string serialNumber(std::time_t when);

/// The offset TZUTC states: [-]hhmm, always four digits, minus only when the
/// clock here is west of UTC.
[[nodiscard]] std::string tzutcOffset(int minutes);

/// The other way about: the minutes east of UTC a stamp of that shape names, as
/// `MessageHeader::utcOffset` carries it — a sign and four digits, "+0300".
/// Zero for anything else, which is the answer a message stating no zone gives
/// and the one a base keeps for it.
[[nodiscard]] int tzutcMinutes(std::string_view offset);

/// The charset a message being written is encoded in, and that its CHRS line
/// announces: `compose_charset` of the area it is going into.
///
/// Where `reply_original_charset` is on for that area and the message answers
/// one, it is instead the charset the message being answered was written in —
/// the quote is what that is for, a message answered in a charset with no room
/// for what it said coming back full of question marks. Both settings are the
/// target area's, as everything a message is written under is: what may be
/// written in an echo is that echo's business, whatever area the answer was
/// begun in.
///
/// `encoded` is everything the base will convert — the header fields, the lines,
/// and the control lines the caller adds. A reply the original's charset has no
/// room for either is written in `compose_charset` after all: keeping somebody
/// else's words is what this is for, and it is not worth losing one's own.
[[nodiscard]] std::string draftCharset(const BuildRequest& request,
                                       const std::vector<std::string>& encoded);

/// The CHRS level of a charset: 1 for seven-bit ASCII, 4 for UTF-8, 2 for the
/// eight-bit sets in between.
[[nodiscard]] int charsetLevel(std::string_view charset);

/// The charset identifier CHRS names it by — the spelling it is stated in, with
/// the names FTS-5003 fixes spelled its way. Those are what the reader normalizes
/// a CHRS onto iconv's names, and so what a reply keeping the original's charset
/// would otherwise announce: iconv calls it ISO-8859-1 and CHRS calls it
/// LATIN-1.
[[nodiscard]] std::string charsetIdentifier(std::string_view charset);

/// The MSGID of a message being answered, without the "MSGID: " in front, or
/// empty when it carries none — in which case FTS-0009 says to write no REPLY.
[[nodiscard]] std::string msgidOf(const domain::MessageBody& body);

/// The echo a message says it was posted to: the tag of its `AREA:` line, or
/// empty when it has none. What `areareplydirect` answers a reply with.
///
/// Only the very first line of the message counts, which is the one place the
/// line is written: `AREA:` is the packet header FTS-0001 puts ahead of
/// everything, it carries no ^A, and the drivers keep it where they find it. A
/// line saying it anywhere else is text of the message that begins with those
/// five characters, and reading an area out of it would answer a reply with
/// whatever somebody happened to quote.
[[nodiscard]] std::string areaTagOf(const domain::MessageBody& body);

}  // namespace amberedit::app
