#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "domain/message.hpp"
#include "msgbase/ftn_msgbase.hpp"
#include "temp_dir.hpp"
#include "test_strings.hpp"

using amberedit::domain::AreaConfig;
using amberedit::domain::MessageDraft;
using amberedit::domain::MsgBaseType;
using amberedit::msgbase::FtnMsgBase;
using amberedit::test::TempDir;
using amberedit::test::valueOf;

namespace {

AreaConfig areaAt(const std::string& path, MsgBaseType type) {
    AreaConfig area;
    area.tag = "write.area";
    area.path = path;
    area.type = type;
    return area;
}

/// A message that says which one it is, which is most of what these tests read
/// back: what a set written as one call comes to is a question about order and
/// about what the base says of itself afterwards.
MessageDraft numbered(uint32_t number) {
    MessageDraft draft;
    draft.from = "Yegor Gluhov";
    draft.to = "All";
    draft.subject = "message " + std::to_string(number);
    draft.origAddr = *amberedit::domain::FtnAddress::parse("2:382/736");
    draft.destAddr = *amberedit::domain::FtnAddress::parse("2:6000/9999");
    draft.charset = "CP866";
    draft.kludges = {"CHRS: CP866 2"};
    draft.lines = {"Body of message " + std::to_string(number)};
    return draft;
}

std::vector<MessageDraft> numberedFrom(uint32_t first, uint32_t count) {
    std::vector<MessageDraft> drafts;
    for (uint32_t i = 0; i < count; ++i) drafts.push_back(numbered(first + i));
    return drafts;
}

/// An empty base of the format, opened.
void makeArea(FtnMsgBase& msgbase, const AreaConfig& area) {
    REQUIRE(FtnMsgBase("CP866").create(area).has_value());
    REQUIRE(msgbase.open(area).has_value());
}

std::vector<std::string> subjectsOf(const FtnMsgBase& msgbase) {
    std::vector<std::string> subjects;
    for (uint32_t number = 1; number <= msgbase.count(); ++number) {
        subjects.push_back(msgbase.header(number).subject);
    }
    return subjects;
}

std::vector<uint32_t> uidsOf(const FtnMsgBase& msgbase) {
    std::vector<uint32_t> uids;
    for (uint32_t number = 1; number <= msgbase.count(); ++number) {
        uids.push_back(msgbase.uidOf(number));
    }
    return uids;
}

/// Every format alike: a set goes in as one call, in the order it was given and
/// after whatever was already there, and the base reads back the same way from
/// disk — which is what says the counters were settled for the whole set and not
/// only for the last message of it.
void checkASetGoesIn(MsgBaseType type) {
    TempDir dir;
    const AreaConfig area = areaAt(dir.path("area"), type);
    FtnMsgBase msgbase("CP866");
    makeArea(msgbase, area);

    // One first, so that the set is appended to an area that already holds
    // something rather than written into an empty one.
    REQUIRE(valueOf(msgbase.write(numbered(1))) == 1);

    const auto report = msgbase.writeAll(numberedFrom(2, 5));
    CHECK(report.written == 5);
    CHECK(report.failed == nullptr);
    CHECK(msgbase.count() == 6);

    const std::vector<std::string> all = {"message 1", "message 2", "message 3",
                                          "message 4", "message 5", "message 6"};
    CHECK(subjectsOf(msgbase) == all);
    const std::vector<uint32_t> uids = uidsOf(msgbase);

    // Every message has an identity of its own, and no two of them share one:
    // a set written under one lock takes its UIDs from the same counter one at
    // a time, and a counter settled once for the set would hand out the same
    // number six times.
    for (const uint32_t uid : uids) CHECK(uid != 0);
    for (size_t i = 1; i < uids.size(); ++i) CHECK(uids[i] != uids[i - 1]);

    msgbase.close();
    FtnMsgBase again("CP866");
    REQUIRE(again.open(area).has_value());
    CHECK(again.count() == 6);
    CHECK(subjectsOf(again) == all);
    CHECK(uidsOf(again) == uids);
    CHECK(again.body(6).text().find("Body of message 6") != std::string::npos);
    // And the area takes another message after it, which is what says the base
    // was left in a state it can be written from rather than merely read.
    REQUIRE(valueOf(again.write(numbered(7))) == 7);
    CHECK(again.header(7).subject == "message 7");
}

/// A set of one is the same as `write()`, and a set of none is nothing to do
/// rather than an answer of its own.
void checkTheEdgesOfASet(MsgBaseType type) {
    TempDir dir;
    const AreaConfig area = areaAt(dir.path("area"), type);
    FtnMsgBase msgbase("CP866");
    makeArea(msgbase, area);

    const auto nothing = msgbase.writeAll({});
    CHECK(nothing.written == 0);
    CHECK(nothing.failed == nullptr);
    CHECK(msgbase.count() == 0);

    const auto one = msgbase.writeAll(numberedFrom(1, 1));
    CHECK(one.written == 1);
    CHECK(msgbase.count() == 1);
    CHECK(msgbase.header(1).subject == "message 1");
}

/// Written the way `write()` writes: the same message, whichever call put it
/// there. The two share `encodeForWriting()` precisely so they cannot drift.
void checkASetIsWrittenLikeOneMessage(MsgBaseType type) {
    TempDir dir;
    const AreaConfig oneAtATime = areaAt(dir.path("one"), type);
    const AreaConfig asASet = areaAt(dir.path("set"), type);

    FtnMsgBase first("CP866");
    makeArea(first, oneAtATime);
    for (uint32_t i = 1; i <= 4; ++i) REQUIRE(first.write(numbered(i)).has_value());

    FtnMsgBase second("CP866");
    makeArea(second, asASet);
    REQUIRE(second.writeAll(numberedFrom(1, 4)).written == 4);

    REQUIRE(first.count() == second.count());
    for (uint32_t number = 1; number <= first.count(); ++number) {
        const auto here = first.header(number);
        const auto there = second.header(number);
        CHECK(here.subject == there.subject);
        CHECK(here.from == there.from);
        CHECK(here.to == there.to);
        CHECK(here.origAddr.toString() == there.origAddr.toString());
        CHECK(here.destAddr.toString() == there.destAddr.toString());
        CHECK(here.attributes == there.attributes);
        CHECK(first.uidOf(number) == second.uidOf(number));
        CHECK(first.body(number).text() == second.body(number).text());
    }
}

/// A base with no area open refuses the set whole and says why, rather than
/// answering with a count of nothing and leaving the caller to guess.
void checkAClosedBaseRefuses(MsgBaseType type) {
    TempDir dir;
    const AreaConfig area = areaAt(dir.path("area"), type);
    FtnMsgBase msgbase("CP866");
    makeArea(msgbase, area);
    REQUIRE(msgbase.writeAll(numberedFrom(1, 2)).written == 2);
    msgbase.close();

    const auto report = msgbase.writeAll(numberedFrom(3, 2));
    CHECK(report.written == 0);
    REQUIRE(report.failed != nullptr);
    CHECK_FALSE(report.failed->message().empty());

    // And nothing reached the disk: the area still holds the two that did.
    FtnMsgBase again("CP866");
    REQUIRE(again.open(area).has_value());
    CHECK(again.count() == 2);
}

}  // namespace

TEST_CASE("A set of messages goes into a Squish base as one call [write][squish]") {
    checkASetGoesIn(MsgBaseType::Squish);
}

TEST_CASE("A set of messages goes into a JAM base as one call [write][jam]") {
    checkASetGoesIn(MsgBaseType::Jam);
}

TEST_CASE("A set of messages goes into a Fido *.msg base as one call [write][opus]") {
    checkASetGoesIn(MsgBaseType::Opus);
}

TEST_CASE("A set of one and a set of none [write]") {
    checkTheEdgesOfASet(MsgBaseType::Squish);
    checkTheEdgesOfASet(MsgBaseType::Jam);
    checkTheEdgesOfASet(MsgBaseType::Opus);
}

TEST_CASE("A set is written exactly as one message at a time would be [write]") {
    checkASetIsWrittenLikeOneMessage(MsgBaseType::Squish);
    checkASetIsWrittenLikeOneMessage(MsgBaseType::Jam);
    checkASetIsWrittenLikeOneMessage(MsgBaseType::Opus);
}

TEST_CASE("A base with no area open takes none of the set [write]") {
    checkAClosedBaseRefuses(MsgBaseType::Squish);
    checkAClosedBaseRefuses(MsgBaseType::Jam);
    checkAClosedBaseRefuses(MsgBaseType::Opus);
}

TEST_CASE("Squish reuses the frames a delete freed for a set [write][squish]") {
    TempDir dir;
    const AreaConfig area = areaAt(dir.path("area"), MsgBaseType::Squish);
    FtnMsgBase msgbase("CP866");
    makeArea(msgbase, area);

    REQUIRE(msgbase.writeAll(numberedFrom(1, 6)).written == 6);
    REQUIRE(msgbase.removeAll({2, 3, 4}).has_value());
    REQUIRE(msgbase.count() == 3);

    // The free chain now holds three frames, and a set written against it walks
    // that chain message by message: a run that read the chain once and handed
    // every message the same frame would lose all but the last of them.
    const auto report = msgbase.writeAll(numberedFrom(7, 3));
    CHECK(report.written == 3);
    CHECK(msgbase.count() == 6);
    CHECK(subjectsOf(msgbase) ==
          std::vector<std::string>{"message 1", "message 5", "message 6", "message 7",
                                   "message 8", "message 9"});

    msgbase.close();
    FtnMsgBase again("CP866");
    REQUIRE(again.open(area).has_value());
    CHECK(again.count() == 6);
    CHECK(again.body(4).text().find("Body of message 7") != std::string::npos);
    CHECK(again.body(6).text().find("Body of message 9") != std::string::npos);
}
