#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "domain/ftn_address.hpp"
#include "domain/message.hpp"

namespace amberedit::msgbase {

/// A message as a base stores it, before anything has been made of it.
///
/// This is the language the format drivers speak: bytes in the charset the
/// message was written in, addresses and stamps as fields, and no conversion
/// of any kind. Everything a reader wants of a message — UTF-8 text, a charset
/// worked out from its own kludges, lines marked as service data — is the
/// adapter's doing above this, which is what keeps the three formats from
/// growing three copies of it.
struct RawHeader {
    /// FTS-0001 attribute bits, which is what `domain::messageAttributes()` reads.
    /// JAM keeps its own set and its driver translates; the other two store
    /// these.
    uint32_t attributes{0};

    /// Whether the base holds this message as already read — see
    /// `domain::MessageHeader::seen`. Each format says it its own way and the
    /// driver answers for it: JAM and Fido *.msg count the reads, Squish sets a
    /// bit beside the attributes above.
    ///
    /// Read out of a stored message and meaningless in a draft: a message on its
    /// way into a base has not been read, and `markSeen()` is the one thing that
    /// sets the mark afterwards.
    bool seen{false};

    std::string from;
    std::string to;
    std::string subject;

    domain::FtnAddress origAddr;
    domain::FtnAddress destAddr;

    domain::MessageDate written;
    domain::MessageDate arrived;

    /// Minutes east of UTC where the message was written. Only Squish has a
    /// field for it; the others leave it at zero and the TZUTC kludge says it.
    int utcOffsetMinutes{0};

    /// The thread links, **as UIDs** — the identifier that outlives a pack,
    /// which is the UMSGID in Squish and the absolute message number in JAM and
    /// Fido *.msg. Turning them into positions needs the base and is the
    /// adapter's job.
    uint32_t replyTo{0};
    std::vector<uint32_t> replies;
};

/// A stored message, header and both halves of its body.
struct RawMessage {
    RawHeader header;

    /// The control lines, each behind its ^A and closed with a carriage
    /// return — the form they are shown in, and the form Fido *.msg stores
    /// them in to begin with. Empty when the message carries none.
    std::string control;

    /// The visible text, exactly as the base holds it: hard carriage returns,
    /// the tearline and origin line in it, and — in JAM — the SEEN-BY and PATH
    /// lines the format keeps apart put back at the end where they belong.
    std::string text;
};

/// A message on its way into a base.
struct RawDraft {
    RawHeader header;
    /// Control lines without their leading ^A, in the order they go out.
    std::vector<std::string> kludges;
    /// The visible text, lines closed with carriage returns.
    std::string text;
};

/// Turns Squish's control block — "^AKLUDGE^AKLUDGE", the lines run together
/// with no separator — into the "^AKLUDGE\r" per line form a reader shows.
///
/// The AREA: line loses its ^A on the way out, as it never had one in the
/// message it came from: it is a packet header that the *.msg driver keeps
/// with the kludges because it stands where they do.
[[nodiscard]] std::string controlBlockToKludges(std::string_view block);

/// The other way about: the lines, each given a ^A, run together into the
/// block Squish stores. The trailing NUL is not included.
[[nodiscard]] std::string kludgesToControlBlock(const std::vector<std::string>& kludges);

/// The offset from UTC the message's TZUTC control line states, read out of the
/// control block in the "^ALINE\r" per line form the drivers hand back.
///
/// Written the way strftime's `%z` writes an offset — a sign and four digits,
/// "+0300" — since that is what it is shown as. FTS-4008 spells it `[-]hhmm`
/// with the plus left off, and asks that a plus be accepted where one is found
/// anyway; TZUTCINFO is read as well, the same paragraph under the name the JAM
/// subfield gave it.
///
/// Empty where the message carries neither, and empty where what it carries is
/// not an offset at all: nothing is a better answer than a wrong clock, and a
/// message stating no zone is the ordinary case rather than an error.
[[nodiscard]] std::string tzutcOffsetOf(std::string_view control);

/// Fills in what the stored header does not say about the two addresses from
/// the kludges that do: the zones from INTL, the points from FMPT and TOPT.
///
/// A header is routinely short of the whole address. Fido *.msg has no field
/// for either — its two words carry a net and a node and nothing else — and a
/// Squish XMSG has all four while plenty of tossers write netmail with the
/// zone words left at zero, FSC-0004 having made the kludges the place a
/// zone is stated. Read out of the control lines both drivers hold anyway,
/// which is why a header-only read still reads them.
///
/// Only a field the header leaves at zero is answered for: what the base
/// states is what the base holds, and a kludge disagreeing with it is not a
/// reader's to overrule. INTL is read the guarded way — one whose net and node
/// are not the header's belongs to a message this one was routed inside of.
void completeAddresses(RawHeader& header, std::string_view control);

/// How much of the end of an echomail message's text is read to look for its
/// origin line, where the message is read for its header alone and there is no
/// text at hand: enough that the SEEN-BY and PATH lines of a widely carried
/// area still stand inside it. A message whose routing runs longer than this is
/// left to its MSGID.
constexpr size_t kOriginTailBytes = 8192;

/// The address an echomail message's origin line signs it with, read out of
/// `tail` — the end of the message's text, or the whole of it.
///
/// FTS-0004 puts the address last on that line and in brackets, and says
/// nothing about what else may stand in them: the last word inside the last
/// brackets that is an address is the one the line is signed with. Invalid
/// where the message's last visible line is not an origin line at all, or the
/// brackets hold no address — a message may perfectly well carry neither.
[[nodiscard]] domain::FtnAddress senderFromOrigin(std::string_view tail);

/// The address a MSGID names: its first word, which FTS-0009 has be the address
/// of the system that wrote the message. Invalid where the message carries no
/// MSGID, or where what stands there is not an address — a bare serial and an
/// internet message id are both written into that field in the wild.
[[nodiscard]] domain::FtnAddress senderFromMsgid(std::string_view control);

/// Fills in the sender of an **echomail** message the base itself does not
/// name, out of what the message says about itself: the origin line first, the
/// MSGID after it.
///
/// JAM is why this is here. The format keeps no address field — the two
/// addresses are optional subfields — and a tosser writing an echo leaves them
/// out, the message being a broadcast rather than a letter to a node; hpt does,
/// and so does this program's own writer. Squish and Fido `*.msg` have header
/// words and usually fill them in, but a tosser is at liberty to leave those at
/// zero in an echo for the same reason. Without this the From line of every
/// message in such an area is a name with no address beside it, and a reply
/// carbon, a twit rule or a nodelist lookup has nothing to match on.
///
/// Only where the header named **nothing at all**: what a base states is what a
/// base holds. The recipient is not answered for — an echomail message is
/// addressed to whoever reads it, and no line of it names a destination node.
void completeEchoSender(RawHeader& header, std::string_view control,
                        std::string_view tail);

/// Splits the leading control lines off a Fido *.msg body, where the kludges
/// are the first lines of the text rather than a block of their own. The
/// AREA: line counts as one of them, which is what smapi does and what keeps
/// an exported echomail message reading the same in either driver.
void splitLeadingKludges(std::string_view body, std::string* control, std::string* text);

/// The DOS-packed date Squish and Fido *.msg keep their stamps in: the year
/// counts from 1980 and the seconds in units of two.
[[nodiscard]] domain::MessageDate fromDosStamp(uint16_t date, uint16_t time);
void toDosStamp(const domain::MessageDate& date, uint16_t* outDate, uint16_t* outTime);

/// JAM's stamps are Unix time, and — the specification is explicit — in local
/// time rather than UTC. So they are broken down as though they were UTC and
/// assembled the same way: the number is the wall clock where the message was
/// written, and turning it into this machine's zone would move every message.
[[nodiscard]] domain::MessageDate fromUnixStamp(uint32_t seconds);
[[nodiscard]] uint32_t toUnixStamp(const domain::MessageDate& date);

/// The date as FTS-0001 spells it in the 20-byte header field:
/// "01 Jan 86  02:34:56". Squish keeps it beside the packed stamp for a tosser
/// to pass on; in Fido *.msg it is the only date the header states in words.
[[nodiscard]] std::string ftscDate(const domain::MessageDate& date);

/// Reads back a date written the FTS-0001 way. An unparsable one comes back
/// invalid, which is what a message with no date at all gives.
[[nodiscard]] domain::MessageDate parseFtscDate(std::string_view text);

/// A fixed-width byte field, as a string without the padding: everything up to
/// the first NUL, or the whole field when there is none.
[[nodiscard]] std::string fromFixedField(const unsigned char* field, size_t capacity);

/// Copies text into a fixed-width field, cut to fit and always NUL-terminated.
/// A 40-character name goes in as the first 35 rather than costing the message:
/// that is what every FTN editor does, the field having been 36 bytes wide
/// since 1986.
void toFixedField(unsigned char* field, size_t capacity, std::string_view text);

}  // namespace amberedit::msgbase
