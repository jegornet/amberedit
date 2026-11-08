#include "app/run_program.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "temp_dir.hpp"
#include "test_programs.hpp"
#include "test_strings.hpp"

using amberedit::app::kOutputLimit;
using amberedit::app::outputLines;
using amberedit::app::runProgram;
using amberedit::app::runProgramForOutput;
using amberedit::app::runProgramWithoutTerminal;
using amberedit::app::startProgram;
using amberedit::test::contains;
using amberedit::test::errorOf;
using amberedit::test::TempDir;

namespace {

std::string contentsOf(const std::string& path) {
    std::ifstream file(path);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

/// Waits for a program left running to have written `path`, for as long as a
/// slow machine could want and no longer.
bool appears(const std::string& path) {
    for (int tries = 0; tries < 500; ++tries) {
        std::error_code ec;
        if (std::filesystem::exists(path, ec) && !contentsOf(path).empty()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

}  // namespace

TEST_CASE("A program is run with the words it was given [runprogram]") {
    TempDir dir;
    const std::string said = dir.path("said");

    // Nothing goes through a shell, so an argument is one argument however many
    // spaces it holds and nothing in it is expanded. The helper writes what
    // reached its own `argv`, which is the thing being asserted.
    CHECK(runProgram({amberedit::test::stubProgram(), "args", said, "two words", "$HOME",
                      "*"})
              .has_value());
    CHECK(contentsOf(said) == "two words\n$HOME\n*\n");
}

TEST_CASE("A program is looked for on $PATH [runprogram]") {
    // The whole of what is asserted here: the name was not a path and was found
    // all the same. Which name that is, is the platform's to say.
    CHECK(runProgram(amberedit::test::aProgramOnThePath()).has_value());
}

TEST_CASE("What a program exited with is nobody's business here [runprogram]") {
    // The failure is a program that did not start, and that alone: what it made
    // of what it was given is between it and the user.
    CHECK(runProgram(amberedit::test::aFailingProgramOnThePath()).has_value());
}

TEST_CASE("A program that cannot be run is said so, by name [runprogram]") {
    const std::string error = errorOf(runProgram({"amberedit-no-such-program"}));
    CHECK_MESSAGE(contains(error, "amberedit-no-such-program"), error);
}

TEST_CASE("An empty command runs nothing and is not a failure [runprogram]") {
    CHECK(runProgram({}).has_value());
}

TEST_CASE(
    "A program run without the terminal is waited for and given none of it "
    "[runprogram]") {
    TempDir dir;
    const std::string said = dir.path("said");
    // Waited for: the file is there the moment the call comes back.
    CHECK(runProgramWithoutTerminal(
              {amberedit::test::stubProgram(), "args", said, "two words"})
              .has_value());
    CHECK(contentsOf(said) == "two words\n");

    // Its standard input is the null device and not the terminal: a program
    // reading it to the end gets nothing at once, where the test's own input
    // would have it wait for whoever ran the tests.
    const std::string read = dir.path("read");
    CHECK(runProgramWithoutTerminal({amberedit::test::stubProgram(), "stdin", read})
              .has_value());
    CHECK(std::filesystem::exists(read));
    CHECK(contentsOf(read).empty());

    CHECK(runProgramWithoutTerminal(amberedit::test::aFailingProgramOnThePath())
              .has_value());
    const std::string error =
        errorOf(runProgramWithoutTerminal({"amberedit-no-such-program"}));
    CHECK_MESSAGE(contains(error, "amberedit-no-such-program"), error);
}

TEST_CASE("A detached program is started and left running [runprogram]") {
    TempDir dir;
    const std::string said = dir.path("said");
    CHECK(startProgram({amberedit::test::stubProgram(), "args", said, "two words"})
              .has_value());
    // Not waited for, so what it writes turns up when it turns up.
    REQUIRE(appears(said));
    CHECK(contentsOf(said) == "two words\n");

    // Not starting is still said, by name: that much is known before the call
    // comes back.
    const std::string error = errorOf(startProgram({"amberedit-no-such-program"}));
    CHECK_MESSAGE(contains(error, "amberedit-no-such-program"), error);
    CHECK(startProgram({}).has_value());
}

TEST_CASE("A program run for its output hands back both of its streams [runprogram]") {
    // Standard output and standard error in one, in the order written — and
    // what it exited with is nobody's business here either: this one says 3.
    const auto printed = runProgramForOutput(
        {amberedit::test::stubProgram(), "both", "scanned 12\\n", "no outbound\\n"});
    REQUIRE(printed.has_value());
    CHECK(printed->bytes == "scanned 12\nno outbound\n");
    CHECK_FALSE(printed->cut);

    const std::string error = errorOf(runProgramForOutput({"amberedit-no-such-program"}));
    CHECK_MESSAGE(contains(error, "amberedit-no-such-program"), error);
}

TEST_CASE("Only the front of a great deal of output is kept [runprogram]") {
    // Read to the end all the same: a program left blocked on a pipe nobody
    // reads would never come back.
    const auto printed =
        runProgramForOutput({amberedit::test::stubProgram(), "many", "40000",
                             "0123456789abcdef0123456789abcde\\n"});
    REQUIRE(printed.has_value());
    CHECK(printed->cut);
    CHECK(printed->bytes.size() == kOutputLimit);
}

TEST_CASE("Output is shown as a terminal would have shown it [runprogram]") {
    // A line feed at the end is not a blank line under the output.
    CHECK(outputLines("one\ntwo\n\n", "UTF-8") == std::vector<std::string>{"one", "two"});
    // DOS line endings, a counter rewritten in place, colors and a tab.
    CHECK(outputLines("a\r\n10%\r55%\r100%\n\x1b[1;31mred\x1b[0m\tx\n", "UTF-8") ==
          std::vector<std::string>{"a", "100%", "red     x"});
    // A title set through OSC, ended by BEL, and a stray ESC at the very end.
    CHECK(outputLines("\x1b]0;title\aok\x1b", "UTF-8") == std::vector<std::string>{"ok"});
    // Decoded out of the charset it was written in.
    CHECK(outputLines("\xef\xf0\xe8\xe2\xe5\xf2", "CP1251") ==
          std::vector<std::string>{"привет"});
    CHECK(outputLines("", "UTF-8").empty());
}
