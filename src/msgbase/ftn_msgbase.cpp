#include "msgbase/ftn_msgbase.hpp"

#include <ctime>
#include <filesystem>

#include "config/text_util.hpp"
#include "msgbase/jam_base.hpp"
#include "msgbase/sdm_base.hpp"
#include "msgbase/squish_base.hpp"

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
        case MsgBaseType::Sdm: return std::make_unique<SdmBase>();
        case MsgBaseType::Unknown:
        case MsgBaseType::Passthrough: break;
    }
    return nullptr;
}

domain::MessageDate nowLocal() {
    const std::time_t now = std::time(nullptr);
    std::tm broken{};
    localtime_r(&now, &broken);
    domain::MessageDate date;
    date.year = static_cast<uint16_t>(broken.tm_year + 1900);
    date.month = static_cast<uint8_t>(broken.tm_mon + 1);
    date.day = static_cast<uint8_t>(broken.tm_mday);
    date.hour = static_cast<uint8_t>(broken.tm_hour);
    date.minute = static_cast<uint8_t>(broken.tm_min);
    date.second = static_cast<uint8_t>(broken.tm_sec);
    return date;
}

}  // namespace

FtnMsgBase::FtnMsgBase(std::string_view defaultCharset) : detector_(defaultCharset) {}

FtnMsgBase::~FtnMsgBase() = default;

MsgBaseType FtnMsgBase::probeType(const std::string& path) {
    if (path.empty()) return MsgBaseType::Unknown;
    std::error_code ec;

    if (std::filesystem::exists(path + ".sqd", ec)) return MsgBaseType::Squish;
    if (std::filesystem::exists(path + ".jhr", ec)) return MsgBaseType::Jam;
    if (std::filesystem::is_directory(path, ec)) return MsgBaseType::Sdm;
    return MsgBaseType::Unknown;
}

bool FtnMsgBase::open(const AreaConfig& area) {
    close();
    lastError_.clear();
    areaConfig_ = area;

    if (area.isPassthrough()) {
        lastError_ = "area " + area.tag + " is passthrough: there is no base on disk";
        return false;
    }

    // The tosser config need not state a type — work it out from the files.
    MsgBaseType type = area.type;
    if (type == MsgBaseType::Unknown) {
        type = probeType(area.path);
        areaConfig_.type = type;
    }

    // A base the tosser config names but that was never created is ordinary;
    // saying which format was looked for is the useful half of the message.
    if (probeType(area.path) != type) {
        lastError_ = "no " + domain::toString(type) + " base at " + area.path;
        return false;
    }

    std::unique_ptr<FormatDriver> driver = makeDriver(type);
    if (!driver) {
        lastError_ = "cannot determine the base type for " + area.path;
        return false;
    }

    // Fido *.msg headers carry no zone of their own; the area's AKA is what
    // its messages are read under.
    const uint16_t defaultZone = area.address.isValid() ? area.address.zone : 2;
    if (!driver->open(area.path, area.kind != AreaKind::Netmail, defaultZone)) {
        lastError_ = "cannot open base " + area.path + ": " + driver->lastError();
        return false;
    }
    driver_ = std::move(driver);
    return true;
}

bool FtnMsgBase::isAbsent(const AreaConfig& area) {
    // A passthrough area has no path at all, and an area whose type nothing
    // states is one there is no format to create. Both are answered "not
    // absent": there is nothing missing that making a base would supply.
    if (area.isPassthrough() || area.type == MsgBaseType::Unknown) return false;
    return probeType(area.path) == MsgBaseType::Unknown;
}

bool FtnMsgBase::create(const AreaConfig& area) {
    close();
    lastError_.clear();

    if (!isAbsent(area)) {
        lastError_ = "there is already a base at " + area.path;
        return false;
    }
    std::unique_ptr<FormatDriver> driver = makeDriver(area.type);
    if (!driver) {
        // isAbsent() has already refused Unknown and Passthrough, so this is
        // a format added to the enum and not to makeDriver().
        lastError_ = "cannot create a base of type " + domain::toString(area.type);
        return false;
    }
    // The driver's own words, unwrapped: it names the file it could not make
    // and why, which is the whole of what there is to say, and a prefix of ours
    // would only say "cannot create" a second time.
    if (!driver->create(area.path)) {
        lastError_ = driver->lastError();
        return false;
    }
    return true;
}

void FtnMsgBase::close() {
    driver_.reset();
    lastError_.clear();
}

uint32_t FtnMsgBase::count() const {
    if (!driver_) return 0;
    return driver_->count();
}

MessageHeader FtnMsgBase::header(uint32_t index) const {
    MessageHeader out;
    out.number = index;
    if (!driver_ || index == 0 || index > driver_->count()) return out;

    RawMessage raw;
    if (!driver_->read(index, raw, /*withText=*/false)) {
        lastError_ =
            "cannot read message " + std::to_string(index) + ": " + driver_->lastError();
        return out;
    }

    // The names and the subject are in the same charset as the body, and the
    // CHRS kludge that says which is part of the message rather than of the
    // header. Reading the control lines here is what keeps a message list from
    // showing subjects in one charset while the reader shows the body in
    // another.
    const std::string charset = detector_.detect(raw.control);
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

MessageBody FtnMsgBase::body(uint32_t index) const {
    MessageBody out;
    if (!driver_ || index == 0 || index > driver_->count()) return out;

    RawMessage raw;
    if (!driver_->read(index, raw, /*withText=*/true)) {
        lastError_ =
            "cannot read message " + std::to_string(index) + ": " + driver_->lastError();
        return out;
    }

    const std::string whole = raw.control + raw.text;
    splitBody(whole, out);

    out.charset = detector_.detect(whole);
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

    raw.header.from = recoder_.fromUtf8(draft.from, charset);
    raw.header.to = recoder_.fromUtf8(draft.to, charset);
    raw.header.subject = recoder_.fromUtf8(draft.subject, charset);
    raw.header.origAddr = draft.origAddr;
    raw.header.destAddr = draft.destAddr;
    raw.header.utcOffsetMinutes = draft.utcOffsetMinutes;

    for (const auto& kludge : draft.kludges) {
        raw.kludges.push_back(recoder_.fromUtf8(kludge, charset));
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

uint32_t FtnMsgBase::write(const domain::MessageDraft& draft) {
    lastError_.clear();
    if (!driver_) {
        lastError_ = "no area is open";
        return 0;
    }

    RawDraft raw = encode(draft);
    // Written now, unless the draft carries a stamp of its own — a message
    // copied or moved out of another area, which was written when it says it
    // was. It arrives here either way: that stamp is this base's own, and the
    // message is reaching it at this moment however old it is.
    const domain::MessageDate now = nowLocal();
    raw.header.written = draft.written.isValid() ? draft.written : now;
    raw.header.arrived = now;

    const uint32_t number = driver_->write(raw);
    if (number == 0) {
        lastError_ = "cannot write the message: " + driver_->lastError();
        return 0;
    }
    return number;
}

bool FtnMsgBase::replace(uint32_t index, const domain::MessageDraft& draft) {
    lastError_.clear();
    if (!driver_) {
        lastError_ = "no area is open";
        return false;
    }
    // Stamped now, like any other message the editor writes: what a changed
    // message is dated by is when it was last written by hand. The stamp it
    // arrived here under is the driver's to keep — that one no rewriting
    // changes — which is why only the written date is filled in.
    RawDraft raw = encode(draft);
    raw.header.written = nowLocal();

    if (!driver_->replace(index, raw)) {
        lastError_ = "cannot change message " + std::to_string(index) + ": " +
                     driver_->lastError();
        return false;
    }
    return true;
}

bool FtnMsgBase::remove(uint32_t index) {
    lastError_.clear();
    if (!driver_) {
        lastError_ = "no area is open";
        return false;
    }
    if (!driver_->remove(index)) {
        lastError_ = "cannot delete message " + std::to_string(index) + ": " +
                     driver_->lastError();
        return false;
    }
    return true;
}

bool FtnMsgBase::markSeen(uint32_t index) {
    lastError_.clear();
    if (!driver_) {
        lastError_ = "no area is open";
        return false;
    }
    if (!driver_->markSeen(index)) {
        lastError_ = "cannot mark message " + std::to_string(index) + " read: " +
                     driver_->lastError();
        return false;
    }
    return true;
}

}  // namespace amberedit::msgbase
