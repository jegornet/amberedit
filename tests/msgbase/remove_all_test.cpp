#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "domain/message.hpp"
#include "msgbase/ftn_msgbase.hpp"
#include "temp_dir.hpp"
#include "temp_squish_base.hpp"
#include "test_strings.hpp"

using amberedit::domain::AreaConfig;
using amberedit::domain::MessageDraft;
using amberedit::domain::MessageInfo;
using amberedit::domain::MsgBaseType;
using amberedit::msgbase::FtnMsgBase;
using amberedit::test::TempDir;
using amberedit::test::TempSquishBase;
using amberedit::test::valueOf;

namespace {

AreaConfig areaAt(const std::string& path, MsgBaseType type) {
    AreaConfig area;
    area.tag = "delete.area";
    area.path = path;
    area.type = type;
    return area;
}

/// A message that says which one it is, which is the whole of what these tests
/// read back: what a set deleted as one call leaves standing is a question
/// about order and nothing else.
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

/// A base of the format with `count` messages in it, subjects numbered from one.
void fill(FtnMsgBase& msgbase, const AreaConfig& area, uint32_t count) {
    REQUIRE(FtnMsgBase("CP866").create(area).has_value());
    REQUIRE(msgbase.open(area).has_value());
    for (uint32_t number = 1; number <= count; ++number) {
        REQUIRE(valueOf(msgbase.write(numbered(number))) == number);
    }
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

/// One value out of the storage report — how the frame a Squish message sits in
/// is asked about from above the port, `info()` being the one call that answers
/// for the record rather than for the message.
std::string reportValue(const MessageInfo& info, const std::string& block,
                        const std::string& label) {
    for (const auto& one : info.blocks) {
        if (one.title != block) continue;
        for (const auto& field : one.fields) {
            if (field.label == label) return field.value;
        }
    }
    return {};
}

std::string frameOf(const FtnMsgBase& msgbase, uint32_t number,
                    const std::string& label) {
    return reportValue(msgbase.info(number), "Message Frame Record:", label);
}

/// Every format alike: the set goes out in one call, the survivors keep their
/// order and their UIDs, and the base reads back the same way from disk.
void checkSetIsTakenOut(MsgBaseType type) {
    TempDir dir;
    const AreaConfig area = areaAt(dir.path("area"), type);
    FtnMsgBase msgbase("CP866");
    fill(msgbase, area, 8);

    const std::vector<uint32_t> before = uidsOf(msgbase);
    // Out of order and from three different places in the area: the numbers are
    // positions as the base stands now, and nothing renumbers until the call
    // comes back.
    REQUIRE(msgbase.removeAll({6, 1, 3}).has_value());
    CHECK(msgbase.count() == 5);
    const std::vector<std::string> left = {"message 2", "message 4", "message 5",
                                           "message 7", "message 8"};
    CHECK(subjectsOf(msgbase) == left);

    // A UID is the message's own and survives the renumbering the delete caused.
    const std::vector<uint32_t> kept = {before[1], before[3], before[4], before[6],
                                        before[7]};
    CHECK(uidsOf(msgbase) == kept);
    // And a mark left on one of the deleted lands on the message before it,
    // which is what a lastread does.
    CHECK(msgbase.indexOfUid(before[2]) == 1);

    msgbase.close();
    FtnMsgBase again("CP866");
    REQUIRE(again.open(area).has_value());
    CHECK(again.count() == 5);
    CHECK(subjectsOf(again) == left);
    CHECK(uidsOf(again) == kept);
    CHECK(again.body(1).text().find("Body of message 2") != std::string::npos);
}

/// A number that is not a message refuses the whole set before anything is
/// written — the caller worked its numbers out before the base was locked.
void checkAStaleNumberRefusesTheSet(MsgBaseType type) {
    TempDir dir;
    const AreaConfig area = areaAt(dir.path("area"), type);
    FtnMsgBase msgbase("CP866");
    fill(msgbase, area, 4);

    const auto refused = msgbase.removeAll({2, 5});
    CHECK_FALSE(refused.has_value());
    CHECK_FALSE(refused.error()->message().empty());
    CHECK(msgbase.count() == 4);
    CHECK_FALSE(msgbase.removeAll({0}).has_value());
    CHECK(msgbase.count() == 4);

    // Nothing was written, so the base on disk still holds all four.
    msgbase.close();
    FtnMsgBase again("CP866");
    REQUIRE(again.open(area).has_value());
    CHECK(again.count() == 4);
    CHECK(subjectsOf(again) ==
          std::vector<std::string>{"message 1", "message 2", "message 3", "message 4"});
}

/// The same message named twice is one message, and an empty set is nothing to
/// do rather than an answer of its own.
void checkASetIsASet(MsgBaseType type) {
    TempDir dir;
    const AreaConfig area = areaAt(dir.path("area"), type);
    FtnMsgBase msgbase("CP866");
    fill(msgbase, area, 3);

    CHECK(msgbase.removeAll({}).has_value());
    CHECK(msgbase.count() == 3);

    REQUIRE(msgbase.removeAll({2, 2}).has_value());
    CHECK(msgbase.count() == 2);
    CHECK(subjectsOf(msgbase) == std::vector<std::string>{"message 1", "message 3"});
}

/// Taking every message out leaves a base that opens, counts nothing and takes
/// a message again — which is where a format's own idea of an empty base gets
/// tested: Squish has a chain with no ends left and a free chain holding every
/// frame it ever had.
void checkAnEmptiedBaseStillWorks(MsgBaseType type) {
    TempDir dir;
    const AreaConfig area = areaAt(dir.path("area"), type);
    FtnMsgBase msgbase("CP866");
    fill(msgbase, area, 5);

    REQUIRE(msgbase.removeAll({1, 2, 3, 4, 5}).has_value());
    CHECK(msgbase.count() == 0);
    msgbase.close();

    FtnMsgBase again("CP866");
    REQUIRE(again.open(area).has_value());
    REQUIRE(again.count() == 0);
    REQUIRE(valueOf(again.write(numbered(9))) == 1);
    CHECK(again.header(1).subject == "message 9");
    CHECK(again.uidOf(1) != 0);
}

}  // namespace

TEST_CASE(
    "A set of messages is taken out of a Squish base as one call "
    "[delete][squish]") {
    checkSetIsTakenOut(MsgBaseType::Squish);
}

TEST_CASE("A set of messages is taken out of a JAM base as one call [delete][jam]") {
    checkSetIsTakenOut(MsgBaseType::Jam);
}

TEST_CASE(
    "A set of messages is taken out of a Fido *.msg base as one call "
    "[delete][opus]") {
    checkSetIsTakenOut(MsgBaseType::Opus);
}

TEST_CASE("A stale number refuses the whole set [delete]") {
    checkAStaleNumberRefusesTheSet(MsgBaseType::Squish);
    checkAStaleNumberRefusesTheSet(MsgBaseType::Jam);
    checkAStaleNumberRefusesTheSet(MsgBaseType::Opus);
}

TEST_CASE("A number named twice takes out one message [delete]") {
    checkASetIsASet(MsgBaseType::Squish);
    checkASetIsASet(MsgBaseType::Jam);
    checkASetIsASet(MsgBaseType::Opus);
}

TEST_CASE("A base emptied by one call opens and takes a message again [delete]") {
    checkAnEmptiedBaseStillWorks(MsgBaseType::Squish);
    checkAnEmptiedBaseStillWorks(MsgBaseType::Jam);
    checkAnEmptiedBaseStillWorks(MsgBaseType::Opus);
}

TEST_CASE("Squish links the frames either side of a deleted run [delete][squish]") {
    TempDir dir;
    const AreaConfig area = areaAt(dir.path("area"), MsgBaseType::Squish);
    FtnMsgBase msgbase("CP866");
    fill(msgbase, area, 6);

    // A run out of the middle and one at the very end: the first is linked over
    // as one, the second leaves the chain with a new last frame.
    REQUIRE(msgbase.removeAll({2, 3, 4, 6}).has_value());
    REQUIRE(msgbase.count() == 2);

    msgbase.close();
    FtnMsgBase again("CP866");
    REQUIRE(again.open(area).has_value());
    REQUIRE(again.count() == 2);
    CHECK(subjectsOf(again) == std::vector<std::string>{"message 1", "message 5"});

    // The chain as the two survivors see it: each names the other, and the ends
    // of it name nothing. A frame still pointing at one of the deleted is what
    // SQFIX would rebuild the index into.
    const std::string first = frameOf(again, 1, "ThisFrame");
    const std::string second = frameOf(again, 2, "ThisFrame");
    CHECK(frameOf(again, 1, "NextFrame") == second);
    CHECK(frameOf(again, 2, "PrevFrame") == first);
    // The ends of the chain point at nothing, and the base header names them.
    CHECK(frameOf(again, 1, "PrevFrame") == "00000000h (0)");
    CHECK(frameOf(again, 2, "NextFrame") == "00000000h (0)");
    CHECK(reportValue(again.info(1), "Message Base Record:", "FirstFrame") == first);
    CHECK(reportValue(again.info(1), "Message Base Record:", "LastFrame") == second);
}

TEST_CASE(
    "Squish gives the frames of a deleted set back to the free chain "
    "[delete][squish]") {
    TempDir dir;
    const AreaConfig area = areaAt(dir.path("area"), MsgBaseType::Squish);
    FtnMsgBase msgbase("CP866");
    fill(msgbase, area, 6);

    const std::string end =
        reportValue(msgbase.info(1), "Message Base Record:", "EndFrame");
    REQUIRE(msgbase.removeAll({2, 3, 4}).has_value());

    // Three frames back on the chain and three messages written again: they go
    // into the frames the deleted ones left, so the file does not grow.
    for (uint32_t number = 7; number <= 9; ++number) {
        REQUIRE(valueOf(msgbase.write(numbered(number))) != 0);
    }
    CHECK(msgbase.count() == 6);
    CHECK(reportValue(msgbase.info(1), "Message Base Record:", "EndFrame") == end);
}

TEST_CASE(
    "A set goes out of the Squish base smapi wrote, ends and all "
    "[delete][squish]") {
    // The fixture in the repository rather than a base written here: what a
    // delete has to leave behind is a base every other FTN program still reads,
    // and this one was written by smapi.
    TempSquishBase base;
    AreaConfig area;
    area.tag = "localnet";
    area.path = base.path();
    area.type = MsgBaseType::Squish;

    FtnMsgBase msgbase("CP866");
    REQUIRE(msgbase.open(area).has_value());
    const uint32_t before = msgbase.count();
    REQUIRE(before > 8);

    const std::string secondSubject = msgbase.header(2).subject;
    const uint32_t secondUid = msgbase.uidOf(2);
    // The first message, a run out of the middle and the last one: the three
    // places a chain can be cut, in one call.
    REQUIRE(msgbase.removeAll({1, 5, 6, 7, before}).has_value());
    CHECK(msgbase.count() == before - 5);
    msgbase.close();

    FtnMsgBase again("CP866");
    REQUIRE(again.open(area).has_value());
    REQUIRE(again.count() == before - 5);
    // The message that was second is now first, and it is the same message.
    CHECK(again.header(1).subject == secondSubject);
    CHECK(again.uidOf(1) == secondUid);
    CHECK(frameOf(again, 1, "PrevFrame") == "00000000h (0)");
    CHECK(frameOf(again, again.count(), "NextFrame") == "00000000h (0)");
    CHECK(reportValue(again.info(1), "Message Base Record:", "FirstFrame") ==
          frameOf(again, 1, "ThisFrame"));
    CHECK(reportValue(again.info(1), "Message Base Record:", "LastFrame") ==
          frameOf(again, again.count(), "ThisFrame"));

    // Every message still reads, and the chain runs through all of them.
    for (uint32_t number = 1; number < again.count(); ++number) {
        CHECK(frameOf(again, number, "NextFrame") ==
              frameOf(again, number + 1, "ThisFrame"));
        CHECK(frameOf(again, number + 1, "PrevFrame") ==
              frameOf(again, number, "ThisFrame"));
        CHECK_FALSE(again.header(number).from.empty());
    }

    // And the frames the five left are there to be written into: a base whose
    // free chain a delete had broken would refuse this.
    //
    // The count is read afterwards and on a line of its own: which side of an
    // `==` is evaluated first is nobody's to say, and one that read it before
    // the write would be comparing the number the message got against the count
    // without it.
    const uint32_t written = valueOf(again.write(numbered(1)));
    REQUIRE(written == again.count());
}
