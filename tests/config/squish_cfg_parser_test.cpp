#include <doctest/doctest.h>

#include <algorithm>

#include "config/path_map.hpp"
#include "config/squish_cfg_parser.hpp"
#include "test_paths.hpp"
#include "test_strings.hpp"

using amberedit::config::PathMap;
using amberedit::config::SquishCfgParser;
using amberedit::domain::AreaConfig;
using amberedit::domain::AreaKind;
using amberedit::domain::MsgBaseType;

namespace {

const AreaConfig* findArea(const std::vector<AreaConfig>& areas, const std::string& tag) {
    const auto it = std::find_if(areas.begin(), areas.end(),
                                 [&](const AreaConfig& area) { return area.tag == tag; });
    return it == areas.end() ? nullptr : &*it;
}

}  // namespace

TEST_CASE("SquishCfgParser parses testdata/tossers/squish.cfg [squishcfg]") {
    SquishCfgParser parser(amberedit::test::projectPath("testdata/tossers/squish.cfg"));
    const auto areas = amberedit::test::valueOf(parser.loadAreas());

    REQUIRE(areas.size() == 7);

    SUBCASE("-$ selects a Squish base") {
        const auto* area = findArea(areas, "localnet");
        REQUIRE(area != nullptr);
        CHECK(area->kind == AreaKind::Echo);
        CHECK(area->type == MsgBaseType::Squish);
        CHECK(area->path == "/home/ftn/msg/localnet");
    }

    SUBCASE("without -$ the base is Fido *.msg") {
        const auto* area = findArea(areas, "NETMAIL");
        REQUIRE(area != nullptr);
        CHECK(area->kind == AreaKind::Netmail);
        CHECK(area->type == MsgBaseType::Sdm);
        CHECK(area->path == "/home/ftn/msg/netmail");
    }

    SUBCASE("-$g gives the group, with the value attached") {
        CHECK(findArea(areas, "NETMAIL")->group == "A");
        CHECK(findArea(areas, "BAD")->group == "Z");
        CHECK(findArea(areas, "localnet")->group == "B");
        CHECK(findArea(areas, "fidotest")->group == "C");
    }

    SUBCASE("-p gives the area's own AKA, the bare addresses are links") {
        const auto* area = findArea(areas, "fidotest");
        REQUIRE(area != nullptr);
        CHECK(area->address.toString() == "2:382/736");
        REQUIRE(area->links.size() == 2);
        CHECK(area->links[0].toString() == "2:382/736.1");
        CHECK(area->links[1].toString() == "2:240/1120");
    }

    SUBCASE("an area may have an AKA and no links") {
        const auto* area = findArea(areas, "PERSONAL.MAIL");
        REQUIRE(area != nullptr);
        CHECK(area->kind == AreaKind::Local);
        CHECK(area->address.toString() == "2:382/736");
        CHECK(area->links.empty());
    }

    SUBCASE("passthrough has no base whatever the other options say") {
        const auto* area = findArea(areas, "su.general");
        REQUIRE(area != nullptr);
        CHECK(area->isPassthrough());
        CHECK(area->type == MsgBaseType::Passthrough);
        CHECK(area->path.empty());
        CHECK(area->group == "A");
        // -p takes the first address, so only the second is a link.
        CHECK(area->address.toString() == "2:382/736");
        REQUIRE(area->links.size() == 1);
        CHECK(area->links[0].toString() == "2:5020/715");
    }

    SUBCASE("BadArea and LocalArea are recognised by keyword") {
        CHECK(findArea(areas, "BAD")->kind == AreaKind::Bad);
        CHECK(findArea(areas, "BAD")->type == MsgBaseType::Squish);
        CHECK(findArea(areas, "PERSONAL.MAIL")->kind == AreaKind::Local);
    }
}

TEST_CASE("SquishCfgParser ignores the tosser's own options [squishcfg]") {
    // -$d30 is a dupe history and -0 a message limit: neither concerns a
    // reader, and neither must be mistaken for an address or a group.
    const auto areas =
        SquishCfgParser::parseText("BadArea BAD /ftn/bad -$ -$gZ -$d30 -p2:382/736\n");

    REQUIRE(areas.size() == 1);
    CHECK(areas[0].group == "Z");
    CHECK(areas[0].type == MsgBaseType::Squish);
    CHECK(areas[0].address.toString() == "2:382/736");
    CHECK(areas[0].links.empty());
}

TEST_CASE("SquishCfgParser: a group may be absent [squishcfg]") {
    const auto areas =
        SquishCfgParser::parseText("EchoArea a.one /ftn/one -$ -p2:382/736\n");

    REQUIRE(areas.size() == 1);
    CHECK(areas[0].group.empty());
}

TEST_CASE("SquishCfgParser: keywords are case-insensitive [squishcfg]") {
    const auto areas = SquishCfgParser::parseText(
        "ECHOAREA a.one /ftn/one -$\n"
        "echoarea a.two /ftn/two -$\n"
        "NetArea a.three /ftn/three\n");

    CHECK(areas.size() == 3);
}

TEST_CASE("SquishCfgParser skips comments and unrelated lines [squishcfg]") {
    const auto areas = SquishCfgParser::parseText(
        "; a comment\n"
        "Address 2:382/736\n"
        "Outbound /ftn/out\n"
        "EchoArea a.one /ftn/one -$   ; trailing comment\n"
        "\n");

    REQUIRE(areas.size() == 1);
    CHECK(areas[0].tag == "a.one");
    CHECK(areas[0].path == "/ftn/one");
}

TEST_CASE("SquishCfgParser leaves the AKA unset when -p is absent [squishcfg]") {
    const auto areas = SquishCfgParser::parseText("EchoArea a.one /ftn/one -$\n");

    REQUIRE(areas.size() == 1);
    CHECK_FALSE(areas[0].address.isValid());
}

TEST_CASE("SquishCfgParser throws on a missing file [squishcfg]") {
    SquishCfgParser parser("/nonexistent/path/squish.cfg");
    CHECK_FALSE(parser.loadAreas().has_value());
}

TEST_CASE("map_path rewrites an area's path [squishcfg]") {
    PathMap paths;
    paths.add("c:\\fido", "/mnt/fido");

    const auto areas = SquishCfgParser::parseText(
        "EchoArea a.one c:\\fido\\msgbase\\one -$ -p2:382/736\n"
        "EchoArea a.two passthrough -0\n",
        paths);

    REQUIRE(areas.size() == 2);
    CHECK(areas[0].type == MsgBaseType::Squish);
    CHECK(areas[0].path == "/mnt/fido/msgbase/one");
    CHECK(areas[0].address.toString() == "2:382/736");
    // A passthrough area has no path for a rule to be asked about.
    CHECK(areas[1].type == MsgBaseType::Passthrough);
    CHECK(areas[1].path.empty());
}
