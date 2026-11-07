#include "app/clipboard.hpp"

#include <doctest/doctest.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "temp_dir.hpp"
#include "test_programs.hpp"
#include "test_strings.hpp"

using amberedit::app::copyThroughProgram;
using amberedit::app::pastedLines;
using amberedit::app::pasteThroughProgram;
using amberedit::test::contains;
using amberedit::test::errorOf;
using amberedit::test::TempDir;

namespace {

std::string contentsOf(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

}  // namespace

TEST_CASE("Copying hands the text to the program on its standard input [clipboard]") {
    TempDir dir;
    const std::string got = dir.path("got");
    const std::string text = "Привет\nвторая строка";
    REQUIRE(copyThroughProgram({amberedit::test::stubProgram(), "stdin", got}, text)
                .has_value());
    CHECK(contentsOf(got) == text);
}

TEST_CASE("Pasting is what the program writes to its standard output [clipboard]") {
    const auto pasted =
        pasteThroughProgram({amberedit::test::stubProgram(), "stdout", "one\\ntwo"});
    REQUIRE(pasted.has_value());
    CHECK(*pasted == "one\ntwo");
}

TEST_CASE("A clipboard program that will not start is said so, by name [clipboard]") {
    const std::string copied =
        errorOf(copyThroughProgram({"amberedit-no-such-program"}, "text"));
    CHECK_MESSAGE(contains(copied, "amberedit-no-such-program"), copied);
    const std::string pasted =
        errorOf(pasteThroughProgram({"amberedit-no-such-program"}));
    CHECK_MESSAGE(contains(pasted, "amberedit-no-such-program"), pasted);
}

TEST_CASE("Pasted text is held to what a message can carry [clipboard]") {
    // Either line ending, a tab opened out to its stop, the control bytes
    // dropped and a broken byte made whole.
    const std::vector<std::string> lines = pastedLines(
        "a\tb\r\nc\x01"
        "d\x1b"
        "\xff\n");
    REQUIRE(lines.size() == 3);
    CHECK(lines[0] == "a       b");
    CHECK(lines[1] == "cd\xEF\xBF\xBD");
    // The break closing it is a break, kept as the empty line after it.
    CHECK(lines[2].empty());
    CHECK(pastedLines("word").size() == 1);
}
