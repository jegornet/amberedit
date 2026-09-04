#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

#include "config/fidoconfig_parser.hpp"
#include "config/path_map.hpp"
#include "temp_dir.hpp"
#include "test_paths.hpp"
#include "test_strings.hpp"

using amberedit::config::FidoconfigParser;
using amberedit::config::PathMap;
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

TEST_CASE("FidoconfigParser parses testdata/tossers/areas [fidoconfig]") {
    FidoconfigParser parser(amberedit::test::projectPath("testdata/tossers/areas"));
    const auto areas = amberedit::test::valueOf(parser.loadAreas());

    REQUIRE(areas.size() == 6);

    SUBCASE("netmailarea of type msg") {
        const auto* netmail = findArea(areas, "NETMAIL");
        REQUIRE(netmail != nullptr);
        CHECK(netmail->kind == AreaKind::Netmail);
        CHECK(netmail->type == MsgBaseType::Sdm);
        CHECK(netmail->path == "/Users/egor/ftn/msg/netmail");
        CHECK(netmail->group == "A");
    }

    SUBCASE("localarea of type squish") {
        const auto* personal = findArea(areas, "PERSONAL.MAIL");
        REQUIRE(personal != nullptr);
        CHECK(personal->kind == AreaKind::Local);
        CHECK(personal->type == MsgBaseType::Squish);
        CHECK(personal->path == "/Users/egor/ftn/msg/personal.mail");
        CHECK(personal->group == "A");
    }

    SUBCASE("badarea and dupearea are recognised by keyword") {
        REQUIRE(findArea(areas, "BAD") != nullptr);
        REQUIRE(findArea(areas, "DUPES") != nullptr);
        CHECK(findArea(areas, "BAD")->kind == AreaKind::Bad);
        CHECK(findArea(areas, "DUPES")->kind == AreaKind::Dupe);
        CHECK(findArea(areas, "BAD")->group == "B");
    }

    SUBCASE("-a gives the AKA, the bare addresses that follow are links") {
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

TEST_CASE("FidoconfigParser does not mistake option arguments for links "
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

TEST_CASE("A boolean flag does not swallow the option after it [fidoconfig]") {
    // husky's `-pack` clears a flag and takes no argument. Treating it as if it
    // did would eat `-b`, and the area would come back with no base type —
    // which is exactly what this parser used to do.
    const auto areas = FidoconfigParser::parseText(
        "EchoArea ru.linux /ftn/ru.linux -pack -b squish -g A\n");

    REQUIRE(areas.size() == 1);
    CHECK(areas[0].type == MsgBaseType::Squish);
    CHECK(areas[0].group == "A");
}

TEST_CASE("An option husky does not have is treated as a flag [fidoconfig]") {
    // The value list must hold value-taking options and nothing else. `-charset`
    // was invented by this parser and is gone; if it ever came back as a value
    // option, `-b` would be its argument and the type would be lost again.
    const auto areas = FidoconfigParser::parseText(
        "EchoArea ru.linux /ftn/ru.linux -charset CP866 -b jam\n");

    REQUIRE(areas.size() == 1);
    CHECK(areas[0].type == MsgBaseType::Jam);
}

TEST_CASE("FidoconfigParser parses a quoted description [fidoconfig]") {
    const auto areas = FidoconfigParser::parseText(
        "EchoArea ru.linux /ftn/ru.linux -b jam -d \"Linux и всё вокруг\"\n");

    REQUIRE(areas.size() == 1);
    CHECK(areas[0].type == MsgBaseType::Jam);
    CHECK(areas[0].description == "Linux и всё вокруг");
}

TEST_CASE("FidoconfigParser reads the area group [fidoconfig]") {
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

TEST_CASE("FidoconfigParser understands passthrough [fidoconfig]") {
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

TEST_CASE("FidoconfigParser leaves the AKA unset when -a is absent [fidoconfig]") {
    const auto areas =
        FidoconfigParser::parseText("EchoArea a.one /ftn/one -b squish 2:5020/715\n");

    REQUIRE(areas.size() == 1);
    CHECK_FALSE(areas[0].address.isValid());
    REQUIRE(areas[0].links.size() == 1);
    CHECK(areas[0].links[0].toString() == "2:5020/715");
}

TEST_CASE("FidoconfigParser skips comments and unrelated directives [fidoconfig]") {
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

TEST_CASE("FidoconfigParser: keywords are case-insensitive [fidoconfig]") {
    const auto areas = FidoconfigParser::parseText(
        "ECHOAREA a.one /ftn/one -b squish\n"
        "echoarea a.two /ftn/two -b squish\n"
        "EchoArea a.three /ftn/three -b squish\n");

    CHECK(areas.size() == 3);
}

TEST_CASE("FidoconfigParser: an unstated base type stays Unknown [fidoconfig]") {
    const auto areas = FidoconfigParser::parseText("EchoArea a.one /ftn/one\n");

    REQUIRE(areas.size() == 1);
    // The type is worked out from the files later, in SmapiMsgBase::probeType().
    CHECK(areas[0].type == MsgBaseType::Unknown);
}

TEST_CASE("FidoconfigParser throws on a missing file [fidoconfig]") {
    FidoconfigParser parser("/nonexistent/path/areas");
    CHECK_FALSE(parser.loadAreas().has_value());
}

TEST_CASE("echoareadefaults states what the areas below it inherit [fidoconfig]") {
    const auto areas = FidoconfigParser::parseText(
        "EchoAreaDefaults -b squish -g F -d \"Fidonet echo\" -a 2:382/736 2:6000/9999\n"
        "EchoArea a.one /ftn/one\n"
        "EchoArea a.two /ftn/two -b jam -g L -d \"its own\" -a 2:6000/9999\n"
        "netmailarea NETMAIL /ftn/netmail\n");

    REQUIRE(areas.size() == 3);

    SUBCASE("an area that states nothing takes all of it") {
        CHECK(areas[0].type == MsgBaseType::Squish);
        CHECK(areas[0].group == "F");
        CHECK(areas[0].description == "Fidonet echo");
        CHECK(areas[0].address.toString() == "2:382/736");
        REQUIRE(areas[0].links.size() == 1);
        CHECK(areas[0].links[0].toString() == "2:6000/9999");
        // The path is the one thing the defaults cannot state.
        CHECK(areas[0].path == "/ftn/one");
    }

    SUBCASE("an option on the area line overrules the default") {
        CHECK(areas[1].type == MsgBaseType::Jam);
        CHECK(areas[1].group == "L");
        CHECK(areas[1].description == "its own");
        CHECK(areas[1].address.toString() == "2:6000/9999");
    }

    SUBCASE("netmail is not echomail and inherits nothing") {
        CHECK(areas[2].type == MsgBaseType::Unknown);
        CHECK(areas[2].group.empty());
        CHECK(areas[2].description.empty());
        CHECK_FALSE(areas[2].address.isValid());
        CHECK(areas[2].links.empty());
    }
}

TEST_CASE("echoareadefaults also speaks for local, bad and dupe areas [fidoconfig]") {
    const auto areas = FidoconfigParser::parseText(
        "EchoAreaDefaults -b squish -g F\n"
        "LocalArea PERSONAL /ftn/personal\n"
        "BadArea BAD /ftn/bad\n"
        "DupeArea DUPES /ftn/dupes\n");

    REQUIRE(areas.size() == 3);
    for (const auto& area : areas) {
        CHECK(area.type == MsgBaseType::Squish);
        CHECK(area.group == "F");
    }
}

TEST_CASE("the links of the defaults come before the area's own [fidoconfig]") {
    const auto areas = FidoconfigParser::parseText(
        "EchoAreaDefaults 2:382/736\n"
        "EchoArea a.one /ftn/one 2:6000/9999\n");

    REQUIRE(areas.size() == 1);
    REQUIRE(areas[0].links.size() == 2);
    CHECK(areas[0].links[0].toString() == "2:382/736");
    CHECK(areas[0].links[1].toString() == "2:6000/9999");
}

TEST_CASE("a second echoareadefaults replaces the first whole [fidoconfig]") {
    const auto areas = FidoconfigParser::parseText(
        "EchoAreaDefaults -b squish -g F -d \"the first\"\n"
        "EchoArea a.one /ftn/one\n"
        "EchoAreaDefaults -g L\n"
        "EchoArea a.two /ftn/two\n"
        "EchoAreaDefaults OFF\n"
        "EchoArea a.three /ftn/three\n"
        "EchoAreaDefaults\n"
        "EchoArea a.four /ftn/four\n");

    REQUIRE(areas.size() == 4);
    CHECK(areas[0].type == MsgBaseType::Squish);
    CHECK(areas[0].description == "the first");

    // The group is all the second statement says, so the type and description
    // of the first are gone rather than kept.
    CHECK(areas[1].group == "L");
    CHECK(areas[1].type == MsgBaseType::Unknown);
    CHECK(areas[1].description.empty());

    // `OFF` is a word husky writes for readability; an empty statement means
    // the same thing.
    CHECK(areas[2].group.empty());
    CHECK(areas[2].type == MsgBaseType::Unknown);
    CHECK(areas[3].group.empty());
    CHECK(areas[3].type == MsgBaseType::Unknown);
}

TEST_CASE("passthrough defaults let the area leave the path out [fidoconfig]") {
    const auto areas = FidoconfigParser::parseText(
        "EchoAreaDefaults passthrough -g F\n"
        "EchoArea a.one 2:382/736\n"
        "EchoArea a.two -g L 2:6000/9999\n"
        "EchoArea a.three /ftn/three -b squish\n");

    REQUIRE(areas.size() == 3);

    // A token holding a path separator is a path whatever the defaults say,
    // which is how husky itself tells the two apart — and why an address in
    // that position reads as one rather than as a link.
    CHECK(areas[0].path == "2:382/736");
    CHECK_FALSE(areas[0].isPassthrough());
    CHECK(areas[0].group == "F");

    CHECK(areas[1].isPassthrough());
    CHECK(areas[1].path.empty());
    CHECK(areas[1].group == "L");
    REQUIRE(areas[1].links.size() == 1);
    CHECK(areas[1].links[0].toString() == "2:6000/9999");

    // An area that names a base of its own is not passthrough for having
    // inherited it.
    CHECK_FALSE(areas[2].isPassthrough());
    CHECK(areas[2].path == "/ftn/three");
    CHECK(areas[2].type == MsgBaseType::Squish);
}

TEST_CASE("set defines what [name] stands for [fidoconfig]") {
    const auto areas = FidoconfigParser::parseText(
        "set base=/ftn/msg\n"
        "set Tag = ru.linux\n"
        "EchoArea [tag] [base]/ru.linux -b squish\n");

    REQUIRE(areas.size() == 1);
    // The name is read without regard to case, the value kept as written.
    CHECK(areas[0].tag == "ru.linux");
    CHECK(areas[0].path == "/ftn/msg/ru.linux");
}

TEST_CASE("set takes a quoted value and a value naming another [fidoconfig]") {
    const auto areas = FidoconfigParser::parseText(
        "set root=/ftn\n"
        "set base=[root]/msg\n"
        "set what=\"Linux and everything around it\"\n"
        "EchoArea ru.linux [base]/ru.linux -d \"[what]\"\n");

    REQUIRE(areas.size() == 1);
    // A line is expanded before it is read, so a definition may use what the
    // definitions above it say.
    CHECK(areas[0].path == "/ftn/msg/ru.linux");
    CHECK(areas[0].description == "Linux and everything around it");
}

TEST_CASE("a variable nobody defined expands to nothing [fidoconfig]") {
    const auto areas = FidoconfigParser::parseText(
        "set base=/ftn/msg\n"
        "EchoArea a.one [base]/one\n"
        "set base=\n"
        "EchoArea a.two [base]/two\n");

    REQUIRE(areas.size() == 2);
    CHECK(areas[0].path == "/ftn/msg/one");
    // An empty definition forgets the variable rather than defining it empty.
    CHECK(areas[1].path == "/two");
}

TEST_CASE("a variable writes a literal bracket [fidoconfig]") {
    const auto areas = FidoconfigParser::parseText("EchoArea a.one /ftn/[[]one\n");

    REQUIRE(areas.size() == 1);
    // The substitution is not looked at again, so the bracket it puts there
    // starts nothing.
    CHECK(areas[0].path == "/ftn/[one");
}

TEST_CASE("an unclosed bracket is a bracket [fidoconfig]") {
    const auto areas = FidoconfigParser::parseText("EchoArea a.one /ftn/[one\n");

    REQUIRE(areas.size() == 1);
    CHECK(areas[0].path == "/ftn/[one");
}

TEST_CASE("what an include leaves behind holds for the file below it [fidoconfig]") {
    // Both a variable and the defaults are the whole parse's, not the file's:
    // a config that keeps its common settings in an included file and its areas
    // in the one that includes it is the ordinary way of writing one.
    const amberedit::test::TempDir dir;
    const std::string common = dir.path("common");
    const std::string config = dir.path("config");

    const auto write = [](const std::string& path, const std::string& text) {
        std::ofstream out(path);
        out << text;
    };
    write(common,
          "set base=/ftn/msg\n"
          "EchoAreaDefaults -b squish -g F\n"
          "EchoArea a.one [base]/one\n");
    write(config,
          "include common\n"
          "EchoArea a.two [base]/two\n");

    FidoconfigParser parser(config);
    const auto areas = amberedit::test::valueOf(parser.loadAreas());

    REQUIRE(areas.size() == 2);
    CHECK(areas[0].path == "/ftn/msg/one");
    CHECK(areas[1].path == "/ftn/msg/two");
    CHECK(areas[1].type == MsgBaseType::Squish);
    CHECK(areas[1].group == "F");
}

TEST_CASE("map_path rewrites an area's path, after the variables [fidoconfig]") {
    PathMap paths;
    paths.add("c:\\fido", "/mnt/fido");

    const auto areas = FidoconfigParser::parseText(
        "set base=c:\\fido\\msgbase\n"
        "EchoArea a.one [base]\\one -b squish\n"
        "EchoArea a.two /home/ftn/two\n"
        "EchoArea a.three passthrough\n",
        paths);

    REQUIRE(areas.size() == 3);
    // The variable is expanded first, so what a rule is asked about is the path
    // the line means and not the text it is written as.
    CHECK(areas[0].path == "/mnt/fido/msgbase/one");
    // A path no rule covers is opened as it stands.
    CHECK(areas[1].path == "/home/ftn/two");
    // And an area with no base of its own is left with none.
    CHECK(areas[2].path.empty());
}

TEST_CASE("map_path reaches the file an include names [fidoconfig]") {
    // The path an include names is the tosser's too, and one it writes as
    // `c:\fido\etc\common` is not a path this machine has a root for: without
    // the rule the file is simply not found and the areas in it are lost.
    const amberedit::test::TempDir dir;
    const std::string common = dir.path("common");
    const std::string config = dir.path("config");

    const auto write = [](const std::string& path, const std::string& text) {
        std::ofstream out(path);
        out << text;
    };
    write(common, "EchoArea a.one c:\\fido\\msgbase\\one\n");
    write(config,
          "include c:\\fido\\etc\\common\n"
          "EchoArea a.two c:\\fido\\msgbase\\two\n");

    PathMap paths;
    paths.add("c:\\fido\\etc", std::filesystem::path(common).parent_path().string());
    paths.add("c:\\fido\\msgbase", "/mnt/fido/msg");

    FidoconfigParser parser(config, paths);
    const auto areas = amberedit::test::valueOf(parser.loadAreas());

    REQUIRE(areas.size() == 2);
    CHECK(areas[0].path == "/mnt/fido/msg/one");
    CHECK(areas[1].path == "/mnt/fido/msg/two");
}
