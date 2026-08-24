#include "msgbase/squish_base.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <system_error>

#include "config/text_util.hpp"
#include "msgbase/byte_order.hpp"
#include "msgbase/file_lock.hpp"
#include "msgbase/info_format.hpp"

namespace amberedit::msgbase {

namespace {

using bytes::readI16;
using bytes::readU16;
using bytes::readU32;
using bytes::writeU16;
using bytes::writeU32;

constexpr uint32_t kFrameId = 0xafae4453u;
constexpr size_t kBaseHeaderSize = 256;
constexpr size_t kFrameHeaderSize = 28;
constexpr size_t kIndexRecordSize = 12;
/// The message header inside a frame: 4 + 36 + 36 + 72 + 8 + 8 + 4 + 4 + 2 +
/// 4 + 36 + 4 + 20.
constexpr size_t kMessageHeaderSize = 238;

constexpr uint16_t kFrameNormal = 0;
constexpr uint16_t kFrameFree = 1;
constexpr uint16_t kFrameUpdate = 3;

/// The attribute bit saying that the frame's `umsgid` field is filled in.
/// SQFIX rebuilds a lost index out of it, so a message written without it is a
/// message that cannot be recovered.
constexpr uint32_t kAttrHasUid = 0x00020000u;
constexpr uint32_t kAttrRead = 0x00000004u;

/// `MSGSEEN` — the bit a reader sets on a message it has shown, which is what
/// the message list goes by. It is not one of the message's own attributes and is
/// deliberately kept out of `RawHeader::attributes`: it belongs to this system
/// rather than to the message, and it travels in `RawHeader::seen` with JAM's
/// TimesRead and the Fido *.msg word, which say the same thing in a field.
constexpr uint32_t kAttrSeen = 0x00080000u;

/// The high bit of an index record's hash, mirroring the READ attribute so
/// that "has this been read" can be answered from the index alone.
constexpr uint32_t kHashRead = 0x80000000u;

/// The base's own name in the area header, eighty bytes at offset 24. Nothing
/// in AmberEdit reads it; SQFIX and the husky tools do, which is why an area
/// created here fills it in.
constexpr size_t kBaseNameSize = 80;

constexpr size_t kFromSize = 36;
constexpr size_t kToSize = 36;
constexpr size_t kSubjectSize = 72;
constexpr size_t kFtscDateSize = 20;
constexpr size_t kMaxReplies = 9;

/// The hash of the To: name an index record carries, hashpjw as the Squish
/// format defines it. Only A-Z are folded: the specification says the
/// locale-dependent `tolower` it once used is deprecated and recommends exactly
/// this, and it is what every base written under the C locale already holds.
uint32_t squishHash(std::string_view name) {
    uint32_t hash = 0;
    for (const char ch : name) {
        const auto folded = static_cast<unsigned char>(config::text::asciiLower(ch));
        hash = (hash << 4) + folded;
        if (const uint32_t high = hash & 0xf0000000u) {
            hash |= high >> 24;
            hash |= high;
        }
    }
    return hash & 0x7fffffffu;
}

void decodeAddress(const unsigned char* field, domain::FtnAddress& out) {
    out.zone = readU16(field);
    out.net = readU16(field + 2);
    out.node = readU16(field + 4);
    out.point = readU16(field + 6);
}

void encodeAddress(unsigned char* field, const domain::FtnAddress& address) {
    writeU16(field, address.zone);
    writeU16(field + 2, address.net);
    writeU16(field + 4, address.node);
    writeU16(field + 6, address.point);
}

void decodeMessageHeader(const unsigned char* raw, RawHeader& out) {
    const uint32_t attributes = readU32(raw);
    out.attributes = attributes & ~kAttrSeen;
    out.seen = (attributes & kAttrSeen) != 0;
    out.from = fromFixedField(raw + 4, kFromSize);
    out.to = fromFixedField(raw + 40, kToSize);
    out.subject = fromFixedField(raw + 76, kSubjectSize);
    decodeAddress(raw + 148, out.origAddr);
    decodeAddress(raw + 156, out.destAddr);
    out.written = fromDosStamp(readU16(raw + 164), readU16(raw + 166));
    out.arrived = fromDosStamp(readU16(raw + 168), readU16(raw + 170));
    out.utcOffsetMinutes = readI16(raw + 172);
    out.replyTo = readU32(raw + 174);
    for (size_t i = 0; i < kMaxReplies; ++i) {
        const uint32_t uid = readU32(raw + 178 + (i * 4));
        if (uid != 0) out.replies.push_back(uid);
    }
    // A tosser that wrote only the ASCII date leaves the packed stamp empty.
    // Reading it back from the field FTS-0001 defines is the difference between
    // a date and a row of zeroes; the packed one has the last word where both
    // are there.
    if (!out.written.isValid()) {
        out.written = parseFtscDate(fromFixedField(raw + 218, kFtscDateSize));
    }
}

void encodeMessageHeader(unsigned char* raw, const RawHeader& header, uint32_t uid) {
    std::memset(raw, 0, kMessageHeaderSize);
    // The UMSGID goes in the frame as well as in the index, and the bit that
    // says so goes with it. `seen` is the other bit that is not in the
    // attributes word above: decodeMessageHeader() takes it out of there and
    // this puts it back, so the two are exact opposites of one another.
    writeU32(raw, header.attributes | kAttrHasUid | (header.seen ? kAttrSeen : 0));
    toFixedField(raw + 4, kFromSize, header.from);
    toFixedField(raw + 40, kToSize, header.to);
    toFixedField(raw + 76, kSubjectSize, header.subject);
    encodeAddress(raw + 148, header.origAddr);
    encodeAddress(raw + 156, header.destAddr);

    uint16_t date = 0;
    uint16_t time = 0;
    toDosStamp(header.written, &date, &time);
    writeU16(raw + 164, date);
    writeU16(raw + 166, time);
    toDosStamp(header.arrived, &date, &time);
    writeU16(raw + 168, date);
    writeU16(raw + 170, time);
    writeU16(raw + 172, static_cast<uint16_t>(header.utcOffsetMinutes));

    writeU32(raw + 174, header.replyTo);
    for (size_t i = 0; i < std::min(kMaxReplies, header.replies.size()); ++i) {
        writeU32(raw + 178 + (i * 4), header.replies[i]);
    }
    writeU32(raw + 214, uid);
    toFixedField(raw + 218, kFtscDateSize, ftscDate(header.written));
}

/// What the control block occupies in a frame: the lines and the NUL closing
/// them, which Squish has always counted in the length — what reads it back
/// expects a string. Nothing at all where there are no control lines.
uint32_t controlBlockLength(const std::string& control) {
    return static_cast<uint32_t>(control.empty() ? 0 : control.size() + 1);
}

}  // namespace

tl::expected<void, ErrorPtr> SquishBase::open(const std::string& path, bool /*echo*/,
                                              uint16_t /*defaultZone*/) {
    close();

    if (!data_.open(path + ".sqd", true)) {
        return failure("cannot open " + path + ".sqd");
    }
    // The index is opened the same way round as the data file: a base that can
    // only be read must not look half writable.
    if (!index_file_.open(path + ".sqi", data_.writable())) {
        auto reason = "cannot open " + path + ".sqi";
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

void SquishBase::close() {
    data_.close();
    index_file_.close();
    base_ = BaseHeader{};
    index_.clear();
    frameHeaderSize_ = static_cast<uint16_t>(kFrameHeaderSize);
}

tl::expected<void, ErrorPtr> SquishBase::create(const std::string& path) {
    close();

    const std::string dataPath = path + ".sqd";
    const std::string indexPath = path + ".sqi";

    // The .sqd is what probeType() looks for, so it is made last of the two: a
    // creation interrupted between them leaves an .sqi nothing claims rather
    // than a Squish base with no index, which is the half that reads as broken.
    BinaryFile index;
    if (!index.create(indexPath)) {
        // The errno the create left behind: "permission denied" or "no such
        // directory" is what the user has to act on, and it is the half of the
        // message that a bare path cannot say.
        return failure("cannot create " + indexPath + ": " + std::strerror(errno));
    }
    // Empty, and deliberately: the index holds one record per message, and
    // there are none.
    index.close();

    BinaryFile data;
    if (!data.create(dataPath)) {
        auto reason = "cannot create " + dataPath + ": " + std::strerror(errno);
        std::error_code ec;
        std::filesystem::remove(indexPath, ec);
        return failure(std::move(reason));
    }

    std::array<unsigned char, kBaseHeaderSize> raw{};
    writeU16(raw.data(), static_cast<uint16_t>(kBaseHeaderSize));
    // The first UMSGID handed out. Zero would be no UID at all — the value
    // `indexOfUid()` answers nothing to — so the numbering starts at one.
    writeU32(raw.data() + 20, 1);
    toFixedField(raw.data() + 24, kBaseNameSize, path);
    // Where the frames would begin, which for a base of no messages is where
    // the header ends. It is the one field readBaseHeader() refuses a zero in:
    // a file of zeroes is exactly what a tosser that died mid-creation leaves,
    // and telling that from an area deliberately made empty is the point of it.
    writeU32(raw.data() + 120, static_cast<uint32_t>(kBaseHeaderSize));
    writeU16(raw.data() + 130, static_cast<uint16_t>(kFrameHeaderSize));

    if (const auto io = data.writeAt(0, raw.data(), raw.size()); io.failed()) {
        auto reason = "cannot write the area header of " + dataPath;
        data.close();
        std::error_code ec;
        std::filesystem::remove(dataPath, ec);
        std::filesystem::remove(indexPath, ec);
        return failure(std::move(reason) + ": " + io.message());
    }
    return {};
}

tl::expected<void, ErrorPtr> SquishBase::readBaseHeader() {
    std::array<unsigned char, kBaseHeaderSize> raw{};
    if (const auto io = data_.readAt(0, raw.data(), raw.size()); io.failed()) {
        return failure("cannot read the area header of " + data_.path() + ": " +
                       io.message());
    }

    const uint16_t headerLength = readU16(raw.data());
    base_.messageCount = readU32(raw.data() + 4);
    base_.highMessage = readU32(raw.data() + 8);
    base_.skipMessages = readU32(raw.data() + 12);
    base_.highWater = readU32(raw.data() + 16);
    base_.nextUid = readU32(raw.data() + 20);
    base_.firstFrame = readU32(raw.data() + 104);
    base_.lastFrame = readU32(raw.data() + 108);
    base_.firstFree = readU32(raw.data() + 112);
    base_.lastFree = readU32(raw.data() + 116);
    base_.endFrame = readU32(raw.data() + 120);
    base_.maxMessages = readU32(raw.data() + 124);
    base_.keepDays = readU16(raw.data() + 128);
    frameHeaderSize_ = readU16(raw.data() + 130);

    // The same checks smapi refuses a base on. They are worth making: every
    // offset below is trusted afterwards, and a header that fails these is not
    // a Squish base at all — most often it is a file of zeroes left by a
    // tosser that died between creating the area and filling it in.
    const bool sane =
        headerLength >= kBaseHeaderSize && headerLength < 1024 &&
        base_.messageCount == base_.highMessage && base_.messageCount <= base_.nextUid &&
        base_.endFrame != 0 && base_.firstFrame <= base_.endFrame &&
        base_.lastFrame <= base_.endFrame && base_.firstFree <= base_.endFrame &&
        base_.lastFree <= base_.endFrame;
    if (!sane) {
        return failure("the area header of " + data_.path() +
                       " is not a valid Squish header");
    }
    // The format says to take the frame header's length from the header rather
    // than from a constant, and that anything but 28 means a version this is
    // not: refusing it is the only honest answer, since every offset in a frame
    // is measured from it.
    if (frameHeaderSize_ != kFrameHeaderSize) {
        return failure(data_.path() + " states a frame header of " +
                       std::to_string(frameHeaderSize_) +
                       " bytes: this is not Squish version one");
    }
    return {};
}

tl::expected<void, ErrorPtr> SquishBase::writeBaseHeader() {
    // Read-modify-write rather than build from scratch: the header holds the
    // base's name and a hundred reserved bytes that are not ours, and a
    // message added by a reader is no reason to drop them.
    std::array<unsigned char, kBaseHeaderSize> raw{};
    if (const auto io = data_.readAt(0, raw.data(), raw.size()); io.failed()) {
        return failure("cannot re-read the area header of " + data_.path() + ": " +
                       io.message());
    }
    writeU32(raw.data() + 4, base_.messageCount);
    writeU32(raw.data() + 8, base_.highMessage);
    writeU32(raw.data() + 16, base_.highWater);
    writeU32(raw.data() + 20, base_.nextUid);
    writeU32(raw.data() + 104, base_.firstFrame);
    writeU32(raw.data() + 108, base_.lastFrame);
    writeU32(raw.data() + 112, base_.firstFree);
    writeU32(raw.data() + 116, base_.lastFree);
    writeU32(raw.data() + 120, base_.endFrame);

    if (const auto io = data_.writeAt(0, raw.data(), raw.size()); io.failed()) {
        return failure("cannot write the area header of " + data_.path() + ": " +
                       io.message());
    }
    return {};
}

tl::expected<void, ErrorPtr> SquishBase::loadIndex() {
    index_.clear();
    const int64_t size = index_file_.size();
    if (size < 0) {
        return failure("cannot size " + index_file_.path());
    }

    // The header states how many messages there are and the index file may be
    // longer than that — Squish grows it 64 records at a time — or, in a base
    // left half written, shorter. What can be read is what there is.
    const auto stored =
        static_cast<uint32_t>(static_cast<uint64_t>(size) / kIndexRecordSize);
    const uint32_t wanted = std::min(base_.messageCount, stored);

    std::vector<unsigned char> raw(static_cast<size_t>(wanted) * kIndexRecordSize);
    if (!raw.empty() && index_file_.readAt(0, raw.data(), raw.size()).failed()) {
        return failure("cannot read " + index_file_.path());
    }

    index_.reserve(wanted);
    for (uint32_t i = 0; i < wanted; ++i) {
        const unsigned char* record =
            raw.data() + (static_cast<size_t>(i) * kIndexRecordSize);
        IndexEntry entry;
        entry.offset = readU32(record);
        entry.uid = readU32(record + 4);
        entry.hash = readU32(record + 8);
        index_.push_back(entry);
    }
    return {};
}

tl::expected<void, ErrorPtr> SquishBase::reload() {
    auto read = readBaseHeader();
    if (!read) return tl::make_unexpected(std::move(read).error());
    return loadIndex();
}

tl::expected<void, ErrorPtr> SquishBase::readFrame(uint32_t offset, Frame& out) const {
    if (offset < kBaseHeaderSize || offset >= base_.endFrame) {
        return failure("frame offset " + std::to_string(offset) + " is outside " +
                       data_.path());
    }
    std::array<unsigned char, kFrameHeaderSize> raw{};
    if (const auto io = data_.readAt(offset, raw.data(), raw.size()); io.failed()) {
        return failure("cannot read the frame at " + std::to_string(offset) + ": " +
                       io.message());
    }
    if (readU32(raw.data()) != kFrameId) {
        return failure("no frame at " + std::to_string(offset) + " in " + data_.path());
    }
    out.next = readU32(raw.data() + 4);
    out.prev = readU32(raw.data() + 8);
    out.frameLength = readU32(raw.data() + 12);
    out.messageLength = readU32(raw.data() + 16);
    out.controlLength = readU32(raw.data() + 20);
    out.type = readU16(raw.data() + 24);
    return {};
}

tl::expected<void, ErrorPtr> SquishBase::writeFrame(uint32_t offset, const Frame& frame) {
    if (offset < kBaseHeaderSize) {
        return failure("refusing to write a frame over the area header");
    }
    std::array<unsigned char, kFrameHeaderSize> raw{};
    writeU32(raw.data(), kFrameId);
    writeU32(raw.data() + 4, frame.next);
    writeU32(raw.data() + 8, frame.prev);
    writeU32(raw.data() + 12, frame.frameLength);
    writeU32(raw.data() + 16, frame.messageLength);
    writeU32(raw.data() + 20, frame.controlLength);
    writeU16(raw.data() + 24, frame.type);
    writeU16(raw.data() + 26, 0);
    if (const auto io = data_.writeAt(offset, raw.data(), raw.size()); io.failed()) {
        return failure("cannot write the frame at " + std::to_string(offset) + ": " +
                       io.message());
    }
    return {};
}

tl::expected<void, ErrorPtr> SquishBase::setFrameNext(uint32_t offset, uint32_t value) {
    Frame frame;
    auto done = readFrame(offset, frame);
    if (!done) return tl::make_unexpected(std::move(done).error());
    frame.next = value;
    return writeFrame(offset, frame);
}

tl::expected<void, ErrorPtr> SquishBase::setFramePrev(uint32_t offset, uint32_t value) {
    Frame frame;
    auto done = readFrame(offset, frame);
    if (!done) return tl::make_unexpected(std::move(done).error());
    frame.prev = value;
    return writeFrame(offset, frame);
}

tl::expected<void, ErrorPtr> SquishBase::read(uint32_t index, RawMessage& out,
                                              bool withText) const {
    if (index == 0 || index > count()) {
        return failure("message " + std::to_string(index) + " is not in the area");
    }
    const IndexEntry& entry = index_[index - 1];
    if (entry.offset == 0) {
        // Somebody else is in the middle of replacing this message: the index
        // record is blanked for as long as that lasts.
        return failure("message " + std::to_string(index) +
                       " is being written by another task");
    }

    Frame frame;
    auto done = readFrame(entry.offset, frame);
    if (!done) return tl::make_unexpected(std::move(done).error());
    if (frame.type == kFrameUpdate) {
        return failure("message " + std::to_string(index) +
                       " is being updated by another task");
    }
    if (frame.type != kFrameNormal || frame.messageLength < kMessageHeaderSize) {
        return failure("the frame of message " + std::to_string(index) +
                       " holds no message");
    }

    std::array<unsigned char, kMessageHeaderSize> raw{};
    if (const auto io =
            data_.readAt(entry.offset + frameHeaderSize_, raw.data(), raw.size());
        io.failed()) {
        return failure("cannot read the header of message " + std::to_string(index) +
                       ": " + io.message());
    }
    out.header = RawHeader{};
    decodeMessageHeader(raw.data(), out.header);

    const uint32_t body = frame.messageLength - static_cast<uint32_t>(kMessageHeaderSize);
    const uint32_t controlLength = std::min(frame.controlLength, body);
    const uint64_t controlAt = entry.offset + frameHeaderSize_ + kMessageHeaderSize;

    out.control.clear();
    if (controlLength != 0) {
        std::string block(controlLength, '\0');
        if (const auto io = data_.readAt(controlAt, &block[0], block.size());
            io.failed()) {
            return failure("cannot read the control block of message " +
                           std::to_string(index) + ": " + io.message());
        }
        out.control = controlBlockToKludges(block);
    }

    // The XMSG has words for the zones and the points, and a tosser is at
    // liberty to leave them at zero and let the kludges say it — which is how
    // netmail written under FSC-0004 usually arrives. Read here rather than
    // above the driver so that a message list, which reads the control block
    // for the charset regardless, shows the same address the reader does.
    completeAddresses(out.header, out.control);

    out.text.clear();
    if (withText) {
        const uint32_t textLength = body - controlLength;
        if (textLength != 0) {
            out.text.assign(textLength, '\0');
            if (const auto io = data_.readAt(controlAt + controlLength, &out.text[0],
                                             out.text.size());
                io.failed()) {
                return failure("cannot read the text of message " +
                               std::to_string(index) + ": " + io.message());
            }
            // Some writers count the terminating NUL in the message length.
            while (!out.text.empty() && out.text.back() == '\0') out.text.pop_back();
        }
    }
    return {};
}

domain::MessageInfo SquishBase::info(uint32_t index) const {
    domain::MessageInfo out;
    if (index == 0 || index > count()) return out;

    const IndexEntry& entry = index_[index - 1];
    Frame frame;
    if (entry.offset == 0 || !readFrame(entry.offset, frame)) return out;

    std::array<unsigned char, kMessageHeaderSize> raw{};
    if (data_.readAt(entry.offset + frameHeaderSize_, raw.data(), raw.size()).failed()) {
        return out;
    }
    RawHeader header;
    decodeMessageHeader(raw.data(), header);

    // Where the base is stands in the Msgbase field below, in the column every
    // other value lines up in; the title says which message of how many.
    out.title =
        "Squish message " + std::to_string(index) + " of " + std::to_string(count());

    // The two halves of the body, where the frame says they are. The lengths
    // are the ones read() works from, the frame's own being the only word on
    // the subject there is.
    const auto headerSize = static_cast<uint32_t>(kMessageHeaderSize);
    const uint32_t body =
        frame.messageLength >= headerSize ? frame.messageLength - headerSize : 0;
    const uint32_t controlLength = std::min(frame.controlLength, body);
    const uint32_t textLength = body - controlLength;
    const uint64_t controlAt = entry.offset + frameHeaderSize_ + kMessageHeaderSize;

    // The stamps as the date they stand for and as the dword they are packed
    // into — the two DOS words as one number, the way every FTN tool has
    // printed an XMSG stamp: a stamp a tosser left empty is exactly what an
    // info report is opened to find out.
    const auto packed = [&raw](size_t at) {
        return " (" + report::hex(readU32(raw.data() + at)) + ")";
    };

    // All nine slots, the empty ones included: which of them a message uses is
    // the thing being looked at, and a list of the non-zero ones would not say.
    std::string replies;
    for (size_t i = 0; i < kMaxReplies; ++i) {
        if (i != 0) replies += ", ";
        replies += std::to_string(readU32(raw.data() + 178 + (i * 4)));
    }

    // The fields, in the order and under the names GoldED+ prints them, so that
    // a report read here and one read there are the same report.
    domain::MessageInfoBlock message;
    message.fields = {
        report::field("Msgbase", data_.path()),
        report::textField("From", header.from),
        report::textField("To", header.to),
        report::textField("Subject", header.subject),
        report::textField("DateTime", fromFixedField(raw.data() + 218, kFtscDateSize)),
        report::field("OrigAddr", report::address(header.origAddr)),
        report::field("DestAddr", report::address(header.destAddr)),
        report::field("Umsgid", std::to_string(readU32(raw.data() + 214))),
        report::field("Reply", std::to_string(header.replyTo)),
        report::field("See", replies),
        report::field("Attr", report::hex(header.attributes) + " (" +
                                  report::bits(header.attributes) + ")"),
        report::field("DateWritten", report::stamp(header.written) + packed(164)),
        report::field("DateArrived", report::stamp(header.arrived) + packed(168)),
        report::field("UTC-Offset", std::to_string(header.utcOffsetMinutes)),
    };
    out.blocks.push_back(std::move(message));

    domain::MessageInfoBlock base;
    base.title = "Message Base Record:";
    base.fields = {
        report::field("TotalMsgs", std::to_string(base_.messageCount)),
        report::field("HighestMsg", std::to_string(base_.highMessage)),
        report::field("NextMsgno", std::to_string(base_.nextUid)),
        report::field("HighWaterMark", std::to_string(base_.highWater)),
        report::field("FirstFrame", report::hexAndDecimal(base_.firstFrame)),
        report::field("LastFrame", report::hexAndDecimal(base_.lastFrame)),
        report::field("FirstFreeFrame", report::hexAndDecimal(base_.firstFree)),
        report::field("LastFreeFrame", report::hexAndDecimal(base_.lastFree)),
        report::field("EndFrame", report::hexAndDecimal(base_.endFrame)),
        report::field("Max/Skip/Days", std::to_string(base_.maxMessages) + "  " +
                                           std::to_string(base_.skipMessages) + "  " +
                                           std::to_string(base_.keepDays)),
        // Not one of GoldED+'s: the format takes the frame header's length from
        // this field rather than from a constant, and a base stating anything
        // but 28 is one AmberEdit refuses to open at all.
        report::field("FrameHdrSize", std::to_string(frameHeaderSize_)),
    };
    out.blocks.push_back(std::move(base));

    domain::MessageInfoBlock record;
    record.title = "Message Index Record:";
    record.fields = {
        report::field("FrameOffset", report::hexAndDecimal(entry.offset)),
        report::field("MessageNumber", report::hexAndDecimal(entry.uid)),
        report::field("HashValue", report::hexAndDecimal(entry.hash)),
    };
    out.blocks.push_back(std::move(record));

    // What the frame type is called, for the two a reader can meet: a frame
    // being written over and a frame that holds no message read differently,
    // and the number alone says neither.
    std::string type = std::to_string(frame.type);
    if (frame.type == kFrameNormal) type += " (message)";
    if (frame.type == kFrameFree) type += " (free)";
    if (frame.type == kFrameUpdate) type += " (being updated)";

    // Read back off the file rather than printed from the constant. It can only
    // be the constant — readFrame() refuses a frame that says anything else —
    // but a report that prints what it expects rather than what is there is a
    // report that cannot show the one thing it would be opened for.
    std::array<unsigned char, 4> frameId{};
    (void)data_.readAt(entry.offset, frameId.data(), frameId.size()).ok();

    domain::MessageInfoBlock frameBlock;
    frameBlock.title = "Message Frame Record:";
    frameBlock.fields = {
        report::field("Frame-ID", report::hex(readU32(frameId.data()))),
        report::field("ThisFrame", report::hexAndDecimal(entry.offset)),
        report::field("PrevFrame", report::hexAndDecimal(frame.prev)),
        report::field("NextFrame", report::hexAndDecimal(frame.next)),
        report::field("FrameLength", std::to_string(frame.frameLength)),
        report::field("TotalLength", std::to_string(frame.messageLength)),
        report::field("CtrlLength", std::to_string(frame.controlLength)),
        report::field("FrameType", type),
    };
    out.blocks.push_back(std::move(frameBlock));

    domain::MessageInfoBlock stored;
    stored.title = report::dumpTitle("Message header (XMSG)", kMessageHeaderSize,
                                     kMessageHeaderSize);
    stored.bytes.assign(reinterpret_cast<const char*>(raw.data()), raw.size());
    out.blocks.push_back(std::move(stored));

    if (controlLength != 0) {
        domain::MessageInfoBlock control;
        control.bytes = report::readDump(data_, controlAt, controlLength);
        control.title =
            report::dumpTitle("Control block", controlLength, control.bytes.size());
        out.blocks.push_back(std::move(control));
    }
    if (textLength != 0) {
        domain::MessageInfoBlock text;
        text.bytes = report::readDump(data_, controlAt + controlLength, textLength);
        text.title = report::dumpTitle("Message text", textLength, text.bytes.size());
        out.blocks.push_back(std::move(text));
    }
    return out;
}

uint32_t SquishBase::uidOf(uint32_t index) const {
    if (index == 0 || index > count()) return 0;
    const uint32_t uid = index_[index - 1].uid;
    // An invalid record says so with all bits set as well as with zero.
    return uid == 0xffffffffu ? 0 : uid;
}

uint32_t SquishBase::indexOfUid(uint32_t uid, bool exact) const {
    if (uid == 0 || index_.empty()) return 0;

    // The index is sorted by UMSGID — the format requires it — so the position
    // is a binary search. The number of records before the first one past `uid`
    // is at once the 1-based position of an exact match and, where there is
    // none, of the nearest earlier message: a mark left on a message that has
    // since been packed away still says how far the reading got.
    const auto past = std::upper_bound(
        index_.begin(), index_.end(), uid,
        [](uint32_t value, const IndexEntry& entry) { return value < entry.uid; });
    const auto position = static_cast<uint32_t>(std::distance(index_.begin(), past));
    if (position == 0) return 0;
    if (exact && index_[position - 1].uid != uid) return 0;
    return position;
}

tl::expected<void, ErrorPtr> SquishBase::allocateFrame(uint32_t length, uint32_t* offset,
                                                       uint32_t* frameLength) {
    *offset = 0;
    *frameLength = 0;

    // The free chain first: a deleted message leaves a hole, and reusing one is
    // what keeps a base that is written and packed for years from growing
    // without end. The first one that fits is taken rather than the best fit
    // the format recommends — that is what smapi does, and a base half of whose
    // frames are the wrong size is a job for a packer, not for a reader.
    uint32_t seen = 0;
    for (uint32_t at = base_.firstFree; at != 0;) {
        Frame frame;
        auto done = readFrame(at, frame);
        if (!done) return tl::make_unexpected(std::move(done).error());
        if (frame.type != kFrameFree || frame.prev != seen || frame.next == at) {
            return failure("the free chain of " + data_.path() + " is broken at " +
                           std::to_string(at));
        }

        if (frame.frameLength >= length) {
            if ((frame.prev == 0 && at != base_.firstFree) ||
                (frame.next == 0 && at != base_.lastFree)) {
                return failure("the free chain of " + data_.path() +
                               " does not end where its "
                               "header says");
            }
            if (frame.prev != 0) {
                auto done2 = setFrameNext(frame.prev, frame.next);
                if (!done2) return tl::make_unexpected(std::move(done2).error());
            }
            if (frame.next != 0) {
                auto done3 = setFramePrev(frame.next, frame.prev);
                if (!done3) return tl::make_unexpected(std::move(done3).error());
            }
            if (base_.firstFree == at) base_.firstFree = frame.next;
            if (base_.lastFree == at) base_.lastFree = frame.prev;

            *offset = at;
            *frameLength = frame.frameLength;
            return {};
        }

        seen = at;
        at = frame.next;
    }

    // Nothing to reuse: the frame goes at the end of the file, and the header's
    // idea of where the end is moves with it.
    *offset = base_.endFrame;
    base_.endFrame = base_.endFrame + frameHeaderSize_ + length;
    return {};
}

tl::expected<void, ErrorPtr> SquishBase::releaseFrame(uint32_t offset, Frame frame) {
    frame.type = kFrameFree;
    frame.messageLength = 0;
    frame.controlLength = 0;
    frame.next = 0;
    frame.prev = base_.lastFree;
    // The frame keeps the length it owns: that is what the next message being
    // written measures itself against.

    if (base_.lastFree == 0) {
        auto done = writeFrame(offset, frame);
        if (!done) return tl::make_unexpected(std::move(done).error());
        base_.firstFree = offset;
        base_.lastFree = offset;
        return {};
    }
    auto done2 = setFrameNext(base_.lastFree, offset);
    if (!done2) return tl::make_unexpected(std::move(done2).error());
    auto done3 = writeFrame(offset, frame);
    if (!done3) return tl::make_unexpected(std::move(done3).error());
    base_.lastFree = offset;
    return {};
}

tl::expected<void, ErrorPtr> SquishBase::writeMessageAt(uint32_t offset,
                                                        const RawHeader& header,
                                                        uint32_t uid,
                                                        const std::string& control,
                                                        const std::string& text) {
    std::array<unsigned char, kMessageHeaderSize> raw{};
    encodeMessageHeader(raw.data(), header, uid);
    if (const auto io = data_.writeAt(offset + frameHeaderSize_, raw.data(), raw.size());
        io.failed()) {
        return failure("cannot write the header of the message at " +
                       std::to_string(offset) + ": " + io.message());
    }

    const uint64_t bodyAt = offset + frameHeaderSize_ + kMessageHeaderSize;
    const uint32_t controlLength = controlBlockLength(control);
    if (controlLength != 0 &&
        data_.writeAt(bodyAt, control.c_str(), controlLength).failed()) {
        return failure("cannot write the control block of the message at " +
                       std::to_string(offset));
    }
    if (!text.empty() &&
        data_.writeAt(bodyAt + controlLength, text.data(), text.size()).failed()) {
        return failure("cannot write the text of the message at " +
                       std::to_string(offset));
    }
    return {};
}

tl::expected<void, ErrorPtr> SquishBase::writeIndexEntry(uint32_t index) {
    std::array<unsigned char, kIndexRecordSize> raw{};
    const IndexEntry& entry = index_[index - 1];
    writeU32(raw.data(), entry.offset);
    writeU32(raw.data() + 4, entry.uid);
    writeU32(raw.data() + 8, entry.hash);
    const uint64_t at = static_cast<uint64_t>(index - 1) * kIndexRecordSize;
    if (const auto io = index_file_.writeAt(at, raw.data(), raw.size()); io.failed()) {
        return failure("cannot write record " + std::to_string(index) + " of " +
                       index_file_.path() + ": " + io.message());
    }
    return {};
}

tl::expected<uint32_t, ErrorPtr> SquishBase::write(const RawDraft& draft) {
    if (!data_.isOpen()) {
        return failure<MsgBaseError>(MsgBaseError::Kind::NoAreaOpen, std::string());
    }
    if (!data_.writable() || !index_file_.writable()) {
        return failure("the base at " + data_.path() + " is not ours to write");
    }

    // Both files, before anything is read that the write depends on: the
    // message count, the next UMSGID and the free chain all come off the disk
    // under the lock and are put back under it.
    FileLock lock;
    if (const auto locked = lock.acquire({&data_, &index_file_}); !locked) {
        return failure<MsgBaseError>(MsgBaseError::Kind::BaseBusy,
                                     locked.error()->message());
    }
    auto done = reload();
    if (!done) return tl::make_unexpected(std::move(done).error());

    const std::string control = kludgesToControlBlock(draft.kludges);
    const uint32_t controlLength = controlBlockLength(control);
    const auto total = static_cast<uint32_t>(kMessageHeaderSize) + controlLength +
                       static_cast<uint32_t>(draft.text.size());

    uint32_t offset = 0;
    uint32_t frameLength = 0;
    auto done2 = allocateFrame(total, &offset, &frameLength);
    if (!done2) return tl::make_unexpected(std::move(done2).error());

    Frame frame;
    frame.prev = base_.lastFrame;
    frame.next = 0;
    frame.frameLength = frameLength != 0 ? frameLength : total;
    frame.messageLength = total;
    frame.controlLength = controlLength;
    frame.type = kFrameNormal;

    // The message goes at the end of the chain, which is where its number comes
    // from: Squish numbers by position and the new message is the last one.
    if (base_.lastFrame != 0) {
        auto done3 = setFrameNext(base_.lastFrame, offset);
        if (!done3) return tl::make_unexpected(std::move(done3).error());
    }
    auto done4 = writeFrame(offset, frame);
    if (!done4) return tl::make_unexpected(std::move(done4).error());

    const uint32_t uid = base_.nextUid;
    auto done5 = writeMessageAt(offset, draft.header, uid, control, draft.text);
    if (!done5) return tl::make_unexpected(std::move(done5).error());

    IndexEntry entry;
    entry.offset = offset;
    entry.uid = uid;
    entry.hash = squishHash(draft.header.to) |
                 ((draft.header.attributes & kAttrRead) != 0 ? kHashRead : 0);
    index_.push_back(entry);
    auto done6 = writeIndexEntry(count());
    if (!done6) return tl::make_unexpected(std::move(done6).error());
    // Squish leaves the index file long and cuts it back to the message count;
    // doing the same keeps a base that another tool wrote from carrying stale
    // records past its end.
    (void)index_file_.truncate(static_cast<uint64_t>(count()) * kIndexRecordSize);

    base_.nextUid = uid + 1;
    base_.messageCount = count();
    base_.highMessage = base_.messageCount;
    if (base_.firstFrame == 0) base_.firstFrame = offset;
    base_.lastFrame = offset;

    // Last of all, and deliberately: until the header says how many messages
    // there are, a reader coming in on the base sees the one just written as
    // the spare index record it was a moment ago and reads the area as it was.
    auto done7 = writeBaseHeader();
    if (!done7) return tl::make_unexpected(std::move(done7).error());
    return count();
}

tl::expected<void, ErrorPtr> SquishBase::replace(uint32_t index, const RawDraft& draft) {
    if (!data_.isOpen()) {
        return failure<MsgBaseError>(MsgBaseError::Kind::NoAreaOpen, std::string());
    }
    if (!data_.writable() || !index_file_.writable()) {
        return failure("the base at " + data_.path() + " is not ours to write");
    }

    FileLock lock;
    if (const auto locked = lock.acquire({&data_, &index_file_}); !locked) {
        return failure<MsgBaseError>(MsgBaseError::Kind::BaseBusy,
                                     locked.error()->message());
    }
    auto done = reload();
    if (!done) return tl::make_unexpected(std::move(done).error());

    if (index == 0 || index > count()) {
        return failure("message " + std::to_string(index) + " is not there to change");
    }
    const IndexEntry entry = index_[index - 1];
    Frame frame;
    if (entry.offset == 0 || !readFrame(entry.offset, frame)) {
        return failure("message " + std::to_string(index) + " has no frame to change");
    }
    if (frame.type != kFrameNormal || frame.messageLength < kMessageHeaderSize) {
        return failure("the frame of message " + std::to_string(index) +
                       " holds no message");
    }

    // What the message keeps whatever is done to its words: when it arrived
    // here, which no amount of rewriting changes, its place in the thread, and
    // whether it has been read — a mark this system made on the message, which
    // rewriting the message does not unmake. Read back off the frame rather
    // than taken from the draft, which knows nothing of any of them. The stamp
    // it is dated by is the draft's: a message written again is written now.
    std::array<unsigned char, kMessageHeaderSize> stored{};
    if (const auto io =
            data_.readAt(entry.offset + frameHeaderSize_, stored.data(), stored.size());
        io.failed()) {
        return failure("cannot read the header of message " + std::to_string(index) +
                       ": " + io.message());
    }
    RawHeader was;
    decodeMessageHeader(stored.data(), was);

    RawHeader header = draft.header;
    header.arrived = was.arrived;
    header.replyTo = was.replyTo;
    header.replies = was.replies;
    header.seen = was.seen;

    const std::string control = kludgesToControlBlock(draft.kludges);
    const uint32_t controlLength = controlBlockLength(control);
    const auto total = static_cast<uint32_t>(kMessageHeaderSize) + controlLength +
                       static_cast<uint32_t>(draft.text.size());

    // Where it fits, it stays: a message that has grown by a line should not
    // cost the base a frame, and one that has shrunk leaves the slack in the
    // frame it already owns for the next change to grow back into.
    const bool moves = frame.frameLength < total;
    uint32_t offset = entry.offset;
    Frame target = frame;
    if (moves) {
        uint32_t frameLength = 0;
        auto done2 = allocateFrame(total, &offset, &frameLength);
        if (!done2) return tl::make_unexpected(std::move(done2).error());
        target.frameLength = frameLength != 0 ? frameLength : total;
    } else {
        // The frame says so while it is half written. It is the one thing the
        // format has to say about a message being changed, and read() already
        // answers a reader that meets it with "another task is updating this"
        // rather than with half a message.
        frame.type = kFrameUpdate;
        auto done3 = writeFrame(offset, frame);
        if (!done3) return tl::make_unexpected(std::move(done3).error());
    }
    target.messageLength = total;
    target.controlLength = controlLength;
    target.type = kFrameNormal;

    auto done4 = writeMessageAt(offset, header, entry.uid, control, draft.text);
    if (!done4) return tl::make_unexpected(std::move(done4).error());
    auto done5 = writeFrame(offset, target);
    if (!done5) return tl::make_unexpected(std::move(done5).error());

    // The index record names where the message is and hashes the name it is
    // addressed to; both can have changed with it. The UMSGID cannot: it is
    // what says this is still the same message.
    index_[index - 1].offset = offset;
    index_[index - 1].hash = squishHash(draft.header.to) |
                             ((draft.header.attributes & kAttrRead) != 0 ? kHashRead : 0);
    auto done6 = writeIndexEntry(index);
    if (!done6) return tl::make_unexpected(std::move(done6).error());
    if (!moves) return {};

    // It moved: the messages either side of it are linked to where it went, and
    // the frame it left goes on the free chain for the next message to grow
    // into. In that order, and after the index record — a reader arriving
    // between the steps finds the message at its new frame and the old one
    // still holding what it held, never an index pointing into free space.
    if (frame.prev != 0) {
        auto done7 = setFrameNext(frame.prev, offset);
        if (!done7) return tl::make_unexpected(std::move(done7).error());
    }
    if (frame.next != 0) {
        auto done8 = setFramePrev(frame.next, offset);
        if (!done8) return tl::make_unexpected(std::move(done8).error());
    }
    if (base_.firstFrame == entry.offset) base_.firstFrame = offset;
    if (base_.lastFrame == entry.offset) base_.lastFrame = offset;
    auto done9 = releaseFrame(entry.offset, frame);
    if (!done9) return tl::make_unexpected(std::move(done9).error());

    return writeBaseHeader();
}

tl::expected<void, ErrorPtr> SquishBase::remove(uint32_t index) {
    if (!data_.isOpen()) {
        return failure<MsgBaseError>(MsgBaseError::Kind::NoAreaOpen, std::string());
    }
    if (!data_.writable() || !index_file_.writable()) {
        return failure("the base at " + data_.path() + " is not ours to write");
    }

    FileLock lock;
    if (const auto locked = lock.acquire({&data_, &index_file_}); !locked) {
        return failure<MsgBaseError>(MsgBaseError::Kind::BaseBusy,
                                     locked.error()->message());
    }
    auto done = reload();
    if (!done) return tl::make_unexpected(std::move(done).error());

    if (index == 0 || index > count()) {
        return failure("message " + std::to_string(index) + " is not there to delete");
    }
    const IndexEntry entry = index_[index - 1];
    Frame frame;
    if (entry.offset == 0 || !readFrame(entry.offset, frame)) {
        return failure("message " + std::to_string(index) + " has no frame to free");
    }

    // Link the messages either side of it over it, then take its number out of
    // the index, then give the frame to the free chain. In that order: a reader
    // that arrives between the steps finds a chain it can walk and an index one
    // message shorter, never an index pointing at a freed frame.
    if (frame.prev != 0) {
        auto done2 = setFrameNext(frame.prev, frame.next);
        if (!done2) return tl::make_unexpected(std::move(done2).error());
    }
    if (frame.next != 0) {
        auto done3 = setFramePrev(frame.next, frame.prev);
        if (!done3) return tl::make_unexpected(std::move(done3).error());
    }
    if (index == 1) base_.firstFrame = frame.next;
    if (index == count()) base_.lastFrame = frame.prev;

    index_.erase(index_.begin() + static_cast<long>(index) - 1);
    // The records after the deleted one, moved up as one write rather than one
    // per message: the tail of a large area is thousands of them.
    std::vector<unsigned char> tail((count() - index + 1) * kIndexRecordSize);
    for (uint32_t at = index; at <= count(); ++at) {
        unsigned char* raw =
            tail.data() + (static_cast<size_t>(at - index) * kIndexRecordSize);
        writeU32(raw, index_[at - 1].offset);
        writeU32(raw + 4, index_[at - 1].uid);
        writeU32(raw + 8, index_[at - 1].hash);
    }
    if (!tail.empty() && index_file_
                             .writeAt(static_cast<uint64_t>(index - 1) * kIndexRecordSize,
                                      tail.data(), tail.size())
                             .failed()) {
        return failure("cannot rewrite the tail of " + index_file_.path());
    }
    if (!index_file_.truncate(static_cast<uint64_t>(count()) * kIndexRecordSize)) {
        return failure("cannot shorten " + index_file_.path());
    }

    auto done4 = releaseFrame(entry.offset, frame);
    if (!done4) return tl::make_unexpected(std::move(done4).error());

    base_.messageCount = count();
    base_.highMessage = base_.messageCount;
    // The high-water mark is a message number, so everything after the deleted
    // message moves under it too.
    if (base_.highWater >= index && base_.highWater != 0) --base_.highWater;

    return writeBaseHeader();
}

tl::expected<void, ErrorPtr> SquishBase::markSeen(uint32_t index) {
    if (!data_.isOpen()) {
        return failure<MsgBaseError>(MsgBaseError::Kind::NoAreaOpen, std::string());
    }
    if (!data_.writable()) {
        return failure("the base at " + data_.path() + " is not ours to write");
    }

    FileLock lock;
    if (const auto locked = lock.acquire({&data_, &index_file_}); !locked) {
        return failure<MsgBaseError>(MsgBaseError::Kind::BaseBusy,
                                     locked.error()->message());
    }
    auto done = reload();
    if (!done) return tl::make_unexpected(std::move(done).error());

    if (index == 0 || index > count()) {
        return failure("message " + std::to_string(index) + " is not there to mark");
    }
    const IndexEntry entry = index_[index - 1];
    Frame frame;
    if (entry.offset == 0 || !readFrame(entry.offset, frame)) {
        return failure("message " + std::to_string(index) + " has no frame to mark");
    }
    if (frame.type != kFrameNormal || frame.messageLength < kMessageHeaderSize) {
        return failure("the frame of message " + std::to_string(index) +
                       " holds no message");
    }

    // The attributes are the first dword of the XMSG, so the mark is four bytes
    // read and four bytes written — the frame is not touched at all, its length
    // and its links being exactly what they were. No `kFrameUpdate` for the same
    // reason: nothing is half written here, and a reader meeting the frame
    // mid-mark finds the message it was already going to find.
    const uint64_t at = entry.offset + frameHeaderSize_;
    std::array<unsigned char, 4> raw{};
    if (const auto io = data_.readAt(at, raw.data(), raw.size()); io.failed()) {
        return failure("cannot read the attributes of message " + std::to_string(index) +
                       ": " + io.message());
    }
    const uint32_t attributes = readU32(raw.data());
    if ((attributes & kAttrSeen) != 0) return {};

    writeU32(raw.data(), attributes | kAttrSeen);
    if (const auto io = data_.writeAt(at, raw.data(), raw.size()); io.failed()) {
        return failure("cannot mark message " + std::to_string(index) +
                       " read: " + io.message());
    }
    return {};
}

}  // namespace amberedit::msgbase
