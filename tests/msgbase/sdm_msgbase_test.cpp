#include <doctest/doctest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include "config/text_util.hpp"
#include "encoding/iconv_recoder.hpp"
#include "msgbase/ftn_msgbase.hpp"
#include "msgbase/raw_message.hpp"
#include "temp_msg_bases.hpp"
#include "test_paths.hpp"
#include "test_strings.hpp"

using amberedit::config::text::startsWith;
using amberedit::domain::AreaConfig;
using amberedit::domain::MsgBaseType;
using amberedit::encoding::isValidUtf8;
using amberedit::msgbase::FtnMsgBase;

namespace fs = std::filesystem;

namespace {

using amberedit::test::TempSdmBase;

AreaConfig netmailArea(const std::string& path) {
    AreaConfig area;
    area.tag = "NETMAIL";
    area.path = path;
    area.type = MsgBaseType::Sdm;
    area.kind = amberedit::domain::AreaKind::Netmail;
    return area;
}

/// The 190 bytes a message file opens with, for the tests that are about where
/// a field lands rather than about what comes back through the port.
constexpr size_t kHeaderSize = 190;

std::string storedHeader(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::string raw(kHeaderSize, '\0');
    in.read(&raw[0], static_cast<std::streamsize>(raw.size()));
    return in.gcount() == static_cast<std::streamsize>(kHeaderSize) ? raw : std::string{};
}

uint16_t wordAt(const std::string& raw, size_t at) {
    return static_cast<uint16_t>(
        static_cast<unsigned char>(raw[at]) |
        (static_cast<unsigned char>(raw[at + 1]) << 8u));  // little-endian, as on disk
}

void putWord(const fs::path& file, size_t at, uint16_t value) {
    std::fstream io(file, std::ios::in | std::ios::out | std::ios::binary);
    io.seekp(static_cast<std::streamoff>(at));
    const char bytes[2] = {static_cast<char>(value & 0xffu),
                           static_cast<char>(value >> 8u)};
    io.write(bytes, sizeof(bytes));
}

/// A *.msg as a writer that fills in only the date in words leaves one: the
/// ASCII date where the header has it and zeroes where the Opus stamps would
/// be — which is also the harmless shape of an FTS-0001-era header, whose
/// zone and point words stand in those same eight bytes.
void writeAsciiDatedMsg(const fs::path& file) {
    std::string raw(kHeaderSize, '\0');
    const auto put = [&raw](size_t at, const std::string& text) {
        raw.replace(at, text.size(), text);
    };
    put(0, "Somebody Else");
    put(36, "Yegor Gluhov");
    put(72, "an old message");
    put(144, "01 Jan 86  02:34:56");
    raw[166] = 1;                        // destnode
    raw[168] = 2;                        // orignode
    raw[172] = static_cast<char>(0xa8);  // orignet 168
    raw[174] = static_cast<char>(0xa8);  // destnet 168
    raw[187] = 1;                        // attr: MSGLOCAL, the high byte of the word

    std::ofstream out(file, std::ios::binary);
    out.write(raw.data(), static_cast<std::streamsize>(raw.size()));
    // Split so that the escape stops at \x01 rather than swallowing the C.
    const std::string body =
        "\x01"
        "CHRS: CP437 2\rHello.\r";
    out.write(body.data(), static_cast<std::streamsize>(body.size()));
    out.put('\0');
}

}  // namespace

TEST_CASE("The Fido *.msg test base is present in the repository [sdm]") {
    REQUIRE(fs::exists(amberedit::test::projectPath("testdata/msgbase/netmail/198.msg")));
}

TEST_CASE("FtnMsgBase opens a Fido *.msg base and counts messages [sdm]") {
    TempSdmBase base;
    FtnMsgBase msgbase;

    REQUIRE(msgbase.open(netmailArea(base.path())));
    CHECK(msgbase.isOpen());
    CHECK(msgbase.count() == 1);

    msgbase.close();
    CHECK_FALSE(msgbase.isOpen());
    CHECK(msgbase.count() == 0);
}

TEST_CASE("FtnMsgBase reads a Fido *.msg header [sdm]") {
    TempSdmBase base;
    FtnMsgBase msgbase;
    REQUIRE(msgbase.open(netmailArea(base.path())));

    const auto header = msgbase.header(1);
    CHECK(header.number == 1);
    CHECK(header.from == "Yegor Gluhov");
    CHECK(header.to == "SysOp");
    CHECK(header.subject == "test");

    // The stamp as the header stores it: "13 Aug 26  10:15:20".
    CHECK(header.date.isValid());
    CHECK(header.date.year == 2026);
    CHECK(header.date.month == 8);
    CHECK(header.date.day == 13);
    CHECK(header.date.hour == 10);
    CHECK(header.date.minute == 15);
    CHECK(header.date.second == 20);

    // FTS-0001 keeps only net/node in the header; the zones come from the
    // INTL kludge.
    CHECK(header.origAddr.toString() == "192:168/2");
    CHECK(header.destAddr.toString() == "192:168/1");
}

TEST_CASE("FtnMsgBase reads a Fido *.msg body as UTF-8 [sdm]") {
    TempSdmBase base;
    FtnMsgBase msgbase;
    REQUIRE(msgbase.open(netmailArea(base.path())));

    const auto body = msgbase.body(1);
    CHECK(isValidUtf8(body.text()));
    CHECK(body.charset == "CP866");
    // Kludges must not leak into the visible text.
    CHECK(body.text().find('\x01') == std::string::npos);
    CHECK_FALSE(body.text().empty());
    CHECK(body.origin == " * Origin:  (192:168/2)");
}

TEST_CASE("FtnMsgBase reads the inline Fido *.msg kludges [sdm]") {
    TempSdmBase base;
    FtnMsgBase msgbase;
    REQUIRE(msgbase.open(netmailArea(base.path())));

    const auto body = msgbase.body(1);
    REQUIRE(body.lines.size() > 2);

    // In a *.msg the kludges are the leading ^A lines of the text itself, and
    // they come back ahead of it in base order, '@' standing in for the ^A.
    CHECK(body.lines.front().kludge);
    CHECK(startsWith(body.lines.front().text, "@"));

    std::string kludges;
    for (const auto& kludge : body.kludges()) kludges += kludge + "|";
    CHECK(kludges.find("@MSGID: 192:168/2 6a7d7d18|") != std::string::npos);
    CHECK(kludges.find("@INTL 192:168/1 192:168/2|") != std::string::npos);
    CHECK(kludges.find("@CHRS: CP866 2|") != std::string::npos);
    CHECK(kludges.find("@TZUTC: 0200|") != std::string::npos);

    std::string text;
    for (const auto& line : body.lines) {
        if (!line.kludge) text += line.text + "|";
    }
    CHECK(text ==
          "Hello SysOp!||test||Yegor||--- gossipEd-darwin/arm64 2.1-dev-b158ec56|"
          " * Origin:  (192:168/2)|");
}

TEST_CASE("FtnMsgBase: out-of-range indexes in a *.msg base are safe [sdm]") {
    TempSdmBase base;
    FtnMsgBase msgbase;
    REQUIRE(msgbase.open(netmailArea(base.path())));

    const uint32_t total = msgbase.count();

    CHECK(msgbase.header(0).from.empty());
    CHECK(msgbase.header(total + 1).from.empty());
    CHECK(msgbase.body(0).text().empty());
    CHECK(msgbase.body(total + 1000).text().empty());
}

TEST_CASE("FtnMsgBase::probeType recognises a *.msg directory [sdm]") {
    TempSdmBase base;
    CHECK(FtnMsgBase::probeType(base.path()) == MsgBaseType::Sdm);
}

TEST_CASE("FtnMsgBase opens a *.msg base with no stated type [sdm]") {
    TempSdmBase base;
    AreaConfig area = netmailArea(base.path());
    area.type = MsgBaseType::Unknown;  // a tosser config with no -b option

    FtnMsgBase msgbase;
    REQUIRE(msgbase.open(area).has_value());
    CHECK(msgbase.count() == 1);
}

TEST_CASE("A *.msg UID is the number in the file name [sdm]") {
    TempSdmBase base;
    FtnMsgBase msgbase;
    REQUIRE(msgbase.open(netmailArea(base.path())));

    // The one message in the fixture lives in 198.msg, and that number is its
    // UID — the position in the area is 1.
    CHECK(msgbase.uidOf(1) == 198);
    CHECK(msgbase.indexOfUid(198) == 1);

    CHECK(msgbase.uidOf(0) == 0);
    CHECK(msgbase.uidOf(msgbase.count() + 1) == 0);
    CHECK(msgbase.indexOfUid(0) == 0);
    // A mark from before the base counts as unread, not as the first message.
    CHECK(msgbase.indexOfUid(197) == 0);
}

TEST_CASE("A lone *.msg message is in no thread [sdm]") {
    TempSdmBase base;
    FtnMsgBase msgbase;
    REQUIRE(msgbase.open(netmailArea(base.path())));

    CHECK(msgbase.thread(1).empty());
    CHECK(msgbase.thread(0).empty());
    CHECK(msgbase.thread(msgbase.count() + 1).empty());
}

TEST_CASE("FtnMsgBase writes netmail into a *.msg base and reads it back [sdm]") {
    TempSdmBase base;
    FtnMsgBase msgbase("CP866");
    REQUIRE(msgbase.open(netmailArea(base.path())));

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

    const uint32_t number = amberedit::test::valueOf(msgbase.write(draft));
    REQUIRE(number == before + 1);
    CHECK(msgbase.count() == before + 1);

    // The message landed in the next numbered file after 198.msg.
    CHECK(fs::exists(base.dir() / "199.msg"));

    const auto header = msgbase.header(number);
    CHECK(header.from == "Yegor Gluhov");
    CHECK(header.to == "Иван Петров");
    CHECK(header.subject == "Привет");
    CHECK(header.origAddr.toString() == "192:168/2");
    CHECK(header.destAddr.toString() == "192:168/3.1");
    CHECK(header.isPrivate());
    CHECK(amberedit::domain::isUnsent(header));

    const auto body = msgbase.body(number);
    std::string kludges;
    std::string text;
    for (const auto& line : body.lines) {
        (line.kludge ? kludges : text).append(line.text).append("|");
    }
    CHECK(kludges ==
          "@INTL 192:168/3 192:168/2|@TOPT 1|@MSGID: 192:168/2 68a1b2c3|"
          "@TZUTC: 0300|@CHRS: CP866 2|");
    CHECK(text == "Привет!||--- AmberEdit| * Origin: AmberEdit test (192:168/2)|");
    CHECK(body.charset == "CP866");
}

TEST_CASE("A *.msg written here dates itself where the header has the date [sdm]") {
    TempSdmBase base;
    FtnMsgBase msgbase("CP866");
    REQUIRE(msgbase.open(netmailArea(base.path())));

    amberedit::domain::MessageDraft draft;
    draft.from = "Yegor Gluhov";
    draft.to = "SysOp";
    // Longer than the thirty-six bytes the date was once written over, which is
    // the whole reason the field stands after the subject and not inside it.
    draft.subject = "A subject long enough to run past the thirty-sixth byte";
    draft.origAddr = *amberedit::domain::FtnAddress::parse("192:168/2");
    draft.destAddr = *amberedit::domain::FtnAddress::parse("192:168/1");
    draft.netmail = true;
    draft.charset = "CP866";
    draft.kludges = {"CHRS: CP866 2"};
    draft.lines = {"Hello."};

    const uint32_t number = amberedit::test::valueOf(msgbase.write(draft));
    REQUIRE(number != 0);
    CHECK(msgbase.header(number).subject == draft.subject);

    const std::string raw = storedHeader(base.dir() / "199.msg");
    REQUIRE(raw.size() == kHeaderSize);
    // The subject stands whole in its own field, and nothing else was written
    // into what is left of it.
    CHECK(raw.compare(72, draft.subject.size(), draft.subject) == 0);
    CHECK(raw.find_first_not_of('\0', 72 + draft.subject.size()) == 144);

    const auto stored = amberedit::msgbase::parseFtscDate(raw.substr(144, 20));
    const auto written = msgbase.header(number).date;
    CHECK(stored.isValid());
    CHECK(stored.year == written.year);
    CHECK(stored.month == written.month);
    CHECK(stored.day == written.day);
    CHECK(stored.hour == written.hour);
    CHECK(stored.minute == written.minute);
}

TEST_CASE("A *.msg carrying only the ASCII date is read with it [sdm]") {
    TempSdmBase base;
    // Written before the base is opened: the scan at open() is what finds it.
    writeAsciiDatedMsg(base.dir() / "200.msg");

    FtnMsgBase msgbase;
    REQUIRE(msgbase.open(netmailArea(base.path())));
    REQUIRE(msgbase.count() == 2);

    const auto header = msgbase.header(2);
    CHECK(header.subject == "an old message");
    REQUIRE(header.date.isValid());
    CHECK(header.date.year == 1986);
    CHECK(header.date.month == 1);
    CHECK(header.date.day == 1);
    CHECK(header.date.hour == 2);
    CHECK(header.date.minute == 34);
    CHECK(header.date.second == 56);
}

TEST_CASE("Changing a *.msg keeps its times-read count [sdm]") {
    TempSdmBase base;
    // Read seven times by whatever kept the file before we got to it.
    putWord(base.dir() / "198.msg", 164, 7);

    FtnMsgBase msgbase("CP866");
    REQUIRE(msgbase.open(netmailArea(base.path())));
    const auto was = msgbase.header(1);

    amberedit::domain::MessageDraft draft;
    draft.from = was.from;
    draft.to = was.to;
    draft.subject = "Changed subject";
    draft.origAddr = was.origAddr;
    draft.destAddr = was.destAddr;
    draft.netmail = true;
    draft.charset = "CP866";
    draft.kludges = {"CHRS: CP866 2"};
    draft.lines = {"A shorter message."};
    REQUIRE(msgbase.replace(1, draft).has_value());

    const std::string raw = storedHeader(base.dir() / "198.msg");
    REQUIRE(raw.size() == kHeaderSize);
    CHECK(wordAt(raw, 164) == 7);
    // The stamp it arrived here under is the file's and no rewriting changes it;
    // the one it is written under is the clock.
    CHECK(wordAt(raw, 180) == 0x5d0d);
    CHECK(wordAt(raw, 182) == 0x51ea);
    CHECK(msgbase.header(1).subject == "Changed subject");
}

TEST_CASE("FtnMsgBase deletes a message from a *.msg base [sdm]") {
    TempSdmBase base;
    FtnMsgBase msgbase;
    REQUIRE(msgbase.open(netmailArea(base.path())));

    const uint32_t before = msgbase.count();
    REQUIRE(before > 0);

    REQUIRE(msgbase.remove(1).has_value());
    CHECK(msgbase.count() == before - 1);
    // The delete is the file going away.
    CHECK_FALSE(fs::exists(base.dir() / "198.msg"));

    const auto removed = msgbase.remove(before + 1);
    CHECK_FALSE(removed.has_value());
    CHECK_FALSE(removed.error()->message().empty());
    CHECK_FALSE(msgbase.remove(0).has_value());
}
