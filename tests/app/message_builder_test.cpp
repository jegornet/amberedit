#include <doctest/doctest.h>

#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "app/message_builder.hpp"
#include "version.hpp"

using amberedit::app::buildDraft;
using amberedit::app::BuildRequest;
using amberedit::app::charsetIdentifier;
using amberedit::app::charsetLevel;
using amberedit::app::ComposeFields;
using amberedit::app::serialNumber;
using amberedit::app::startingText;
using amberedit::app::tzutcMinutes;
using amberedit::app::tzutcOffset;
using amberedit::config::AppConfig;
using amberedit::domain::AreaConfig;
using amberedit::domain::AreaKind;
using amberedit::domain::MessageBody;
using amberedit::domain::MessageHeader;

namespace {

AppConfig config() {
    AppConfig cfg;
    cfg.userName = "Yegor Gluhov";
    // A new message is written in `compose_charset`. `default_charset` is
    // deliberately a different one here: it is the fallback for *reading* a
    // message that declares no charset of its own, and nothing the builder
    // writes may come from it — a CHRS naming KOI8-R below would say so.
    cfg.composeCharset = "CP866";
    cfg.defaultCharset = "KOI8-R";
    cfg.quoteString = " FL> ";
    cfg.quoteMargin = 78;
    // The origin text is empty by default, and most of what follows is about
    // the lines round it rather than about the text itself.
    cfg.origin = "AmberEdit test";
    return cfg;
}

AreaConfig areaOf(AreaKind kind) {
    AreaConfig area;
    area.tag = kind == AreaKind::Netmail ? "NETMAIL" : "test.echo";
    area.kind = kind;
    return area;
}

/// The example from the specification: a netmail from 2:382/736.1 to
/// 2:5015/46.120.
ComposeFields netmailFields() {
    ComposeFields fields;
    fields.netmail = true;
    fields.fromName = "Yegor Gluhov";
    fields.fromAddr = "2:382/736.1";
    fields.toName = "Vasya Pupkin";
    fields.toAddr = "2:5015/46.120";
    fields.subject = "test";
    return fields;
}

/// The kludges of a draft, one per line, for reading in a failure.
std::string kludgesOf(const amberedit::domain::MessageDraft& draft) {
    std::string out;
    for (const auto& kludge : draft.kludges) out += kludge + "|";
    return out;
}

/// A file that removes itself, for the template tests.
class TempFile {
public:
    explicit TempFile(const std::string& content) {
        path_ = std::filesystem::temp_directory_path() /
                ("amberedit-template-" + std::to_string(::getpid()) + ".tpl");
        std::ofstream out(path_);
        out << content;
    }
    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }
    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;

    [[nodiscard]] std::string path() const { return path_.string(); }

private:
    std::filesystem::path path_;
};

}  // namespace

TEST_CASE("A MSGID serial number is the time in eight hex digits [builder]") {
    // FTS-0009 asks for eight hexadecimal characters unique to the system for
    // three years; seconds since the epoch are unique for a great deal longer.
    CHECK(serialNumber(0) == "00000000");
    CHECK(serialNumber(1) == "00000001");
    CHECK(serialNumber(0x68A1B2C3) == "68a1b2c3");
}

TEST_CASE("TZUTC states the offset in four digits [builder]") {
    CHECK(tzutcOffset(0) == "0000");
    CHECK(tzutcOffset(120) == "0200");
    CHECK(tzutcOffset(180) == "0300");
    CHECK(tzutcOffset(330) == "0530");    // India
    CHECK(tzutcOffset(-420) == "-0700");  // Pacific Daylight Time
    CHECK(tzutcOffset(-210) == "-0330");  // Newfoundland
    // No plus in front of a positive offset, FTS-4008 is explicit about it.
    CHECK(tzutcOffset(780) == "1300");
}

TEST_CASE("An offset is read back the way it was written [builder]") {
    // What a header carries — strftime's `%z` spelling, a sign and four digits
    // — turned back into the minutes a base stores.
    CHECK(tzutcMinutes("+0000") == 0);
    CHECK(tzutcMinutes("+0300") == 180);
    CHECK(tzutcMinutes("+0530") == 330);
    CHECK(tzutcMinutes("-0700") == -420);
    CHECK(tzutcMinutes("-0330") == -210);

    // A message stating no zone, or stating something that is not one: zero is
    // what the base keeps for both, and nothing is a better answer than a wrong
    // clock.
    CHECK(tzutcMinutes("") == 0);
    CHECK(tzutcMinutes("0300") == 0);
    CHECK(tzutcMinutes("+03:00") == 0);
    CHECK(tzutcMinutes("+03o0") == 0);
}

TEST_CASE("A message copied into another area is the same message [builder]") {
    namespace attr = amberedit::domain::attr;

    MessageHeader header;
    header.number = 7;
    header.from = "Ivan Petrov";
    header.to = "All";
    header.subject = "hello";
    header.date = {2024, 5, 17, 21, 4, 30};
    header.utcOffset = "+0300";
    header.origAddr = *amberedit::domain::FtnAddress::parse("2:5020/1042");
    header.attributes = attr::kLocal | attr::kSent;

    MessageBody body;
    body.charset = "KOI8-R";
    body.lines = {{"@MSGID: 2:5020/1042 5f3ac2e1", true, false},
                  {"@CHRS: KOI8-R 2", true, false},
                  {"a line of the message", false, false},
                  {"--- AmberEdit", false, true},
                  {" * Origin: somewhere (2:5020/1042)", false, true},
                  {"SEEN-BY: 5020/1042", true, false},
                  {"@PATH: 5020/1042", true, false}};

    const auto draft = amberedit::app::copyOf(header, body, /*netmail=*/false);

    // Everything a person can see, and the attributes and the stamp it carries: this
    // is the message itself in another area, not one written to look like it.
    CHECK(draft.from == "Ivan Petrov");
    CHECK(draft.to == "All");
    CHECK(draft.subject == "hello");
    CHECK(draft.attributes == (attr::kLocal | attr::kSent));
    CHECK(draft.origAddr.toString() == "2:5020/1042");
    CHECK(draft.charset == "KOI8-R");
    CHECK(draft.utcOffsetMinutes == 180);
    CHECK(draft.written.year == 2024);
    CHECK(draft.written.hour == 21);
    CHECK(draft.written.minute == 4);

    // The MSGID travels with it: it is what the network tells two messages apart
    // by, and a copy that made a new one would be a second message wearing the
    // first one's words. Nothing is added — no REPLY, no MSGID of ours.
    CHECK(kludgesOf(draft) == "MSGID: 2:5020/1042 5f3ac2e1|CHRS: KOI8-R 2|");
    // The text with the pair closing it, and the routing lines after that —
    // where the formats keep them, and where they were read from.
    CHECK(draft.lines == std::vector<std::string>{"a line of the message",
                                                  "--- AmberEdit",
                                                  " * Origin: somewhere (2:5020/1042)",
                                                  "SEEN-BY: 5020/1042",
                                                  "\x01PATH: 5020/1042"});
}

TEST_CASE("CHRS names the charset and its level [builder]") {
    CHECK(charsetLevel("ASCII") == 1);
    CHECK(charsetLevel("us-ascii") == 1);
    CHECK(charsetLevel("UTF-8") == 4);
    CHECK(charsetLevel("utf8") == 4);
    CHECK(charsetLevel("CP866") == 2);
    CHECK(charsetLevel("LATIN-1") == 2);

    CHECK(charsetIdentifier("utf8") == "UTF-8");
    CHECK(charsetIdentifier("us-ascii") == "ASCII");
    CHECK(charsetIdentifier("CP866") == "CP866");
}

TEST_CASE("A netmail carries INTL, FMPT and TOPT [builder]") {
    const AppConfig cfg = config();
    const AreaConfig area = areaOf(AreaKind::Netmail);
    const BuildRequest request{cfg,     area,    netmailFields(), nullptr,
                               nullptr, nullptr, 0x68A1B2C3,      180};

    const auto draft = buildDraft(request, {"hello"});

    // FSC-0004 addresses zone:net/node; the points travel in FMPT and TOPT.
    CHECK(kludgesOf(draft) ==
          "INTL 2:5015/46 2:382/736|"
          "FMPT 1|"
          "TOPT 120|"
          "MSGID: 2:382/736.1 68a1b2c3|"
          "TZUTC: 0300|"
          "CHRS: CP866 2|");
    CHECK(draft.netmail);
    CHECK(draft.origAddr.toString() == "2:382/736.1");
    CHECK(draft.destAddr.toString() == "2:5015/46.120");
}

TEST_CASE("The draft carries the attributes the header screen set [builder]") {
    namespace attr = amberedit::domain::attr;

    const AppConfig cfg = config();
    const AreaConfig area = areaOf(AreaKind::Netmail);

    ComposeFields fields = netmailFields();
    // A netmail as the prefill leaves it, with Crash added and Private taken
    // off on the header screen. All of it goes to the base as it stands — the
    // attributes are the author's, and nothing between here and the disk adds to
    // them or takes from them.
    fields.attributes = attr::kLocal | attr::kCrash;
    const BuildRequest request{cfg,     area,    fields,     nullptr,
                               nullptr, nullptr, 0x68A1B2C3, 180};

    CHECK(buildDraft(request, {"hello"}).attributes == (attr::kLocal | attr::kCrash));
}

TEST_CASE("A point only appears where there is one [builder]") {
    const AppConfig cfg = config();
    const AreaConfig area = areaOf(AreaKind::Netmail);

    ComposeFields fields = netmailFields();
    fields.fromAddr = "2:382/736";
    fields.toAddr = "2:5015/46";
    const BuildRequest request{cfg,     area,    fields,     nullptr,
                               nullptr, nullptr, 0x68A1B2C3, 0};

    CHECK(kludgesOf(buildDraft(request, {})) ==
          "INTL 2:5015/46 2:382/736|"
          "MSGID: 2:382/736 68a1b2c3|"
          "TZUTC: 0000|"
          "CHRS: CP866 2|");
}

TEST_CASE("Echomail is not addressed, so it carries no INTL [builder]") {
    const AppConfig cfg = config();
    const AreaConfig area = areaOf(AreaKind::Echo);

    ComposeFields fields = netmailFields();
    fields.netmail = false;
    fields.toName = "All";
    fields.toAddr.clear();
    const BuildRequest request{cfg,     area,    fields,     nullptr,
                               nullptr, nullptr, 0x68A1B2C3, 180};

    CHECK(kludgesOf(buildDraft(request, {})) ==
          "MSGID: 2:382/736.1 68a1b2c3|"
          "TZUTC: 0300|"
          "CHRS: CP866 2|");
}

TEST_CASE("The area a message names is read off its first line only [builder]") {
    const auto tagOf = [](std::vector<amberedit::domain::MessageLine> lines) {
        MessageBody body;
        body.lines = std::move(lines);
        return amberedit::app::areaTagOf(body);
    };

    // The packet header as the drivers hand it back: the first line of the
    // message, and without the ^A no AREA: line ever had.
    CHECK(tagOf({{"AREA:RU.LINUX", false}, {"@MSGID: 192:168/3 5f3a1b2c", true}}) ==
          "RU.LINUX");
    // A base that stored one with a ^A after all shows it as '@', and says the
    // same thing.
    CHECK(tagOf({{"@AREA:RU.LINUX", true}}) == "RU.LINUX");
    CHECK(tagOf({{"AREA: ru.linux ", false}}) == "ru.linux");

    // Anywhere but first it is a line of the message, whatever it says — and a
    // message that names no area at all is the usual case.
    CHECK(
        tagOf({{"@MSGID: 192:168/3 5f3a1b2c", true}, {"AREA:RU.LINUX", false}}).empty());
    CHECK(tagOf({{"hello", false}, {"AREA:RU.LINUX", false}}).empty());
    CHECK(tagOf({{"@MSGID: 192:168/3 5f3a1b2c", true}, {"hello", false}}).empty());
    CHECK(tagOf({}).empty());
    // The five characters have to be the whole of the prefix, and a line with
    // nothing after them names nothing.
    CHECK(tagOf({{"AREAS: ru.linux", false}}).empty());
    CHECK(tagOf({{"AREA:", false}}).empty());
}

TEST_CASE("A reply carries REPLY, and only when there is a MSGID to copy [builder]") {
    const AppConfig cfg = config();
    const AreaConfig area = areaOf(AreaKind::Echo);
    ComposeFields fields = netmailFields();
    fields.netmail = false;

    MessageHeader original;
    original.from = "Vasya Pupkin";

    MessageBody withMsgid;
    withMsgid.lines = {{"@MSGID: 192:168/3.1 5f3a1b2c", true}, {"hello", false}};
    const BuildRequest quoted{cfg,        area,    fields,     &original,
                              &withMsgid, nullptr, 0x68A1B2C3, 180};
    CHECK(kludgesOf(buildDraft(quoted, {})) ==
          "MSGID: 2:382/736.1 68a1b2c3|"
          "REPLY: 192:168/3.1 5f3a1b2c|"
          "TZUTC: 0300|"
          "CHRS: CP866 2|");

    // FTS-0009: no REPLY is written in answer to a message that carries no
    // MSGID.
    MessageBody without;
    without.lines = {{"hello", false}};
    const BuildRequest bare{cfg,      area,    fields,     &original,
                            &without, nullptr, 0x68A1B2C3, 180};
    CHECK(kludgesOf(buildDraft(bare, {})) ==
          "MSGID: 2:382/736.1 68a1b2c3|"
          "TZUTC: 0300|"
          "CHRS: CP866 2|");
}

namespace {

/// What a message closes with, as the builder writes it. Spelled out from
/// kProgramId rather than by hand, so that raising the version stays a matter
/// of the one line in CMakeLists.txt.
const std::string kTearline = "--- " + std::string(amberedit::kProgramId);
const std::string kOrigin = " * Origin: AmberEdit test (2:382/736.1)";
const std::string kClosing = kTearline + "|" + kOrigin;

/// A draft's text as one string, lines separated by '|'.
std::string textOf(const amberedit::domain::MessageDraft& draft) {
    std::string out;
    for (const auto& line : draft.lines) {
        if (!out.empty()) out += '|';
        out += line;
    }
    return out;
}

BuildRequest echoRequest(const AppConfig& cfg, const AreaConfig& area,
                         ComposeFields& fields) {
    fields.netmail = false;
    return BuildRequest{cfg, area, fields, nullptr, nullptr, nullptr, 0x68A1B2C3, 180};
}

}  // namespace

TEST_CASE("The tearline and the origin say what the config asks them to [builder]") {
    AppConfig cfg = config();
    const AreaConfig area = areaOf(AreaKind::Echo);
    ComposeFields fields = netmailFields();
    fields.netmail = false;

    // Both are template lines: the default tearline names the program through
    // @longpid and @version rather than spelling the version out, and @pid is
    // the bare name without the system.
    cfg.tearline = "@pid @version";
    cfg.origin = "somewhere in @areaname";
    // The request holds the config by reference, so the checks below can go on
    // changing it.
    const BuildRequest request{cfg,     area,    fields,     nullptr,
                               nullptr, nullptr, 0x68A1B2C3, 180};
    CHECK(textOf(buildDraft(request, {"hi"})) ==
          "hi|--- AmberEdit " + std::string(amberedit::kVersion) +
              "| * Origin: somewhere in test.echo (2:382/736.1)");

    // @longpid is the same name with the system it was built for under it,
    // which is the default and what kProgramId spells out.
    cfg.tearline = "@longpid @version";
    CHECK(textOf(buildDraft(request, {"hi"})) ==
          "hi|" + kTearline + "| * Origin: somewhere in test.echo (2:382/736.1)");

    // Nothing configured for the origin — the default — still leaves the line
    // there: an echomail message without one is one a tosser may refuse.
    cfg.origin.clear();
    CHECK(textOf(buildDraft(request, {"hi"})) ==
          "hi|" + kTearline + "| * Origin:  (2:382/736.1)");

    // An empty tearline is still a tearline, and still stops a tosser reading
    // on — so it is written with nothing after the markers rather than with a
    // trailing space.
    cfg.tearline.clear();
    CHECK(textOf(buildDraft(request, {"hi"})) == "hi|---| * Origin:  (2:382/736.1)");
}

TEST_CASE("A message ends with the tearline and the origin [builder]") {
    const AppConfig cfg = config();
    const AreaConfig area = areaOf(AreaKind::Echo);
    ComposeFields fields = netmailFields();
    const BuildRequest request = echoRequest(cfg, area, fields);

    CHECK(textOf(buildDraft(request, {"hello", "world"})) == "hello|world|" + kClosing);
}

TEST_CASE("The tearline the editor already carries is not written twice [builder]") {
    const AppConfig cfg = config();
    const AreaConfig area = areaOf(AreaKind::Echo);
    ComposeFields fields = netmailFields();
    const BuildRequest request = echoRequest(cfg, area, fields);

    const std::vector<std::string> closed{"hello", kTearline, kOrigin};
    CHECK(textOf(buildDraft(request, closed)) == "hello|" + kClosing);

    // Blank lines after them are padding, and taking them off is what leaves
    // the pair closing the message.
    std::vector<std::string> padded = closed;
    padded.emplace_back();
    padded.emplace_back("   ");
    CHECK(textOf(buildDraft(request, padded)) == "hello|" + kClosing);
}

TEST_CASE("A tearline in the middle of a message is invalidated [builder]") {
    const AppConfig cfg = config();
    const AreaConfig area = areaOf(AreaKind::Echo);
    ComposeFields fields = netmailFields();
    const BuildRequest request = echoRequest(cfg, area, fields);

    // A tosser stops at the first tearline it meets, so one left above the
    // text has to stop being one: the markers are broken and a fresh pair is
    // written at the end.
    CHECK(textOf(buildDraft(request, {kTearline, kOrigin, "an afterthought"})) ==
          "-+-" + kTearline.substr(3) + "| +" + kOrigin.substr(2) + "|an afterthought|" +
              kClosing);

    // The same for one carried in from another message, when the writing goes
    // on after it. A pair standing last is what the message closes with,
    // whoever wrote it, and is left alone.
    CHECK(textOf(buildDraft(request, {"as they wrote:", "--- GoldED+/LNX 1.1.5",
                                      " * Origin: somewhere (2:5015/46)", "quite so"})) ==
          "as they wrote:|-+- GoldED+/LNX 1.1.5| + Origin: somewhere (2:5015/46)|"
          "quite so|" +
              kClosing);

    // A tearline with no origin under it does not close a message either.
    CHECK(textOf(buildDraft(request, {"hello", "---"})) == "hello|-+-|" + kClosing);
}

TEST_CASE("The editor opens with the message already closed [builder]") {
    AppConfig cfg = config();
    const AreaConfig area = areaOf(AreaKind::Echo);
    ComposeFields fields = netmailFields();
    fields.netmail = false;
    const BuildRequest request{cfg,     area,    fields,     nullptr,
                               nullptr, nullptr, 0x68A1B2C3, 180};

    // No template: an empty message, with a line to type on above the pair
    // that closes it.
    const auto text = startingText(request);
    REQUIRE(text.lines.size() == 3);
    CHECK(text.lines[0].empty());
    CHECK(text.lines[1] == kTearline);
    CHECK(text.lines[2] == kOrigin);
    CHECK(text.cursorLine == 0);
}

TEST_CASE("The editor opens on the template, quote and all [builder]") {
    const TempFile tpl(
        "; a comment\n"
        "@quoted@odate @otime, @oname wrote to @tname:\n"
        "@quoted@position\n"
        "@quote\n"
        "@newHello @tname.\n");

    AppConfig cfg = config();
    cfg.templatePath = tpl.path();
    const AreaConfig area = areaOf(AreaKind::Echo);

    ComposeFields fields = netmailFields();
    fields.netmail = false;
    fields.toName = "Vasya Pupkin";

    MessageHeader original;
    original.from = "Vasya Pupkin";
    original.date = {2026, 8, 10, 21, 19, 36};
    MessageBody body;
    body.lines = {{"@MSGID: 1", true}, {"hello there", false}, {"--- GoldED", true}};

    const BuildRequest request{cfg,   area,    fields,     &original,
                               &body, nullptr, 0x68A1B2C3, 180};
    const auto text = startingText(request);

    CHECK(text.error.empty());
    REQUIRE(text.lines.size() == 5);
    // @odate and @otime in the default formats: the answered message's own
    // stamp, its seconds left off as template_time_format asks.
    CHECK(text.lines[0] == "10 Aug 26 21:19, Vasya Pupkin wrote to Vasya Pupkin:");
    CHECK(text.lines[1].empty());
    // The quote carries the author's initials, and neither the kludge nor the
    // tearline is the author's words.
    CHECK(text.lines[2] == " VP> hello there");
    // The pair closing the message is there from the start.
    CHECK(text.lines[3] == kTearline);
    CHECK(text.lines[4] == kOrigin);
    CHECK(text.cursorLine == 1);
}

TEST_CASE("A reply moved into another area opens on the template's @moved lines "
          "[builder]") {
    const TempFile tpl(
        "@moved*** Answering a msg posted in area @OEcho.\n"
        "@moved\n"
        "@quoted@oname wrote:\n"
        "@quote\n");

    AppConfig cfg = config();
    cfg.templatePath = tpl.path();
    // Where the reply is going, and where the message it answers was read.
    AreaConfig target = areaOf(AreaKind::Echo);
    target.tag = "test.other";
    const AreaConfig source = areaOf(AreaKind::Echo);

    ComposeFields fields = netmailFields();
    fields.netmail = false;
    fields.moved = true;

    MessageHeader original;
    original.from = "Vasya Pupkin";
    MessageBody body;
    body.lines = {{"hello there", false}};

    const BuildRequest moved{cfg,   target,  fields,     &original,
                             &body, &source, 0x68A1B2C3, 180};
    const auto text = startingText(moved);

    REQUIRE(text.lines.size() == 6);
    CHECK(text.lines[0] == "*** Answering a msg posted in area test.echo.");
    CHECK(text.lines[1].empty());
    CHECK(text.lines[2] == "Vasya Pupkin wrote:");
    CHECK(text.lines[3] == " VP> hello there");

    // The same reply written back into the area it came from drops those lines
    // and says nothing about a move, which is what @moved is for.
    const BuildRequest stayed{cfg,   source,  fields,     &original,
                              &body, nullptr, 0x68A1B2C3, 180};
    const auto plain = startingText(stayed);
    REQUIRE(plain.lines.size() == 4);
    CHECK(plain.lines[0] == "Vasya Pupkin wrote:");
}

TEST_CASE("A forward carries the message rather than answering it [builder]") {
    const TempFile tpl(
        "@forward* Forwarded by @CName\n"
        "@forward* Area : @OEcho\n"
        "@forward* From : @OName\n"
        "@message\n"
        "@quoted@oname wrote:\n"
        "@quote\n");

    AppConfig cfg = config();
    cfg.templatePath = tpl.path();
    AreaConfig target = areaOf(AreaKind::Echo);
    target.tag = "test.other";
    const AreaConfig source = areaOf(AreaKind::Echo);

    ComposeFields fields = netmailFields();
    fields.netmail = false;
    fields.toName = "All";
    fields.forward = true;

    MessageHeader original;
    original.from = "Vasya Pupkin";
    MessageBody body;
    body.lines = {{"@MSGID: 192:168/3.1 5f3a1b2c", true},
                  {"hello there", false},
                  {"--- GoldED", true}};

    const BuildRequest request{cfg,   target,  fields,     &original,
                               &body, &source, 0x68A1B2C3, 180};
    const auto text = startingText(request);

    REQUIRE(text.lines.size() == 6);
    CHECK(text.lines[0] == "* Forwarded by Yegor Gluhov");
    // The area it was read in, not the one it is going to.
    CHECK(text.lines[1] == "* Area : test.echo");
    CHECK(text.lines[2] == "* From : Vasya Pupkin");
    // The message itself, as it was written: no quote prefix, and nothing of
    // the kludges or the tearline that are not the author's words. The @quoted
    // lines are not a forward's, so neither is there.
    CHECK(text.lines[3] == "hello there");
    CHECK(text.lines[4] == kTearline);
    CHECK(text.lines[5] == kOrigin);

    // It answers nothing, so it carries no link back to what it passes on.
    CHECK(kludgesOf(buildDraft(request, {})) ==
          "MSGID: 2:382/736.1 68a1b2c3|"
          "TZUTC: 0300|"
          "CHRS: CP866 2|");
}

TEST_CASE("A forward carries a CC: line as text and not as a command [builder]") {
    const TempFile tpl("@message\n");

    AppConfig cfg = config();
    cfg.templatePath = tpl.path();
    const AreaConfig target = areaOf(AreaKind::Echo);

    ComposeFields fields = netmailFields();
    fields.netmail = false;
    fields.toName = "All";
    fields.forward = true;

    MessageHeader original;
    original.from = "Vasya Pupkin";
    MessageBody body;
    body.lines = {
        {"CC: Ivan Ivanov", false}, {"XC: ru.linux", false}, {"hello there", false}};

    const BuildRequest request{cfg, target, fields, &original, &body, nullptr, 0, 0};
    const auto text = startingText(request);

    // The commands of the message being passed on are its author's, and they
    // are spoiled by their prefix on the way in — the whole line, recipients
    // and all, so that nothing is copied anywhere on their account.
    REQUIRE(text.lines.size() == 5);
    CHECK(text.lines[0] == "!CC: Ivan Ivanov");
    CHECK(text.lines[1] == "!XC: ru.linux");
    CHECK(text.lines[2] == "hello there");
}

TEST_CASE("A message read with its kludges showing is quoted and forwarded with them "
          "[builder]") {
    const TempFile tpl(
        "@forward* Forwarded by @CName\n"
        "@message\n"
        "@quoted@oname wrote:\n"
        "@quote\n");

    AppConfig cfg = config();
    cfg.templatePath = tpl.path();
    const AreaConfig area = areaOf(AreaKind::Echo);

    ComposeFields fields = netmailFields();
    fields.netmail = false;
    fields.toName = "Vasya Pupkin";

    MessageHeader original;
    original.from = "Vasya Pupkin";
    // The message as the drivers hand it back and the reader shows it: control
    // lines either side of the text, ^A standing as '@', and the pair closing it
    // flagged as the trailer it is.
    MessageBody body;
    body.lines = {{"AREA:RU.LINUX", true, false},
                  {"@MSGID: 192:168/3.1 5f3a1b2c", true, false},
                  {"hello there", false, false},
                  {"--- GoldED", false, true},
                  {" * Origin: somewhere (192:168/3.1)", false, true},
                  {"SEEN-BY: 382/736", true, false}};

    BuildRequest request{cfg,   area,    fields,     &original,
                         &body, nullptr, 0x68A1B2C3, 180};
    request.kludgesShown = true;

    // A reply quotes them along with the text: somebody who turned the kludges
    // on to point at one can answer the line they are pointing at.
    const auto reply = startingText(request);
    REQUIRE(reply.lines.size() == 7);
    CHECK(reply.lines[0] == "Vasya Pupkin wrote:");
    CHECK(reply.lines[1] == " VP> AREA:RU.LINUX");
    CHECK(reply.lines[2] == " VP> @MSGID: 192:168/3.1 5f3a1b2c");
    CHECK(reply.lines[3] == " VP> hello there");
    CHECK(reply.lines[4] == " VP> SEEN-BY: 382/736");
    // Its own tearline and origin, never the answered message's.
    CHECK(reply.lines[5] == kTearline);
    CHECK(reply.lines[6] == kOrigin);

    // A forward carries the same lines, unquoted, where @message stands.
    fields.forward = true;
    request.fields = fields;
    const auto forwarded = startingText(request);
    REQUIRE(forwarded.lines.size() == 7);
    CHECK(forwarded.lines[0] == "* Forwarded by Yegor Gluhov");
    CHECK(forwarded.lines[1] == "AREA:RU.LINUX");
    CHECK(forwarded.lines[2] == "@MSGID: 192:168/3.1 5f3a1b2c");
    CHECK(forwarded.lines[3] == "hello there");
    CHECK(forwarded.lines[4] == "SEEN-BY: 382/736");
    CHECK(forwarded.lines[5] == kTearline);
    CHECK(forwarded.lines[6] == kOrigin);

    // With the kludges off, which is how the reader stands by default, only the
    // text is carried either way.
    request.kludgesShown = false;
    const auto plain = startingText(request);
    REQUIRE(plain.lines.size() == 4);
    CHECK(plain.lines[1] == "hello there");
}

TEST_CASE("A forward starts on the bare @position, above the signature "
          "[builder]") {
    // The shipped template's shape: a `@position` every message honours, a
    // `@quoted@position` only a reply reaches, and a signature under both.
    const TempFile tpl(
        "@forward* Forwarded by @CName\n"
        "@message\n"
        "Hello @pseudo!\n"
        "@position\n"
        "@quoted@position\n"
        "@quote\n"
        "@CFName\n");

    AppConfig cfg = config();
    cfg.templatePath = tpl.path();
    const AreaConfig area = areaOf(AreaKind::Echo);

    ComposeFields fields = netmailFields();
    fields.netmail = false;
    fields.toName = "All";
    fields.forward = true;

    MessageHeader original;
    original.from = "Vasya Pupkin";
    MessageBody body;
    body.lines = {{"hello there", false}};

    const BuildRequest request{cfg,   area,    fields,     &original,
                               &body, nullptr, 0x68A1B2C3, 180};
    const auto text = startingText(request);

    // "* Forwarded by", the message, the greeting, the line @position names,
    // the signature, and the pair closing the message.
    REQUIRE(text.lines.size() == 7);
    CHECK(text.cursorLine == 3);
    CHECK(text.lines[3].empty());
    // The writing starts above the user's own name, not under it: the end of
    // the text is where the message is signed and closed, not where it is
    // written.
    CHECK(text.lines[4] == "Yegor");
    CHECK(text.lines[5] == kTearline);
    CHECK(text.lines[6] == kOrigin);
}

TEST_CASE("A changed message keeps what the base already held [builder]") {
    MessageBody body;
    body.charset = "KOI8-R";
    // The order every format keeps: the control lines before the message, the
    // routing after the origin.
    body.lines = {{"@MSGID: 192:168/3.1 5f3a1b2c", true},
                  {"@TZUTC: 0500", true},
                  {"@CHRS: KOI8-R 2", true},
                  {"hello there", false},
                  {"--- GoldED", false, true},
                  {" * Origin: somewhere (192:168/3.1)", false, true},
                  {"SEEN-BY: 168/2 3", true},
                  {"@PATH: 168/3", true}};

    const auto kept = amberedit::app::preservedLines(body);
    CHECK(kept.charset == "KOI8-R");
    CHECK(kept.kludges == std::vector<std::string>{"MSGID: 192:168/3.1 5f3a1b2c",
                                                   "TZUTC: 0500", "CHRS: KOI8-R 2"});
    // The ^A is put back on what carried one; SEEN-BY never had one.
    CHECK(kept.trailing ==
          std::vector<std::string>{"SEEN-BY: 168/2 3", "\x01PATH: 168/3"});

    ComposeFields fields = netmailFields();
    fields.changing = true;
    fields.subject = "changed";

    const amberedit::app::ChangeStamp stamp{0x68A1B2C3, 180, "2:382/736.1"};
    const auto draft = amberedit::app::buildChange(
        fields, kept, {"*** Changed by Yegor Gluhov", "hello there, changed"}, stamp);

    // No REPLY is invented for it and no tearline of ours put under the one it
    // closes with. The two control lines that do change are the ones about the
    // writing rather than about the message: a MSGID naming this system and
    // this moment — what went out under the old one is not what it now says —
    // and the TZUTC that says which clock the new stamp is on. Both stand where
    // they stood.
    CHECK(kludgesOf(draft) == "MSGID: 2:382/736.1 68a1b2c3|TZUTC: 0300|CHRS: KOI8-R 2|");
    CHECK(draft.utcOffsetMinutes == 180);
    CHECK(draft.subject == "changed");
    // It is written back in the charset the CHRS line among those kludges names.
    CHECK(draft.charset == "KOI8-R");
    // The text as edited, with the routing back where it stood.
    CHECK(draft.lines == std::vector<std::string>{"*** Changed by Yegor Gluhov",
                                                  "hello there, changed",
                                                  "SEEN-BY: 168/2 3", "\x01PATH: 168/3"});

    // A message that carried neither line is given both, behind the routing
    // that stands in front of every other control line: anything written here
    // carries a MSGID and says which clock it was written by.
    MessageBody bare;
    bare.lines = {{"@INTL 2:5015/46 2:382/736", true},
                  {"@FMPT 1", true},
                  {"@CHRS: CP866 2", true},
                  {"hello there", false}};
    const auto plain = amberedit::app::buildChange(
        fields, amberedit::app::preservedLines(bare), {"hello there, changed"}, stamp);
    CHECK(kludgesOf(plain) ==
          "INTL 2:5015/46 2:382/736|FMPT 1|MSGID: 2:382/736.1 68a1b2c3|TZUTC: 0300|"
          "CHRS: CP866 2|");
}

TEST_CASE("A template that cannot be read still leaves a message to write [builder]") {
    AppConfig cfg = config();
    cfg.templatePath = "/nonexistent/amberedit.tpl";
    const AreaConfig area = areaOf(AreaKind::Echo);

    ComposeFields fields = netmailFields();
    fields.netmail = false;

    MessageHeader original;
    original.from = "Vasya Pupkin";
    MessageBody body;
    body.lines = {{"hello there", false}};

    const BuildRequest request{cfg,   area,    fields,     &original,
                               &body, nullptr, 0x68A1B2C3, 180};
    const auto text = startingText(request);

    CHECK_FALSE(text.error.empty());
    // The quote is the one thing a reply cannot be written without, and the
    // message is closed off as any other.
    REQUIRE(text.lines.size() == 3);
    CHECK(text.lines[0] == " VP> hello there");
    CHECK(text.lines[1] == kTearline);
}

TEST_CASE("A message carries the settings of the area group it is written in "
          "[builder]") {
    // The builder reads everything off the config it is handed, so a per-area
    // setting reaches it by the config being resolved for that area first. This
    // is that resolution and what comes out the other end of it.
    const AppConfig cfg = AppConfig::loadFromString(
        "tosser_config /dev/null\n"
        "tosser_config_format fidoconfig\n"
        "default_charset CP866\n"
        "compose_charset CP866\n"
        "origin Somewhere in the world\n"
        "group\n"
        "  member esp.*\n"
        "  compose_charset LATIN-1\n"
        "  origin En algún lugar del mundo\n"
        "endgroup\n");

    AreaConfig grouped = areaOf(AreaKind::Echo);
    grouped.tag = "esp.argentina";
    AreaConfig plain = areaOf(AreaKind::Echo);

    ComposeFields fields = netmailFields();
    fields.netmail = false;
    fields.toName = "All";
    fields.toAddr.clear();

    const AppConfig forGrouped = cfg.effectiveFor(grouped);
    const BuildRequest inGroup{forGrouped, grouped, fields,     nullptr,
                               nullptr,    nullptr, 0x68A1B2C3, 180};
    const auto draft = buildDraft(inGroup, {});
    CHECK(draft.charset == "LATIN-1");
    CHECK(kludgesOf(draft) ==
          "MSGID: 2:382/736.1 68a1b2c3|"
          "TZUTC: 0300|"
          "CHRS: LATIN-1 2|");
    CHECK(draft.lines.back().find("En algún lugar del mundo") != std::string::npos);

    // An area no group covers is written exactly as it was before there were
    // groups at all.
    const AppConfig forPlain = cfg.effectiveFor(plain);
    const BuildRequest outside{forPlain, plain,   fields,     nullptr,
                               nullptr,  nullptr, 0x68A1B2C3, 180};
    const auto ordinary = buildDraft(outside, {});
    CHECK(ordinary.charset == "CP866");
    CHECK(ordinary.lines.back().find("Somewhere in the world") != std::string::npos);
}
