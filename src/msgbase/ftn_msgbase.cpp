#include "msgbase/ftn_msgbase.hpp"

#include <algorithm>
#include <cstddef>
#include <ctime>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "config/text_util.hpp"
#include "msgbase/jam_base.hpp"
#include "msgbase/opus_base.hpp"
#include "msgbase/squish_base.hpp"
#include "msgbase/echotoss_log.hpp"
#include "sys/time.hpp"

namespace amberedit::msgbase {

using domain::AreaConfig;
using domain::AreaKind;
using domain::MessageBody;
using domain::MessageHeader;
using domain::MsgBaseType;

namespace {

constexpr char kSoh = '\x01';

/// Splits a raw body — the control lines first, then the text, as the drivers
/// hand it back — into lines, marking the service ones. The order is left
/// exactly as the base has it: the AREA: line and MSGID and friends ahead of
/// the text, SEEN-BY and PATH behind the origin line, because that is
/// information in itself.
void splitBody(std::string_view raw, MessageBody& out) {
    size_t pos = 0;
    while (pos <= raw.size()) {
        size_t lineEnd = raw.find_first_of("\r\n", pos);
        const bool lastLine = lineEnd == std::string_view::npos;
        if (lastLine) lineEnd = raw.size();
        std::string_view line = raw.substr(pos, lineEnd - pos);

        // Service data stored without a ^A, at either end of the message: the
        // AREA: line naming the echo a message arrived in, which FTS-0001 puts
        // in front of everything and which is therefore only ever the very
        // first line of one, and the SEEN-BY routing behind the origin. Both
        // are shown exactly as the base has them — there is no ^A to stand in
        // for.
        const bool bare = (pos == 0 && config::text::startsWith(line, "AREA:")) ||
                          config::text::startsWith(line, "SEEN-BY:");

        if (!line.empty() && line.front() == kSoh) {
            // ^A cannot be printed, and '@' is the conventional stand-in for it.
            out.lines.push_back({"@" + std::string(line.substr(1)), true, false});
        } else if (bare) {
            out.lines.push_back({std::string(line), true, false});
        } else {
            if (domain::isOriginLine(line)) out.origin = std::string(line);
            out.lines.push_back({std::string(line), false, false});
        }

        if (lastLine) break;
        pos = lineEnd + 1;
        // Treat \r\n as a single line break.
        if (raw[lineEnd] == '\r' && pos < raw.size() && raw[pos] == '\n') ++pos;
    }

    // Trailing blank text lines are padding, not content.
    while (!out.lines.empty() && !out.lines.back().kludge &&
           out.lines.back().text.find_first_not_of(' ') == std::string::npos) {
        out.lines.pop_back();
    }

    domain::markTrailer(out.lines);
}

/// The driver for a base type, or nothing where the type names no format we
/// have one for. Opening a base and creating one both start here, and a format
/// added to the switch is added to both at once.
std::unique_ptr<FormatDriver> makeDriver(MsgBaseType type) {
    switch (type) {
        case MsgBaseType::Squish: return std::make_unique<SquishBase>();
        case MsgBaseType::Jam: return std::make_unique<JamBase>();
        case MsgBaseType::Opus: return std::make_unique<OpusBase>();
        case MsgBaseType::Unknown:
        case MsgBaseType::Passthrough: break;
    }
    return nullptr;
}

/// What FTS-0001 leaves for the text of a header field in a packed message: it
/// keeps 36 bytes for either name and 72 for the subject, the terminating zero
/// among them. The same room the stored message has, and the same numbers
/// Squish and Fido *.msg lay their fixed fields out by.
constexpr size_t kNameBytes = 35;
constexpr size_t kSubjectBytes = 71;

/// The start of the character `pos` stands in the middle of, or `pos` itself
/// where it already begins one. UTF-8 continuation bytes are the only ones with
/// their top two bits at 10, so walking back over them lands on a lead byte.
size_t charStart(const std::string& text, size_t pos) {
    while (pos > 0 && (static_cast<unsigned char>(text[pos]) & 0xC0) == 0x80) --pos;
    return pos;
}

/// The field encoded in `charset` and cut to `capacity` bytes: the longest run
/// of whole characters from the front of it that fits.
///
/// The cut is made on the UTF-8 side and the text encoded again, rather than
/// taken off the encoded bytes, because a byte count lands inside a character
/// in every charset that spells one in more than one byte — UTF-8 being one a
/// message may well be written in, and one where a field ending in half a
/// character is a field no reader can make anything of. Which character the
/// cut falls after is a question only the encoder can answer, since what a
/// character costs is the charset's business, so the answer is asked for by
/// encoding a shorter piece until one fits.
std::string fitField(encoding::IconvRecoder& recoder, const std::string& text,
                     const std::string& charset, size_t capacity) {
    std::string encoded = recoder.fromUtf8(text, charset);
    size_t end = text.size();
    while (encoded.size() > capacity && end > 0) {
        end = charStart(text, end - 1);
        encoded = recoder.fromUtf8(text.substr(0, end), charset);
    }
    return encoded;
}

/// Whether a message reaching a base is one a tosser has to scan out of it:
/// written here and not yet sent.
///
/// **This is what decides the `echotosslog` line**, and it is the message's own
/// two attributes rather than which call wrote it. A message composed here
/// starts Loc (`app::startingAttributes()`) and a change takes Snt back off, so
/// everything the user sits down and writes qualifies; mail that arrived from
/// the network carries neither and does not, however it comes to be written into
/// an area. Which is the whole of the rule: carrying somebody else's mail from
/// one echo to another is not this node writing it, and a tosser told otherwise
/// would export the lot a second time — while a message of the user's own
/// carried into another area still has to go out of it.
bool needsScanningOut(uint32_t attributes) {
    return (attributes & domain::attr::kLocal) != 0 &&
           (attributes & domain::attr::kSent) == 0;
}

domain::MessageDate nowLocal() {
    const std::time_t now = std::time(nullptr);
    const std::tm broken = sys::localTime(now);
    domain::MessageDate date;
    date.year = static_cast<uint16_t>(broken.tm_year + 1900);
    date.month = static_cast<uint8_t>(broken.tm_mon + 1);
    date.day = static_cast<uint8_t>(broken.tm_mday);
    date.hour = static_cast<uint8_t>(broken.tm_hour);
    date.minute = static_cast<uint8_t>(broken.tm_min);
    date.second = static_cast<uint8_t>(broken.tm_sec);
    return date;
}

/// The files a base of this format is read through, in the order a person
/// would look for them: the one the base is found by first.
///
/// A Fido *.msg area has none — the base is the directory and every file in it
/// is a message, so there is no file whose absence makes it broken rather than
/// empty. An area of an unknown or passthrough type has none either, and for
/// the same reason nothing here asks about it.
std::vector<std::string> partsOf(MsgBaseType type, const std::string& path) {
    switch (type) {
        case MsgBaseType::Squish: return {path + ".sqd", path + ".sqi"};
        case MsgBaseType::Jam: return {path + ".jhr", path + ".jdx", path + ".jdt"};
        default: return {};
    }
}

/// Which of them are not there, in that same order. Empty for a base that is
/// all present, and for a format with no files to count.
std::vector<std::string> missingParts(MsgBaseType type, const std::string& path) {
    std::vector<std::string> missing;
    std::error_code ec;
    for (std::string& file : partsOf(type, path)) {
        if (!std::filesystem::exists(file, ec)) missing.push_back(std::move(file));
    }
    return missing;
}

/// Whether a base of exactly this format stands at the path: the file it is
/// found by, or, for Fido *.msg, the directory that is the base.
///
/// This is the question `probeType()` answers for every format at once, asked
/// of one. Where the config states a type, that is the only format the area has
/// anything to do with: a Squish base a tosser keeps under the same name is
/// another area's files sharing a directory, and it neither opens this area nor
/// stands between the user and creating it.
bool standsAs(MsgBaseType type, const std::string& path) {
    std::error_code ec;
    switch (type) {
        case MsgBaseType::Squish: return std::filesystem::exists(path + ".sqd", ec);
        case MsgBaseType::Jam: return std::filesystem::exists(path + ".jhr", ec);
        case MsgBaseType::Opus: return std::filesystem::is_directory(path, ec);
        default: return false;
    }
}

/// Whether nothing of this format is at the path — not the file it is found by,
/// and not one of the others it is read through either. The one state creating
/// a base answers, and what `isAbsent()` is.
bool nothingOfItAt(MsgBaseType type, const std::string& path) {
    return !standsAs(type, path) &&
           missingParts(type, path).size() == partsOf(type, path).size();
}

/// The missing ones as the error names them: `a.jdx, a.jdt`. One sentence with
/// a list in it rather than a sentence per file — they went together and they
/// are put back together.
std::string listOf(const std::vector<std::string>& files) {
    std::string out;
    for (const std::string& file : files) {
        if (!out.empty()) out += ", ";
        out += file;
    }
    return out;
}

}  // namespace

FtnMsgBase::FtnMsgBase(std::string_view defaultCharset, bool fieldLimits, bool ucsKludges,
                       std::string echotossLogPath)
    : detector_(defaultCharset),
      fieldLimits_(fieldLimits),
      ucsKludges_(ucsKludges),
      echotossLogPath_(std::move(echotossLogPath)) {}

FtnMsgBase::~FtnMsgBase() = default;

MsgBaseType FtnMsgBase::probeType(const std::string& path) {
    if (path.empty()) return MsgBaseType::Unknown;
    std::error_code ec;

    if (std::filesystem::exists(path + ".sqd", ec)) return MsgBaseType::Squish;
    if (std::filesystem::exists(path + ".jhr", ec)) return MsgBaseType::Jam;
    if (std::filesystem::is_directory(path, ec)) return MsgBaseType::Opus;
    return MsgBaseType::Unknown;
}

tl::expected<void, ErrorPtr> FtnMsgBase::open(const AreaConfig& area) {
    close();
    areaConfig_ = area;

    if (area.isPassthrough()) {
        return failure<MsgBaseError>(MsgBaseError::Kind::Passthrough, area.tag);
    }

    // The tosser config need not state a type — work it out from the files.
    MsgBaseType type = area.type;
    if (type == MsgBaseType::Unknown) {
        type = probeType(area.path);
        areaConfig_.type = type;
    }

    // Nothing states the format and nothing on disk suggested one: there is no
    // driver to ask and no base to look for. Said before the path is, because
    // "cannot determine the base type" is the complaint, not a missing base.
    std::unique_ptr<FormatDriver> driver = makeDriver(type);
    if (!driver) {
        return failure<MsgBaseError>(MsgBaseError::Kind::UnknownType, area.path);
    }

    // Everything below asks about `type` and about nothing else that may be at
    // the path. An area declared JAM where a Squish base already stands under
    // the same name is a JAM area: the .sqd belongs to whoever put it there,
    // and it neither opens this one nor is a reason to refuse to make it. Only
    // an area whose type nothing states goes by what is on disk, and that was
    // settled above.
    //
    // A base the tosser config names but that was never created is ordinary,
    // and `Absent` is what `AreaManager` offers to create on; saying which
    // format was looked for is the useful half of the message.
    //
    // Which of the format's own files are on disk is asked here and not left to
    // the driver: the driver would refuse a base missing its index fine, but
    // only this knows that the file it is missing is one of a set and that the
    // rest of that set is standing there holding messages. `Incomplete` is what
    // says so, and it is what keeps `AreaManager` from creating over it.
    if (nothingOfItAt(type, area.path)) {
        return failure<MsgBaseError>(MsgBaseError::Kind::Absent, area.path,
                                     std::string(domain::nameOf(type)));
    }
    if (const std::vector<std::string> missing = missingParts(type, area.path);
        !missing.empty()) {
        return failure<MsgBaseError>(MsgBaseError::Kind::Incomplete, area.path,
                                     listOf(missing));
    }

    // Fido *.msg headers carry no zone of their own; the area's AKA is what
    // its messages are read under.
    const uint16_t defaultZone = area.address.isValid() ? area.address.zone : 2;
    const auto opened =
        driver->open(area.path, area.kind != AreaKind::Netmail, defaultZone);
    if (!opened) {
        return failure<MsgBaseError>(MsgBaseError::Kind::CannotOpen, area.path,
                                     opened.error()->message());
    }
    driver_ = std::move(driver);
    return {};
}

bool FtnMsgBase::isAbsent(const AreaConfig& area) {
    // A passthrough area has no path at all, and an area whose type nothing
    // states is one there is no format to create. Both are answered "not
    // absent": there is nothing missing that making a base would supply.
    if (area.isPassthrough() || area.type == MsgBaseType::Unknown) return false;
    // Of the area's own format, and of nothing else standing at the path: a
    // base of another format there belongs to another area, and refusing to
    // make this one because of it would leave the user with an area that
    // neither opens nor can be created.
    //
    // And *nothing* of it, which is more than the file a base is found by: one
    // that lost its .jhr or its .sqd still has the rest of itself on disk, and
    // that is messages. open() calls that `Incomplete`.
    return nothingOfItAt(area.type, area.path);
}

tl::expected<void, ErrorPtr> FtnMsgBase::create(const AreaConfig& area) {
    close();

    if (!isAbsent(area)) {
        // Something of this format is already on disk. A whole base is
        // `AlreadyExists`; the files a base that lost one left behind are
        // `Incomplete` and name what is gone — "there is already a base" would
        // send the user looking for one that no longer opens.
        if (const std::vector<std::string> missing = missingParts(area.type, area.path);
            !missing.empty() &&
            missing.size() < partsOf(area.type, area.path).size()) {
            return failure<MsgBaseError>(MsgBaseError::Kind::Incomplete, area.path,
                                         listOf(missing));
        }
        return failure<MsgBaseError>(MsgBaseError::Kind::AlreadyExists, area.path);
    }
    std::unique_ptr<FormatDriver> driver = makeDriver(area.type);
    if (!driver) {
        // isAbsent() has already refused Unknown and Passthrough, so this is
        // a format added to the enum and not to makeDriver().
        return failure<MsgBaseError>(MsgBaseError::Kind::CannotMakeType,
                                     std::string(domain::nameOf(area.type)));
    }
    // The driver's own words, unwrapped: it names the file it could not make
    // and why, which is the whole of what there is to say, and a prefix of ours
    // would only say "cannot create" a second time.
    return driver->create(area.path);
}

void FtnMsgBase::close() {
    driver_.reset();
}

uint32_t FtnMsgBase::count() const {
    if (!driver_) return 0;
    return driver_->count();
}

MessageHeader FtnMsgBase::header(uint32_t index) const {
    return header(index, std::string{});
}

MessageBody FtnMsgBase::body(uint32_t index) const {
    return body(index, std::string{});
}

MessageHeader FtnMsgBase::header(uint32_t index, const std::string& asked) const {
    MessageHeader out;
    out.number = index;
    if (!driver_ || index == 0 || index > driver_->count()) return out;

    RawMessage raw;
    // A message that cannot be read comes back empty, as the port says it does:
    // this is drawn from a row at a time and there is nowhere to say more.
    if (!driver_->read(index, raw, /*withText=*/false)) return out;

    // The names and the subject are in the same charset as the body, and the
    // CHRS kludge that says which is part of the message rather than of the
    // header. Reading the control lines here is what keeps a message list from
    // showing subjects in one charset while the reader shows the body in
    // another.
    //
    // Or the charset asked for, where the reader has asked for one. Taken as it
    // stands rather than put through `detect()`: it is an iconv name by the time
    // it reaches this class, and that one falls back on the area's default for a
    // name it does not know, which is the opposite of what somebody who has just
    // typed a name means.
    const std::string charset = asked.empty() ? detector_.detect(raw.control) : asked;
    out.charset = charset;
    out.from = recoder_.toUtf8(raw.header.from, charset);
    out.to = recoder_.toUtf8(raw.header.to, charset);
    out.subject = recoder_.toUtf8(raw.header.subject, charset);
    out.date = raw.header.written;
    out.arrivalDate = raw.header.arrived;
    if (!out.date.isValid()) out.date = out.arrivalDate;
    // Which clock the written stamp is on, out of the same control lines the
    // charset came from — they are read here already, and a Date column that
    // shows the offset must not have to read the message's text to find it.
    out.utcOffset = tzutcOffsetOf(raw.control);
    out.origAddr = raw.header.origAddr;
    out.destAddr = raw.header.destAddr;
    out.attributes = raw.header.attributes;
    out.seen = raw.header.seen;
    return out;
}

MessageBody FtnMsgBase::body(uint32_t index, const std::string& asked) const {
    MessageBody out;
    if (!driver_ || index == 0 || index > driver_->count()) return out;

    RawMessage raw;
    if (!driver_->read(index, raw, /*withText=*/true)) return out;

    const std::string whole = raw.control + raw.text;
    splitBody(whole, out);

    // The control lines, and not the text after them, for the same reason
    // header() reads them: one message is read in one charset, and a charset
    // taken from the whole body here would be a charset a message list built
    // from the control block alone could not arrive at.
    // Or the charset asked for, as in header() and for the same reasons.
    const std::string declared = detector_.detect(raw.control);
    out.charset = asked.empty() ? declared : asked;

    // Whether that answer came from the message or from the area's
    // `default_charset` — which is what the reader tells the user before asking
    // them to name another one. Asked of what `detect()` settled on rather than
    // of the kludge alone: a CHRS naming something this machine's iconv has
    // never heard of is a kludge that decided nothing, and the default stood in
    // for it. Read off the message either way, so it still says what the
    // message declares once another charset has been asked for.
    const std::string named = encoding::CharsetDetector::normalize(
        encoding::CharsetDetector::extractChrsKludge(raw.control));
    out.charsetDeclared = !named.empty() && named == declared;
    for (auto& line : out.lines) line.text = recoder_.toUtf8(line.text, out.charset);
    out.origin = recoder_.toUtf8(out.origin, out.charset);
    return out;
}

domain::MessageThread FtnMsgBase::thread(uint32_t index) const {
    domain::MessageThread out;
    if (!driver_ || index == 0 || index > driver_->count()) return out;

    RawMessage raw;
    if (!driver_->read(index, raw, /*withText=*/false)) return out;

    // The links are kept as UIDs; what the reader shows is positions, and a
    // link to a message that has since been deleted is left out rather than
    // pointed at nothing.
    out.replyTo = driver_->indexOfUid(raw.header.replyTo, /*exact=*/true);
    for (const uint32_t uid : raw.header.replies) {
        if (const uint32_t number = driver_->indexOfUid(uid, /*exact=*/true)) {
            out.replies.push_back(number);
        }
    }
    return out;
}

domain::MessageInfo FtnMsgBase::info(uint32_t index) const {
    domain::MessageInfo out;
    if (!driver_ || index == 0 || index > driver_->count()) return out;

    out = driver_->info(index);

    // The values that are text out of the message are in the message's own
    // charset, like everything else a driver hands back, and they are converted
    // here for the same reason the header fields are: above this adapter there
    // is nothing but UTF-8. The numbers, the offsets and the attributes are left
    // exactly as the driver wrote them — they are ASCII in every charset there
    // is, and running them through iconv would only invite it to have an
    // opinion.
    //
    // The dumped bytes are not converted at all: they are the bytes, which is
    // the whole point of showing them, and what stands beside them on screen is
    // an ASCII column rather than a decoded one.
    RawMessage raw;
    if (!driver_->read(index, raw, /*withText=*/false)) return out;
    const std::string charset = detector_.detect(raw.control);
    for (auto& block : out.blocks) {
        for (auto& field : block.fields) {
            if (field.text) field.value = recoder_.toUtf8(field.value, charset);
        }
    }
    return out;
}

uint32_t FtnMsgBase::uidOf(uint32_t index) const {
    if (!driver_) return 0;
    return driver_->uidOf(index);
}

uint32_t FtnMsgBase::indexOfUid(uint32_t uid) const {
    if (!driver_ || uid == 0) return 0;
    // Not exact: a mark left on a message that has since been packed away
    // still says how far the reading got, and the message before it is the
    // honest answer — the same one GoldED settles on.
    return driver_->indexOfUid(uid, /*exact=*/false);
}

RawDraft FtnMsgBase::encode(const domain::MessageDraft& draft) const {
    RawDraft raw;
    // The attributes as the draft states them, MSGLOCAL and MSGPRIVATE included:
    // they are the author's, decided on the compose screen where they can be
    // seen and changed, and a base that added bits of its own would be writing
    // a message nobody asked for.
    raw.header.attributes = draft.attributes;
    // The charset the draft names, and where it names none — a message copied
    // out of a body that could not be read — the one this area is read in. It
    // is `default_charset` rather than `compose_charset`, which the builder has
    // already had its say about: this is not a message being written here, it
    // is one being put back, and the area's own charset is the closest thing to
    // the charset it came in.
    const std::string charset =
        draft.charset.empty() ? detector_.defaultCharset() : draft.charset;

    // Whether the message states its whole From, To and Subject in the control
    // lines FSP-1030 keeps for them, `ucs_kludges` asking. Only in UTF-8: that
    // is the charset the standard is about, and the one where a field cut to
    // its 35 or 71 bytes loses three quarters of a name rather than none of it.
    const bool statesUcsFields = ucsKludges_ && domain::isUtf8Charset(charset);
    std::vector<std::string> ucsFieldLines;

    // Cut to what a packed message has room for where `compose_fts1_field_limits`
    // asks for it, and this is the one place the count can be taken: the editor
    // holds the fields to a length in characters, and what those cost in bytes
    // is decided here, by the charset the message is written in. Off, they go
    // as they were typed and the format decides — Squish and Fido *.msg cut
    // them to their fixed fields, JAM stores what it is handed.
    //
    // A field that overruns its room in a UTF-8 message is cut whatever that
    // setting says, and its whole text written into a UCS line beside it. The
    // setting is about what a field too long for a packet is left to — the
    // format, or this cut — and there is no third answer once the message
    // itself states what the field was cut out of: FSP-1030 has the packed
    // field never empty, so it is written short and the line says the rest.
    const auto field = [&](const std::string& text, size_t capacity,
                           std::string_view line) {
        if (statesUcsFields && text.size() > capacity) {
            ucsFieldLines.emplace_back(std::string(line) + ' ' + text);
            return fitField(recoder_, text, charset, capacity);
        }
        return fieldLimits_ ? fitField(recoder_, text, charset, capacity)
                            : recoder_.fromUtf8(text, charset);
    };
    raw.header.from = field(draft.from, kNameBytes, domain::ucs::kFromLine);
    raw.header.to = field(draft.to, kNameBytes, domain::ucs::kToLine);
    raw.header.subject = field(draft.subject, kSubjectBytes, domain::ucs::kSubjectLine);
    raw.header.origAddr = draft.origAddr;
    raw.header.destAddr = draft.destAddr;
    raw.header.utcOffsetMinutes = draft.utcOffsetMinutes;
    // The thread link, turned from the number the draft names into the UID the
    // formats keep it as — the same conversion `thread()` makes the other way
    // round. A number naming no message here leaves the link at nothing rather
    // than at a UID the base never issued.
    raw.header.replyTo = draft.replyTo != 0 ? driver_->uidOf(draft.replyTo) : 0;

    for (const auto& kludge : draft.kludges) {
        // A UCS line the draft carried is about the message it was read out of.
        // A copy or a change writes its own fields and its own cut, and the old
        // line left standing would name a text this message's field was never
        // cut out of — or none at all, where the field now fits.
        if (statesUcsFields && domain::isUcsFieldLine(kludge)) continue;
        raw.kludges.push_back(recoder_.fromUtf8(kludge, charset));
    }
    // Behind the lines the draft brought, as the last thing said about the
    // message: these describe its header fields, and nothing routes by them.
    for (const auto& line : ucsFieldLines) {
        raw.kludges.push_back(recoder_.fromUtf8(line, charset));
    }
    // A hard carriage return ends a line in an FTN message (FTS-0001); the
    // 0x0A a text editor would leave has no place in one, which is why the
    // draft carries lines rather than text.
    for (const auto& line : draft.lines) {
        raw.text += recoder_.fromUtf8(line, charset);
        raw.text += '\r';
    }
    return raw;
}

RawDraft FtnMsgBase::encodeForWriting(const domain::MessageDraft& draft) const {
    RawDraft raw = encode(draft);
    // Written now, unless the draft carries a stamp of its own — a message
    // copied or moved out of another area, which was written when it says it
    // was. It arrives here either way: that stamp is this base's own, and the
    // message is reaching it at this moment however old it is.
    const domain::MessageDate now = nowLocal();
    raw.header.written = draft.written.isValid() ? draft.written : now;
    raw.header.arrived = now;
    return raw;
}

tl::expected<uint32_t, ErrorPtr> FtnMsgBase::write(const domain::MessageDraft& draft) {
    if (!driver_)
        return failure<MsgBaseError>(MsgBaseError::Kind::NoAreaOpen, std::string());

    const auto written = driver_->write(encodeForWriting(draft));
    if (!written)
        return failure("cannot write the message: " + written.error()->message());
    // The area now holds a message nothing outside AmberEdit has been told
    // about. Where the config names an `echotosslog`, this is where the area is
    // named in it — after the base has taken the message and not before, so
    // that nothing is announced that was never written, and only for a message
    // there is something to announce about: see `needsScanningOut()`.
    if (needsScanningOut(draft.attributes)) {
        appendEchotossLog(echotossLogPath_, areaConfig_.tag);
    }
    return *written;
}

ports::WriteReport FtnMsgBase::writeAll(const std::vector<domain::MessageDraft>& drafts) {
    ports::WriteReport report;
    if (!driver_) {
        report.failed =
            failure<MsgBaseError>(MsgBaseError::Kind::NoAreaOpen, std::string()).value();
        return report;
    }
    if (drafts.empty()) return report;

    // Every draft encoded before any of them is written, which is what the
    // driver takes: the lock goes on down there and converting a message out of
    // UTF-8 is not work to be doing while an area is held unwritable. It is a
    // second copy of the set for the length of the call — bounded by what the
    // caller hands over, and `app::kChunkBytes` is what bounds that.
    std::vector<RawDraft> raw;
    raw.reserve(drafts.size());
    for (const auto& draft : drafts) raw.push_back(encodeForWriting(draft));

    WriteReport done = driver_->writeAll(raw);
    report.written = done.written;
    if (done.failed) {
        // How far it got as well as what stopped it: a set half written is the
        // answer a caller acts on, and "cannot write 2000 messages" said of a
        // run that wrote 1999 of them would be the one thing it must not
        // believe.
        report.failed = failure("wrote " + std::to_string(report.written) + " of " +
                                std::to_string(raw.size()) +
                                " messages: " + done.failed->message())
                            .value();
    }

    // One line for the set, and only where something in it has to go out of this
    // area — the same question `write()` asks of its one message, asked of the
    // ones that reached the base rather than of the ones that were offered. A
    // carried set is usually mail that arrived from the network and says nothing
    // here; a set of the user's own, carried into another area, still has to be
    // scanned out of it. The line says the area has something new in it, and it
    // says that no better for being written once a message.
    const auto arrived = drafts.begin() + static_cast<std::ptrdiff_t>(report.written);
    if (std::any_of(drafts.begin(), arrived, [](const domain::MessageDraft& one) {
            return needsScanningOut(one.attributes);
        })) {
        appendEchotossLog(echotossLogPath_, areaConfig_.tag);
    }
    return report;
}

tl::expected<void, ErrorPtr> FtnMsgBase::replace(uint32_t index,
                                                 const domain::MessageDraft& draft) {
    if (!driver_)
        return failure<MsgBaseError>(MsgBaseError::Kind::NoAreaOpen, std::string());
    // Stamped now, like any other message the editor writes: what a changed
    // message is dated by is when it was last written by hand. The stamp it
    // arrived here under is the driver's to keep — that one no rewriting
    // changes — which is why only the written date is filled in.
    RawDraft raw = encode(draft);
    raw.header.written = nowLocal();

    const auto changed = driver_->replace(index, raw);
    if (!changed) {
        return failure("cannot change message " + std::to_string(index) + ": " +
                       changed.error()->message());
    }
    return {};
}

tl::expected<void, ErrorPtr> FtnMsgBase::remove(uint32_t index) {
    if (!driver_)
        return failure<MsgBaseError>(MsgBaseError::Kind::NoAreaOpen, std::string());
    const auto removed = driver_->remove(index);
    if (!removed) {
        return failure("cannot delete message " + std::to_string(index) + ": " +
                       removed.error()->message());
    }
    return {};
}

tl::expected<void, ErrorPtr> FtnMsgBase::removeAll(const std::vector<uint32_t>& indexes) {
    if (!driver_)
        return failure<MsgBaseError>(MsgBaseError::Kind::NoAreaOpen, std::string());
    const auto removed = driver_->removeAll(indexes);
    if (!removed) {
        return failure("cannot delete " + std::to_string(indexes.size()) +
                       " messages: " + removed.error()->message());
    }
    return {};
}

tl::expected<void, ErrorPtr> FtnMsgBase::markSeen(uint32_t index) {
    if (!driver_)
        return failure<MsgBaseError>(MsgBaseError::Kind::NoAreaOpen, std::string());
    const auto marked = driver_->markSeen(index);
    if (!marked) {
        return failure("cannot mark message " + std::to_string(index) +
                       " read: " + marked.error()->message());
    }
    return {};
}

}  // namespace amberedit::msgbase
