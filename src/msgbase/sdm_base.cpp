#include "msgbase/sdm_base.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#include "config/text_util.hpp"
#include "msgbase/binary_file.hpp"
#include "msgbase/byte_order.hpp"
#include "msgbase/file_lock.hpp"
#include "msgbase/info_format.hpp"

namespace amberedit::msgbase {

namespace {

namespace fs = std::filesystem;
using bytes::readU16;
using bytes::readU32;
using bytes::writeU16;
using config::text::startsWith;

/// The stored message header, FTS-0001's layout with the **Opus** reading of
/// the eight bytes at 176. The two-byte address fields carry no zone and no
/// point; those live in the INTL/FMPT/TOPT kludges.
constexpr size_t kHeaderSize = 190;
constexpr size_t kFromSize = 36;
constexpr size_t kToSize = 36;
constexpr size_t kSubjectSize = 72;
constexpr size_t kDateSize = 20;

/// The offsets worth a name, being the ones read in one function and written in
/// another — the rest of the header is spelled inline, as in the other drivers.
///
/// The ASCII date follows the subject rather than standing inside it, and the
/// eight bytes at 176 are a union: FTS-0001 puts the zone and point words
/// there, Opus put two packed stamps — written and arrived, a DOS date word
/// then a DOS time word — and the Opus reading is what all but the oldest
/// *.msg on disk were written under. AmberEdit reads and writes that half, as
/// GoldED+ does unless its FIDOMSGTYPE says otherwise, and there is
/// deliberately no setting and no sniffing for the other: nothing in the bytes
/// tells the two apart, and a guess would silently change a netmail address.
/// A header that is the FTSC union instead simply has no stamps, and the ASCII
/// date below answers for it.
constexpr size_t kDateOffset = 144;
constexpr size_t kTimesReadOffset = 164;
constexpr size_t kWrittenOffset = 176;
constexpr size_t kArrivedOffset = 180;

/// The name a message file has: its number and nothing else. Parsed by hand
/// rather than through atoi so that "12a.msg" is not read as message 12.
uint32_t numberOfName(const fs::path& name) {
    if (name.extension() != ".msg") return 0;
    const std::string stem = name.stem().string();
    if (stem.empty() || stem.size() > 9) return 0;
    uint32_t number = 0;
    for (const char ch : stem) {
        if (ch < '0' || ch > '9') return 0;
        number = (number * 10) + static_cast<uint32_t>(ch - '0');
    }
    return number;
}

bool hasKludge(const std::vector<std::string>& kludges, std::string_view name) {
    return std::any_of(kludges.begin(), kludges.end(), [name](const std::string& kludge) {
        return startsWith(kludge, name);
    });
}

}  // namespace

Result<void> SdmBase::open(const std::string& path, bool echo, uint16_t defaultZone) {
    close();
    echo_ = echo;
    defaultZone_ = defaultZone != 0 ? defaultZone : 2;

    std::error_code ec;
    if (!fs::is_directory(path, ec)) {
        return failure(path + " is not a directory");
    }
    directory_ = path;
    if (auto scanned = scan(); !scanned) {
        auto reason = std::move(scanned).error();
        close();
        return tl::make_unexpected(std::move(reason));
    }
    return {};
}

void SdmBase::close() {
    directory_.clear();
    numbers_.clear();
}

Result<void> SdmBase::create(const std::string& path) {
    close();

    // The base is the directory, and an empty directory is an empty base:
    // there is no header to write and no index to make. Any parent the path
    // names is made with it, the way a tosser creating an area under a spool
    // root would.
    std::error_code ec;
    if (fs::exists(path, ec)) {
        return failure("there is already something at " + path);
    }
    ec.clear();
    if (!fs::create_directories(path, ec)) {
        return failure("cannot create the directory " + path +
                       (ec ? ": " + ec.message() : ""));
    }
    return {};
}

std::string SdmBase::fileFor(uint32_t number) const {
    return (fs::path(directory_) / (std::to_string(number) + ".msg")).string();
}

Result<void> SdmBase::scan() {
    numbers_.clear();
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(directory_, ec)) {
        const uint32_t number = numberOfName(entry.path().filename());
        if (number != 0) numbers_.push_back(number);
    }
    if (ec) {
        return failure("cannot list " + directory_);
    }
    std::sort(numbers_.begin(), numbers_.end());
    return {};
}

uint32_t SdmBase::uidOf(uint32_t index) const {
    if (index == 0 || index > count()) return 0;
    return numbers_[index - 1];
}

uint32_t SdmBase::indexOfUid(uint32_t uid, bool exact) const {
    if (uid == 0 || numbers_.empty()) return 0;
    const auto past = std::upper_bound(numbers_.begin(), numbers_.end(), uid);
    const auto position = static_cast<uint32_t>(std::distance(numbers_.begin(), past));
    if (position == 0) return 0;
    if (exact && numbers_[position - 1] != uid) return 0;
    return position;
}

Result<void> SdmBase::read(uint32_t index, RawMessage& out, bool withText) const {
    if (index == 0 || index > count()) {
        return failure("message " + std::to_string(index) + " is not in the area");
    }
    const uint32_t number = numbers_[index - 1];

    BinaryFile file;
    if (!file.open(fileFor(number), false)) {
        return failure("cannot open " + fileFor(number));
    }
    std::array<unsigned char, kHeaderSize> raw{};
    if (const auto io = file.readAt(0, raw.data(), raw.size()); io.failed()) {
        return failure("cannot read the header of " + fileFor(number) + ": " +
                       io.message());
    }

    out.header = RawHeader{};
    out.header.from = fromFixedField(raw.data(), kFromSize);
    out.header.to = fromFixedField(raw.data() + 36, kToSize);
    out.header.subject = fromFixedField(raw.data() + 72, kSubjectSize);
    // The header's address words carry a net and a node; the zone is what a
    // kludge says it is, and the area's where none does. The points come from
    // kludges or nowhere.
    out.header.destAddr.node = readU16(raw.data() + 166);
    out.header.origAddr.node = readU16(raw.data() + 168);
    out.header.origAddr.net = readU16(raw.data() + 172);
    out.header.destAddr.net = readU16(raw.data() + 174);
    out.header.written = fromDosStamp(readU16(raw.data() + kWrittenOffset),
                                      readU16(raw.data() + kWrittenOffset + 2));
    out.header.arrived = fromDosStamp(readU16(raw.data() + kArrivedOffset),
                                      readU16(raw.data() + kArrivedOffset + 2));
    out.header.replyTo = readU16(raw.data() + 184);
    out.header.attributes = readU16(raw.data() + 186);
    // The attribute word here is sixteen bits wide, so Squish's MSGSEEN has
    // nowhere to go in it. FTS-0001 gives the header a read count instead, and
    // that is what says it: any count at all is the mark, as in JAM.
    out.header.seen = readU16(raw.data() + kTimesReadOffset) != 0;
    if (const uint32_t up = readU16(raw.data() + 188)) out.header.replies.push_back(up);

    // Many a writer leaves the packed stamps empty, and a header written under
    // the FTSC reading of those eight bytes carries no stamp at all: the ASCII
    // date field answers for whichever is missing.
    const domain::MessageDate asciiDate =
        parseFtscDate(fromFixedField(raw.data() + kDateOffset, kDateSize));
    if (!out.header.written.isValid()) out.header.written = asciiDate;
    if (!out.header.arrived.isValid()) out.header.arrived = out.header.written;

    // The rest of the file is the text, the kludges inline at its head and a
    // NUL closing it. Both halves are wanted even for a header-only read: the
    // zone and the points are in the kludges.
    const int64_t size = file.size();
    std::string body;
    if (size > static_cast<int64_t>(kHeaderSize)) {
        body.assign(static_cast<size_t>(size) - kHeaderSize, '\0');
        if (const auto io = file.readAt(kHeaderSize, &body[0], body.size());
            io.failed()) {
            return failure("cannot read the text of " + fileFor(number) + ": " +
                           io.message());
        }
        const size_t terminator = body.find('\0');
        if (terminator != std::string::npos) body.resize(terminator);
    }
    splitLeadingKludges(body, &out.control, &out.text);
    if (!withText) out.text.clear();

    // INTL names both zones; FMPT and TOPT the points. Nothing else states
    // either, so what the kludges do not say is the area's own zone — the one
    // a *.msg header is read under.
    completeAddresses(out.header, out.control);
    if (out.header.origAddr.zone == 0) out.header.origAddr.zone = defaultZone_;
    if (out.header.destAddr.zone == 0) out.header.destAddr.zone = defaultZone_;
    return {};
}

domain::MessageInfo SdmBase::info(uint32_t index) const {
    domain::MessageInfo out;
    if (index == 0 || index > count()) return out;
    const uint32_t number = numbers_[index - 1];

    BinaryFile file;
    if (!file.open(fileFor(number), false)) return out;
    std::array<unsigned char, kHeaderSize> raw{};
    if (file.readAt(0, raw.data(), raw.size()).failed()) return out;

    out.title =
        "Fido *.msg message " + std::to_string(index) + " of " + std::to_string(count());

    const int64_t size = file.size();
    const auto headerSize = static_cast<int64_t>(kHeaderSize);
    const auto body = static_cast<uint64_t>(std::max<int64_t>(0, size - headerSize));
    const uint16_t attributes = readU16(raw.data() + 186);

    // The eight bytes at 176 are shown as the written and arrived stamps: that
    // is the reading the driver takes and the one all but the oldest writers
    // meant by them. A header carrying the FTSC union instead — zone and point
    // words there — is read off the dump below, nothing in the bytes telling
    // the two apart. As the stamp the header states and as the dword it is
    // packed into, since a stamp a writer left empty is exactly what a report
    // is opened to find out.
    const auto stamp = [&raw](size_t at) {
        const domain::MessageDate date =
            fromDosStamp(readU16(raw.data() + at), readU16(raw.data() + at + 2));
        return report::stamp(date) + " (" + report::hex(readU32(raw.data() + at)) + ")";
    };
    // The address the header itself carries: a net and a node, the zone and the
    // point living in INTL/FMPT/TOPT. Written the way GoldED+ writes it in a
    // report, which is how the two programs' reports stay comparable.
    const auto address = [&raw](size_t net, size_t node) {
        return std::to_string(readU16(raw.data() + net)) + "/" +
               std::to_string(readU16(raw.data() + node));
    };

    domain::MessageInfoBlock header;
    header.fields = {
        report::field("File", fileFor(number)),
        report::field("FileSize", size < 0 ? "-" : std::to_string(size)),
        report::field("Number", std::to_string(number)),
        report::textField("From", fromFixedField(raw.data(), kFromSize)),
        report::textField("To", fromFixedField(raw.data() + 36, kToSize)),
        report::textField("Subject", fromFixedField(raw.data() + 72, kSubjectSize)),
        report::textField("DateTime",
                          fromFixedField(raw.data() + kDateOffset, kDateSize)),
        report::field("OrigAddr", address(172, 168)),
        report::field("DestAddr", address(174, 166)),
        report::field("Reply", std::to_string(readU16(raw.data() + 184))),
        report::field("See", std::to_string(readU16(raw.data() + 188))),
        report::field("TimesRead",
                      std::to_string(readU16(raw.data() + kTimesReadOffset))),
        report::field("Cost", std::to_string(readU16(raw.data() + 170))),
        report::field("Attr",
                      report::hex(attributes, 4) + " (" + report::bits(attributes) + ")"),
        report::field("Written", stamp(kWrittenOffset)),
        report::field("Arrived", stamp(kArrivedOffset)),
    };
    out.blocks.push_back(std::move(header));

    domain::MessageInfoBlock stored;
    stored.title = report::dumpTitle("Message header", kHeaderSize, kHeaderSize);
    stored.bytes.assign(reinterpret_cast<const char*>(raw.data()), raw.size());
    out.blocks.push_back(std::move(stored));

    // The kludges and the text together, as the file holds them: there is no
    // separating them on disk here, which is itself worth seeing.
    if (body != 0) {
        domain::MessageInfoBlock text;
        text.bytes = report::readDump(file, kHeaderSize, body);
        text.title = report::dumpTitle("Message text", body, text.bytes.size());
        out.blocks.push_back(std::move(text));
    }
    return out;
}

std::string SdmBase::encodeBody(const RawDraft& draft) const {
    // The kludges the two-byte address fields cannot carry, put in front where
    // the draft does not already state them — the same INTL/FMPT/TOPT rules
    // every FTN editor applies, and only for netmail: echomail is broadcast and carries
    // its path in SEEN-BY instead.
    std::vector<std::string> kludges;
    if (!echo_) {
        const auto threeD = [](const domain::FtnAddress& address) {
            return std::to_string(address.zone) + ':' + std::to_string(address.net) +
                   '/' + std::to_string(address.node);
        };
        if ((draft.header.destAddr.zone != defaultZone_ ||
             draft.header.origAddr.zone != defaultZone_) &&
            !hasKludge(draft.kludges, "INTL")) {
            kludges.push_back("INTL " + threeD(draft.header.destAddr) + " " +
                              threeD(draft.header.origAddr));
        }
        if (draft.header.origAddr.point != 0 && !hasKludge(draft.kludges, "FMPT")) {
            kludges.push_back("FMPT " + std::to_string(draft.header.origAddr.point));
        }
        if (draft.header.destAddr.point != 0 && !hasKludge(draft.kludges, "TOPT")) {
            kludges.push_back("TOPT " + std::to_string(draft.header.destAddr.point));
        }
    }
    kludges.insert(kludges.end(), draft.kludges.begin(), draft.kludges.end());

    std::string body;
    for (const auto& kludge : kludges) {
        body += '\x01';
        body += kludge;
        body += '\r';
    }
    body += draft.text;
    body += '\0';  // FTS-0001: the text is a NUL-terminated string
    return body;
}

void SdmBase::encodeHeader(const RawHeader& header, unsigned char* raw) const {
    std::memset(raw, 0, kHeaderSize);
    toFixedField(raw, kFromSize, header.from);
    toFixedField(raw + 36, kToSize, header.to);
    toFixedField(raw + 72, kSubjectSize, header.subject);
    toFixedField(raw + kDateOffset, kDateSize, ftscDate(header.written));
    writeU16(raw + kTimesReadOffset, 0);  // times read
    writeU16(raw + 166, header.destAddr.node);
    writeU16(raw + 168, header.origAddr.node);
    writeU16(raw + 170, 0);  // cost
    writeU16(raw + 172, header.origAddr.net);
    writeU16(raw + 174, header.destAddr.net);
    uint16_t date = 0;
    uint16_t time = 0;
    toDosStamp(header.written, &date, &time);
    writeU16(raw + kWrittenOffset, date);
    writeU16(raw + kWrittenOffset + 2, time);
    toDosStamp(header.arrived, &date, &time);
    writeU16(raw + kArrivedOffset, date);
    writeU16(raw + kArrivedOffset + 2, time);
    writeU16(raw + 184, static_cast<uint16_t>(header.replyTo));
    writeU16(raw + 186, static_cast<uint16_t>(header.attributes));
    writeU16(raw + 188, header.replies.empty()
                            ? uint16_t{0}
                            : static_cast<uint16_t>(header.replies.front()));
}

Result<uint32_t> SdmBase::write(const RawDraft& draft) {
    if (directory_.empty()) {
        return failure<MsgBaseError>(MsgBaseError::Kind::NoAreaOpen, std::string());
    }

    const std::string body = encodeBody(draft);
    std::array<unsigned char, kHeaderSize> raw{};
    encodeHeader(draft.header, raw.data());

    // There is no base file to lock: the message file is the unit of storage,
    // and O_EXCL is what keeps two writers off the same number. The loser of a
    // race rescans — a tosser may have filled the number in between — and
    // takes the next.
    for (int attempt = 0; attempt < 8; ++attempt) {
        auto done = scan();
        if (!done) return tl::make_unexpected(std::move(done).error());
        uint32_t number = numbers_.empty() ? 0 : numbers_.back();
        ++number;
        // In an echo area 1.msg is the high-water mark, not a message: the
        // first real message is 2.
        if (echo_ && number == 1) number = 2;

        BinaryFile file;
        if (!file.create(fileFor(number))) continue;
        if (file.writeAt(0, raw.data(), raw.size()).failed() ||
            file.writeAt(kHeaderSize, body.data(), body.size()).failed()) {
            auto reason = "cannot write " + fileFor(number);
            file.close();
            std::error_code ec;
            fs::remove(fileFor(number), ec);  // half a message is worse than none
            return failure(std::move(reason));
        }
        numbers_.push_back(number);  // scan() sorted; the new number is highest
        return count();
    }
    return failure("cannot find a free message number in " + directory_);
}

Result<void> SdmBase::replace(uint32_t index, const RawDraft& draft) {
    if (directory_.empty()) {
        return failure<MsgBaseError>(MsgBaseError::Kind::NoAreaOpen, std::string());
    }
    if (index == 0 || index > count()) {
        return failure("message " + std::to_string(index) + " is not there to change");
    }
    const uint32_t number = numbers_[index - 1];

    BinaryFile file;
    if (!file.open(fileFor(number), true) || !file.writable()) {
        return failure("cannot write " + fileFor(number));
    }
    // The message file is the base as far as this message is concerned, so it
    // is what the change is serialised on. Writing a new message needs no lock
    // — O_EXCL settles who owns a number — but rewriting one does: a tosser
    // reading it meanwhile would otherwise get half of each.
    FileLock lock;
    if (const auto locked = lock.acquire({&file}); !locked) {
        return failure<MsgBaseError>(MsgBaseError::Kind::MessageBusy,
                                     locked.error()->message());
    }

    std::array<unsigned char, kHeaderSize> was{};
    if (const auto io = file.readAt(0, was.data(), was.size()); io.failed()) {
        return failure("cannot read the header of " + fileFor(number) + ": " +
                       io.message());
    }

    const std::string body = encodeBody(draft);
    std::array<unsigned char, kHeaderSize> raw{};
    encodeHeader(draft.header, raw.data());
    // What the message keeps: when it arrived here, how often it has been read,
    // and the two links FTS-0001 gives it. The date it is written under is the
    // draft's — a message written again is written now — and it goes into both
    // the ASCII field and the packed stamp, which encodeHeader() has done.
    std::memcpy(raw.data() + kTimesReadOffset, was.data() + kTimesReadOffset, 2);
    std::memcpy(raw.data() + kArrivedOffset, was.data() + kArrivedOffset, 4);
    std::memcpy(raw.data() + 184, was.data() + 184, 2);
    std::memcpy(raw.data() + 188, was.data() + 188, 2);

    // The file is the message: what it held is written over from the top and
    // the file cut back to what the message now takes. Nothing else in the area
    // is touched — a message here is a file of its own, so its number is its
    // own too, whatever becomes of its length.
    if (file.writeAt(0, raw.data(), raw.size()).failed() ||
        file.writeAt(kHeaderSize, body.data(), body.size()).failed() ||
        !file.truncate(kHeaderSize + body.size())) {
        return failure("cannot write " + fileFor(number));
    }
    return {};
}

Result<void> SdmBase::remove(uint32_t index) {
    if (directory_.empty()) {
        return failure<MsgBaseError>(MsgBaseError::Kind::NoAreaOpen, std::string());
    }
    if (index == 0 || index > count()) {
        return failure("message " + std::to_string(index) + " is not there to delete");
    }
    const uint32_t number = numbers_[index - 1];
    std::error_code ec;
    if (!fs::remove(fileFor(number), ec) || ec) {
        return failure("cannot delete " + fileFor(number));
    }
    numbers_.erase(numbers_.begin() + static_cast<long>(index) - 1);
    return {};
}

Result<void> SdmBase::markSeen(uint32_t index) {
    if (directory_.empty()) {
        return failure<MsgBaseError>(MsgBaseError::Kind::NoAreaOpen, std::string());
    }
    if (index == 0 || index > count()) {
        return failure("message " + std::to_string(index) + " is not there to mark");
    }
    const uint32_t number = numbers_[index - 1];

    BinaryFile file;
    if (!file.open(fileFor(number), true) || !file.writable()) {
        return failure("cannot write " + fileFor(number));
    }
    // The message file is the base as far as this message is concerned, and
    // replace() may be rewriting the whole of it — including the word this
    // patches, which it carries over from what it read.
    FileLock lock;
    if (const auto locked = lock.acquire({&file}); !locked) {
        return failure<MsgBaseError>(MsgBaseError::Kind::MessageBusy,
                                     locked.error()->message());
    }

    std::array<unsigned char, 2> raw{};
    if (const auto io = file.readAt(kTimesReadOffset, raw.data(), raw.size());
        io.failed()) {
        return failure("cannot read the header of " + fileFor(number) + ": " +
                       io.message());
    }
    if (readU16(raw.data()) != 0) return {};

    writeU16(raw.data(), 1);
    if (const auto io = file.writeAt(kTimesReadOffset, raw.data(), raw.size());
        io.failed()) {
        return failure("cannot mark message " + std::to_string(index) +
                       " read: " + io.message());
    }
    return {};
}

}  // namespace amberedit::msgbase
