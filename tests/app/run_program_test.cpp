#include "app/run_program.hpp"

#include <doctest/doctest.h>

#include <fstream>
#include <sstream>
#include <string>

#include "temp_dir.hpp"
#include "test_programs.hpp"
#include "test_strings.hpp"

using amberedit::app::runProgram;
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
