#include <doctest/doctest.h>

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "app/remove_messages.hpp"
#include "domain/message.hpp"
#include "msgbase/ftn_msgbase.hpp"
#include "temp_squish_base.hpp"
#include "test_strings.hpp"

using amberedit::app::kRemoveChunk;
using amberedit::app::removeMessages;
using amberedit::app::RemoveReport;
using amberedit::app::RemoveRequest;
using amberedit::domain::AreaConfig;
using amberedit::domain::MessageDraft;
using amberedit::domain::MsgBaseType;
using amberedit::msgbase::FtnMsgBase;
using amberedit::test::TempSquishBase;
using amberedit::test::valueOf;

namespace {

/// A Squish base holding `count` messages whose subject says which they are —
/// the fixture's own mail thrown away first, since what these tests read back is
/// how many and which.
struct Area {
    explicit Area(uint32_t count) : area(config(base.path())) {
        REQUIRE(msgbase.open(area).has_value());
        std::vector<uint32_t> all;
        for (uint32_t number = 1; number <= msgbase.count(); ++number) {
            all.push_back(number);
        }
        REQUIRE(msgbase.removeAll(all).has_value());

        for (uint32_t number = 1; number <= count; ++number) {
            MessageDraft draft;
            draft.from = "Yegor Gluhov";
            draft.to = "All";
            draft.subject = "message " + std::to_string(number);
            draft.lines = {"one line of text"};
            REQUIRE(valueOf(msgbase.write(draft)) == number);
        }
    }

    static AreaConfig config(const std::string& path) {
        AreaConfig area;
        area.tag = "localnet";
        area.path = path;
        area.type = MsgBaseType::Squish;
        return area;
    }

    /// The UIDs of the messages at these numbers, which is how a set is named.
    [[nodiscard]] std::set<uint32_t> uidsAt(const std::vector<uint32_t>& numbers) {
        std::set<uint32_t> uids;
        for (const uint32_t number : numbers) uids.insert(msgbase.uidOf(number));
        return uids;
    }

    [[nodiscard]] std::vector<std::string> subjects() {
        std::vector<std::string> out;
        for (uint32_t number = 1; number <= msgbase.count(); ++number) {
            out.push_back(msgbase.header(number).subject);
        }
        return out;
    }

    TempSquishBase base;
    AreaConfig area;
    FtnMsgBase msgbase{"CP866"};
};

}  // namespace

TEST_CASE("A set is taken out of the area whatever order it stands in [delete]") {
    Area area(8);
    const std::set<uint32_t> uids = area.uidsAt({2, 5, 6});

    const RemoveReport report = removeMessages(area.msgbase, {uids, nullptr});

    CHECK_FALSE(report.stopped);
    CHECK(report.removed.size() == 3);
    CHECK(std::set<uint32_t>(report.removed.begin(), report.removed.end()) == uids);
    CHECK(area.subjects() == std::vector<std::string>{"message 1", "message 3",
                                                      "message 4", "message 7",
                                                      "message 8"});
}

TEST_CASE("A UID the area does not hold names nothing [delete]") {
    Area area(4);
    std::set<uint32_t> uids = area.uidsAt({3});
    uids.insert(0xfeedfaceu);

    const RemoveReport report = removeMessages(area.msgbase, {uids, nullptr});

    CHECK(report.removed.size() == 1);
    CHECK(area.msgbase.count() == 3);
    CHECK(area.subjects() ==
          std::vector<std::string>{"message 1", "message 2", "message 4"});
}

TEST_CASE("An empty set is nothing to do [delete]") {
    Area area(3);
    const std::set<uint32_t> none;

    const RemoveReport report = removeMessages(area.msgbase, {none, nullptr});

    CHECK(report.removed.empty());
    CHECK_FALSE(report.stopped);
    CHECK(area.msgbase.count() == 3);
}

TEST_CASE("A run longer than one chunk takes the whole set out [delete]") {
    // Two chunks and a little, so that the walk goes round more than once: what
    // the chunking has to get right is that the numbers it has not reached yet
    // still name the same messages after a chunk has gone out from above them.
    const auto total = static_cast<uint32_t>((kRemoveChunk * 2) + 100);
    Area area(total);
    std::vector<uint32_t> numbers;
    for (uint32_t number = 2; number < total; ++number) numbers.push_back(number);
    const std::set<uint32_t> uids = area.uidsAt(numbers);

    const RemoveReport report = removeMessages(area.msgbase, {uids, nullptr});

    CHECK_FALSE(report.stopped);
    CHECK(report.removed.size() == total - 2);
    REQUIRE(area.msgbase.count() == 2);
    CHECK(area.subjects() ==
          std::vector<std::string>{"message 1", "message " + std::to_string(total)});
}

TEST_CASE(
    "onMessage stops the run and leaves the message it was asked about "
    "[delete]") {
    Area area(6);
    const std::set<uint32_t> uids = area.uidsAt({1, 2, 3, 4, 5, 6});

    // The walk runs backwards, so the two it gets to first are messages 6 and 5,
    // and the third is where it is stopped.
    int asked = 0;
    const RemoveRequest request{uids, [&asked] { return ++asked < 3; }};
    const RemoveReport report = removeMessages(area.msgbase, request);

    CHECK(report.stopped);
    CHECK(report.removed.size() == 2);
    REQUIRE(area.msgbase.count() == 4);
    CHECK(area.subjects() ==
          std::vector<std::string>{"message 1", "message 2", "message 3", "message 4"});
}

TEST_CASE(
    "A run stopped inside a chunk keeps what the chunk had gathered "
    "[delete]") {
    // The run is stopped before any chunk is full, so everything gathered so far
    // goes out in the one call the end of the walk makes: what a stopped run
    // leaves behind is the messages it never counted, not the ones it did.
    const auto total = static_cast<uint32_t>(kRemoveChunk + 10);
    Area area(total);
    std::vector<uint32_t> numbers;
    for (uint32_t number = 1; number <= total; ++number) numbers.push_back(number);
    const std::set<uint32_t> uids = area.uidsAt(numbers);

    int asked = 0;
    const RemoveRequest request{uids, [&asked] { return ++asked <= 5; }};
    const RemoveReport report = removeMessages(area.msgbase, request);

    CHECK(report.stopped);
    CHECK(report.removed.size() == 5);
    CHECK(area.msgbase.count() == total - 5);
    // The five it took are the five at the end, the walk running backwards.
    CHECK(area.msgbase.header(area.msgbase.count()).subject ==
          "message " + std::to_string(total - 5));
}
