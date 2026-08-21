#include <catch2/catch.hpp>

#include <cstdint>

#include "domain/message.hpp"

using amberedit::domain::isOriginLine;
using amberedit::domain::isTearline;
using amberedit::domain::MessageBody;
using amberedit::domain::MessageDate;
using amberedit::domain::messageAttributes;
using amberedit::domain::MessageHeader;

namespace {

/// The XMSG attribute bits, by the names FTS-0001 gives them.
constexpr uint32_t kPrivate = 0x0001;
constexpr uint32_t kCrash = 0x0002;
constexpr uint32_t kRead = 0x0004;
constexpr uint32_t kSent = 0x0008;
constexpr uint32_t kLocal = 0x0100;

std::vector<std::string> attributesOf(uint32_t attributes) {
    MessageHeader header;
    header.attributes = attributes;
    return messageAttributes(header);
}

}  // namespace

TEST_CASE("messageAttributes shows nothing for a message with no attributes",
          "[message]") {
    // Not an empty pair of brackets: a message that carries no attributes has
    // nothing to say about itself.
    CHECK(attributesOf(0).empty());
}

TEST_CASE("messageAttributes names the attributes that are set", "[message]") {
    CHECK(attributesOf(kRead | kPrivate) == std::vector<std::string>{"Rcv", "Pvt"});
    CHECK(attributesOf(kCrash) == std::vector<std::string>{"Cra"});
}

TEST_CASE("isUnsent is local without sent", "[message]") {
    const auto unsent = [](uint32_t attributes) {
        MessageHeader header;
        header.attributes = attributes;
        return amberedit::domain::isUnsent(header);
    };
    CHECK(unsent(kLocal));
    CHECK(unsent(kLocal | kPrivate));
    CHECK_FALSE(unsent(kLocal | kSent));
    // A message that merely arrived is not the user's to send.
    CHECK_FALSE(unsent(kRead));
    CHECK_FALSE(unsent(0));
}

TEST_CASE("messageAttributes calls an unsent local message unsent", "[message]") {
    // The base has no bit for it — it is the absence of one — but it is the
    // first thing worth knowing about a message of one's own.
    CHECK(attributesOf(kLocal) == std::vector<std::string>{"Uns", "Loc"});
    CHECK(attributesOf(kLocal | kSent) == std::vector<std::string>{"Snt", "Loc"});
    // Unsent is about a message written here. One that merely arrived unsent
    // is not the user's to send.
    CHECK(attributesOf(kRead) == std::vector<std::string>{"Rcv"});
}

TEST_CASE("MessageDate writes itself out as a strftime format asks", "[message]") {
    const MessageDate date{2026, 8, 10, 21, 19, 36};

    CHECK(date.format("%d %b %y %H:%M") == "10 Aug 26 21:19");
    CHECK(date.format("%Y-%m-%d %H:%M:%S") == "2026-08-10 21:19:36");
    CHECK(date.format("written") == "written");

    // The weekday and the day of the year are worked out from the date itself:
    // nothing in an FTN stamp carries either, and mktime() would answer in a
    // time zone the stamp was never in. 10 August 2026 is a Monday, and the
    // 222nd day of the year.
    CHECK(date.format("%a") == "Mon");
    CHECK(date.format("%j") == "222");
    // Before the epoch, where the arithmetic could as easily go negative.
    CHECK(MessageDate{1969, 7, 20, 20, 17, 40}.format("%a %j") == "Sun 201");

    // A stamp that is no date has nothing to write out: a message carrying
    // none shows a blank rather than a row of zeroes.
    CHECK(MessageDate{}.format("%d %b %y").empty());

    // A base can hold anything, and a field out of range must not index past
    // strftime's own tables. It is clamped rather than refused: a month shown
    // wrong is a better answer than a crash.
    MessageDate broken = date;
    broken.month = 13;
    broken.hour = 61;
    CHECK(broken.format("%m %H") == "12 23");
}

TEST_CASE("MessageDate writes the zone it is given for %z", "[message]") {
    const MessageDate date{2026, 8, 10, 21, 19, 36};

    // The offset is the message's own — its TZUTC's — rather than anything
    // strftime could work out: struct tm holds no zone for an FTN stamp, and
    // glibc would answer "+0000" and have every message written in UTC.
    CHECK(date.format("%d %b %y %H:%M %z", "+0300") == "10 Aug 26 21:19 +0300");
    CHECK(date.format("%z", "-0330") == "-0330");

    // A message stating no zone writes none, rather than a zone that is not
    // its own — which is also what the arrival stamp gets, the message having
    // nothing to say about this system's clock. The space the missing offset
    // was to stand behind goes with it: the stamp is trimmed.
    CHECK(date.format("%d %b %y %z") == "10 Aug 26");
    CHECK(date.format("%z").empty());

    // %%z is a literal '%' and a 'z'. It was never a specifier and is not one
    // now.
    CHECK(date.format("%%z", "+0300") == "%z");
    // A '%' in the zone is written, not handed on to strftime as the start of
    // a specifier.
    CHECK(date.format("%z", "%d") == "%d");
    // A '%' at the very end has nothing after it to make a specifier of.
    CHECK(date.format("%H%", "+0300") == "21%");
}

TEST_CASE("MessageDate trims the stamp it writes", "[message]") {
    const MessageDate date{2026, 8, 10, 21, 19, 36};

    // Both ends, and whatever put the blank there — a specifier that wrote
    // nothing, or a format written with a space of its own. A column measured
    // off a stamp ending in a blank would be a column wider than its contents.
    CHECK(date.format("  %H:%M  ") == "21:19");
    CHECK(date.format("%z %H:%M %z") == "21:19");
    CHECK(date.format("\t%d %b %y\t") == "10 Aug 26");
    // Inside the stamp nothing is touched: the spaces there are the format's.
    CHECK(date.format("%d %b %y  %H:%M") == "10 Aug 26  21:19");
    // A format that writes nothing but blank writes nothing at all.
    CHECK(date.format("   ").empty());
}

TEST_CASE("isTearline recognises the FTS-0004 tearline", "[message]") {
    CHECK(isTearline("---"));
    CHECK(isTearline("--- GoldED+/LNX 1.1.5-b20250409"));
    CHECK(isTearline("--- "));
}

TEST_CASE("isTearline rejects lines that merely start with hyphens", "[message]") {
    CHECK_FALSE(isTearline("----"));
    CHECK_FALSE(isTearline("--"));
    CHECK_FALSE(isTearline("---text"));
    CHECK_FALSE(isTearline(" ---"));  // the tearline starts at column one
    CHECK_FALSE(isTearline(""));
}

TEST_CASE("isOriginLine accepts whatever stands in the parentheses", "[message]") {
    // The address may be plain 4D, carry a domain, or name the network. None of
    // that is parsed, so all of it must be accepted.
    CHECK(isOriginLine(" * Origin: Somewhere (2:382/736)"));
    CHECK(isOriginLine(" * Origin: Somewhere (2:382/736@fidonet)"));
    CHECK(isOriginLine(" * Origin: Somewhere (Fidonet 2:382/736)"));
    CHECK(isOriginLine(" * Origin: Jegornet (192:168/1)"));
    CHECK(isOriginLine(" * Origin:"));
}

TEST_CASE("isOriginLine rejects near misses", "[message]") {
    CHECK_FALSE(isOriginLine("* Origin: no leading space (2:1/1)"));
    CHECK_FALSE(isOriginLine("  * Origin: two leading spaces (2:1/1)"));
    CHECK_FALSE(isOriginLine(" * origin: lowercase (2:1/1)"));
    CHECK_FALSE(isOriginLine("Origin: bare (2:1/1)"));
    CHECK_FALSE(isOriginLine(""));
}

namespace {

std::vector<bool> trailerFlags(std::vector<amberedit::domain::MessageLine> lines) {
    amberedit::domain::markTrailer(lines);
    std::vector<bool> flags;
    flags.reserve(lines.size());
    for (const auto& line : lines) flags.push_back(line.trailer);
    return flags;
}

}  // namespace

TEST_CASE("markTrailer flags the closing tearline and origin", "[message]") {
    CHECK(
        trailerFlags({{"Hello All!"}, {""}, {"--- GoldED+"}, {" * Origin: x (2:1/1)"}}) ==
        std::vector<bool>{false, false, true, true});
}

TEST_CASE("markTrailer steps over the kludges that follow the origin", "[message]") {
    // SEEN-BY and PATH are stored after the origin, so the walk back has to
    // ignore them or it would never reach it.
    CHECK(trailerFlags({{"Hello All!"},
                        {"---"},
                        {" * Origin: x (2:1/1)"},
                        {"SEEN-BY: 1/1", true},
                        {"@PATH: 1/1", true}}) ==
          std::vector<bool>{false, true, true, false, false});
}

TEST_CASE("markTrailer accepts a tearline with no origin", "[message]") {
    // Netmail and local areas routinely carry one without the other.
    CHECK(trailerFlags({{"Hello All!"}, {"--- GoldED+"}}) ==
          std::vector<bool>{false, true});
}

TEST_CASE("markTrailer leaves a mid-message separator alone", "[message]") {
    // This is the whole point of walking from the end: authors use '---' as a
    // separator, and only the one closing the message is a tearline.
    CHECK(trailerFlags({{"Hello All!"},
                        {"---"},
                        {"still the message body"},
                        {"--- GoldED+"},
                        {" * Origin: x (2:1/1)"}}) ==
          std::vector<bool>{false, false, false, true, true});

    CHECK(trailerFlags({{"---"}, {"body follows the separator"}}) ==
          std::vector<bool>{false, false});
}

TEST_CASE("markTrailer flags nothing when a message has no trailer", "[message]") {
    CHECK(trailerFlags({{"Hello All!"}, {"Yegor"}}) == std::vector<bool>{false, false});
    CHECK(trailerFlags({}).empty());
}

TEST_CASE("MessageBody keeps text and kludges apart but in order", "[message]") {
    MessageBody body;
    body.lines = {
        {"@MSGID: 2:1/1 abcd1234", true},
        {"Hello All!", false},
        {"", false},
        {"--- Ed", false},
        {" * Origin: x (2:1/1)", false},
        {"SEEN-BY: 1/1", true},
        {"@PATH: 1/1", true},
    };

    CHECK(body.text() == "Hello All!\n\n--- Ed\n * Origin: x (2:1/1)");
    CHECK(body.kludges() == std::vector<std::string>{"@MSGID: 2:1/1 abcd1234",
                                                     "SEEN-BY: 1/1", "@PATH: 1/1"});
}
