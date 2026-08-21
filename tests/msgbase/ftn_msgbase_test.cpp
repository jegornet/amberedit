#include <doctest/doctest.h>

#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>

#include "config/text_util.hpp"
#include "encoding/iconv_recoder.hpp"
#include "msgbase/ftn_msgbase.hpp"
#include "msgbase/raw_message.hpp"
#include "temp_squish_base.hpp"
#include "test_paths.hpp"

using amberedit::config::text::startsWith;
using amberedit::domain::AreaConfig;
using amberedit::domain::MsgBaseType;
using amberedit::encoding::isValidUtf8;
using amberedit::msgbase::FtnMsgBase;

namespace fs = std::filesystem;

namespace {

using amberedit::test::TempSquishBase;

AreaConfig localnetArea(const std::string& path) {
    AreaConfig area;
    area.tag = "localnet";
    area.path = path;
    area.type = MsgBaseType::Squish;
    return area;
}

/// Today, as the base stamps a message written now. The clock here, in no time
/// zone of its own, which is what an FTN stamp is.
std::string today() {
    const std::time_t now = std::time(nullptr);
    std::tm broken{};
    localtime_r(&now, &broken);
    char buffer[16];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", &broken);
    return buffer;
}

}  // namespace

TEST_CASE("tzutcOffsetOf reads the zone a message states [msgbase]") {
    using amberedit::msgbase::tzutcOffsetOf;

    // The control block as a driver hands it back: a ^A in front of each line
    // and a carriage return behind it.
    const std::string block =
        "\001MSGID: 192:168/2 68a1b2c3\r\001TZUTC: 0300\r\001CHRS: CP866 2\r";
    // FTS-4008 writes a positive offset without a sign; an offset is shown with
    // one, the way strftime's %z writes it.
    CHECK(tzutcOffsetOf(block) == "+0300");
    CHECK(tzutcOffsetOf("\001TZUTC: -0500\r") == "-0500");
    // A plus is not to be written but is to be accepted where it is found.
    CHECK(tzutcOffsetOf("\001TZUTC: +0530\r") == "+0530");
    // Nothing is west of UTC at UTC.
    CHECK(tzutcOffsetOf("\001TZUTC: -0000\r") == "+0000");
    // TZUTCINFO is the same paragraph under the name JAM's subfield gave it.
    CHECK(tzutcOffsetOf("\001TZUTCINFO: 0200\r") == "+0200");

    // A message stating no zone states none: there is nothing to write for %z
    // and nothing worth guessing.
    CHECK(tzutcOffsetOf("\001MSGID: 192:168/2 68a1b2c3\r").empty());
    CHECK(tzutcOffsetOf("").empty());
    // All four digits must be present, and what is not an offset is not one.
    CHECK(tzutcOffsetOf("\001TZUTC: 300\r").empty());
    CHECK(tzutcOffsetOf("\001TZUTC: MSK\r").empty());
    CHECK(tzutcOffsetOf("\001TZUTC:\r").empty());
    // A line naming it and saying nothing usable leaves the other one to say
    // it, rather than answering for the message itself.
    CHECK(tzutcOffsetOf("\001TZUTCINFO: MSK\r\001TZUTC: 0300\r") == "+0300");
}

TEST_CASE("completeAddresses reads what the header left to the kludges [msgbase]") {
    using amberedit::msgbase::completeAddresses;
    using amberedit::msgbase::RawHeader;

    // A netmail message as a great many tossers store it in a Squish base: the
    // nets and the nodes in the header, the zones only in INTL.
    const std::string control =
        "\001INTL 2:382/736 2:5059/38\r\001MSGID: 2:5059/38 6a6c78a5\r";
    RawHeader header;
    header.origAddr.net = 5059;
    header.origAddr.node = 38;
    header.destAddr.net = 382;
    header.destAddr.node = 736;
    completeAddresses(header, control);
    CHECK(header.origAddr.toString() == "2:5059/38");
    CHECK(header.destAddr.toString() == "2:382/736");

    // The points are FMPT's and TOPT's to state, and each says one address.
    RawHeader points = header;
    points.origAddr.zone = 0;
    points.destAddr.zone = 0;
    completeAddresses(points, control + "\001FMPT 5\r\001TOPT 12\r");
    CHECK(points.origAddr.toString() == "2:5059/38.5");
    CHECK(points.destAddr.toString() == "2:382/736.12");

    // What the header states, it states: a base holding a zone of its own is
    // not corrected by a kludge disagreeing with it.
    RawHeader stored = header;
    stored.origAddr.point = 3;
    completeAddresses(stored, "\001INTL 3:382/736 3:5059/38\r\001FMPT 5\r");
    CHECK(stored.origAddr.toString() == "2:5059/38.3");
    CHECK(stored.destAddr.toString() == "2:382/736");

    // An INTL naming other nodes belongs to the message this one was routed
    // inside of and says nothing about this one.
    RawHeader routed;
    routed.origAddr.net = 5059;
    routed.origAddr.node = 38;
    routed.destAddr.net = 382;
    routed.destAddr.node = 736;
    completeAddresses(routed, "\001INTL 2:5020/1042 2:5030/1\r");
    CHECK(routed.origAddr.zone == 0);
    CHECK(routed.destAddr.zone == 0);

    // A message carrying none of them is left as the base holds it, and what
    // is not a point number is not one.
    RawHeader bare = routed;
    completeAddresses(bare, "\001MSGID: 2:5059/38 6a6c78a5\r");
    CHECK(bare.origAddr.toString() == "0:5059/38");
    completeAddresses(bare, "\001FMPT nope\r\001TOPT \r");
    CHECK(bare.origAddr.point == 0);
    CHECK(bare.destAddr.point == 0);
}

TEST_CASE("The Squish test base is present in the repository [squish]") {
    REQUIRE(fs::exists(amberedit::test::projectPath("testdata/msgbase/localnet.sqd")));
    REQUIRE(fs::exists(amberedit::test::projectPath("testdata/msgbase/localnet.sqi")));
}

TEST_CASE("FtnMsgBase opens a Squish base and counts messages [squish]") {
    TempSquishBase base;
    FtnMsgBase msgbase;

    REQUIRE(msgbase.open(localnetArea(base.path())));
    CHECK(msgbase.isOpen());
    CHECK(msgbase.count() > 0);
    CHECK(msgbase.lastError().empty());

    msgbase.close();
    CHECK_FALSE(msgbase.isOpen());
    CHECK(msgbase.count() == 0);
}

TEST_CASE("FtnMsgBase reads the headers of every message [squish]") {
    TempSquishBase base;
    FtnMsgBase msgbase;
    REQUIRE(msgbase.open(localnetArea(base.path())));

    const uint32_t total = msgbase.count();
    REQUIRE(total > 0);

    for (uint32_t i = 1; i <= total; ++i) {
        const auto header = msgbase.header(i);
        INFO("message " << i);
        CHECK(header.number == i);
        // Fields are converted at the adapter boundary — only UTF-8 comes out.
        CHECK(isValidUtf8(header.from));
        CHECK(isValidUtf8(header.to));
        CHECK(isValidUtf8(header.subject));
        CHECK_FALSE(header.from.empty());
        CHECK(header.date.isValid());
    }
}

TEST_CASE("FtnMsgBase reads message bodies as UTF-8 [squish]") {
    TempSquishBase base;
    FtnMsgBase msgbase;
    REQUIRE(msgbase.open(localnetArea(base.path())));

    const uint32_t total = msgbase.count();
    REQUIRE(total > 0);

    bool sawText = false;
    for (uint32_t i = 1; i <= total; ++i) {
        const auto body = msgbase.body(i);
        INFO("message " << i);
        CHECK(isValidUtf8(body.text()));
        CHECK_FALSE(body.charset.empty());
        // Kludges must not leak into the visible text.
        CHECK(body.text().find('\x01') == std::string::npos);
        if (!body.text().empty()) sawText = true;
    }
    CHECK(sawText);
}

TEST_CASE("FtnMsgBase keeps kludges out of the text [squish]") {
    TempSquishBase base;
    FtnMsgBase msgbase;
    REQUIRE(msgbase.open(localnetArea(base.path())));

    bool sawKludges = false;
    for (uint32_t i = 1; i <= msgbase.count(); ++i) {
        if (!msgbase.body(i).kludges().empty()) {
            sawKludges = true;
            break;
        }
    }
    // Any message that went through a tosser carries at least MSGID/PID/TZUTC.
    CHECK(sawKludges);
}

TEST_CASE("FtnMsgBase keeps the body lines in base order [squish]") {
    TempSquishBase base;
    FtnMsgBase msgbase;
    REQUIRE(msgbase.open(localnetArea(base.path())));

    const auto body = msgbase.body(1);
    REQUIRE(body.lines.size() > 2);

    // MSGID and friends come out of the control block, ahead of the text;
    // SEEN-BY and PATH sit at the end, behind the origin line. Neither is
    // hoisted or reordered.
    CHECK(body.lines.front().kludge);
    CHECK(startsWith(body.lines.front().text, "@MSGID:"));
    CHECK(body.lines.back().kludge);
    // PATH carries a ^A and so shows up as @PATH; SEEN-BY does not.
    CHECK((startsWith(body.lines.back().text, "SEEN-BY:") ||
           startsWith(body.lines.back().text, "@PATH:")));

    const auto firstText = std::find_if(body.lines.begin(), body.lines.end(),
                                        [](const auto& line) { return !line.kludge; });
    REQUIRE(firstText != body.lines.end());
    // There is visible text between the leading and the trailing service lines.
    CHECK(std::distance(body.lines.begin(), firstText) > 0);
}

TEST_CASE("The AREA: line a packet carries is service data [squish]") {
    TempSquishBase base;
    FtnMsgBase msgbase;
    REQUIRE(msgbase.open(localnetArea(base.path())));

    amberedit::domain::MessageDraft draft;
    draft.from = "Ivan Petrov";
    draft.to = "All";
    draft.subject = "out of a packet";
    draft.kludges = {"AREA:RU.LINUX", "MSGID: 192:168/3 5f3a1b2c"};
    // The same five characters in the message itself, which is a line of it.
    draft.lines = {"hello", "AREA:NOT.A.KLUDGE"};
    const uint32_t number = msgbase.write(draft);
    REQUIRE(number != 0);

    const auto body = msgbase.body(number);
    REQUIRE(body.lines.size() > 3);
    // The head of the message: service data, and shown as it stands — it never
    // carried a ^A for a '@' to stand in for, as SEEN-BY does not either.
    CHECK(body.lines.front().kludge);
    CHECK(body.lines.front().text == "AREA:RU.LINUX");
    CHECK(startsWith(body.lines[1].text, "@MSGID:"));

    // Only the very first line is the packet header. Anywhere else those
    // characters are text, and the reader shows them as the author wrote them.
    const auto inText =
        std::find_if(body.lines.begin(), body.lines.end(), [](const auto& line) {
            return line.text == "AREA:NOT.A.KLUDGE";
        });
    REQUIRE(inText != body.lines.end());
    CHECK_FALSE(inText->kludge);
}

TEST_CASE("FtnMsgBase adds no marker to lines that carry none [squish]") {
    TempSquishBase base;
    FtnMsgBase msgbase;
    REQUIRE(msgbase.open(localnetArea(base.path())));

    bool sawSeenBy = false;
    for (uint32_t i = 1; i <= msgbase.count(); ++i) {
        for (const auto& kludge : msgbase.body(i).kludges()) {
            INFO("message " << i << ": " << kludge);
            // SEEN-BY is stored without a ^A, so it must not acquire the '@' that
            // stands in for one. PATH does carry a ^A and keeps its '@'.
            if (startsWith(kludge, "SEEN-BY:")) sawSeenBy = true;
            CHECK_FALSE(startsWith(kludge, "@SEEN-BY:"));
        }
    }
    CHECK(sawSeenBy);
}

TEST_CASE("FtnMsgBase: out-of-range indexes are safe [squish]") {
    TempSquishBase base;
    FtnMsgBase msgbase;
    REQUIRE(msgbase.open(localnetArea(base.path())));

    const uint32_t total = msgbase.count();

    CHECK(msgbase.header(0).from.empty());
    CHECK(msgbase.header(total + 1).from.empty());
    CHECK(msgbase.body(0).text().empty());
    CHECK(msgbase.body(total + 1000).text().empty());
}

TEST_CASE("FtnMsgBase: reading before open() does not crash [squish]") {
    FtnMsgBase msgbase;
    CHECK(msgbase.count() == 0);
    CHECK(msgbase.header(1).from.empty());
    CHECK(msgbase.body(1).text().empty());
}

TEST_CASE("FtnMsgBase reports a missing base [squish]") {
    FtnMsgBase msgbase;
    CHECK_FALSE(msgbase.open(localnetArea("/nonexistent/path/area")));
    CHECK_FALSE(msgbase.lastError().empty());
    CHECK_FALSE(msgbase.isOpen());
}

TEST_CASE("FtnMsgBase refuses a base that is not there [squish]") {
    // A tosser config naming an area that was never created is ordinary, and
    // the error should say which format was looked for.
    AreaConfig area;
    area.tag = "NETMAIL";
    area.path = "/nonexistent/path/netmail";
    area.type = MsgBaseType::Sdm;
    area.kind = amberedit::domain::AreaKind::Netmail;

    FtnMsgBase msgbase;
    CHECK_FALSE(msgbase.open(area));
    CHECK_FALSE(msgbase.lastError().empty());
    CHECK_FALSE(msgbase.isOpen());

    // The same for an echo area, in case the two ever open differently.
    area.kind = amberedit::domain::AreaKind::Echo;
    CHECK_FALSE(msgbase.open(area));
    CHECK_FALSE(msgbase.lastError().empty());
}

TEST_CASE("FtnMsgBase opens a base on a long path [squish]") {
    // smapi used to overrun a fixed 78-byte buffer on a path this long, so the
    // adapter refused it. The native drivers have no such buffer: a deep spool
    // directory is an ordinary place for a base.
    const fs::path dir = fs::temp_directory_path() /
                         ("amberedit-long-" + std::to_string(::getpid())) /
                         std::string(90, 'd');
    fs::create_directories(dir);

    AreaConfig area;
    area.tag = "NETMAIL";
    area.path = dir.string();
    area.type = MsgBaseType::Sdm;
    area.kind = amberedit::domain::AreaKind::Netmail;
    REQUIRE(area.path.size() > 78);
    REQUIRE(FtnMsgBase::probeType(area.path) == MsgBaseType::Sdm);

    FtnMsgBase msgbase;
    CHECK(msgbase.open(area));
    CHECK(msgbase.count() == 0);

    std::error_code ec;
    fs::remove_all(dir.parent_path(), ec);
}

TEST_CASE("FtnMsgBase refuses to open a passthrough area [squish]") {
    AreaConfig area;
    area.tag = "su.general";
    area.type = MsgBaseType::Passthrough;

    FtnMsgBase msgbase;
    CHECK_FALSE(msgbase.open(area));
    CHECK_FALSE(msgbase.lastError().empty());
}

TEST_CASE("FtnMsgBase::probeType works the format out from the files [squish]") {
    TempSquishBase base;
    CHECK(FtnMsgBase::probeType(base.path()) == MsgBaseType::Squish);
    CHECK(FtnMsgBase::probeType("/nonexistent/path/area") == MsgBaseType::Unknown);
    CHECK(FtnMsgBase::probeType("") == MsgBaseType::Unknown);
}

TEST_CASE("Squish marks a message read and keeps it so [squish]") {
    TempSquishBase base;
    FtnMsgBase msgbase;
    REQUIRE(msgbase.open(localnetArea(base.path())));
    REQUIRE(msgbase.count() > 0);

    // The fixture is a base as a tosser leaves one: nothing in it has been read.
    CHECK_FALSE(msgbase.header(1).seen);

    REQUIRE(msgbase.markSeen(1));
    CHECK(msgbase.lastError().empty());
    CHECK(msgbase.header(1).seen);
    // Only the message asked for, and nothing else about it: MSGSEEN is not one
    // of the message's own attributes and must not turn up among them.
    CHECK_FALSE(msgbase.header(2).seen);
    CHECK(msgbase.header(1).attributes == msgbase.header(2).attributes);

    // Marking a message already marked is not an error and writes nothing.
    REQUIRE(msgbase.markSeen(1));
    CHECK(msgbase.header(1).seen);

    // The mark is on the disk, not in this object.
    FtnMsgBase again;
    REQUIRE(again.open(localnetArea(base.path())));
    CHECK(again.header(1).seen);
    CHECK_FALSE(again.header(2).seen);

    CHECK_FALSE(msgbase.markSeen(0));
    CHECK_FALSE(msgbase.markSeen(msgbase.count() + 1));
}

TEST_CASE("A changed Squish message is still marked read [squish]") {
    // The mark belongs to this system rather than to the message: rewriting the
    // message's words does not unmake the fact that somebody has read it.
    TempSquishBase base;
    FtnMsgBase msgbase;
    REQUIRE(msgbase.open(localnetArea(base.path())));
    REQUIRE(msgbase.count() > 0);
    REQUIRE(msgbase.markSeen(1));

    amberedit::domain::MessageDraft draft;
    draft.from = "Yegor Gluhov";
    draft.to = "All";
    draft.subject = "Rewritten";
    draft.lines = {"A second version of the same message."};
    REQUIRE(msgbase.replace(1, draft));

    CHECK(msgbase.header(1).subject == "Rewritten");
    CHECK(msgbase.header(1).seen);
}

TEST_CASE("FtnMsgBase opens a base with no stated type [squish]") {
    TempSquishBase base;
    AreaConfig area = localnetArea(base.path());
    area.type = MsgBaseType::Unknown;  // a tosser config with no -b option

    FtnMsgBase msgbase;
    REQUIRE(msgbase.open(area));
    CHECK(msgbase.count() > 0);
}

TEST_CASE("FtnMsgBase can be reopened [squish]") {
    TempSquishBase base;
    FtnMsgBase msgbase;

    REQUIRE(msgbase.open(localnetArea(base.path())));
    const uint32_t first = msgbase.count();

    REQUIRE(msgbase.open(localnetArea(base.path())));
    CHECK(msgbase.count() == first);
}

TEST_CASE("FtnMsgBase converts between positions and UIDs [squish]") {
    TempSquishBase base;
    FtnMsgBase msgbase;
    REQUIRE(msgbase.open(localnetArea(base.path())));

    const uint32_t total = msgbase.count();
    REQUIRE(total > 0);

    for (uint32_t i = 1; i <= total; ++i) {
        const uint32_t uid = msgbase.uidOf(i);
        CHECK(uid != 0);
        // The pair is what a lastread mark rests on: a position written out as
        // a UID has to come back as the same position.
        CHECK(msgbase.indexOfUid(uid) == i);
    }

    // Outside the base in both directions.
    CHECK(msgbase.uidOf(0) == 0);
    CHECK(msgbase.uidOf(total + 1) == 0);
    CHECK(msgbase.indexOfUid(0) == 0);
}

TEST_CASE("A UID from before the base lands on nothing [squish]") {
    TempSquishBase base;
    FtnMsgBase msgbase;
    REQUIRE(msgbase.open(localnetArea(base.path())));
    REQUIRE(msgbase.count() > 0);

    // Squish UMSGIDs start at 1, so nothing at or before this survives and the
    // mark counts as unread rather than as the first message.
    CHECK(msgbase.indexOfUid(msgbase.uidOf(1) - 1) == 0);
}

TEST_CASE("A message's own CHRS decides its header, not the default [squish]") {
    // The names and the subject live in the header, but the CHRS kludge that
    // says what charset they are in lives in the body. Reading a header on its
    // own used to fall back to `default_charset`, so an area whose messages
    // were in anything else showed mojibake subjects in the message list while
    // the reader rendered the bodies correctly.
    //
    // testdata/msgbase/charsets holds three messages for this: "Привет" in
    // KOI8-R with a matching CHRS, the same word in CP866 with its own CHRS,
    // and one with no CHRS at all.
    // The fixture stores "\xF0\xD2\xC9\xD7\xC5\xD4" (KOI8-R) and
    // "\x8F\xE0\xA8\xA2\xA5\xE2" (CP866) — both the same word.
    const std::string expected = "Привет";

    AreaConfig area;
    area.tag = "charsets";
    area.path = amberedit::test::projectPath("testdata/msgbase/charsets");
    area.type = MsgBaseType::Squish;

    for (const char* fallback : {"CP866", "KOI8-R", "CP1251"}) {
        INFO("default_charset = " << fallback);
        FtnMsgBase msgbase(fallback);
        REQUIRE(msgbase.open(area));
        REQUIRE(msgbase.count() == 3);

        // Whatever the default is, a message that states its charset is read
        // in that charset — both of these come back as the same word.
        CHECK(msgbase.header(1).subject == expected);
        CHECK(msgbase.header(2).subject == expected);
        // And the header agrees with the body it came from.
        CHECK(msgbase.body(1).charset == "KOI8-R");
        CHECK(msgbase.body(2).charset == "CP866");
    }

    // The third message states nothing, so the default has the only say — it
    // reads correctly under CP866 and as something else under KOI8-R.
    FtnMsgBase asCp866("CP866");
    REQUIRE(asCp866.open(area));
    CHECK(asCp866.header(3).subject == expected);

    FtnMsgBase asKoi8("KOI8-R");
    REQUIRE(asKoi8.open(area));
    CHECK(asKoi8.header(3).subject != expected);
    CHECK(isValidUtf8(asKoi8.header(3).subject));
}

TEST_CASE("The charset test base is present in the repository [squish]") {
    REQUIRE(fs::exists(amberedit::test::projectPath("testdata/msgbase/charsets.sqd")));
    REQUIRE(fs::exists(amberedit::test::projectPath("testdata/msgbase/charsets.sqi")));
}

TEST_CASE("FtnMsgBase writes a message and reads it back [squish]") {
    TempSquishBase base;
    FtnMsgBase msgbase("CP866");
    REQUIRE(msgbase.open(localnetArea(base.path())));

    const uint32_t before = msgbase.count();
    REQUIRE(before > 0);

    amberedit::domain::MessageDraft draft;
    draft.from = "Yegor Gluhov";
    draft.to = "Иван Петров";
    draft.subject = "Привет";
    draft.origAddr = *amberedit::domain::FtnAddress::parse("192:168/2");
    draft.destAddr = *amberedit::domain::FtnAddress::parse("192:168/3.1");
    draft.netmail = true;
    draft.attributes =
        amberedit::domain::attr::kLocal | amberedit::domain::attr::kPrivate;
    draft.charset = "CP866";
    draft.utcOffsetMinutes = 180;
    draft.kludges = {"INTL 192:168/3 192:168/2", "TOPT 1", "MSGID: 192:168/2 68a1b2c3",
                     "TZUTC: 0300", "CHRS: CP866 2"};
    draft.lines = {"Привет!", "", "--- AmberEdit",
                   " * Origin: AmberEdit test (192:168/2)"};

    const uint32_t number = msgbase.write(draft);
    REQUIRE(number == before + 1);
    CHECK(msgbase.lastError().empty());
    CHECK(msgbase.count() == before + 1);

    // The header comes back as it went in, converted both ways: what was
    // written in CP866 is read as UTF-8 again.
    const auto header = msgbase.header(number);
    CHECK(header.from == "Yegor Gluhov");
    CHECK(header.to == "Иван Петров");
    CHECK(header.subject == "Привет");
    CHECK(header.origAddr.toString() == "192:168/2");
    CHECK(header.destAddr.toString() == "192:168/3.1");
    CHECK(header.isPrivate());
    CHECK(amberedit::domain::isUnsent(header));
    // The clock the message says it was written by, read off its own TZUTC —
    // out of the control lines the header already reads for the charset, so
    // that a Date column asking for `%z` costs no second read.
    CHECK(header.utcOffset == "+0300");

    const auto body = msgbase.body(number);
    std::string kludges;
    std::string text;
    for (const auto& line : body.lines) {
        (line.kludge ? kludges : text).append(line.text).append("|");
    }
    // The control lines come back with '@' where the ^A was, in the order they
    // were written.
    CHECK(kludges ==
          "@INTL 192:168/3 192:168/2|@TOPT 1|@MSGID: 192:168/2 68a1b2c3|"
          "@TZUTC: 0300|@CHRS: CP866 2|");
    CHECK(text == "Привет!||--- AmberEdit| * Origin: AmberEdit test (192:168/2)|");
    CHECK(body.charset == "CP866");
}

TEST_CASE("A draft naming no charset is written in the area's own [squish]") {
    // A draft carries no charset only where the body it was made of could not
    // be read — copyOf() and buildChange() pass on what the base said, and a
    // base that read nothing said nothing. The message still has to land on
    // disk in a charset, and the only one to hand is the one this area is read
    // in, so that is what encode() falls back to. KOI8-R rather than CP866
    // here: the fallback has to follow the area, not a code page picked once
    // and written into the draft struct.
    TempSquishBase base;
    FtnMsgBase msgbase("KOI8-R");
    REQUIRE(msgbase.open(localnetArea(base.path())));

    amberedit::domain::MessageDraft draft;
    draft.from = "Yegor Gluhov";
    draft.to = "Иван Петров";
    draft.subject = "Привет";
    draft.origAddr = *amberedit::domain::FtnAddress::parse("192:168/2");
    draft.kludges = {"MSGID: 192:168/2 68a1b2c3"};
    draft.lines = {"Привет!"};
    REQUIRE(draft.charset.empty());

    const uint32_t number = msgbase.write(draft);
    REQUIRE(number != 0);

    // Written in KOI8-R and read back in KOI8-R, which is the same round trip
    // a message declaring the charset makes. Written in anything else it would
    // come back mojibake, since the message names no charset of its own for the
    // reader to go by.
    const auto header = msgbase.header(number);
    CHECK(header.to == "Иван Петров");
    CHECK(header.subject == "Привет");

    const auto body = msgbase.body(number);
    CHECK(body.charset == "KOI8-R");
    std::string text;
    for (const auto& line : body.lines) {
        if (!line.kludge) text.append(line.text).append("|");
    }
    CHECK(text == "Привет!|");
}

TEST_CASE("A Squish header short of a zone is read out of its kludges [squish]") {
    // What a tosser writes into a Squish base: the XMSG address words carry a
    // net and a node, the zone and the point are left at zero, and INTL, FMPT
    // and TOPT — which the message goes out with anyway — say the rest.
    TempSquishBase base;
    FtnMsgBase msgbase("CP866");
    REQUIRE(msgbase.open(localnetArea(base.path())));

    amberedit::domain::MessageDraft draft;
    draft.from = "Yegor Gluhov";
    draft.to = "Andrey Mundirov";
    draft.subject = "ping";
    draft.origAddr = *amberedit::domain::FtnAddress::parse("0:168/2");
    draft.destAddr = *amberedit::domain::FtnAddress::parse("0:168/3");
    draft.netmail = true;
    draft.charset = "CP866";
    draft.kludges = {"INTL 192:168/3 192:168/2", "FMPT 5", "TOPT 1",
                     "MSGID: 192:168/2.5 68a1b2c3"};
    draft.lines = {"Hello!"};

    const uint32_t number = msgbase.write(draft);
    REQUIRE(number != 0);

    const auto header = msgbase.header(number);
    CHECK(header.origAddr.toString() == "192:168/2.5");
    CHECK(header.destAddr.toString() == "192:168/3.1");
}

TEST_CASE("A changed message is written over the one it was [squish]") {
    TempSquishBase base;
    FtnMsgBase msgbase("CP866");
    REQUIRE(msgbase.open(localnetArea(base.path())));

    const uint32_t before = msgbase.count();
    REQUIRE(before > 2);
    const auto was = msgbase.header(1);
    const uint32_t uid = msgbase.uidOf(1);
    const std::string second = msgbase.header(2).subject;

    amberedit::domain::MessageDraft draft;
    draft.from = was.from;
    draft.to = was.to;
    draft.subject = "Changed subject";
    draft.origAddr = was.origAddr;
    draft.destAddr = was.destAddr;
    draft.charset = "CP866";
    draft.kludges = {"MSGID: 192:168/2 68a1b2c3", "CHRS: CP866 2"};
    draft.lines = {"A shorter message."};

    REQUIRE(msgbase.replace(1, draft));
    CHECK(msgbase.lastError().empty());

    // The area is the same length, the message is where it was, and every
    // other message is where it was.
    CHECK(msgbase.count() == before);
    CHECK(msgbase.header(2).subject == second);
    CHECK(msgbase.header(1).subject == "Changed subject");
    CHECK(msgbase.body(1).text() == "A shorter message.");
    // It is the same message: the UMSGID a lastread mark is made of is the
    // base's rather than the draft's, and so is the stamp it arrived here
    // under. The date it is written under is now — it has just been written.
    CHECK(msgbase.uidOf(1) == uid);
    CHECK(msgbase.header(1).date.format("%Y-%m-%d") == today());
    CHECK(msgbase.header(1).date.format("%Y-%m-%d %H:%M") !=
          was.date.format("%Y-%m-%d %H:%M"));
    CHECK(msgbase.header(1).arrivalDate.format("%Y-%m-%d %H:%M") ==
          was.arrivalDate.format("%Y-%m-%d %H:%M"));

    // It reads back the same way through a base opened afresh, which is what
    // says the index and the frame agree on disk and not only in memory.
    FtnMsgBase again("CP866");
    REQUIRE(again.open(localnetArea(base.path())));
    CHECK(again.count() == before);
    CHECK(again.header(1).subject == "Changed subject");
    CHECK(again.uidOf(1) == uid);
    CHECK(again.header(2).subject == second);
}

TEST_CASE("A changed message that has outgrown its frame moves to another [squish]") {
    TempSquishBase base;
    FtnMsgBase msgbase("CP866");
    REQUIRE(msgbase.open(localnetArea(base.path())));

    const uint32_t before = msgbase.count();
    REQUIRE(before > 2);
    const auto was = msgbase.header(1);
    const uint32_t uid = msgbase.uidOf(1);
    const uint32_t lastUid = msgbase.uidOf(before);
    const std::string last = msgbase.header(before).subject;
    const auto size = fs::file_size(base.path() + ".sqd");

    amberedit::domain::MessageDraft draft;
    draft.from = was.from;
    draft.to = was.to;
    draft.subject = was.subject;
    draft.origAddr = was.origAddr;
    draft.destAddr = was.destAddr;
    draft.charset = "CP866";
    draft.kludges = {"MSGID: 192:168/2 68a1b2c3"};
    // Far more than the first message of the base has room for, so the frame
    // it lies in cannot hold it and another has to be found.
    draft.lines.assign(200, "A line of a message that has grown a good deal.");

    REQUIRE(msgbase.replace(1, draft));
    CHECK(msgbase.lastError().empty());
    CHECK(msgbase.count() == before);
    CHECK(msgbase.uidOf(1) == uid);
    CHECK(msgbase.body(1).lines.size() > 100);
    // Nothing was copied up or down the base to make room: the messages after
    // it are the same messages, at the same numbers.
    CHECK(msgbase.header(before).subject == last);
    CHECK(msgbase.uidOf(before) == lastUid);
    // The file grew by the one frame the message went into, rather than by the
    // whole of the base being written out again.
    CHECK(fs::file_size(base.path() + ".sqd") > size);

    FtnMsgBase again("CP866");
    REQUIRE(again.open(localnetArea(base.path())));
    CHECK(again.count() == before);
    CHECK(again.body(1).lines.size() > 100);
    CHECK(again.header(before).subject == last);

    // And the frame that was left goes back to the base: the next message
    // written takes it rather than the end of the file.
    const auto grown = fs::file_size(base.path() + ".sqd");
    amberedit::domain::MessageDraft small = draft;
    small.lines = {"Small enough for the hole the first message left."};
    REQUIRE(again.write(small) == before + 1);
    CHECK(fs::file_size(base.path() + ".sqd") == grown);
}

TEST_CASE("FtnMsgBase deletes a message [squish]") {
    TempSquishBase base;
    FtnMsgBase msgbase("CP866");
    REQUIRE(msgbase.open(localnetArea(base.path())));

    const uint32_t before = msgbase.count();
    REQUIRE(before > 2);
    const std::string second = msgbase.header(2).subject;
    const std::string third = msgbase.header(3).subject;
    REQUIRE(second != third);

    REQUIRE(msgbase.remove(2));
    CHECK(msgbase.lastError().empty());
    CHECK(msgbase.count() == before - 1);
    // Everything after it moved up one, so the number that named it now names
    // what followed it.
    CHECK(msgbase.header(2).subject == third);

    // There is no message past the end to delete, and saying so is not the
    // same as deleting one.
    CHECK_FALSE(msgbase.remove(before));
    CHECK_FALSE(msgbase.lastError().empty());
    CHECK(msgbase.count() == before - 1);
    CHECK_FALSE(msgbase.remove(0));
}

TEST_CASE("FtnMsgBase reads the thread links, and nothing where there are none "
          "[squish]") {
    TempSquishBase base;
    FtnMsgBase msgbase;
    REQUIRE(msgbase.open(localnetArea(base.path())));

    // Nothing outside the area is in any thread.
    CHECK(msgbase.thread(0).empty());
    CHECK(msgbase.thread(msgbase.count() + 1).empty());

    // The one thread the fixture holds, as GoldED linked it: message 34
    // answers 33, and each of them says so from its own end. The reply 33
    // also has in the base is a message the fixture does not carry, and a
    // link to a message that is not there is left out rather than shown.
    std::string threads;
    for (uint32_t i = 1; i <= msgbase.count(); ++i) {
        const auto thread = msgbase.thread(i);
        if (thread.empty()) continue;
        threads += std::to_string(i) + ":";
        if (thread.replyTo != 0) threads += " -" + std::to_string(thread.replyTo);
        for (const uint32_t reply : thread.replies) {
            threads += " +" + std::to_string(reply);
        }
        threads += "|";
    }
    CHECK(threads == "33: +34|34: -33|");
}
