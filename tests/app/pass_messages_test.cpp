#include <doctest/doctest.h>

#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "app/area_manager.hpp"
#include "app/pass_messages.hpp"
#include "config/app_config.hpp"
#include "domain/message.hpp"
#include "msgbase/null_lastread_store.hpp"
#include "ports/i_area_source.hpp"
#include "temp_squish_base.hpp"
#include "test_strings.hpp"

using amberedit::app::AreaManager;
using amberedit::app::kChunkMessages;
using amberedit::app::passMessages;
using amberedit::app::PassReport;
using amberedit::app::PassRequest;
using amberedit::config::AppConfig;
using amberedit::domain::AreaConfig;
using amberedit::domain::MessageDraft;
using amberedit::domain::MsgBaseType;
using amberedit::test::TempSquishBase;
using amberedit::test::valueOf;

namespace {

class TwoAreaSource final : public amberedit::ports::IAreaConfigSource {
public:
    TwoAreaSource(AreaConfig first, AreaConfig second)
        : areas_{std::move(first), std::move(second)} {}
    tl::expected<std::vector<AreaConfig>, amberedit::ErrorPtr> loadAreas() override {
        return areas_;
    }

private:
    std::vector<AreaConfig> areas_;
};

AreaConfig areaAt(const std::string& tag, const std::string& path) {
    AreaConfig area;
    area.tag = tag;
    area.path = path;
    area.type = MsgBaseType::Squish;
    return area;
}

/// Two areas on Squish bases of their own, and the manager that opens them one
/// at a time — which is the whole reason a run is chunked at all.
struct Fixture {
    Fixture()
        : source(areaAt("localnet", here.path())),
          target(areaAt("test.other", there.path())),
          manager(std::make_unique<TwoAreaSource>(source, target),
                  std::make_unique<amberedit::msgbase::NullLastReadStore>(), config) {
        static_cast<void>(manager.reload());
    }

    /// Empties an area and fills it with `count` messages whose subject says
    /// which they are, so that the order they arrive in can be read back.
    void fill(const AreaConfig& area, uint32_t count) {
        amberedit::ports::IMsgBase* base = valueOf(manager.openArea(area));
        REQUIRE(base != nullptr);
        while (base->count() > 0) REQUIRE(base->remove(1).has_value());
        for (uint32_t i = 0; i < count; ++i) {
            MessageDraft draft;
            draft.from = "Vasya Pupkin";
            draft.to = "All";
            draft.subject = "message " + std::to_string(i);
            draft.lines = {"one line of text"};
            REQUIRE(base->write(draft).has_value());
        }
        manager.closeCurrentArea();
    }

    /// Every message of an area, by subject, in the order it holds them.
    std::vector<std::string> subjectsIn(const AreaConfig& area) {
        amberedit::ports::IMsgBase* base = valueOf(manager.openArea(area));
        REQUIRE(base != nullptr);
        std::vector<std::string> subjects;
        for (uint32_t number = 1; number <= base->count(); ++number) {
            subjects.push_back(base->header(number).subject);
        }
        manager.closeCurrentArea();
        return subjects;
    }

    /// The UID of every message in an area.
    std::set<uint32_t> uidsIn(const AreaConfig& area) {
        amberedit::ports::IMsgBase* base = valueOf(manager.openArea(area));
        REQUIRE(base != nullptr);
        std::set<uint32_t> uids;
        for (uint32_t number = 1; number <= base->count(); ++number) {
            uids.insert(base->uidOf(number));
        }
        manager.closeCurrentArea();
        return uids;
    }

    TempSquishBase here;
    TempSquishBase there;
    AreaConfig source;
    AreaConfig target;
    AppConfig config;
    AreaManager manager;
};

}  // namespace

TEST_CASE("A run longer than a chunk goes over whole and in order [pass][squish]") {
    Fixture fixture;
    // Past the bound, so that the run swaps bases more than once — which is the
    // one thing a shorter set never exercises.
    const auto count = static_cast<uint32_t>(kChunkMessages + 8);
    fixture.fill(fixture.source, count);
    fixture.fill(fixture.target, 0);
    const std::set<uint32_t> uids = fixture.uidsIn(fixture.source);
    REQUIRE(uids.size() == count);

    const PassRequest request{
        fixture.source, fixture.target, uids, /*remember=*/true, {}};
    const PassReport report = passMessages(fixture.manager, request);

    CHECK(report.written == count);
    CHECK_FALSE(report.stopped);
    CHECK(report.stored == uids);
    // Every one of them, once, in the order they stood in: a chunk boundary is
    // not a place where a message may be dropped or written twice.
    const std::vector<std::string> arrived = fixture.subjectsIn(fixture.target);
    REQUIRE(arrived.size() == count);
    CHECK(arrived.front() == "message 0");
    CHECK(arrived.back() == "message " + std::to_string(count - 1));
    // And the source is untouched — the taking out is the caller's half.
    CHECK(fixture.subjectsIn(fixture.source).size() == count);
}

TEST_CASE("A run into the area it reads does not copy its own copies [pass][squish]") {
    Fixture fixture;
    // Again past a chunk, which is where a walk that followed the end of the
    // base would keep finding more to do.
    const auto count = static_cast<uint32_t>(kChunkMessages + 8);
    fixture.fill(fixture.source, count);
    const std::set<uint32_t> uids = fixture.uidsIn(fixture.source);

    const PassRequest request{
        fixture.source, fixture.source, uids, /*remember=*/false, {}};
    const PassReport report = passMessages(fixture.manager, request);

    CHECK(report.written == count);
    CHECK(fixture.subjectsIn(fixture.source).size() == count * 2);
}

TEST_CASE("Only a move gathers what went over [pass][squish]") {
    Fixture fixture;
    fixture.fill(fixture.source, 4);
    fixture.fill(fixture.target, 0);
    const std::set<uint32_t> uids = fixture.uidsIn(fixture.source);

    const PassRequest copying{
        fixture.source, fixture.target, uids, /*remember=*/false, {}};
    const PassReport report = passMessages(fixture.manager, copying);

    CHECK(report.written == 4);
    // Nothing is taken out of the source after a copy, so there is nothing for
    // the set to name and it is not gathered at all.
    CHECK(report.stored.empty());
}

TEST_CASE("The run stops where it is told to [pass][squish]") {
    Fixture fixture;
    fixture.fill(fixture.source, 10);
    fixture.fill(fixture.target, 0);
    const std::set<uint32_t> uids = fixture.uidsIn(fixture.source);

    int seen = 0;
    const PassRequest request{fixture.source, fixture.target, uids, /*remember=*/true,
                              [&seen] { return ++seen <= 3; }};
    const PassReport report = passMessages(fixture.manager, request);

    // Three went over and the fourth was never touched: the answer is given
    // before the message is carried, not after.
    CHECK(report.stopped);
    CHECK(report.written == 3);
    CHECK(report.stored.size() == 3);
    CHECK(fixture.subjectsIn(fixture.target) ==
          std::vector<std::string>{"message 0", "message 1", "message 2"});
    CHECK(fixture.subjectsIn(fixture.source).size() == 10);
}

TEST_CASE("A run over nothing is a run that does nothing [pass][squish]") {
    Fixture fixture;
    fixture.fill(fixture.source, 4);
    fixture.fill(fixture.target, 0);

    SUBCASE("no UIDs at all") {
        const std::set<uint32_t> none;
        const PassRequest request{fixture.source,
                                  fixture.target,
                                  none,
                                  /*remember=*/true,
                                  {}};
        const PassReport report = passMessages(fixture.manager, request);
        CHECK(report.written == 0);
        CHECK_FALSE(report.stopped);
    }
    SUBCASE("UIDs the area does not hold") {
        // A mark left on a message the base has since packed away names nothing
        // and is passed over rather than answered for.
        const std::set<uint32_t> strangers{900001, 900002};
        const PassRequest request{fixture.source,
                                  fixture.target,
                                  strangers,
                                  /*remember=*/true,
                                  {}};
        const PassReport report = passMessages(fixture.manager, request);
        CHECK(report.written == 0);
    }
    CHECK(fixture.subjectsIn(fixture.target).empty());
}
