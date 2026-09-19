#include <doctest/doctest.h>

#include <algorithm>

#include "config/areas_bbs_parser.hpp"
#include "config/path_map.hpp"
#include "test_paths.hpp"
#include "test_strings.hpp"

using amberedit::config::AreasBbsParser;
using amberedit::config::PathMap;
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
    const auto areas = amberedit::test::valueOf(parser.loadAreas());

    // Five lines, two of them passthrough and so not in the list.
    REQUIRE(areas.size() == 3);

    SUBCASE("no prefix means Fido *.msg") {
        const auto* area = findArea(areas, "localnet");
        REQUIRE(area != nullptr);
        CHECK(area->type == MsgBaseType::Opus);
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

    SUBCASE("a P field means passthrough, and such an area is left out") {
        // There is no base on disk behind a P, with links written after the
        // tag or without.
        CHECK(findArea(areas, "su.general") == nullptr);
        CHECK(findArea(areas, "su.tormoz") == nullptr);
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
    CHECK_FALSE(parser.loadAreas().has_value());
}

TEST_CASE("map_path rewrites the path under its type prefix [areasbbs]") {
    PathMap paths;
    paths.add("c:\\fido", "/mnt/fido");

    const auto areas = AreasBbsParser::parseText(
        "$c:\\fido\\msgbase\\one a.one 2:5020/1\n"
        "!c:/FIDO/msgbase/two a.two\n"
        "d:\\other\\three a.three\n"
        "P a.four 2:5020/1\n",
        paths);

    // Three of the four: the P line names no path for a rule to be asked
    // about, and no area for the list either.
    REQUIRE(areas.size() == 3);
    // The prefix names the base type and is not part of the path, so a rule
    // sees the path and the type survives it.
    CHECK(areas[0].type == MsgBaseType::Squish);
    CHECK(areas[0].path == "/mnt/fido/msgbase/one");
    CHECK(areas[1].type == MsgBaseType::Jam);
    CHECK(areas[1].path == "/mnt/fido/msgbase/two");
    CHECK(areas[2].path == "d:\\other\\three");
}

TEST_CASE("AreasBbsParser leaves passthrough areas out [areasbbs]") {
    const auto areas = AreasBbsParser::parseText(
        "$/ftn/one a.one 2:5020/1\n"
        "P     su.tormoz                    2:5020/9999\n"
        "p su.lower 2:5020/1\n"
        "!/ftn/two a.two\n");

    // The mail only passes through such an area: nothing is written down, so
    // there is no base to open and no line to put in the list. The field is
    // matched without regard to case, as the tosser matches it — a lower-case
    // `p` is the same marker and not a relative path.
    REQUIRE(areas.size() == 2);
    CHECK(areas[0].tag == "a.one");
    CHECK(areas[1].tag == "a.two");
}
