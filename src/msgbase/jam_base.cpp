#include "msgbase/jam_base.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <system_error>

#include "config/text_util.hpp"
#include "msgbase/byte_order.hpp"
#include "msgbase/file_lock.hpp"
#include "msgbase/info_format.hpp"
#include "msgbase/jam_crc32.hpp"

namespace amberedit::msgbase {

namespace {

using bytes::readU16;
using bytes::readU32;
using bytes::writeU16;
using bytes::writeU32;
using config::text::startsWith;

constexpr size_t kInfoSize = 1024;
constexpr size_t kFixedHeaderSize = 76;
/// The number the last scan reached, in the info block after BaseMsgNum.
/// Nothing here acts on it — the lastread files are what a reader keeps — so
/// it is read where it is shown rather than held with the rest.
constexpr size_t kHighWaterOffset = 24;
constexpr size_t kIndexRecordSize = 8;
constexpr size_t kSubfieldHeaderSize = 8;
constexpr uint32_t kNoRecord = 0xffffffffu;
constexpr char kSignature[4] = {'J', 'A', 'M', '\0'};
constexpr char kSoh = '\x01';

/// Headers up to this much .jhr are read in one piece; a bigger file is read a
/// header at a time. The same trade smapi makes: one read against the memory
/// to hold a whole spool area.
constexpr int64_t kHeadersInCoreLimit = int64_t{16} * 1024 * 1024;

/// Subfield IDs (JAM-001, "Defined LoID codes").
enum : uint16_t {
    kSubOrigAddress = 0,
    kSubDestAddress = 1,
    kSubSenderName = 2,
    kSubReceiverName = 3,
    kSubMsgId = 4,
    kSubReplyId = 5,
    kSubSubject = 6,
    kSubPid = 7,
    kSubTrace = 8,
    kSubFtsKludge = 2000,
    kSubSeenBy = 2001,
    kSubPath = 2002,
    kSubFlags = 2003,
    kSubTzUtc = 2004,
};

/// JAM attribute bits.
enum : uint32_t {
    kJamLocal = 0x00000001u,
    kJamInTransit = 0x00000002u,
    kJamPrivate = 0x00000004u,
    kJamRead = 0x00000008u,
    kJamSent = 0x00000010u,
    kJamKillSent = 0x00000020u,
    kJamHold = 0x00000080u,
    kJamCrash = 0x00000100u,
    kJamImmediate = 0x00000200u,
    kJamDirect = 0x00000400u,
    kJamFileRequest = 0x00001000u,
    kJamFileAttach = 0x00002000u,
    kJamReceiptRequest = 0x00010000u,
    kJamConfirmRequest = 0x00020000u,
    kJamOrphan = 0x00040000u,
    kJamTypeEcho = 0x01000000u,
    kJamTypeNet = 0x02000000u,
    kJamLocked = 0x40000000u,
    kJamDeleted = 0x80000000u,
};

/// FTS-0001 attribute bits, as `domain/message.cpp` reads them and the other
/// two formats store them.
enum : uint32_t {
    kMsgPrivate = 0x0001u,
    kMsgCrash = 0x0002u,
    kMsgRead = 0x0004u,
    kMsgSent = 0x0008u,
    kMsgFileAttach = 0x0010u,
    kMsgInTransit = 0x0020u,
    kMsgOrphan = 0x0040u,
    kMsgKillSent = 0x0080u,
    kMsgLocal = 0x0100u,
    kMsgHold = 0x0200u,
    kMsgDirect = 0x0400u,
    kMsgFileRequest = 0x0800u,
    kMsgReceiptRequest = 0x1000u,
    kMsgConfirmReceipt = 0x2000u,
    kMsgImmediate = 0x00040000u,
    kMsgLockedGed = 0x40000000u,
};

/// The pairs the two attribute words translate through, one table serving
/// both directions so they cannot drift apart.
struct AttrPair {
    uint32_t jam;
    uint32_t msg;
};
constexpr AttrPair kAttrPairs[] = {
    {kJamLocal, kMsgLocal},
    {kJamPrivate, kMsgPrivate},
    {kJamRead, kMsgRead},
    {kJamSent, kMsgSent},
    {kJamKillSent, kMsgKillSent},
    {kJamHold, kMsgHold},
    {kJamCrash, kMsgCrash},
    {kJamFileRequest, kMsgFileRequest},
    {kJamFileAttach, kMsgFileAttach},
    {kJamInTransit, kMsgInTransit},
    {kJamReceiptRequest, kMsgReceiptRequest},
    {kJamConfirmRequest, kMsgConfirmReceipt},
    {kJamOrphan, kMsgOrphan},
    {kJamDirect, kMsgDirect},
    {kJamImmediate, kMsgImmediate},
    {kJamLocked, kMsgLockedGed},
};

uint32_t jamToMsgAttributes(uint32_t jam) {
    uint32_t out = 0;
    for (const auto& pair : kAttrPairs) {
        if ((jam & pair.jam) != 0) out |= pair.msg;
    }
    return out;
}

uint32_t msgToJamAttributes(uint32_t msg) {
    uint32_t out = 0;
    for (const auto& pair : kAttrPairs) {
        if ((msg & pair.msg) != 0) out |= pair.jam;
    }
    return out;
}

void parseAddressInto(const std::string& text, domain::FtnAddress& out) {
    if (const auto parsed = domain::FtnAddress::parse(text)) out = *parsed;
}

/// A 4D address the way the address subfields spell it. The domain is left
/// off deliberately: FtnAddress::toString() would include one, and what a
/// base stores is what INTL and the header fields carry — 4D.
std::string addressText(const domain::FtnAddress& address) {
    std::string out = std::to_string(address.zone) + ':' + std::to_string(address.net) +
                      '/' + std::to_string(address.node);
    if (address.point != 0) out += '.' + std::to_string(address.point);
    return out;
}

/// One control line as the reader shows it, appended to `control`.
void appendKludge(std::string& control, std::string_view name, std::string_view value) {
    control += kSoh;
    control.append(name);
    control.append(value);
    control += '\r';
}

/// Where a kludge goes when a message is stored: which subfield, and with how
/// much of its text stripped. JAM keeps the well-known kludges as data, not as
/// text — "MSGID: " goes in as subfield 4 holding only the value.
struct KludgeMapping {
    std::string_view prefix;
    uint16_t id;
};
/// What a subfield is called in JAM-001, for the report `i` shows. The ones
/// the driver acts on and the ones it passes over both: an info report is
/// opened precisely to find out what a base holds that a reader does not show.
std::string subfieldName(uint16_t id) {
    switch (id) {
        case kSubOrigAddress: return "OADDRESS";
        case kSubDestAddress: return "DADDRESS";
        case kSubSenderName: return "SENDERNAME";
        case kSubReceiverName: return "RECEIVERNAME";
        case kSubMsgId: return "MSGID";
        case kSubReplyId: return "REPLYID";
        case kSubSubject: return "SUBJECT";
        case kSubPid: return "PID";
        case kSubTrace: return "TRACE";
        case 9: return "ENCLOSEDFILE";
        case 10: return "ENCLOSEDFILEWALIAS";
        case 11: return "ENCLOSEDFREQ";
        case 12: return "ENCLOSEDFILEWCARD";
        case 13: return "ENCLOSEDINDIRECTFILE";
        case 1000: return "EMBINDAT";
        case kSubFtsKludge: return "FTSKLUDGE";
        case kSubSeenBy: return "SEENBY2D";
        case kSubPath: return "PATH2D";
        case kSubFlags: return "FLAGS";
        case kSubTzUtc: return "TZUTCINFO";
        default: return "?";
    }
}

constexpr KludgeMapping kKludgeMappings[] = {
    {"MSGID: ", kSubMsgId}, {"REPLY: ", kSubReplyId},  {"PID: ", kSubPid},
    {"Via ", kSubTrace},    {"FLAGS ", kSubFlags},     {"TZUTC: ", kSubTzUtc},
    {"PATH: ", kSubPath},   {"SEEN-BY: ", kSubSeenBy},
};

}  // namespace

tl::expected<void, ErrorPtr> JamBase::open(const std::string& path, bool echo,
                                           uint16_t /*defaultZone*/) {
    close();
    echo_ = echo;

    if (!headers_.open(path + ".jhr", true)) {
        return failure("cannot open " + path + ".jhr");
    }
    const bool writable = headers_.writable();
    if (!index_.open(path + ".jdx", writable) || !text_.open(path + ".jdt", writable)) {
        auto reason = "cannot open the index or text file of " + path;
        close();
        return failure(std::move(reason));
    }
    if (auto read = reload(); !read) {
        auto reason = std::move(read).error();
        close();
        return tl::make_unexpected(std::move(reason));
    }
    return {};
}

void JamBase::close() {
    headers_.close();
    index_.close();
    text_.close();
    info_ = Info{};
    active_.clear();
}

tl::expected<void, ErrorPtr> JamBase::create(const std::string& path) {
    close();

    const std::string headerPath = path + ".jhr";
    const std::string indexPath = path + ".jdx";
    const std::string textPath = path + ".jdt";

    // Whatever has been made when something fails, taken back again: half a
    // base on disk is worse than none, since probeType() would find it and
    // every attempt after this one would open it instead of making it.
    const auto giveBack = [&headerPath, &indexPath, &textPath] {
        std::error_code ec;
        std::filesystem::remove(headerPath, ec);
        std::filesystem::remove(indexPath, ec);
        std::filesystem::remove(textPath, ec);
    };

    // Both of the empty ones first, and the .jhr — the file probeType() looks
    // for, and the only one carrying anything — last: an interrupted creation
    // then leaves files no base claims rather than a JAM base missing the two
    // it is read through.
    BinaryFile index;
    BinaryFile text;
    if (!index.create(indexPath) || !text.create(textPath)) {
        // The errno the create left behind: "permission denied" or "no such
        // directory" is what the user has to act on, and it is the half of the
        // message that a bare path cannot say.
        auto reason = "cannot create the index or text file of " + path + ": " +
                      std::strerror(errno);
        index.close();
        text.close();
        giveBack();
        return failure(std::move(reason));
    }
    index.close();
    text.close();

    BinaryFile headers;
    if (!headers.create(headerPath)) {
        auto reason = "cannot create " + headerPath + ": " + std::strerror(errno);
        giveBack();
        return failure(std::move(reason));
    }

    std::array<unsigned char, kInfoSize> raw{};
    std::memcpy(raw.data(), kSignature, sizeof(kSignature));
    // When the area came into being, which is what the field is for and the one
    // thing about an empty base worth recording.
    writeU32(raw.data() + 4, static_cast<uint32_t>(std::time(nullptr)));
    // JAM-001 spells "no password" as a CRC of all ones rather than as a zero,
    // which is a legitimate CRC.
    writeU32(raw.data() + 16, 0xffffffffu);
    // The number the first message will carry. Packing moves this rather than
    // renumbering the records, so it is a base number and not a count.
    writeU32(raw.data() + 20, 1);

    if (const auto io = headers.writeAt(0, raw.data(), raw.size()); io.failed()) {
        auto reason = "cannot write the info block of " + headerPath;
        headers.close();
        giveBack();
        return failure(std::move(reason) + ": " + io.message());
    }
    return {};
}

tl::expected<void, ErrorPtr> JamBase::readInfo() {
    std::array<unsigned char, kInfoSize> raw{};
    if (const auto io = headers_.readAt(0, raw.data(), raw.size()); io.failed()) {
        return failure("cannot read the info block of " + headers_.path() + ": " +
                       io.message());
    }
    if (std::memcmp(raw.data(), kSignature, sizeof(kSignature)) != 0) {
        return failure(headers_.path() + " does not carry the JAM signature");
    }
    info_.dateCreated = readU32(raw.data() + 4);
    info_.modCounter = readU32(raw.data() + 8);
    info_.activeMessages = readU32(raw.data() + 12);
    info_.passwordCrc = readU32(raw.data() + 16);
    info_.baseMessageNumber = readU32(raw.data() + 20);
    return {};
}

tl::expected<void, ErrorPtr> JamBase::writeInfo() {
    // Read-modify-write: the reserved space is not ours.
    std::array<unsigned char, kInfoSize> raw{};
    if (const auto io = headers_.readAt(0, raw.data(), raw.size()); io.failed()) {
        return failure("cannot re-read the info block of " + headers_.path() + ": " +
                       io.message());
    }
    writeU32(raw.data() + 8, info_.modCounter);
    writeU32(raw.data() + 12, info_.activeMessages);
    if (const auto io = headers_.writeAt(0, raw.data(), raw.size()); io.failed()) {
        return failure("cannot write the info block of " + headers_.path() + ": " +
                       io.message());
    }
    return {};
}

tl::expected<void, ErrorPtr> JamBase::readHeaderAt(uint32_t offset, Header& out) const {
    std::array<unsigned char, kFixedHeaderSize> raw{};
    if (const auto io = headers_.readAt(offset, raw.data(), raw.size()); io.failed()) {
        return failure("cannot read the header at " + std::to_string(offset) + " in " +
                       headers_.path() + ": " + io.message());
    }
    if (std::memcmp(raw.data(), kSignature, sizeof(kSignature)) != 0) {
        return failure("no message header at " + std::to_string(offset) + " in " +
                       headers_.path());
    }
    out.subfieldLength = readU32(raw.data() + 8);
    out.timesRead = readU32(raw.data() + 12);
    out.msgIdCrc = readU32(raw.data() + 16);
    out.replyCrc = readU32(raw.data() + 20);
    out.replyTo = readU32(raw.data() + 24);
    out.replyFirst = readU32(raw.data() + 28);
    out.replyNext = readU32(raw.data() + 32);
    out.dateWritten = readU32(raw.data() + 36);
    out.dateReceived = readU32(raw.data() + 40);
    out.dateProcessed = readU32(raw.data() + 44);
    out.number = readU32(raw.data() + 48);
    out.attributes = readU32(raw.data() + 52);
    out.attributes2 = readU32(raw.data() + 56);
    out.textOffset = readU32(raw.data() + 60);
    out.textLength = readU32(raw.data() + 64);
    out.passwordCrc = readU32(raw.data() + 68);
    out.cost = readU32(raw.data() + 72);
    return {};
}

tl::expected<void, ErrorPtr> JamBase::loadActive() {
    active_.clear();

    const int64_t indexSize = index_.size();
    const int64_t headersSize = headers_.size();
    if (indexSize < 0 || headersSize < 0) {
        return failure("cannot size the files of " + headers_.path());
    }
    const auto records =
        static_cast<uint32_t>(static_cast<uint64_t>(indexSize) / kIndexRecordSize);
    if (records == 0) return {};

    std::vector<unsigned char> rawIndex(static_cast<size_t>(records) * kIndexRecordSize);
    if (const auto io = index_.readAt(0, rawIndex.data(), rawIndex.size()); io.failed()) {
        return failure("cannot read " + index_.path() + ": " + io.message());
    }

    // The headers, in one read where the file is modest. A header at a time
    // works either way; this makes opening a large area one seek, not one per
    // message.
    std::vector<unsigned char> rawHeaders;
    if (headersSize <= kHeadersInCoreLimit) {
        rawHeaders.resize(static_cast<size_t>(headersSize));
        if (const auto io = headers_.readAt(0, rawHeaders.data(), rawHeaders.size());
            io.failed()) {
            return failure("cannot read " + headers_.path() + ": " + io.message());
        }
    }

    active_.reserve(std::min(records, info_.activeMessages));
    for (uint32_t record = 0; record < records; ++record) {
        const unsigned char* raw =
            rawIndex.data() + (static_cast<size_t>(record) * kIndexRecordSize);
        const uint32_t headerOffset = readU32(raw + 4);
        if (headerOffset == kNoRecord) continue;  // a deleted message's record
        if (static_cast<uint64_t>(headerOffset) + kFixedHeaderSize >
            static_cast<uint64_t>(headersSize)) {
            continue;  // the index points past the header file: not a message
        }

        ActiveMessage message;
        message.indexRecord = record;
        message.headerOffset = headerOffset;
        if (!rawHeaders.empty()) {
            if (std::memcmp(rawHeaders.data() + headerOffset, kSignature,
                            sizeof(kSignature)) != 0) {
                continue;
            }
            const unsigned char* h = rawHeaders.data() + headerOffset;
            message.header.subfieldLength = readU32(h + 8);
            message.header.timesRead = readU32(h + 12);
            message.header.msgIdCrc = readU32(h + 16);
            message.header.replyCrc = readU32(h + 20);
            message.header.replyTo = readU32(h + 24);
            message.header.replyFirst = readU32(h + 28);
            message.header.replyNext = readU32(h + 32);
            message.header.dateWritten = readU32(h + 36);
            message.header.dateReceived = readU32(h + 40);
            message.header.dateProcessed = readU32(h + 44);
            message.header.number = readU32(h + 48);
            message.header.attributes = readU32(h + 52);
            message.header.attributes2 = readU32(h + 56);
            message.header.textOffset = readU32(h + 60);
            message.header.textLength = readU32(h + 64);
            message.header.passwordCrc = readU32(h + 68);
            message.header.cost = readU32(h + 72);
        } else if (!readHeaderAt(headerOffset, message.header)) {
            continue;  // a torn header ends the message, not the area
        }
        if ((message.header.attributes & kJamDeleted) != 0) continue;
        active_.push_back(message);
    }
    return {};
}

tl::expected<void, ErrorPtr> JamBase::reload() {
    auto read = readInfo();
    if (!read) return tl::make_unexpected(std::move(read).error());
    return loadActive();
}

uint32_t JamBase::uidOfEntry(const ActiveMessage& message) const {
    return message.indexRecord + info_.baseMessageNumber;
}

uint32_t JamBase::uidOf(uint32_t index) const {
    if (index == 0 || index > count()) return 0;
    return uidOfEntry(active_[index - 1]);
}

uint32_t JamBase::indexOfUid(uint32_t uid, bool exact) const {
    if (uid == 0 || active_.empty()) return 0;

    // Index records never move, so the active table is sorted by record number
    // and therefore by UID.
    const auto past =
        std::upper_bound(active_.begin(), active_.end(), uid,
                         [this](uint32_t value, const ActiveMessage& entry) {
                             return value < uidOfEntry(entry);
                         });
    const auto position = static_cast<uint32_t>(std::distance(active_.begin(), past));
    if (position == 0) return 0;
    if (exact && uidOfEntry(active_[position - 1]) != uid) return 0;
    return position;
}

tl::expected<void, ErrorPtr> JamBase::readSubfields(const ActiveMessage& message,
                                                    std::vector<Subfield>& out) const {
    out.clear();
    if (message.header.subfieldLength == 0) return {};

    std::vector<unsigned char> raw(message.header.subfieldLength);
    if (const auto io = headers_.readAt(message.headerOffset + kFixedHeaderSize,
                                        raw.data(), raw.size());
        io.failed()) {
        return failure("cannot read the subfields at " +
                       std::to_string(message.headerOffset) + ": " + io.message());
    }

    size_t pos = 0;
    while (pos + kSubfieldHeaderSize <= raw.size()) {
        const uint16_t id = readU16(raw.data() + pos);
        const uint32_t length = readU32(raw.data() + pos + 4);
        pos += kSubfieldHeaderSize;
        if (length > raw.size() - pos) break;  // a torn subfield ends the list
        Subfield field;
        field.id = id;
        field.data.assign(reinterpret_cast<const char*>(raw.data() + pos), length);
        out.push_back(std::move(field));
        pos += length;
    }
    return {};
}

tl::expected<void, ErrorPtr> JamBase::read(uint32_t index, RawMessage& out,
                                           bool withText) const {
    if (index == 0 || index > count()) {
        return failure("message " + std::to_string(index) + " is not in the area");
    }
    const ActiveMessage& message = active_[index - 1];
    const Header& header = message.header;

    out.header = RawHeader{};
    out.header.attributes = jamToMsgAttributes(header.attributes);
    // JAM counts the reads rather than marking them, so any count at all is
    // the mark: JAM-001 has no bit for it, and every reader that keeps this
    // field keeps it the same way.
    out.header.seen = header.timesRead != 0;
    out.header.written = fromUnixStamp(header.dateWritten);
    out.header.arrived = fromUnixStamp(header.dateProcessed);
    out.header.replyTo = header.replyTo;
    // The answers are a chain — the message names its first, each answer names
    // the next answer to the same message — and walking it here costs memory
    // lookups only, the headers being in the active table. The cap is because
    // nothing guarantees the chain ends: a base written by something with a
    // bug in it would otherwise be read for ever.
    constexpr int kMaxChain = 64;
    uint32_t link = header.replyFirst;
    for (int step = 0; step < kMaxChain && link != 0; ++step) {
        out.header.replies.push_back(link);
        const uint32_t position = indexOfUid(link, true);
        if (position == 0) break;
        link = active_[position - 1].header.replyNext;
    }

    std::vector<Subfield> subfields;
    auto done = readSubfields(message, subfields);
    if (!done) return tl::make_unexpected(std::move(done).error());

    // The subfields carry what other formats keep in the header and in the
    // kludges both. The header fields come first; the kludges are rebuilt in
    // subfield order, leading service lines into `control` and the trailing
    // ones — Via, SEEN-BY, PATH, which stand after the origin — onto the end
    // of the text.
    std::string trailing;
    for (const auto& field : subfields) {
        switch (field.id) {
            case kSubSenderName: out.header.from = field.data; break;
            case kSubReceiverName: out.header.to = field.data; break;
            case kSubSubject: out.header.subject = field.data; break;
            case kSubOrigAddress:
                parseAddressInto(field.data, out.header.origAddr);
                break;
            case kSubDestAddress:
                parseAddressInto(field.data, out.header.destAddr);
                break;
            case kSubMsgId: appendKludge(out.control, "MSGID: ", field.data); break;
            case kSubReplyId: appendKludge(out.control, "REPLY: ", field.data); break;
            case kSubPid: appendKludge(out.control, "PID: ", field.data); break;
            case kSubTzUtc: appendKludge(out.control, "TZUTC: ", field.data); break;
            case kSubFlags: appendKludge(out.control, "FLAGS ", field.data); break;
            case kSubTrace: appendKludge(trailing, "Via ", field.data); break;
            case kSubPath: appendKludge(trailing, "PATH: ", field.data); break;
            case kSubSeenBy:
                // SEEN-BY is stored without a ^A everywhere, this format
                // included.
                trailing.append("SEEN-BY: ").append(field.data).append(1, '\r');
                break;
            case kSubFtsKludge:
                if (startsWith(field.data, "Via") || startsWith(field.data, "Recd")) {
                    appendKludge(trailing, "", field.data);
                } else {
                    appendKludge(out.control, "", field.data);
                }
                break;
            default: break;  // file attaches and the like: nothing to show
        }
    }

    // JAM keeps no INTL/FMPT/TOPT — their data lives in the address subfields
    // — but the message went out with them and comes back the same way.
    if (!echo_ && out.header.origAddr.isValid() && out.header.destAddr.isValid()) {
        // INTL carries the two addresses 3D, the points going to FMPT/TOPT.
        const auto threeD = [](const domain::FtnAddress& address) {
            domain::FtnAddress out3d = address;
            out3d.point = 0;
            return addressText(out3d);
        };
        std::string synthesized;
        appendKludge(synthesized, "INTL ",
                     threeD(out.header.destAddr) + " " + threeD(out.header.origAddr));
        if (out.header.origAddr.point != 0) {
            appendKludge(synthesized, "FMPT ", std::to_string(out.header.origAddr.point));
        }
        if (out.header.destAddr.point != 0) {
            appendKludge(synthesized, "TOPT ", std::to_string(out.header.destAddr.point));
        }
        out.control.insert(0, synthesized);
    }

    out.text.clear();
    if (withText) {
        if (header.textLength != 0) {
            out.text.assign(header.textLength, '\0');
            if (const auto io =
                    text_.readAt(header.textOffset, &out.text[0], out.text.size());
                io.failed()) {
                return failure("cannot read the text of message " +
                               std::to_string(index) + ": " + io.message());
            }
        }
        if (!out.text.empty() && out.text.back() != '\r') out.text += '\r';
        out.text += trailing;
    }
    return {};
}

domain::MessageInfo JamBase::info(uint32_t index) const {
    domain::MessageInfo out;
    if (index == 0 || index > count()) return out;

    const ActiveMessage& message = active_[index - 1];
    const Header& header = message.header;
    out.title = "JAM message " + std::to_string(index) + " of " + std::to_string(count());

    // A JAM stamp is Unix time in local time, so both spellings are worth
    // having: what it says, and the number a specification is checked against.
    const auto when = [](uint32_t seconds) {
        return report::stamp(fromUnixStamp(seconds)) + " (" + report::hex(seconds) + ")";
    };

    // The record itself, read once: the two fields the driver has no use for —
    // the revision and the word reserved beside it — are read out of it, and
    // the dump at the end of the report is the same bytes.
    const std::string stored =
        report::readDump(headers_, message.headerOffset, kFixedHeaderSize);
    const auto* record = reinterpret_cast<const unsigned char*>(stored.data());
    const bool haveRecord = stored.size() == kFixedHeaderSize;

    // The fields, in the order and under the names GoldED+ prints them, so that
    // a report read here and one read there are the same report.
    domain::MessageInfoBlock fixed;
    fixed.fields = {
        report::field("Msgbase", headers_.path()),
        report::field("Signature", haveRecord ? stored.substr(0, 3) : std::string{}),
        report::field("Revision", haveRecord ? std::to_string(readU16(record + 4)) : ""),
        report::field("ReservedWord",
                      haveRecord ? std::to_string(readU16(record + 6)) : ""),
        report::field("SubfieldLen", std::to_string(header.subfieldLength)),
        report::field("TimesRead", std::to_string(header.timesRead)),
        report::field("MSGIDcrc", report::hex(header.msgIdCrc)),
        report::field("REPLYcrc", report::hex(header.replyCrc)),
        report::field("ReplyTo", std::to_string(header.replyTo)),
        report::field("Reply1st", std::to_string(header.replyFirst)),
        report::field("ReplyNext", std::to_string(header.replyNext)),
        report::field("DateWritten", when(header.dateWritten)),
        report::field("DateReceived", when(header.dateReceived)),
        report::field("DateProcessed", when(header.dateProcessed)),
        report::field("MessageNumber", std::to_string(header.number)),
        report::field("Attribute", report::hex(header.attributes) + " (" +
                                       report::bits(header.attributes) + ")"),
        report::field("Attribute2", report::hex(header.attributes2) + " (" +
                                        report::bits(header.attributes2) + ")"),
        // Not one of GoldED+'s: JAM keeps its own attribute bits and the driver
        // translates, so an attribute on screen and a bit in the base are two
        // different words and the report says both.
        report::field("As FTS-0001", report::hex(jamToMsgAttributes(header.attributes))),
        report::field("Offset", report::hexAndDecimal(header.textOffset)),
        report::field("TxtLen", std::to_string(header.textLength)),
        report::field("PasswordCRC", report::hex(header.passwordCrc)),
        report::field("Cost", std::to_string(header.cost)),
        report::field("HdrOffset", report::hexAndDecimal(message.headerOffset)),
    };
    out.blocks.push_back(std::move(fixed));

    // The index record is where the message number comes from — its position
    // plus BaseMsgNum — so the position is worth stating beside what it holds.
    domain::MessageInfoBlock indexBlock;
    indexBlock.title = "Index Record:";
    indexBlock.fields = {
        report::field("Record", std::to_string(message.indexRecord)),
        report::field("Uid", std::to_string(uidOfEntry(message))),
    };
    std::array<unsigned char, kIndexRecordSize> rawIndex{};
    if (index_
            .readAt(static_cast<uint64_t>(message.indexRecord) * kIndexRecordSize,
                    rawIndex.data(), rawIndex.size())
            .ok()) {
        indexBlock.fields.push_back(
            report::field("UserCrc", report::hex(readU32(rawIndex.data()))));
        indexBlock.fields.push_back(report::field(
            "HeaderOffset", report::hexAndDecimal(readU32(rawIndex.data() + 4))));
    }
    out.blocks.push_back(std::move(indexBlock));

    domain::MessageInfoBlock base;
    base.title = "Base Header:";
    base.fields = {
        report::field("DateCreated", when(info_.dateCreated)),
        report::field("ModCounter", std::to_string(info_.modCounter)),
        report::field("ActiveMsgs", std::to_string(info_.activeMessages)),
        report::field("PasswordCRC", report::hex(info_.passwordCrc)),
        report::field("BaseMsgNum", std::to_string(info_.baseMessageNumber)),
    };
    // The mark the last scan reached. Nothing in AmberEdit reads it — the
    // lastread files are what a reader keeps — so it is read here rather than
    // held in the info block the driver works from.
    std::array<unsigned char, 4> highWater{};
    if (headers_.readAt(kHighWaterOffset, highWater.data(), highWater.size()).ok()) {
        base.fields.push_back(
            report::field("HighWaterMark", std::to_string(readU32(highWater.data()))));
    }
    out.blocks.push_back(std::move(base));

    // The subfields are where a JAM message keeps everything the other formats
    // put in a header field or a kludge, so they are the half of the report
    // worth reading: one line each, in the order the base stores them, each
    // under the number and the name JAM-001 gives it.
    std::vector<Subfield> subfields;
    if (readSubfields(message, subfields)) {
        domain::MessageInfoBlock block;
        block.title = "Subfields:";
        for (const auto& field : subfields) {
            block.fields.push_back(report::textField(
                report::padded(field.id, 5) + " " + subfieldName(field.id), field.data));
        }
        if (subfields.empty()) block.fields.push_back(report::field("(none)", ""));
        out.blocks.push_back(std::move(block));
    }

    domain::MessageInfoBlock dump;
    dump.title =
        report::dumpTitle("Message header (JAMHDR)", kFixedHeaderSize, stored.size());
    dump.bytes = stored;
    out.blocks.push_back(std::move(dump));

    if (header.subfieldLength != 0) {
        domain::MessageInfoBlock block;
        block.bytes = report::readDump(headers_, message.headerOffset + kFixedHeaderSize,
                                       header.subfieldLength);
        block.title = report::dumpTitle("Subfield block", header.subfieldLength,
                                        block.bytes.size());
        out.blocks.push_back(std::move(block));
    }
    if (header.textLength != 0) {
        domain::MessageInfoBlock text;
        text.bytes = report::readDump(text_, header.textOffset, header.textLength);
        text.title =
            report::dumpTitle("Message text", header.textLength, text.bytes.size());
        out.blocks.push_back(std::move(text));
    }
    return out;
}

void JamBase::encodeDraft(const RawDraft& draft, Header& header,
                          std::string& subfieldBlock, std::string& text) const {
    // The subfields: names and subject always, the addresses in netmail — an
    // echomail message is broadcast and its address subfields would only
    // repeat the AKA — and then the kludges. INTL, FMPT and TOPT are never
    // stored: the format is explicit that their data belongs to the address
    // subfields, and they are put back on reading.
    std::vector<Subfield> subfields;
    const auto add = [&subfields](uint16_t id, std::string data) {
        Subfield field;
        field.id = id;
        field.data = std::move(data);
        subfields.push_back(std::move(field));
    };
    add(kSubSenderName, draft.header.from);
    add(kSubReceiverName, draft.header.to);
    add(kSubSubject, draft.header.subject);
    if (!echo_) {
        if (draft.header.origAddr.isValid()) {
            add(kSubOrigAddress, addressText(draft.header.origAddr));
        }
        if (draft.header.destAddr.isValid()) {
            add(kSubDestAddress, addressText(draft.header.destAddr));
        }
    }
    const auto addKludge = [&](std::string_view kludge) {
        // INTL, FMPT and TOPT are never stored: their data lives in the
        // address subfields, and the reader puts them back.
        if (startsWith(kludge, "INTL ") || startsWith(kludge, "FMPT ") ||
            startsWith(kludge, "TOPT ")) {
            return;
        }
        for (const auto& mapping : kKludgeMappings) {
            if (startsWith(kludge, mapping.prefix)) {
                add(mapping.id, std::string(kludge.substr(mapping.prefix.size())));
                if (mapping.id == kSubMsgId) {
                    header.msgIdCrc = jamCrc32(subfields.back().data);
                } else if (mapping.id == kSubReplyId) {
                    header.replyCrc = jamCrc32(subfields.back().data);
                }
                return;
            }
        }
        add(kSubFtsKludge, std::string(kludge));
    };
    for (const auto& kludge : draft.kludges) addKludge(kludge);

    // SEEN-BY and PATH stand in the text of every other format, and a message
    // passed on may carry them there; JAM stores them apart, so they are
    // lifted out the way its own tossers do.
    text.clear();
    text.reserve(draft.text.size());
    size_t pos = 0;
    while (pos <= draft.text.size()) {
        size_t lineEnd = draft.text.find('\r', pos);
        const bool last = lineEnd == std::string::npos;
        if (last) lineEnd = draft.text.size();
        const std::string_view line(draft.text.data() + pos, lineEnd - pos);
        if (startsWith(line, "SEEN-BY: ")) {
            add(kSubSeenBy, std::string(line.substr(9)));
        } else if (!line.empty() && line.front() == kSoh) {
            addKludge(line.substr(1));
        } else if (!last || !line.empty()) {
            text.append(line);
            text += '\r';
        }
        if (last) break;
        pos = lineEnd + 1;
    }

    subfieldBlock.clear();
    for (const auto& field : subfields) {
        std::array<unsigned char, kSubfieldHeaderSize> raw{};
        writeU16(raw.data(), field.id);
        writeU32(raw.data() + 4, static_cast<uint32_t>(field.data.size()));
        subfieldBlock.append(reinterpret_cast<const char*>(raw.data()), raw.size());
        subfieldBlock.append(field.data);
    }
    header.subfieldLength = static_cast<uint32_t>(subfieldBlock.size());
}

tl::expected<void, ErrorPtr> JamBase::writeHeaderAt(uint32_t offset, const Header& header,
                                                    const std::string& subfields) {
    std::array<unsigned char, kFixedHeaderSize> rawHeader{};
    std::memcpy(rawHeader.data(), kSignature, sizeof(kSignature));
    writeU16(rawHeader.data() + 4, 1);  // revision
    writeU32(rawHeader.data() + 8, header.subfieldLength);
    writeU32(rawHeader.data() + 12, header.timesRead);
    writeU32(rawHeader.data() + 16, header.msgIdCrc);
    writeU32(rawHeader.data() + 20, header.replyCrc);
    writeU32(rawHeader.data() + 24, header.replyTo);
    writeU32(rawHeader.data() + 28, header.replyFirst);
    writeU32(rawHeader.data() + 32, header.replyNext);
    writeU32(rawHeader.data() + 36, header.dateWritten);
    writeU32(rawHeader.data() + 40, header.dateReceived);
    writeU32(rawHeader.data() + 44, header.dateProcessed);
    writeU32(rawHeader.data() + 48, header.number);
    writeU32(rawHeader.data() + 52, header.attributes);
    writeU32(rawHeader.data() + 56, header.attributes2);
    writeU32(rawHeader.data() + 60, header.textOffset);
    writeU32(rawHeader.data() + 64, header.textLength);
    writeU32(rawHeader.data() + 68, header.passwordCrc);
    writeU32(rawHeader.data() + 72, header.cost);

    if (headers_.writeAt(offset, rawHeader.data(), rawHeader.size()).failed() ||
        headers_
            .writeAt(static_cast<uint64_t>(offset) + kFixedHeaderSize, subfields.data(),
                     subfields.size())
            .failed()) {
        return failure("cannot write the message header at " + std::to_string(offset) +
                       " in " + headers_.path());
    }
    return {};
}

tl::expected<void, ErrorPtr> JamBase::writeIndexRecord(uint32_t record,
                                                       const std::string& to,
                                                       uint32_t headerOffset) {
    std::array<unsigned char, kIndexRecordSize> raw{};
    writeU32(raw.data(), jamCrc32(to));
    writeU32(raw.data() + 4, headerOffset);
    if (const auto io = index_.writeAt(static_cast<uint64_t>(record) * kIndexRecordSize,
                                       raw.data(), raw.size());
        io.failed()) {
        return failure("cannot write record " + std::to_string(record) + " of " +
                       index_.path() + ": " + io.message());
    }
    return {};
}

tl::expected<uint32_t, ErrorPtr> JamBase::write(const RawDraft& draft) {
    if (!headers_.isOpen()) {
        return failure<MsgBaseError>(MsgBaseError::Kind::NoAreaOpen, std::string());
    }
    if (!headers_.writable() || !index_.writable() || !text_.writable()) {
        return failure("the base at " + headers_.path() + " is not ours to write");
    }

    // All three files, before anything the write depends on is read: the
    // counters and both files' lengths come off the disk under the lock.
    FileLock lock;
    if (const auto locked = lock.acquire({&headers_, &index_, &text_}); !locked) {
        return failure<MsgBaseError>(MsgBaseError::Kind::BaseBusy,
                                     locked.error()->message());
    }
    auto done = reload();
    if (!done) return tl::make_unexpected(std::move(done).error());

    Header header;
    header.attributes = msgToJamAttributes(draft.header.attributes) |
                        (echo_ ? kJamTypeEcho : kJamTypeNet);
    header.dateWritten = toUnixStamp(draft.header.written);
    header.dateProcessed = toUnixStamp(draft.header.arrived);
    header.replyTo = draft.header.replyTo;
    header.timesRead = 0;

    std::string subfieldBlock;
    std::string text;
    encodeDraft(draft, header, subfieldBlock, text);

    const int64_t headerEnd = headers_.size();
    const int64_t indexEnd = index_.size();
    const int64_t textEnd = text_.size();
    if (headerEnd < 0 || indexEnd < 0 || textEnd < 0) {
        return failure("cannot size the files of " + headers_.path());
    }

    const auto record =
        static_cast<uint32_t>(static_cast<uint64_t>(indexEnd) / kIndexRecordSize);
    header.number = record + info_.baseMessageNumber;
    header.textOffset = static_cast<uint32_t>(textEnd);
    header.textLength = static_cast<uint32_t>(text.size());

    if (!text.empty() &&
        text_.writeAt(static_cast<uint64_t>(textEnd), text.data(), text.size())
            .failed()) {
        return failure("cannot write the text of the new message");
    }
    if (!writeHeaderAt(static_cast<uint32_t>(headerEnd), header, subfieldBlock)) {
        (void)headers_.truncate(static_cast<uint64_t>(headerEnd));
        (void)text_.truncate(static_cast<uint64_t>(textEnd));
        return 0;
    }

    // The index record last: it is what makes the message visible, so a write
    // that dies before this point leaves a header nothing refers to rather
    // than a message with half a header.
    if (!writeIndexRecord(record, draft.header.to, static_cast<uint32_t>(headerEnd))) {
        (void)headers_.truncate(static_cast<uint64_t>(headerEnd));
        (void)text_.truncate(static_cast<uint64_t>(textEnd));
        return 0;
    }

    info_.activeMessages += 1;
    info_.modCounter += 1;
    auto done2 = writeInfo();
    if (!done2) return tl::make_unexpected(std::move(done2).error());

    ActiveMessage message;
    message.indexRecord = record;
    message.headerOffset = static_cast<uint32_t>(headerEnd);
    message.header = header;
    active_.push_back(message);
    return count();
}

tl::expected<void, ErrorPtr> JamBase::replace(uint32_t index, const RawDraft& draft) {
    if (!headers_.isOpen()) {
        return failure<MsgBaseError>(MsgBaseError::Kind::NoAreaOpen, std::string());
    }
    if (!headers_.writable() || !index_.writable() || !text_.writable()) {
        return failure("the base at " + headers_.path() + " is not ours to write");
    }

    FileLock lock;
    if (const auto locked = lock.acquire({&headers_, &index_, &text_}); !locked) {
        return failure<MsgBaseError>(MsgBaseError::Kind::BaseBusy,
                                     locked.error()->message());
    }
    auto done = reload();
    if (!done) return tl::make_unexpected(std::move(done).error());

    if (index == 0 || index > count()) {
        return failure("message " + std::to_string(index) + " is not there to change");
    }
    const ActiveMessage message = active_[index - 1];

    // Everything the message keeps is what it is read back off its own header:
    // its number — which is its UID, and its index record with it — the stamps
    // the transport gave it, the chain of answers it stands in, and how often it
    // has been read. Only the stamp it is dated by moves: a message written
    // again is written now.
    Header header;
    auto done2 = readHeaderAt(message.headerOffset, header);
    if (!done2) return tl::make_unexpected(std::move(done2).error());
    header.dateWritten = toUnixStamp(draft.header.written);
    header.attributes = msgToJamAttributes(draft.header.attributes) |
                        (echo_ ? kJamTypeEcho : kJamTypeNet);
    // The kludges are written afresh, so the two CRCs they key are as well: a
    // message whose MSGID has gone must not still be found by it.
    header.msgIdCrc = 0xffffffffu;
    header.replyCrc = 0xffffffffu;

    std::string subfieldBlock;
    std::string text;
    encodeDraft(draft, header, subfieldBlock, text);

    const int64_t headerEnd = headers_.size();
    const int64_t textEnd = text_.size();
    if (headerEnd < 0 || textEnd < 0) {
        return failure("cannot size the files of " + headers_.path());
    }

    // The text goes back where it was as long as it still fits there — the
    // format keeps no free list, so anything else is written at the end and
    // what it left is a packer's to reclaim.
    const bool textMoves = text.size() > message.header.textLength;
    header.textOffset =
        textMoves ? static_cast<uint32_t>(textEnd) : message.header.textOffset;
    header.textLength = static_cast<uint32_t>(text.size());
    if (!text.empty() &&
        text_.writeAt(header.textOffset, text.data(), text.size()).failed()) {
        return failure("cannot write the text of message " + std::to_string(index));
    }

    // The header the same way, but only where the subfields take exactly the
    // room they took: what stands after a header in the .jhr is the next
    // message's, and a subfield block a byte longer would be written over it.
    const bool headerMoves = subfieldBlock.size() != message.header.subfieldLength;
    const uint32_t headerOffset =
        headerMoves ? static_cast<uint32_t>(headerEnd) : message.headerOffset;
    if (auto written = writeHeaderAt(headerOffset, header, subfieldBlock); !written) {
        if (headerMoves) (void)headers_.truncate(static_cast<uint64_t>(headerEnd));
        return tl::make_unexpected(std::move(written).error());
    }

    // The index record: where the header now is, and the CRC of the name the
    // message is addressed to, which may have changed with it. Its position is
    // the message's number, and that never changes.
    if (auto written =
            writeIndexRecord(message.indexRecord, draft.header.to, headerOffset);
        !written) {
        if (headerMoves) (void)headers_.truncate(static_cast<uint64_t>(headerEnd));
        return tl::make_unexpected(std::move(written).error());
    }

    if (headerMoves) {
        // Nothing points at the header left behind any more. Marking it deleted
        // is what tells a packer to drop it; its text is not dropped with it,
        // the live header holding whatever of it is still in use.
        std::array<unsigned char, kFixedHeaderSize> raw{};
        if (const auto io = headers_.readAt(message.headerOffset, raw.data(), raw.size());
            io.failed()) {
            return failure("cannot re-read the old header of message " +
                           std::to_string(index) + ": " + io.message());
        }
        writeU32(raw.data() + 52, readU32(raw.data() + 52) | kJamDeleted);
        writeU32(raw.data() + 64, 0);  // TxtLen: the new header owns the text
        if (const auto io =
                headers_.writeAt(message.headerOffset, raw.data(), raw.size());
            io.failed()) {
            return failure("cannot mark the old header of message " +
                           std::to_string(index) + " deleted: " + io.message());
        }
    }

    info_.modCounter += 1;
    auto done3 = writeInfo();
    if (!done3) return tl::make_unexpected(std::move(done3).error());

    active_[index - 1].headerOffset = headerOffset;
    active_[index - 1].header = header;
    return {};
}

tl::expected<void, ErrorPtr> JamBase::remove(uint32_t index) {
    if (!headers_.isOpen()) {
        return failure<MsgBaseError>(MsgBaseError::Kind::NoAreaOpen, std::string());
    }
    if (!headers_.writable() || !index_.writable()) {
        return failure("the base at " + headers_.path() + " is not ours to write");
    }

    FileLock lock;
    if (const auto locked = lock.acquire({&headers_, &index_, &text_}); !locked) {
        return failure<MsgBaseError>(MsgBaseError::Kind::BaseBusy,
                                     locked.error()->message());
    }
    auto done = reload();
    if (!done) return tl::make_unexpected(std::move(done).error());

    if (index == 0 || index > count()) {
        return failure("message " + std::to_string(index) + " is not there to delete");
    }
    const ActiveMessage message = active_[index - 1];

    // A JAM delete moves nothing: the header stays where it is, marked
    // deleted with no text, and the index record is blanked. The text is left
    // for a packer — the format keeps no free list to give it to.
    std::array<unsigned char, kFixedHeaderSize> raw{};
    if (const auto io = headers_.readAt(message.headerOffset, raw.data(), raw.size());
        io.failed()) {
        return failure("cannot re-read the header of message " + std::to_string(index) +
                       ": " + io.message());
    }
    writeU32(raw.data() + 52, readU32(raw.data() + 52) | kJamDeleted);
    writeU32(raw.data() + 64, 0);  // TxtLen, so a packer does not keep the text
    if (const auto io = headers_.writeAt(message.headerOffset, raw.data(), raw.size());
        io.failed()) {
        return failure("cannot mark message " + std::to_string(index) +
                       " deleted: " + io.message());
    }

    std::array<unsigned char, kIndexRecordSize> rawIndex{};
    writeU32(rawIndex.data(), kNoRecord);
    writeU32(rawIndex.data() + 4, kNoRecord);
    if (const auto io =
            index_.writeAt(static_cast<uint64_t>(message.indexRecord) * kIndexRecordSize,
                           rawIndex.data(), rawIndex.size());
        io.failed()) {
        return failure("cannot blank the index record of message " +
                       std::to_string(index) + ": " + io.message());
    }

    if (info_.activeMessages != 0) info_.activeMessages -= 1;
    info_.modCounter += 1;
    auto done2 = writeInfo();
    if (!done2) return tl::make_unexpected(std::move(done2).error());

    active_.erase(active_.begin() + static_cast<long>(index) - 1);
    return {};
}

tl::expected<void, ErrorPtr> JamBase::markSeen(uint32_t index) {
    if (!headers_.isOpen()) {
        return failure<MsgBaseError>(MsgBaseError::Kind::NoAreaOpen, std::string());
    }
    if (!headers_.writable()) {
        return failure("the base at " + headers_.path() + " is not ours to write");
    }

    // Already marked, as far as the table read when the area was opened knows —
    // and that is enough to answer with, since nothing takes the mark off again:
    // a message another task has marked meanwhile is marked either way, and the
    // point of asking here is to open a message without touching the disk in the
    // ordinary case, where it has been read before.
    if (index != 0 && index <= count() && active_[index - 1].header.timesRead != 0) {
        return {};
    }

    FileLock lock;
    if (const auto locked = lock.acquire({&headers_, &index_, &text_}); !locked) {
        return failure<MsgBaseError>(MsgBaseError::Kind::BaseBusy,
                                     locked.error()->message());
    }
    auto done = reload();
    if (!done) return tl::make_unexpected(std::move(done).error());

    if (index == 0 || index > count()) {
        return failure("message " + std::to_string(index) + " is not there to mark");
    }
    const ActiveMessage& message = active_[index - 1];

    // Read back, patched and written whole, as the delete above does it: the
    // dword at +12 is the only one that changes, and the rest of the record goes
    // back exactly as it came.
    std::array<unsigned char, kFixedHeaderSize> raw{};
    if (const auto io = headers_.readAt(message.headerOffset, raw.data(), raw.size());
        io.failed()) {
        return failure("cannot re-read the header of message " + std::to_string(index) +
                       ": " + io.message());
    }
    const uint32_t timesRead = readU32(raw.data() + 12);
    if (timesRead != 0) {
        active_[index - 1].header.timesRead = timesRead;
        return {};
    }
    writeU32(raw.data() + 12, 1);
    if (const auto io = headers_.writeAt(message.headerOffset, raw.data(), raw.size());
        io.failed()) {
        return failure("cannot mark message " + std::to_string(index) +
                       " read: " + io.message());
    }
    active_[index - 1].header.timesRead = 1;
    return {};
}

}  // namespace amberedit::msgbase
