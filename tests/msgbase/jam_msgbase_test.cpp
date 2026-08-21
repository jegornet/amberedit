#include <catch2/catch.hpp>

#include <cstdint>
#include <filesystem>
#include <string>

#include "config/text_util.hpp"
#include "encoding/iconv_recoder.hpp"
#include "msgbase/ftn_msgbase.hpp"
#include "temp_msg_bases.hpp"
#include "test_paths.hpp"

using amberedit::config::text::startsWith;
using amberedit::domain::AreaConfig;
using amberedit::domain::MsgBaseType;
using amberedit::encoding::isValidUtf8;
using amberedit::msgbase::FtnMsgBase;

namespace fs = std::filesystem;

namespace {

using amberedit::test::TempJamBase;

AreaConfig jamArea(const std::string& path) {
    AreaConfig area;
    area.tag = "area2";
    area.path = path;
    area.type = MsgBaseType::Jam;
    return area;
}

}  // namespace

TEST_CASE("The JAM test base is present in the repository", "[jam]") {
    REQUIRE(fs::exists(amberedit::test::projectPath("testdata/msgbase/area2.jhr")));
    REQUIRE(fs::exists(amberedit::test::projectPath("testdata/msgbase/area2.jdt")));
    REQUIRE(fs::exists(amberedit::test::projectPath("testdata/msgbase/area2.jdx")));
}

TEST_CASE("FtnMsgBase opens a JAM base and counts messages", "[jam]") {
    TempJamBase base;
    FtnMsgBase msgbase;

    REQUIRE(msgbase.open(jamArea(base.path())));
    CHECK(msgbase.isOpen());
    CHECK(msgbase.count() == 1);
    CHECK(msgbase.lastError().empty());

    msgbase.close();
    CHECK_FALSE(msgbase.isOpen());
    CHECK(msgbase.count() == 0);
}

TEST_CASE("FtnMsgBase reads a JAM header", "[jam]") {
    TempJamBase base;
    FtnMsgBase msgbase;
    REQUIRE(msgbase.open(jamArea(base.path())));

    const auto header = msgbase.header(1);
    CHECK(header.number == 1);
    CHECK(header.from == "Yegor Gluhov");
    CHECK(header.to == "All");
    CHECK(header.subject == "test");
    CHECK(header.date.isValid());
    CHECK(header.origAddr.toString() == "192:168/2");
    // An echomail message names no destination.
    CHECK_FALSE(header.isPrivate());
}

TEST_CASE("FtnMsgBase reads a JAM body as UTF-8", "[jam]") {
    TempJamBase base;
    FtnMsgBase msgbase;
    REQUIRE(msgbase.open(jamArea(base.path())));

    const auto body = msgbase.body(1);
    CHECK(isValidUtf8(body.text()));
    // The CHRS kludge lives in a subfield, not in the text, and still decides
    // the charset.
    CHECK(body.charset == "CP866");
    // Kludges must not leak into the visible text.
    CHECK(body.text().find('\x01') == std::string::npos);
    CHECK_FALSE(body.text().empty());
    CHECK(body.origin == " * Origin:  (192:168/2)");
}

TEST_CASE("FtnMsgBase reads the JAM kludges from the subfields", "[jam]") {
    TempJamBase base;
    FtnMsgBase msgbase;
    REQUIRE(msgbase.open(jamArea(base.path())));

    const auto body = msgbase.body(1);
    REQUIRE(body.lines.size() > 2);

    // JAM keeps the kludges in header subfields; they come back ahead of the
    // text in the "^AKLUDGE" form, with '@' standing in for the ^A.
    CHECK(body.lines.front().kludge);
    CHECK(startsWith(body.lines.front().text, "@"));

    std::string kludges;
    for (const auto& kludge : body.kludges()) kludges += kludge + "|";
    CHECK(kludges.find("@MSGID: 192:168/2 6a7d7d8d|") != std::string::npos);
    CHECK(kludges.find("@CHRS: CP866 2|") != std::string::npos);
    CHECK(kludges.find("@TZUTC: 0200|") != std::string::npos);

    std::string text;
    for (const auto& line : body.lines) {
        if (!line.kludge) text += line.text + "|";
    }
    CHECK(text ==
          "Hello All!||subj||Yegor||--- gossipEd-darwin/arm64 2.1-dev-b158ec56|"
          " * Origin:  (192:168/2)|");
}

TEST_CASE("FtnMsgBase: out-of-range indexes in a JAM base are safe", "[jam]") {
    TempJamBase base;
    FtnMsgBase msgbase;
    REQUIRE(msgbase.open(jamArea(base.path())));

    const uint32_t total = msgbase.count();

    CHECK(msgbase.header(0).from.empty());
    CHECK(msgbase.header(total + 1).from.empty());
    CHECK(msgbase.body(0).text().empty());
    CHECK(msgbase.body(total + 1000).text().empty());
}

TEST_CASE("FtnMsgBase::probeType recognises a JAM base", "[jam]") {
    TempJamBase base;
    CHECK(FtnMsgBase::probeType(base.path()) == MsgBaseType::Jam);
}

TEST_CASE("FtnMsgBase opens a JAM base with no stated type", "[jam]") {
    TempJamBase base;
    AreaConfig area = jamArea(base.path());
    area.type = MsgBaseType::Unknown;  // a tosser config with no -b option

    FtnMsgBase msgbase;
    REQUIRE(msgbase.open(area));
    CHECK(msgbase.count() == 1);
}

TEST_CASE("FtnMsgBase converts JAM positions and UIDs both ways", "[jam]") {
    TempJamBase base;
    FtnMsgBase msgbase;
    REQUIRE(msgbase.open(jamArea(base.path())));

    const uint32_t total = msgbase.count();
    REQUIRE(total > 0);

    for (uint32_t i = 1; i <= total; ++i) {
        const uint32_t uid = msgbase.uidOf(i);
        CHECK(uid != 0);
        CHECK(msgbase.indexOfUid(uid) == i);
    }

    CHECK(msgbase.uidOf(0) == 0);
    CHECK(msgbase.uidOf(total + 1) == 0);
    CHECK(msgbase.indexOfUid(0) == 0);
}

TEST_CASE("A lone JAM message is in no thread", "[jam]") {
    TempJamBase base;
    FtnMsgBase msgbase;
    REQUIRE(msgbase.open(jamArea(base.path())));

    CHECK(msgbase.thread(1).empty());
    CHECK(msgbase.thread(0).empty());
    CHECK(msgbase.thread(msgbase.count() + 1).empty());
}

TEST_CASE("FtnMsgBase writes a message into a JAM base and reads it back", "[jam]") {
    TempJamBase base;
    FtnMsgBase msgbase("CP866");
    REQUIRE(msgbase.open(jamArea(base.path())));

    const uint32_t before = msgbase.count();
    REQUIRE(before > 0);

    amberedit::domain::MessageDraft draft;
    draft.from = "Yegor Gluhov";
    draft.to = "Иван Петров";
    draft.subject = "Привет";
    draft.origAddr = *amberedit::domain::FtnAddress::parse("192:168/2");
    draft.netmail = false;
    // Echomail written here: local, and not private — the attribute an echo never
    // carries.
    draft.attributes = amberedit::domain::attr::kLocal;
    draft.charset = "CP866";
    draft.utcOffsetMinutes = 180;
    draft.kludges = {"MSGID: 192:168/2 68a1b2c3", "TZUTC: 0300", "CHRS: CP866 2"};
    draft.lines = {"Привет, All!", "", "--- AmberEdit",
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
    // JAM stores an address subfield for netmail only; an echomail message
    // carries its origin in the Origin line and MSGID, so the header comes
    // back with none — the driver skips the subfield for echo areas.
    CHECK(header.origAddr.toString() == "0:0/0");
    CHECK_FALSE(header.isPrivate());

    const auto body = msgbase.body(number);
    std::string kludges;
    std::string text;
    for (const auto& line : body.lines) {
        (line.kludge ? kludges : text).append(line.text).append("|");
    }
    CHECK(kludges == "@MSGID: 192:168/2 68a1b2c3|@TZUTC: 0300|@CHRS: CP866 2|");
    CHECK(text == "Привет, All!||--- AmberEdit| * Origin: AmberEdit test (192:168/2)|");
    CHECK(body.charset == "CP866");
}

TEST_CASE("FtnMsgBase deletes a message from a JAM base", "[jam]") {
    TempJamBase base;
    FtnMsgBase msgbase;
    REQUIRE(msgbase.open(jamArea(base.path())));

    const uint32_t before = msgbase.count();
    REQUIRE(before > 0);

    REQUIRE(msgbase.remove(1));
    CHECK(msgbase.lastError().empty());
    CHECK(msgbase.count() == before - 1);

    // There is nothing past the end to delete, and saying so is not the same
    // as deleting.
    CHECK_FALSE(msgbase.remove(before + 1));
    CHECK_FALSE(msgbase.lastError().empty());
    CHECK_FALSE(msgbase.remove(0));
}
