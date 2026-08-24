#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "config/cfg_file.hpp"
#include "test_strings.hpp"

using amberedit::config::CfgEntry;
using amberedit::config::parseCfg;
using amberedit::test::contains;
using amberedit::test::errorOf;
using amberedit::test::valueOf;

namespace {

/// The parsed file written back out as "key=value|value", which is short enough
/// to compare a whole file against in one line.
std::string flatten(const std::vector<CfgEntry>& entries) {
    std::string out;
    for (const auto& entry : entries) {
        if (!out.empty()) out += ' ';
        out += entry.key + '=';
        for (size_t i = 0; i < entry.values.size(); ++i) {
            if (i != 0) out += '|';
            out += entry.values[i];
        }
    }
    return out;
}

std::string parsed(const std::string& text) {
    return flatten(valueOf(parseCfg(text, "t.cfg")));
}

/// Why the same text would not parse. Empty means it parsed.
std::string parseError(const std::string& text) {
    return errorOf(parseCfg(text, "t.cfg"));
}

}  // namespace

TEST_CASE("A config line is a key and the values after it [cfg_file]") {
    CHECK(parsed("name Vasya Pupkin") == "name=Vasya|Pupkin");
    CHECK(parsed("aka 2:5020/9999.1\naka 192:168/2") ==
          "aka=2:5020/9999.1 aka=192:168/2");
    CHECK(parsed("back_button") == "back_button=");
}

TEST_CASE("Keys are case-insensitive and values are not [cfg_file]") {
    // The tosser configs beside ours are all read this way, and a path or a
    // name is the one thing that cannot be folded.
    CHECK(parsed("Quote_String Ab") == "quote_string=Ab");
}

TEST_CASE("Blank lines, comments and stray whitespace are dropped [cfg_file]") {
    CHECK(parsed("\n   \n# a comment\n\tname  Vasya  \t\n").find("name=Vasya") == 0);
    CHECK(parsed("name Vasya # our man") == "name=Vasya");
}

TEST_CASE("Quotes are what a value with spaces of its own is written in "
          "[cfg_file]") {
    CHECK(parsed("quote_string \" FL> \"") == "quote_string= FL> ");
    CHECK(parsed("origin \"\"") == "origin=");
    CHECK(parsed("origin \"A BBS  somewhere\"") == "origin=A BBS  somewhere");
    // A quote may open anywhere in a word, which is how a value may hold one
    // half that needs quoting and one that does not.
    CHECK(parsed("theme ~/\"my themes\"/night.cfg") == "theme=~/my themes/night.cfg");
}

TEST_CASE("A '#' is a comment where a word begins and text where it does not "
          "[cfg_file]") {
    CHECK(parsed("theme a#b.cfg") == "theme=a#b.cfg");
    CHECK(parsed("origin \"# 1 BBS\"") == "origin=# 1 BBS");
    CHECK(parsed("origin A BBS # and a comment") == "origin=A|BBS");
}

TEST_CASE("A quote that is never closed is refused [cfg_file]") {
    // Guessing where it ended would silently take the rest of the line, and a
    // config is read once at startup where a mistake is cheap to report.
    const std::string error = parseError("origin \"A BBS");
    CHECK_MESSAGE(contains(error, "t.cfg:1: a quoted value is never closed"), error);
}

TEST_CASE("What a toml config left behind is named for what it is [cfg_file]") {
    const std::string error = parseError("[general]\ntosser_config a");
    CHECK_MESSAGE(contains(error, "[section]"), error);
    const std::string error2 = parseError("tosser_config = \"a\"");
    CHECK_MESSAGE(contains(error2, "old toml spelling"), error2);
}

TEST_CASE("An entry says which line it came from [cfg_file]") {
    const auto entries = valueOf(parseCfg("# a comment\n\nname Vasya\n", "t.cfg"));
    REQUIRE(entries.size() == 1);
    CHECK(entries.front().line == 3);
    const std::string error = entries.front().fail("no good").value()->message();
    CHECK_MESSAGE(contains(error, "t.cfg:3: no good"), error);
}

TEST_CASE("Values are read as the kind the setting wants [cfg_file]") {
    const auto only = [](const std::string& line) {
        return valueOf(parseCfg(line, "t.cfg")).front();
    };

    CHECK(valueOf(only("quote_margin 78").number()) == 78);
    CHECK(valueOf(only("lastread_user -1").number()) == -1);
    CHECK(valueOf(only("quote_margin 78").numberIn(20, 255)) == 78);
    CHECK(valueOf(only("back_button on").flag()));
    CHECK_FALSE(valueOf(only("back_button OFF").flag()));

    const std::string error = errorOf(only("quote_margin 7x").number());
    CHECK_MESSAGE(contains(error, "whole number"), error);
    const std::string error2 = errorOf(only("quote_margin 300").numberIn(20, 255));
    CHECK_MESSAGE(contains(error2, "between 20 and 255"), error2);
    const std::string error3 = errorOf(only("back_button 1").flag());
    CHECK_MESSAGE(contains(error3, "on or off"), error3);
    const std::string error4 = errorOf(only("quote_margin").number());
    CHECK_MESSAGE(contains(error4, "exactly one value"), error4);
    const std::string error5 = errorOf(only("quote_margin 20 30").number());
    CHECK_MESSAGE(contains(error5, "exactly one value"), error5);
    const std::string error6 = errorOf(only("name").text());
    CHECK_MESSAGE(contains(error6, "needs a value"), error6);
}
