#include <catch2/catch.hpp>

#include <algorithm>

#include "config/fidoconfig_parser.hpp"
#include "test_paths.hpp"

using amberedit::config::FidoconfigParser;
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

TEST_CASE("FidoconfigParser parses testdata/tossers/areas", "[fidoconfig]") {
    FidoconfigParser parser(amberedit::test::projectPath("testdata/tossers/areas"));
    const auto areas = parser.loadAreas();

    REQUIRE(areas.size() == 6);

    SECTION("netmailarea of type msg") {
        const auto* netmail = findArea(areas, "NETMAIL");
        REQUIRE(netmail != nullptr);
        CHECK(netmail->kind == AreaKind::Netmail);
        CHECK(netmail->type == MsgBaseType::Sdm);
        CHECK(netmail->path == "/Users/egor/ftn/msg/netmail");
        CHECK(netmail->group == "A");
    }

    SECTION("localarea of type squish") {
        const auto* personal = findArea(areas, "PERSONAL.MAIL");
        REQUIRE(personal != nullptr);
        CHECK(personal->kind == AreaKind::Local);
        CHECK(personal->type == MsgBaseType::Squish);
        CHECK(personal->path == "/Users/egor/ftn/msg/personal.mail");
        CHECK(personal->group == "A");
    }

    SECTION("badarea and dupearea are recognised by keyword") {
        REQUIRE(findArea(areas, "BAD") != nullptr);
        REQUIRE(findArea(areas, "DUPES") != nullptr);
        CHECK(findArea(areas, "BAD")->kind == AreaKind::Bad);
        CHECK(findArea(areas, "DUPES")->kind == AreaKind::Dupe);
        CHECK(findArea(areas, "BAD")->group == "B");
    }

    SECTION("-a gives the AKA, the bare addresses that follow are links") {
        const auto* localnet = findArea(areas, "localnet");
        REQUIRE(localnet != nullptr);
        CHECK(localnet->kind == AreaKind::Echo);
        CHECK(localnet->type == MsgBaseType::Squish);
        CHECK(localnet->path == "/Users/egor/ftn/msg/localnet");

        // -a takes one address, the area's own; everything after it is a link.
        CHECK(localnet->address.toString() == "192:168/2");
        REQUIRE(localnet->links.size() == 1);
        CHECK(localnet->links[0].toString() == "192:168/1");
    }
}

TEST_CASE("FidoconfigParser does not mistake option arguments for links",
          "[fidoconfig]") {
    const auto areas = FidoconfigParser::parseText(
        "EchoArea ru.linux /ftn/ru.linux -b squish -dupehistory 14 -p 5 "
        "2:5020/715 2:5020/9999.1\n");

    REQUIRE(areas.size() == 1);
    CHECK(areas[0].type == MsgBaseType::Squish);
    // -dupehistory 14 and -p 5 are not addresses; bare tokens at the end are.
    CHECK_FALSE(areas[0].address.isValid());
    REQUIRE(areas[0].links.size() == 2);
    CHECK(areas[0].links[0].toString() == "2:5020/715");
    CHECK(areas[0].links[1].toString() == "2:5020/9999.1");
}

TEST_CASE("A boolean flag does not swallow the option after it", "[fidoconfig]") {
    // husky's `-pack` clears a flag and takes no argument. Treating it as if it
    // did would eat `-b`, and the area would come back with no base type —
    // which is exactly what this parser used to do.
    const auto areas = FidoconfigParser::parseText(
        "EchoArea ru.linux /ftn/ru.linux -pack -b squish -g A\n");

    REQUIRE(areas.size() == 1);
    CHECK(areas[0].type == MsgBaseType::Squish);
    CHECK(areas[0].group == "A");
}

TEST_CASE("An option husky does not have is treated as a flag", "[fidoconfig]") {
    // The value list must hold value-taking options and nothing else. `-charset`
    // was invented by this parser and is gone; if it ever came back as a value
    // option, `-b` would be its argument and the type would be lost again.
    const auto areas = FidoconfigParser::parseText(
        "EchoArea ru.linux /ftn/ru.linux -charset CP866 -b jam\n");

    REQUIRE(areas.size() == 1);
    CHECK(areas[0].type == MsgBaseType::Jam);
}

TEST_CASE("FidoconfigParser parses a quoted description", "[fidoconfig]") {
    const auto areas = FidoconfigParser::parseText(
        "EchoArea ru.linux /ftn/ru.linux -b jam -d \"Linux и всё вокруг\"\n");

    REQUIRE(areas.size() == 1);
    CHECK(areas[0].type == MsgBaseType::Jam);
    CHECK(areas[0].description == "Linux и всё вокруг");
}

TEST_CASE("FidoconfigParser reads the area group", "[fidoconfig]") {
    const auto areas = FidoconfigParser::parseText(
        "EchoArea a.one /ftn/one -b squish -g A\n"
        "EchoArea a.two /ftn/two -b squish -g Fidonet\n"
        "EchoArea a.three /ftn/three -b squish -group Net\n"
        "EchoArea a.four /ftn/four -b squish\n");

    REQUIRE(areas.size() == 4);
    CHECK(areas[0].group == "A");
    CHECK(areas[1].group == "Fidonet");
    // `-g` and nothing else: husky has no long `-group` spelling, so this is an
    // unknown flag and `Net` a bare token that is not an address.
    CHECK(areas[2].group.empty());
    CHECK(areas[3].group.empty());  // the option is optional
}

TEST_CASE("FidoconfigParser understands passthrough", "[fidoconfig]") {
    const auto areas = FidoconfigParser::parseText(
        "EchoArea su.general passthrough -a 2:5020/1 2:5020/715\n");

    REQUIRE(areas.size() == 1);
    CHECK(areas[0].isPassthrough());
    CHECK(areas[0].type == MsgBaseType::Passthrough);
    CHECK(areas[0].path.empty());
    CHECK(areas[0].address.toString() == "2:5020/1");
    REQUIRE(areas[0].links.size() == 1);
    CHECK(areas[0].links[0].toString() == "2:5020/715");
}

TEST_CASE("FidoconfigParser leaves the AKA unset when -a is absent", "[fidoconfig]") {
    const auto areas =
        FidoconfigParser::parseText("EchoArea a.one /ftn/one -b squish 2:5020/715\n");

    REQUIRE(areas.size() == 1);
    CHECK_FALSE(areas[0].address.isValid());
    REQUIRE(areas[0].links.size() == 1);
    CHECK(areas[0].links[0].toString() == "2:5020/715");
}

TEST_CASE("FidoconfigParser skips comments and unrelated directives", "[fidoconfig]") {
    const auto areas = FidoconfigParser::parseText(
        "# comment line\n"
        "Address 2:5020/9999.1\n"
        "LogFileDir /var/log/husky\n"
        "EchoArea ru.perl /ftn/ru.perl -b squish  # trailing comment\n"
        "\n");

    REQUIRE(areas.size() == 1);
    CHECK(areas[0].tag == "ru.perl");
    CHECK(areas[0].path == "/ftn/ru.perl");
}

TEST_CASE("FidoconfigParser: keywords are case-insensitive", "[fidoconfig]") {
    const auto areas = FidoconfigParser::parseText(
        "ECHOAREA a.one /ftn/one -b squish\n"
        "echoarea a.two /ftn/two -b squish\n"
        "EchoArea a.three /ftn/three -b squish\n");

    CHECK(areas.size() == 3);
}

TEST_CASE("FidoconfigParser: an unstated base type stays Unknown", "[fidoconfig]") {
    const auto areas = FidoconfigParser::parseText("EchoArea a.one /ftn/one\n");

    REQUIRE(areas.size() == 1);
    // The type is worked out from the files later, in SmapiMsgBase::probeType().
    CHECK(areas[0].type == MsgBaseType::Unknown);
}

TEST_CASE("FidoconfigParser throws on a missing file", "[fidoconfig]") {
    FidoconfigParser parser("/nonexistent/path/areas");
    CHECK_THROWS(parser.loadAreas());
}
