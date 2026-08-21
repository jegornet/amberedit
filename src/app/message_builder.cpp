#include "app/message_builder.hpp"

#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <sstream>

#include "app/copy_commands.hpp"
#include "app/msg_template.hpp"
#include "app/quoting.hpp"
#include "config/text_util.hpp"
#include "version.hpp"

namespace amberedit::app {
namespace {

/// The tearline built from what the config puts after the "--- ". A tearline
/// names the program that wrote the message (FTS-0004) — the one thing it is
/// read for, when a message turns out to have been written wrong — but what it
/// says is the writer's to decide, and an empty one is still a tearline.
std::string tearlineFrom(const std::string& text) {
    return text.empty() ? "---" : "--- " + text;
}

/// The address as INTL writes it: three dimensions, the point left off. FSC-0004
/// addresses zone:net/node there, and the points travel in FMPT and TOPT.
std::string threeDimensional(const domain::FtnAddress& address) {
    return std::to_string(address.zone) + ':' + std::to_string(address.net) + '/' +
           std::to_string(address.node);
}

std::string addressText(const domain::FtnAddress& address) {
    return address.isValid() ? address.toString() : "";
}

/// The origin line a message written from `address` closes with: the text the
/// config gives, between the marker and the address in brackets.
std::string originLine(const std::string& text, const std::string& address) {
    return " * Origin: " + text + " (" + address + ")";
}

bool isBlank(std::string_view line) {
    return line.find_first_not_of(" \t") == std::string_view::npos;
}

/// The message closed off the way FTS-0004 asks for: a tearline naming what
/// wrote it, then the origin line carrying the address it was written from.
///
/// The editor opens with both already there, so most of the time they are the
/// last two lines and this leaves them alone. A user who deleted them gets
/// them back. One left in the middle of the message is not this message's
/// closing pair — it is text that happens to look like one, or a pair the
/// typing moved — and is invalidated rather than left to be read as ours: a
/// tosser stops at the first tearline it finds, and would cut the message
/// there.
std::vector<std::string> closeMessage(std::vector<std::string> lines,
                                      const std::string& tearline,
                                      const std::string& origin) {
    // Blank lines at the end are padding. Dropping them first is also what
    // lets a tearline and origin followed by nothing but blanks still count as
    // closing the message.
    while (!lines.empty() && isBlank(lines.back())) lines.pop_back();

    const size_t count = lines.size();
    if (count >= 2 && domain::isTearline(lines[count - 2]) &&
        domain::isOriginLine(lines[count - 1])) {
        return lines;
    }

    // The conventional way to spoil one: the markers are broken so that
    // nothing downstream reads them as control lines, and they stay readable
    // to a person.
    for (auto& line : lines) {
        if (domain::isTearline(line)) {
            line = "-+-" + line.substr(3);
        } else if (domain::isOriginLine(line)) {
            line = " + Origin:" + line.substr(std::string_view(" * Origin:").size());
        }
    }
    lines.push_back(tearline);
    lines.push_back(origin);
    return lines;
}

/// What of the message being answered is carried into the answer: its text, and
/// the control lines too where the reader is showing them — `kludges` is the
/// reader's `k`, so the quote and the forward carry what was on screen.
///
/// Never the tearline and origin closing it, whether or not the kludges are on:
/// neither is the author's words, and the message being written closes with a
/// pair of its own. The control lines are carried exactly as the reader shows
/// them, ^A standing as '@' — what goes into the answer is text about a message
/// and not control data of the answer's own.
std::vector<std::string> quotableLines(const domain::MessageBody& body, bool kludges) {
    std::vector<std::string> out;
    for (const auto& line : body.lines) {
        if (line.trailer) continue;
        if (line.kludge && !kludges) continue;
        out.push_back(line.text);
    }
    return out;
}

/// Whether the control line is one of those that stand in front of every other:
/// the AREA: line a packet carries at the head of a message, the routing
/// FSC-0004 writes, and the MSGID a message is known by.
bool standsFirst(std::string_view kludge) {
    return config::text::startsWith(kludge, "AREA:") ||
           config::text::startsWith(kludge, "INTL ") ||
           config::text::startsWith(kludge, "FMPT ") ||
           config::text::startsWith(kludge, "TOPT ") ||
           config::text::startsWith(kludge, "MSGID:");
}

/// Writes `line` over the first control line beginning with `prefix`, or puts
/// it in where the message carries none — behind the lines that stand in front,
/// which is where a message written from scratch has it.
void setKludge(std::vector<std::string>& kludges, std::string_view prefix,
               std::string line) {
    for (auto& kludge : kludges) {
        if (config::text::startsWith(kludge, prefix)) {
            kludge = std::move(line);
            return;
        }
    }
    auto at = kludges.begin();
    while (at != kludges.end() && standsFirst(*at)) ++at;
    kludges.insert(at, std::move(line));
}

TemplateContext contextFor(const BuildRequest& request) {
    const auto& fields = request.fields;
    const domain::MessageDate now = localStamp(request.now);

    TemplateContext context;
    context.cname = fields.fromName;
    context.caddr = fields.fromAddr;
    if (const auto parsed = domain::FtnAddress::parse(fields.fromAddr)) {
        context.c3daddr = threeDimensional(*parsed);
    }
    context.cdate = now.format(request.config.templateDateFormat);
    context.ctime = now.format(request.config.templateTimeFormat);
    context.ctzoffset = tzutcOffset(request.utcOffsetMinutes);
    context.cecho = request.area.tag;
    context.cdesc = request.area.description;
    // Where the message being answered was read. The two differ only for a
    // reply moved into another area, which is exactly when a template has
    // anything to say about them — GoldED's own writes "Answering a msg posted
    // in area @OEcho" on its @moved lines.
    context.oecho =
        request.originalArea != nullptr ? request.originalArea->tag : context.cecho;
    context.odesc = request.originalArea != nullptr ? request.originalArea->description
                                                    : context.cdesc;

    context.tname = fields.toName;
    context.taddr = fields.toAddr;
    if (const auto parsed = domain::FtnAddress::parse(fields.toAddr)) {
        context.t3daddr = threeDimensional(*parsed);
    }

    // The `d*` tokens name whoever the message being answered was written to —
    // us, in an ordinary reply. Only a new message, which answers nothing, has
    // them fall back to its own recipient.
    context.dname = fields.toName;
    context.daddr = fields.toAddr;
    context.d3daddr = context.t3daddr;
    if (request.original != nullptr) {
        context.dname = request.original->to;
        context.daddr = addressText(request.original->destAddr);
        context.d3daddr = request.original->destAddr.isValid()
                              ? threeDimensional(request.original->destAddr)
                              : "";
    }

    context.subject = fields.subject;
    // @pid is the bare name and @longpid the name with the system under it —
    // "AmberEdit/linux", the form FTN programs have always signed with — while
    // @ver/@rev/@version carry the number. That is what lets the tearline the
    // config asks for by default, "@longpid @version", come out as
    // "AmberEdit/linux 0.1" with the version written down nowhere but in
    // CMakeLists.txt.
    context.version = std::string(kVersion);
    context.pid = std::string(kProgramName);
    context.longpid = std::string(kLongProgramName);
    context.areaname = request.area.tag;
    context.areapath = request.area.path;
    context.areatype = domain::nameOf(request.area.type);

    // A forward carries the message it passes on, so it has an `original` like
    // a reply — and is a new message in every other respect, which is what the
    // template is told: @new rather than @reply, and no @quote of it.
    const bool answering = request.original != nullptr && !fields.forward;
    // A message being changed is neither: it was written once already, and what
    // the template has to say about it is on its @changed lines alone.
    context.isNew = !answering && !fields.changing;
    context.isReply = answering;
    context.isChanged = fields.changing;
    context.isQuoted = answering && request.originalBody != nullptr;
    context.isMoved = answering && request.originalArea != nullptr;
    context.isForward = fields.forward && request.original != nullptr;
    context.isNet = fields.netmail;
    context.isEcho = !fields.netmail;

    if (request.original != nullptr) {
        context.oname = request.original->from;
        context.oaddr = addressText(request.original->origAddr);
        if (request.original->origAddr.isValid()) {
            context.o3daddr = threeDimensional(request.original->origAddr);
        }
        context.odate = request.original->date.format(request.config.templateDateFormat);
        context.otime = request.original->date.format(request.config.templateTimeFormat);
    }
    if (request.originalBody != nullptr) {
        context.omsgid = msgidOf(*request.originalBody);
        const std::vector<std::string> carried =
            quotableLines(*request.originalBody, request.kludgesShown);
        // One or the other, never both: a forward puts the message in whole
        // where @message stands, and a reply quotes it where @quote does. Both
        // tokens are unconditional inserts in the template GoldED ships, so
        // filling both would put the same message in twice.
        if (context.isForward) {
            // Disarmed on the way in: a `CC:` line of the message being passed
            // on is text about that message, and carrying it out would send
            // copies its author asked for and this writer did not.
            context.message = carried;
            disarmCopyCommands(context.message);
        } else {
            context.quote = quoteLines(carried, context.oname, request.config.quoteString,
                                       request.config.quoteMargin);
        }
    }
    if (!request.config.templatePath.empty()) {
        context.includeDir =
            std::filesystem::path(request.config.templatePath).parent_path().string();
    }

    // Last, because these two are themselves written with tokens in them: they
    // are expanded against the context as it stands here, in which @tearline
    // and @origin are still empty — which is what keeps a tearline that names
    // itself from asking for itself.
    context.tearline = tearlineFrom(expandTokens(request.config.tearline, context));
    context.origin = expandTokens(request.config.origin, context);
    return context;
}

}  // namespace

/// The fields the local time shows, in no time zone of their own — which is
/// exactly what @cdate and @ctime say, and what lets both of them and the
/// answered message's @odate/@otime be written by one piece of code.
domain::MessageDate localStamp(std::time_t when) {
    std::tm broken{};
    localtime_r(&when, &broken);

    domain::MessageDate stamp;
    stamp.year = static_cast<uint16_t>(broken.tm_year + 1900);
    stamp.month = static_cast<uint8_t>(broken.tm_mon + 1);
    stamp.day = static_cast<uint8_t>(broken.tm_mday);
    stamp.hour = static_cast<uint8_t>(broken.tm_hour);
    stamp.minute = static_cast<uint8_t>(broken.tm_min);
    stamp.second = static_cast<uint8_t>(broken.tm_sec);
    return stamp;
}

std::string serialNumber(std::time_t when) {
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(8)
        << static_cast<uint32_t>(static_cast<uint64_t>(when) & 0xFFFFFFFFull);
    return out.str();
}

std::string tzutcOffset(int minutes) {
    const int magnitude = minutes < 0 ? -minutes : minutes;
    std::ostringstream out;
    if (minutes < 0) out << '-';
    out << std::setfill('0') << std::setw(2) << magnitude / 60 << std::setw(2)
        << magnitude % 60;
    return out.str();
}

int tzutcMinutes(std::string_view offset) {
    // The shape tzutcOffset() writes and the shape a header carries: a sign and
    // four digits. Anything else is a message stating no zone at all — the
    // ordinary case rather than an error — and zero is what the base keeps for
    // one.
    constexpr size_t kLength = 5;  // sign, hh, mm
    if (offset.size() != kLength) return 0;
    if (offset.front() != '+' && offset.front() != '-') return 0;
    for (size_t i = 1; i < kLength; ++i) {
        if (offset[i] < '0' || offset[i] > '9') return 0;
    }

    const int hours = ((offset[1] - '0') * 10) + (offset[2] - '0');
    const int minutes = ((offset[3] - '0') * 10) + (offset[4] - '0');
    const int total = (hours * 60) + minutes;
    return offset.front() == '-' ? -total : total;
}

int charsetLevel(std::string_view charset) {
    const std::string name = config::text::toLower(charset);
    if (name == "utf-8" || name == "utf8") return 4;
    if (name == "ascii" || name == "us-ascii") return 1;
    return 2;
}

std::string charsetIdentifier(std::string_view charset) {
    const std::string name = config::text::toLower(charset);
    if (name == "utf-8" || name == "utf8") return "UTF-8";
    if (name == "ascii" || name == "us-ascii") return "ASCII";
    return std::string(charset);
}

std::string msgidOf(const domain::MessageBody& body) {
    constexpr std::string_view kPrefix = "MSGID:";
    for (const auto& line : body.lines) {
        if (!line.kludge) continue;
        // The adapter shows a kludge's ^A as '@', that being the conventional
        // stand-in for a character no terminal prints.
        std::string_view text{line.text};
        if (!text.empty() && (text.front() == '@' || text.front() == '\x01')) {
            text.remove_prefix(1);
        }
        if (!config::text::startsWith(text, kPrefix)) continue;
        return std::string(config::text::trim(text.substr(kPrefix.size())));
    }
    return {};
}

std::string areaTagOf(const domain::MessageBody& body) {
    constexpr std::string_view kPrefix = "AREA:";
    if (body.lines.empty()) return {};

    // The very first line and no other. AREA: is a packet header rather than a
    // kludge — it carries no ^A of its own and is only ever written ahead of
    // MSGID and the rest — so a line saying it further down is a line of the
    // message that happens to begin with those five characters.
    std::string_view text{body.lines.front().text};
    // Where a base has stored it with a ^A after all, the adapter shows that as
    // '@'. Both are stepped over, as msgidOf() steps over them: what the line
    // says is the same either way.
    if (!text.empty() && (text.front() == '@' || text.front() == '\x01')) {
        text.remove_prefix(1);
    }
    if (!config::text::startsWith(text, kPrefix)) return {};
    return std::string(config::text::trim(text.substr(kPrefix.size())));
}

StartingText startingText(const BuildRequest& request) {
    StartingText out;
    const TemplateContext context = contextFor(request);

    std::string templateText;
    bool haveTemplate = true;
    if (request.config.templatePath.empty()) {
        // No template configured: a reply still opens on the quote, which is
        // the one thing it cannot be written without.
        haveTemplate = false;
    } else if (const auto read = config::text::readFile(request.config.templatePath)) {
        templateText = *read;
    } else {
        out.error = "template: " + read.error();
        haveTemplate = false;
    }

    if (haveTemplate) {
        TemplateResult expanded = expandTemplate(templateText, context);
        out.lines = std::move(expanded.lines);
        out.cursorLine = expanded.cursorLine < 0 ? 0 : expanded.cursorLine;
    } else {
        out.lines = context.quote;
    }

    // The message closes with a tearline and an origin from the moment it is
    // opened, rather than having them appear at the last moment: they are part
    // of what is being written, and a user who wants them gone should be able
    // to delete them and see them gone.
    out.lines = closeMessage(std::move(out.lines), context.tearline,
                             originLine(context.origin, request.fields.fromAddr));

    // The cursor belongs in the message, not on the lines closing it — so a
    // template that names no @position, or one that produced no text at all,
    // gets a line to start typing on.
    const int closing = static_cast<int>(out.lines.size()) - 2;
    if (out.cursorLine >= closing) {
        out.lines.insert(out.lines.begin() + closing, std::string{});
        out.cursorLine = closing;
    }
    return out;
}

PreservedLines preservedLines(const domain::MessageBody& body) {
    PreservedLines kept;
    kept.charset = body.charset;

    bool text = false;
    for (const auto& line : body.lines) {
        if (!line.kludge) {
            text = true;
            continue;
        }
        // The adapter shows a kludge's ^A as '@', that being the conventional
        // stand-in for a character no terminal prints; what goes back to the
        // base is the ^A. SEEN-BY carries none in the first place.
        const bool control = !line.text.empty() && line.text.front() == '@';
        if (!text && control) {
            kept.kludges.push_back(line.text.substr(1));
        } else if (!text && config::text::startsWith(line.text, "AREA:")) {
            // The one service line before the text that never had a ^A. It goes
            // back at the head of the message, where the packet put it, and not
            // among the lines trailing it.
            kept.kludges.push_back(line.text);
        } else if (control) {
            kept.trailing.push_back('\x01' + line.text.substr(1));
        } else {
            kept.trailing.push_back(line.text);
        }
    }
    return kept;
}

domain::MessageDraft copyOf(const domain::MessageHeader& header,
                            const domain::MessageBody& body, bool netmail) {
    // The same split a change makes, and for the same reason: the service lines
    // stand where the formats keep them, MSGID and its like ahead of the text
    // and SEEN-BY and PATH after the origin, and a message put back together
    // the other way round is one no tosser could read.
    const PreservedLines kept = preservedLines(body);

    domain::MessageDraft draft;
    draft.from = header.from;
    draft.to = header.to;
    draft.subject = header.subject;
    draft.origAddr = header.origAddr;
    draft.destAddr = header.destAddr;
    draft.netmail = netmail;
    // The attributes as the message carries them — read, sent, private and the rest.
    // They are what became of this message, and it is this message that is being
    // put into another area.
    draft.attributes = header.attributes;
    draft.written = header.date;
    // The charset it was read in, so that the CHRS line among the kludges — kept
    // with the rest — still says what the bytes are. Empty only where the body
    // could not be read at all, and then the base it is going into writes it in
    // the charset it reads that area in; see FtnMsgBase::encode().
    draft.charset = kept.charset;
    // The zone the message states it was written by, so the base stores the
    // stamp above under the clock the message itself names.
    draft.utcOffsetMinutes = tzutcMinutes(header.utcOffset);

    draft.kludges = kept.kludges;
    // Everything a person can see, the tearline and the origin line among it:
    // those close the message and travel with it.
    for (const auto& line : body.lines) {
        if (!line.kludge) draft.lines.push_back(line.text);
    }
    draft.lines.insert(draft.lines.end(), kept.trailing.begin(), kept.trailing.end());
    return draft;
}

domain::MessageDraft buildChange(const ComposeFields& fields, const PreservedLines& kept,
                                 const std::vector<std::string>& text,
                                 const ChangeStamp& stamp) {
    domain::MessageDraft draft;
    draft.from = fields.fromName;
    draft.to = fields.toName;
    draft.subject = fields.subject;
    draft.netmail = fields.netmail;
    draft.attributes = fields.attributes;
    if (const auto from = domain::FtnAddress::parse(fields.fromAddr))
        draft.origAddr = *from;
    if (const auto to = domain::FtnAddress::parse(fields.toAddr)) draft.destAddr = *to;
    // The charset the message was read in, so that the CHRS line among the
    // kludges — which is kept along with the rest — still says what the bytes
    // are. Empty only where the body could not be read at all, and then the
    // base writes it in the charset it reads that area in; see
    // FtnMsgBase::encode().
    draft.charset = kept.charset;
    draft.utcOffsetMinutes = stamp.utcOffsetMinutes;

    // The kludges as the message carried them, save for the two that describe
    // the writing rather than the message. The MSGID is this system's and this
    // moment's: what went out under the old one is not what the message now
    // says, and a MSGID is what a network tells two messages apart by. TZUTC
    // goes with the stamp the base dates the message by, which is the clock
    // here. What answers the message keeps pointing at it — the thread links
    // are the base's own, and `replace()` keeps them — but a REPLY made of the
    // old MSGID on another system no longer names it, which is what changing a
    // message that has been out costs.
    draft.kludges = kept.kludges;
    // The address this system is known by, or — where the config names none and
    // the area is presented under none — the one the message says it is from.
    // With neither, the MSGID it carried is left standing: FTS-0009 has no
    // MSGID without an address in it, and a message keeping an old one is
    // better than a message carrying a broken one.
    const std::string origin = !stamp.address.empty() ? stamp.address : fields.fromAddr;
    if (!origin.empty()) {
        setKludge(draft.kludges,
                  "MSGID:", "MSGID: " + origin + " " + serialNumber(stamp.now));
    }
    setKludge(draft.kludges, "TZUTC:", "TZUTC: " + tzutcOffset(stamp.utcOffsetMinutes));
    draft.lines = text;
    draft.lines.insert(draft.lines.end(), kept.trailing.begin(), kept.trailing.end());
    return draft;
}

std::vector<std::string> changeNotice(const BuildRequest& request) {
    if (request.config.templatePath.empty()) return {};

    // @CName and @CAddr are the current user everywhere else, and the notice is
    // the one place it matters that they are: a message being changed carries
    // whoever wrote it in its From fields, and "*** Changed by" is about the
    // hand it is in now. The address is the one this area is presented under,
    // as it is for a message written here.
    TemplateContext context = contextFor(request);
    context.cname = request.config.userName;
    context.caddr = ownAddress(request.config, request.area);
    context.c3daddr.clear();
    if (const auto parsed = domain::FtnAddress::parse(context.caddr)) {
        context.c3daddr = threeDimensional(*parsed);
    }

    const auto text = config::text::readFile(request.config.templatePath);
    // The same template the editor would have opened on, and the same silence:
    // a notice that cannot be read is no reason to refuse the change.
    if (!text) return {};
    return conditionalLines(*text, "changed", context);
}

domain::MessageDraft buildDraft(const BuildRequest& request,
                                const std::vector<std::string>& text) {
    const auto& fields = request.fields;

    domain::MessageDraft draft;
    draft.from = fields.fromName;
    draft.to = fields.toName;
    draft.subject = fields.subject;
    draft.netmail = fields.netmail;
    // The attributes exactly as the header screen left them: the prefill's Loc and
    // Pvt, less whatever was turned off there, plus whatever was turned on.
    draft.attributes = fields.attributes;
    draft.utcOffsetMinutes = request.utcOffsetMinutes;
    // The charset the area this message is going into is written in — the
    // config's `compose_charset`, or an area group's where one covers the tag,
    // the caller having resolved that before building the request.
    // `default_charset` has no say here: it is what a message that declares
    // nothing is read in, which is a statement about the echoes, not about what
    // this user writes.
    draft.charset = request.config.composeCharset;

    if (const auto from = domain::FtnAddress::parse(fields.fromAddr))
        draft.origAddr = *from;
    if (const auto to = domain::FtnAddress::parse(fields.toAddr)) draft.destAddr = *to;

    // Routing first, as every FTN editor writes it: where the message is going
    // and where it came from, then what it is, then how to read it.
    if (draft.netmail && draft.origAddr.isValid() && draft.destAddr.isValid()) {
        draft.kludges.push_back("INTL " + threeDimensional(draft.destAddr) + " " +
                                threeDimensional(draft.origAddr));
        if (draft.origAddr.point != 0) {
            draft.kludges.push_back("FMPT " + std::to_string(draft.origAddr.point));
        }
        if (draft.destAddr.point != 0) {
            draft.kludges.push_back("TOPT " + std::to_string(draft.destAddr.point));
        }
    }

    draft.kludges.push_back("MSGID: " + addressText(draft.origAddr) + " " +
                            serialNumber(request.now));
    // No REPLY without a MSGID to copy: FTS-0009 says a reply to a message that
    // carries none is written without one. A forward answers nothing, so it
    // carries no link back to what it passes on either.
    if (request.originalBody != nullptr && !fields.forward) {
        if (const std::string msgid = msgidOf(*request.originalBody); !msgid.empty()) {
            draft.kludges.push_back("REPLY: " + msgid);
        }
    }
    draft.kludges.push_back("TZUTC: " + tzutcOffset(request.utcOffsetMinutes));
    draft.kludges.push_back("CHRS: " + charsetIdentifier(draft.charset) + " " +
                            std::to_string(charsetLevel(draft.charset)));
    // Last of them, after the lines FTS-0009 and its like ask for: these are
    // the writer's own note of who else has this message, and nothing routes
    // by them.
    draft.kludges.insert(draft.kludges.end(), request.extraKludges.begin(),
                         request.extraKludges.end());

    // The same pair the editor opened on, so that saving leaves standing what
    // the user has been looking at all along.
    const TemplateContext context = contextFor(request);
    draft.lines = closeMessage(text, context.tearline,
                               originLine(context.origin, addressText(draft.origAddr)));
    return draft;
}

}  // namespace amberedit::app
