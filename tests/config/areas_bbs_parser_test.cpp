#include <doctest/doctest.h>

#include <algorithm>

#include "config/areas_bbs_parser.hpp"
#include "test_paths.hpp"

using amberedit::config::AreasBbsParser;
using amberedit::domain::AreaConfig;
using amberedit::domain::MsgBaseType;

namespace {

const AreaConfig* findArea(const std::vector<AreaConfig>& areas, const std::string& tag) {
    const auto it = std::find_if(areas.begin(), areas.end(),
                                 [&](const AreaConfig& area) { return area.tag == tag; });
    return it == areas.end() ? nullptr : &*it;
}

}  // namespace

TEST_CASE("AreasBbsParser parses testdata/tossers/areas.bbs [areasbbs]") {
    AreasBbsParser parser(amberedit::test::projectPath("testdata/tossers/areas.bbs"));
    const auto areas = parser.loadAreas();

    REQUIRE(areas.size() == 4);

    SUBCASE("no prefix means Fido *.msg") {
        const auto* area = findArea(areas, "localnet");
        REQUIRE(area != nullptr);
        CHECK(area->type == MsgBaseType::Sdm);
        CHECK(area->path == "/home/ftn/msg/localnet");
        REQUIRE(area->links.size() == 2);
        CHECK(area->links[0].toString() == "192:168/2");
        CHECK(area->links[1].toString() == "192:168/1.1");
    }

    SUBCASE("a ! prefix means a JAM base") {
        const auto* area = findArea(areas, "fidotest");
        REQUIRE(area != nullptr);
        CHECK(area->type == MsgBaseType::Jam);
        CHECK(area->path == "/home/ftn/msg/fidotest");
        REQUIRE(area->links.size() == 2);
        CHECK(area->links[0].toString() == "2:382/736.1");
        CHECK(area->links[1].toString() == "2:240/1120");
    }

    SUBCASE("a $ prefix means a Squish base") {
        const auto* area = findArea(areas, "ru.ai");
        REQUIRE(area != nullptr);
        CHECK(area->type == MsgBaseType::Squish);
        CHECK(area->path == "/home/ftn/msg/ru.ai");
        REQUIRE(area->links.size() == 2);
        CHECK(area->links[0].toString() == "2:382/736.1");
        CHECK(area->links[1].toString() == "2:5015/46");
    }

    SUBCASE("a P field means passthrough with no path") {
        const auto* area = findArea(areas, "su.general");
        REQUIRE(area != nullptr);
        CHECK(area->type == MsgBaseType::Passthrough);
        CHECK(area->isPassthrough());
        CHECK(area->path.empty());
        // An area with no links is legal and still belongs in the list.
        CHECK(area->links.empty());
    }

    SUBCASE("the format carries no groups") {
        for (const auto& area : areas) {
            INFO(area.tag);
            CHECK(area.group.empty());
        }
    }
}

TEST_CASE("AreasBbsParser leaves the group empty [areasbbs]") {
    // The format has no notion of groups, so the area list column is blank for
    // every area read from one.
    const auto areas = AreasBbsParser::parseText("$/ftn/one a.one 2:5020/1\n");
    REQUIRE(areas.size() == 1);
    CHECK(areas[0].group.empty());
}

TEST_CASE("AreasBbsParser: comments and blank lines [areasbbs]") {
    const auto areas = AreasBbsParser::parseText(
        "; file header\n"
        "\n"
        ";$/ftn/skipped skipped 2:5020/1\n"
        "$/ftn/kept kept 2:5020/1\n");

    REQUIRE(areas.size() == 1);
    CHECK(areas[0].tag == "kept");
}

TEST_CASE("AreasBbsParser: a line without a tag is ignored [areasbbs]") {
    CHECK(AreasBbsParser::parseText("$/ftn/one\n").empty());
    CHECK(AreasBbsParser::parseText("P\n").empty());
}

TEST_CASE("AreasBbsParser: junk tokens do not become links [areasbbs]") {
    const auto areas =
        AreasBbsParser::parseText("$/ftn/one a.one 2:5020/1 не-адрес 2:5020/2\n");

    REQUIRE(areas.size() == 1);
    REQUIRE(areas[0].links.size() == 2);
    CHECK(areas[0].links[0].toString() == "2:5020/1");
    CHECK(areas[0].links[1].toString() == "2:5020/2");
}

TEST_CASE("AreasBbsParser throws on a missing file [areasbbs]") {
    AreasBbsParser parser("/nonexistent/path/areas.bbs");
    CHECK_THROWS(parser.loadAreas());
}
