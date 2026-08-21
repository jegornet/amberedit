#include <doctest/doctest.h>

#include <unistd.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include "config/text_util.hpp"
#include "msgbase/ftn_msgbase.hpp"
#include "msgbase/jam_crc32.hpp"

using amberedit::config::text::startsWith;
using amberedit::domain::AreaConfig;
using amberedit::domain::AreaKind;
using amberedit::domain::MessageDraft;
using amberedit::domain::MsgBaseType;
using amberedit::msgbase::FtnMsgBase;

namespace fs = std::filesystem;

namespace {

/// A directory of its own per test, gone when the test is.
class TempDir {
public:
    TempDir() {
        dir_ =
            fs::temp_directory_path() / ("amberedit-fmt-" + std::to_string(::getpid()) +
                                         "-" + std::to_string(counter_++));
        fs::create_directories(dir_);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    [[nodiscard]] fs::path path() const { return dir_; }

private:
    fs::path dir_;
    static inline int counter_ = 0;
};

/// An empty JAM base, as a tosser leaves one after creating the area: the
/// 1024-byte info block with the signature, no messages, BaseMsgNum 1.
void createEmptyJamBase(const std::string& path) {
    std::string info(1024, '\0');
    info[0] = 'J';
    info[1] = 'A';
    info[2] = 'M';
    info[12] = '\0';                                 // ActiveMsgs = 0
    for (int i = 16; i < 20; ++i) info[i] = '\xff';  // PasswordCRC = none
    info[20] = 1;                                    // BaseMsgNum = 1
    std::ofstream headers(path + ".jhr", std::ios::binary);
    headers.write(info.data(), static_cast<std::streamsize>(info.size()));
    std::ofstream index(path + ".jdx", std::ios::binary);
    std::ofstream text(path + ".jdt", std::ios::binary);
}

MessageDraft netmailDraft() {
    MessageDraft draft;
    draft.from = "Yegor Gluhov";
    draft.to = "Иван Петров";
    draft.subject = "Привет";
    draft.origAddr = *amberedit::domain::FtnAddress::parse("192:168/2");
    draft.destAddr = *amberedit::domain::FtnAddress::parse("192:168/3.1");
    draft.netmail = true;
    // What the compose screen puts on a netmail: written here, and private as
    // netmail has always been. The base stores what it is given.
    draft.attributes =
        amberedit::domain::attr::kLocal | amberedit::domain::attr::kPrivate;
    draft.charset = "CP866";
    draft.utcOffsetMinutes = 180;
    draft.kludges = {"MSGID: 192:168/2 68a1b2c3", "TZUTC: 0300", "CHRS: CP866 2"};
    draft.lines = {"Привет!", "", "--- AmberEdit",
                   " * Origin: AmberEdit test (192:168/2)"};
    return draft;
}

}  // namespace

TEST_CASE("A JAM base is written and read back [jam]") {
    TempDir dir;
    const std::string path = (dir.path() / "netmail").string();
    createEmptyJamBase(path);
    REQUIRE(FtnMsgBase::probeType(path) == MsgBaseType::Jam);

    AreaConfig area;
    area.tag = "netmail.jam";
    area.path = path;
    area.type = MsgBaseType::Jam;
    area.kind = AreaKind::Netmail;

    FtnMsgBase msgbase("CP866");
    REQUIRE(msgbase.open(area));
    CHECK(msgbase.count() == 0);

    const uint32_t number = msgbase.write(netmailDraft());
    REQUIRE(number == 1);
    CHECK(msgbase.lastError().empty());
    CHECK(msgbase.count() == 1);

    // The header comes back as it went in, converted both ways: what was
    // written in CP866 is read as UTF-8 again.
    const auto header = msgbase.header(1);
    CHECK(header.from == "Yegor Gluhov");
    CHECK(header.to == "Иван Петров");
    CHECK(header.subject == "Привет");
    CHECK(header.origAddr.toString() == "192:168/2");
    CHECK(header.destAddr.toString() == "192:168/3.1");
    CHECK(header.isPrivate());
    CHECK(amberedit::domain::isUnsent(header));
    CHECK(header.date.isValid());

    const auto body = msgbase.body(1);
    CHECK(body.charset == "CP866");
    std::string kludges;
    std::string text;
    for (const auto& line : body.lines) {
        (line.kludge ? kludges : text).append(line.text).append("|");
    }
    // JAM stores INTL/FMPT/TOPT as address subfields and the rest as typed
    // records; they all come back as the control lines the message went out
    // with. The addresses first — that is where the driver rebuilds them.
    CHECK(kludges ==
          "@INTL 192:168/3 192:168/2|@TOPT 1|@MSGID: 192:168/2 68a1b2c3|"
          "@TZUTC: 0300|@CHRS: CP866 2|");
    CHECK(text == "Привет!||--- AmberEdit| * Origin: AmberEdit test (192:168/2)|");
}

TEST_CASE("A JAM message number survives deletions before it [jam]") {
    TempDir dir;
    const std::string path = (dir.path() / "echo").string();
    createEmptyJamBase(path);

    AreaConfig area;
    area.tag = "test.echo";
    area.path = path;
    area.type = MsgBaseType::Jam;
    area.kind = AreaKind::Echo;

    FtnMsgBase msgbase("CP866");
    REQUIRE(msgbase.open(area));

    MessageDraft draft = netmailDraft();
    draft.netmail = false;
    for (const char* subject : {"one", "two", "three"}) {
        draft.subject = subject;
        REQUIRE(msgbase.write(draft) != 0);
    }
    REQUIRE(msgbase.count() == 3);

    // The UID is the JAM message number, which nothing renumbers: deleting the
    // first message moves the positions but not the numbers.
    const uint32_t uidOfThird = msgbase.uidOf(3);
    REQUIRE(msgbase.remove(1));
    CHECK(msgbase.count() == 2);
    CHECK(msgbase.header(2).subject == "three");
    CHECK(msgbase.uidOf(2) == uidOfThird);
    CHECK(msgbase.indexOfUid(uidOfThird) == 2);

    // The lastread conversion: a mark on the deleted message lands on the
    // message before it, not nowhere.
    CHECK(msgbase.indexOfUid(uidOfThird - 1) == 1);
}

TEST_CASE("JAM keeps SEEN-BY and PATH apart and puts them back [jam]") {
    TempDir dir;
    const std::string path = (dir.path() / "echo").string();
    createEmptyJamBase(path);

    AreaConfig area;
    area.tag = "test.echo";
    area.path = path;
    area.type = MsgBaseType::Jam;
    area.kind = AreaKind::Echo;

    FtnMsgBase msgbase("CP866");
    REQUIRE(msgbase.open(area));

    MessageDraft draft = netmailDraft();
    draft.netmail = false;
    // A forwarded or rescanned message carries its routing in the text; JAM
    // stores those lines as subfields and the reader shows them at the end.
    draft.lines = {"Body text", "--- AmberEdit", " * Origin: test (192:168/2)",
                   "SEEN-BY: 168/2 3", "\x01PATH: 168/2"};
    REQUIRE(msgbase.write(draft) == 1);

    const auto body = msgbase.body(1);
    REQUIRE(body.lines.size() >= 2);
    const auto& last = body.lines.back();
    const auto& beforeLast = body.lines[body.lines.size() - 2];
    CHECK(beforeLast.text == "SEEN-BY: 168/2 3");
    CHECK(beforeLast.kludge);
    CHECK(last.text == "@PATH: 168/2");
    CHECK(last.kludge);
    // And they are not in the visible text.
    CHECK(body.text().find("SEEN-BY") == std::string::npos);
}

TEST_CASE("A changed JAM message keeps its number and its place [jam]") {
    TempDir dir;
    const std::string path = (dir.path() / "echo").string();
    createEmptyJamBase(path);

    AreaConfig area;
    area.tag = "test.echo";
    area.path = path;
    area.type = MsgBaseType::Jam;
    area.kind = AreaKind::Echo;

    FtnMsgBase msgbase("CP866");
    REQUIRE(msgbase.open(area));

    MessageDraft draft = netmailDraft();
    draft.netmail = false;
    for (const char* subject : {"one", "two", "three"}) {
        draft.subject = subject;
        REQUIRE(msgbase.write(draft) != 0);
    }
    const uint32_t uid = msgbase.uidOf(2);
    const auto was = msgbase.header(2);

    SUBCASE("the header stays where it is where the subfields still fit") {
        const auto headers = fs::file_size(path + ".jhr");
        MessageDraft change = draft;
        change.subject = "two";  // the same subfields, so the same room
        change.lines = {"A shorter message."};
        REQUIRE(msgbase.replace(2, change));

        CHECK(msgbase.count() == 3);
        CHECK(msgbase.uidOf(2) == uid);
        CHECK(msgbase.body(2).text() == "A shorter message.");
        CHECK(fs::file_size(path + ".jhr") == headers);
    }

    SUBCASE("a subject of another length takes a header at the end of the file") {
        MessageDraft change = draft;
        change.subject = "two, with rather more to say for itself";
        change.lines = {"Changed."};
        REQUIRE(msgbase.replace(2, change));

        CHECK(msgbase.count() == 3);
        CHECK(msgbase.uidOf(2) == uid);
        CHECK(msgbase.header(2).subject == change.subject);
        CHECK(msgbase.header(1).subject == "one");
        CHECK(msgbase.header(3).subject == "three");
        // Dated by the hour it was changed in — it has just been written — and
        // still valid, which the JAM stamp being seconds since the epoch is not
        // where a driver has written a zero over it.
        CHECK(msgbase.header(2).date.isValid());
        CHECK(msgbase.header(2).date.format("%Y-%m-%d") == was.date.format("%Y-%m-%d"));
    }

    // Whichever way round, a base opened afresh reads what was written.
    FtnMsgBase again("CP866");
    REQUIRE(again.open(area));
    CHECK(again.count() == 3);
    CHECK(again.uidOf(2) == uid);
    CHECK(again.header(1).subject == "one");
    CHECK(again.header(3).subject == "three");
}

TEST_CASE("A JAM message is marked read in TimesRead [jam]") {
    TempDir dir;
    const std::string path = (dir.path() / "netmail").string();
    createEmptyJamBase(path);

    AreaConfig area;
    area.tag = "netmail.jam";
    area.path = path;
    area.type = MsgBaseType::Jam;
    area.kind = AreaKind::Netmail;

    FtnMsgBase msgbase("CP866");
    REQUIRE(msgbase.open(area));
    REQUIRE(msgbase.write(netmailDraft()) == 1);
    REQUIRE(msgbase.write(netmailDraft()) == 2);

    // A message JAM has just been given has been read no times at all.
    CHECK_FALSE(msgbase.header(1).seen);

    REQUIRE(msgbase.markSeen(1));
    CHECK(msgbase.lastError().empty());
    CHECK(msgbase.header(1).seen);
    CHECK_FALSE(msgbase.header(2).seen);

    // TimesRead is a mark here and not a tally: reading a message twice writes
    // once, and a base that already counts several reads is left as it is.
    REQUIRE(msgbase.markSeen(1));
    CHECK(msgbase.header(1).seen);

    FtnMsgBase again("CP866");
    REQUIRE(again.open(area));
    CHECK(again.header(1).seen);
    CHECK_FALSE(again.header(2).seen);

    CHECK_FALSE(msgbase.markSeen(0));
    CHECK_FALSE(msgbase.markSeen(3));
}

TEST_CASE("A changed JAM message is still marked read [jam]") {
    TempDir dir;
    const std::string path = (dir.path() / "netmail").string();
    createEmptyJamBase(path);

    AreaConfig area;
    area.tag = "netmail.jam";
    area.path = path;
    area.type = MsgBaseType::Jam;
    area.kind = AreaKind::Netmail;

    FtnMsgBase msgbase("CP866");
    REQUIRE(msgbase.open(area));
    REQUIRE(msgbase.write(netmailDraft()) == 1);
    REQUIRE(msgbase.markSeen(1));

    MessageDraft draft = netmailDraft();
    draft.subject = "Rewritten";
    REQUIRE(msgbase.replace(1, draft));

    CHECK(msgbase.header(1).subject == "Rewritten");
    CHECK(msgbase.header(1).seen);
}

TEST_CASE("A Fido *.msg message is marked read in times_read [sdm]") {
    TempDir dir;
    AreaConfig area;
    area.tag = "netmail";
    area.path = dir.path().string();
    area.type = MsgBaseType::Sdm;
    area.kind = AreaKind::Netmail;
    area.address = *amberedit::domain::FtnAddress::parse("192:168/2");

    FtnMsgBase msgbase("CP866");
    REQUIRE(msgbase.open(area));
    REQUIRE(msgbase.write(netmailDraft()) == 1);
    REQUIRE(msgbase.write(netmailDraft()) == 2);

    // The *.msg attribute word is sixteen bits wide, so MSGSEEN has nowhere to
    // go in it and the read count is what says this instead.
    CHECK_FALSE(msgbase.header(1).seen);

    REQUIRE(msgbase.markSeen(1));
    CHECK(msgbase.header(1).seen);
    CHECK_FALSE(msgbase.header(2).seen);
    REQUIRE(msgbase.markSeen(1));
    CHECK(msgbase.header(1).seen);

    FtnMsgBase again("CP866");
    REQUIRE(again.open(area));
    CHECK(again.header(1).seen);
    CHECK_FALSE(again.header(2).seen);

    // And it survives the message being rewritten, replace() carrying the word
    // over from what it read.
    MessageDraft draft = netmailDraft();
    draft.subject = "Rewritten";
    REQUIRE(msgbase.replace(1, draft));
    CHECK(msgbase.header(1).subject == "Rewritten");
    CHECK(msgbase.header(1).seen);

    CHECK_FALSE(msgbase.markSeen(0));
    CHECK_FALSE(msgbase.markSeen(3));
}

TEST_CASE("A Fido *.msg base is written and read back [sdm]") {
    TempDir dir;
    AreaConfig area;
    area.tag = "netmail";
    area.path = dir.path().string();
    area.type = MsgBaseType::Sdm;
    area.kind = AreaKind::Netmail;
    // The area's AKA is what zones are read against.
    area.address = *amberedit::domain::FtnAddress::parse("192:168/2");

    FtnMsgBase msgbase("CP866");
    REQUIRE(msgbase.open(area));
    CHECK(msgbase.count() == 0);

    MessageDraft draft = netmailDraft();
    // Another zone, and no INTL/TOPT among the kludges: the driver must add
    // them, the two-byte header fields having no room for a zone or a point.
    draft.destAddr = *amberedit::domain::FtnAddress::parse("5:5/5.1");
    draft.kludges = {"MSGID: 192:168/2 68a1b2c3", "CHRS: CP866 2"};
    const uint32_t number = msgbase.write(draft);
    REQUIRE(number == 1);
    CHECK(fs::exists(dir.path() / "1.msg"));

    const auto header = msgbase.header(1);
    CHECK(header.from == "Yegor Gluhov");
    CHECK(header.to == "Иван Петров");
    CHECK(header.subject == "Привет");
    // The zones and the point come back out of the kludges the driver wrote.
    CHECK(header.origAddr.toString() == "192:168/2");
    CHECK(header.destAddr.toString() == "5:5/5.1");
    CHECK(header.date.isValid());

    const auto body = msgbase.body(1);
    CHECK(body.charset == "CP866");
    std::string kludges;
    for (const auto& line : body.lines) {
        if (line.kludge) kludges.append(line.text).append("|");
    }
    CHECK(startsWith(kludges, "@INTL 5:5/5 192:168/2|@TOPT 1|"));
    CHECK(kludges.find("@MSGID: 192:168/2 68a1b2c3|") != std::string::npos);
    CHECK(body.text().find("Привет!") != std::string::npos);

    REQUIRE(msgbase.remove(1));
    CHECK(msgbase.count() == 0);
    CHECK_FALSE(fs::exists(dir.path() / "1.msg"));
}

TEST_CASE("A changed *.msg message is rewritten in its own file [sdm]") {
    TempDir dir;
    AreaConfig area;
    area.tag = "netmail";
    area.path = dir.path().string();
    area.type = MsgBaseType::Sdm;
    area.kind = AreaKind::Netmail;
    area.address = *amberedit::domain::FtnAddress::parse("192:168/2");

    FtnMsgBase msgbase("CP866");
    REQUIRE(msgbase.open(area));
    REQUIRE(msgbase.write(netmailDraft()) == 1);
    REQUIRE(msgbase.write(netmailDraft()) == 2);
    const auto was = msgbase.header(1);

    MessageDraft change = netmailDraft();
    change.subject = "Changed";
    change.lines = {"One line."};
    REQUIRE(msgbase.replace(1, change));
    CHECK(msgbase.lastError().empty());

    // Its own file and no other: the number is the file name, so a message that
    // has grown or shrunk still answers to it.
    CHECK(msgbase.count() == 2);
    CHECK(msgbase.uidOf(1) == 1);
    CHECK(fs::exists(dir.path() / "1.msg"));
    CHECK(fs::exists(dir.path() / "2.msg"));
    CHECK(msgbase.header(1).subject == "Changed");
    CHECK(msgbase.body(1).text() == "One line.");
    // Dated by the hour it was changed in, in both the packed stamp and the
    // ASCII date field FTS-0001 keeps beside it — the reader falls back to the
    // second where the first is empty, so a stamp lost either way would show.
    CHECK(msgbase.header(1).date.isValid());
    CHECK(msgbase.header(1).date.format("%Y-%m-%d") == was.date.format("%Y-%m-%d"));
    // What the message shrank by is given back rather than left behind it: the
    // file is cut to what the message now takes.
    CHECK(fs::file_size(dir.path() / "1.msg") < fs::file_size(dir.path() / "2.msg"));

    FtnMsgBase again("CP866");
    REQUIRE(again.open(area));
    CHECK(again.header(1).subject == "Changed");
    CHECK(again.body(1).text() == "One line.");
}

TEST_CASE("An echo *.msg area starts at 2.msg [sdm]") {
    // 1.msg in an echo area is the high-water mark, not a message; the first
    // message written must not take its number.
    TempDir dir;
    AreaConfig area;
    area.tag = "test.echo";
    area.path = dir.path().string();
    area.type = MsgBaseType::Sdm;
    area.kind = AreaKind::Echo;

    FtnMsgBase msgbase("CP866");
    REQUIRE(msgbase.open(area));

    MessageDraft draft = netmailDraft();
    draft.netmail = false;
    REQUIRE(msgbase.write(draft) == 1);
    CHECK_FALSE(fs::exists(dir.path() / "1.msg"));
    CHECK(fs::exists(dir.path() / "2.msg"));
    CHECK(msgbase.uidOf(1) == 2);
}

TEST_CASE("The JAM index record keys by the recipient's name [jam]") {
    // The CRC in the .jdx is what other JAM software finds messages by; a
    // wrong one is invisible corruption until somebody else's reader searches.
    TempDir dir;
    const std::string path = (dir.path() / "crc").string();
    createEmptyJamBase(path);

    AreaConfig area;
    area.tag = "crc.jam";
    area.path = path;
    area.type = MsgBaseType::Jam;
    area.kind = AreaKind::Echo;

    FtnMsgBase msgbase("CP866");
    REQUIRE(msgbase.open(area));
    MessageDraft draft = netmailDraft();
    draft.netmail = false;
    draft.to = "All";
    REQUIRE(msgbase.write(draft) == 1);

    std::ifstream jdx(path + ".jdx", std::ios::binary);
    unsigned char record[8] = {0};
    jdx.read(reinterpret_cast<char*>(record), sizeof(record));
    REQUIRE(jdx.gcount() == 8);
    const uint32_t crc =
        static_cast<uint32_t>(record[0]) | static_cast<uint32_t>(record[1]) << 8 |
        static_cast<uint32_t>(record[2]) << 16 | static_cast<uint32_t>(record[3]) << 24;
    CHECK(crc == amberedit::msgbase::jamCrc32("All"));
    CHECK(crc == 0xc4e78e22u);  // the value every JAM implementation agrees on
}
