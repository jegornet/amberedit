#include <doctest/doctest.h>

#include <string>

#include "domain/message.hpp"
#include "msgbase/ftn_msgbase.hpp"
#include "msgbase/raw_message.hpp"
#include "temp_dir.hpp"
#include "test_strings.hpp"

using amberedit::domain::AreaConfig;
using amberedit::domain::AreaKind;
using amberedit::domain::MessageDraft;
using amberedit::domain::MsgBaseType;
using amberedit::msgbase::completeEchoSender;
using amberedit::msgbase::FtnMsgBase;
using amberedit::msgbase::RawHeader;
using amberedit::msgbase::senderFromMsgid;
using amberedit::msgbase::senderFromOrigin;
using amberedit::test::TempDir;
using amberedit::test::valueOf;

namespace {

/// The address a tail of message text signs itself with, as a string, so that
/// a check reads as the line does.
std::string origin(const std::string& tail) {
    return senderFromOrigin(tail).toString();
}

std::string msgid(const std::string& control) {
    return senderFromMsgid(control).toString();
}

/// An echo area of `type` at `path`, made and left empty.
AreaConfig echoAreaAt(const std::string& path, MsgBaseType type) {
    AreaConfig area;
    area.tag = "test.echo";
    area.path = path;
    area.type = type;
    area.kind = AreaKind::Echo;
    return area;
}

/// One echomail message written with no sender in its header — which is what
/// a tosser leaves in a JAM echo and is free to leave in the other two — and
/// the header read back out of the base afterwards.
///
/// Both halves of the read are checked: `header()` stops at the stored header
/// and has to go looking in the text for the origin line, `body()` has the
/// whole text already. They must answer the same, or a message list and the
/// reader would show different addresses for one message.
void checkSender(MsgBaseType type, const std::vector<std::string>& kludges,
                 const std::vector<std::string>& lines, const std::string& expected) {
    TempDir dir;
    const std::string path = dir.path("echo");
    const AreaConfig area = echoAreaAt(path, type);

    FtnMsgBase created("CP866");
    REQUIRE(created.create(area).has_value());

    FtnMsgBase msgbase("CP866");
    REQUIRE(msgbase.open(area).has_value());

    MessageDraft draft;
    draft.from = "Yegor Gluhov";
    draft.to = "All";
    draft.subject = "Test";
    draft.charset = "CP866";
    draft.kludges = kludges;
    draft.lines = lines;
    REQUIRE(valueOf(msgbase.write(draft)) == 1);

    CHECK(msgbase.header(1).origAddr.toString() == expected);

    FtnMsgBase again("CP866");
    REQUIRE(again.open(area).has_value());
    CHECK(again.header(1).origAddr.toString() == expected);
}

const std::vector<std::string> kSignedLines = {"Hello All!", "", "--- AmberEdit",
                                               " * Origin: to err is human (2:382/736)"};

}  // namespace

TEST_CASE("The sender of an echomail message is read off its origin line [msgbase]") {
    CHECK(origin(" * Origin: to err is human (2:382/736)\r") == "2:382/736");

    // A point, and a domain the line may be written with: what a base holds is
    // 4D, so the domain is dropped rather than carried into the header.
    CHECK(origin(" * Origin: Somewhere (2:382/736.12@fidonet)\r") == "2:382/736.12");

    // The brackets are not the address alone in a great many origin lines.
    CHECK(origin(" * Origin: Somewhere (мой узел 2:6000/9999)\r") == "2:6000/9999");
    CHECK(origin(" * Origin: The (best) BBS (2:6000/9999)\r") == "2:6000/9999");
    CHECK(origin(" * Origin: Node (2:6000/9999 UTC+3)\r") == "2:6000/9999");

    // The line stands under the text and above the routing, and is looked for
    // from the end of the message backwards.
    CHECK(origin("Hello!\r--- tosser\r * Origin: BBS (2:382/736)\r"
                 "SEEN-BY: 382/736 6000/9999\r\x01PATH: 382/736\r") == "2:382/736");
    CHECK(origin(" * Origin: BBS (2:382/736)\r\x01Via 2:382/736 @20250915\r") ==
          "2:382/736");
    CHECK(origin(" * Origin: BBS (2:382/736)\r\r  \r") == "2:382/736");
}

TEST_CASE("An origin line that names no address answers nothing [msgbase]") {
    CHECK(origin(" * Origin: a line with no brackets at all\r") == "0:0/0");
    CHECK(origin(" * Origin: BBS (somewhere warm)\r") == "0:0/0");
    CHECK(origin("") == "0:0/0");

    // A message may carry no origin line — netmail rescanned into an echo,
    // anything written by hand — and the line above one is not an origin line
    // for standing where one would.
    CHECK(origin("Hello All!\r--- AmberEdit\r") == "0:0/0");
    CHECK(origin(" * Origin: BBS (2:382/736)\rAnd a line after it\r") == "0:0/0");
}

TEST_CASE("The sender falls back on the MSGID [msgbase]") {
    CHECK(msgid("\x01MSGID: 2:382/736 5f1a2b3c\r") == "2:382/736");
    CHECK(msgid("\x01MSGID: 2:382/736.12@fidonet 5f1a2b3c\r") == "2:382/736.12");

    // Nothing obliges a MSGID to carry an address, and plenty do not.
    CHECK(msgid("\x01MSGID: 5f1a2b3c\r") == "0:0/0");
    CHECK(msgid("\x01MSGID: <abc@example.org>\r") == "0:0/0");
    CHECK(msgid("\x01TZUTC: 0300\r") == "0:0/0");
}

TEST_CASE("The origin line is asked before the MSGID [msgbase]") {
    RawHeader header;
    completeEchoSender(header, "\x01MSGID: 2:6000/9999 5f1a2b3c\r",
                       " * Origin: BBS (2:382/736)\r");
    CHECK(header.origAddr.toString() == "2:382/736");
}

TEST_CASE("What the base itself names is not overruled [msgbase]") {
    RawHeader header;
    header.origAddr = *amberedit::domain::FtnAddress::parse("2:6000/9999");
    completeEchoSender(header, "\x01MSGID: 2:382/736 5f1a2b3c\r",
                       " * Origin: BBS (2:382/736)\r");
    CHECK(header.origAddr.toString() == "2:6000/9999");
}

TEST_CASE("An echo message with no address in the base is read off its text [jam]") {
    checkSender(MsgBaseType::Jam, {"MSGID: 2:6000/9999 5f1a2b3c"}, kSignedLines,
                "2:382/736");
    checkSender(MsgBaseType::Jam, {"MSGID: 2:6000/9999 5f1a2b3c"}, {"Hello All!"},
                "2:6000/9999");
}

TEST_CASE("An echo message with no address in the base is read off its text [squish]") {
    checkSender(MsgBaseType::Squish, {"MSGID: 2:6000/9999 5f1a2b3c"}, kSignedLines,
                "2:382/736");
    checkSender(MsgBaseType::Squish, {"MSGID: 2:6000/9999 5f1a2b3c"}, {"Hello All!"},
                "2:6000/9999");
}

TEST_CASE("An echo message with no address in the base is read off its text [sdm]") {
    checkSender(MsgBaseType::Sdm, {"MSGID: 2:6000/9999 5f1a2b3c"}, kSignedLines,
                "2:382/736");
    checkSender(MsgBaseType::Sdm, {"MSGID: 2:6000/9999 5f1a2b3c"}, {"Hello All!"},
                "2:6000/9999");
}

TEST_CASE("Netmail is not read off an origin line [jam]") {
    TempDir dir;
    const std::string path = dir.path("netmail");
    AreaConfig area = echoAreaAt(path, MsgBaseType::Jam);
    area.kind = AreaKind::Netmail;

    FtnMsgBase created("CP866");
    REQUIRE(created.create(area).has_value());
    FtnMsgBase msgbase("CP866");
    REQUIRE(msgbase.open(area).has_value());

    MessageDraft draft;
    draft.from = "Yegor Gluhov";
    draft.to = "Sysop";
    draft.subject = "Test";
    draft.netmail = true;
    draft.charset = "CP866";
    draft.kludges = {"MSGID: 2:6000/9999 5f1a2b3c"};
    draft.lines = kSignedLines;
    REQUIRE(valueOf(msgbase.write(draft)) == 1);

    // A letter to a node says who wrote it in its header and its INTL, and the
    // origin line a netmail area's messages may carry is somebody else's: a
    // rescanned echomail message, a receipt quoting one.
    CHECK(msgbase.header(1).origAddr.toString() == "0:0/0");
}
