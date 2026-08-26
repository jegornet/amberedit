#include "app/run_program.hpp"

#include <doctest/doctest.h>

#include <sys/stat.h>

#include <fstream>
#include <sstream>
#include <string>

#include "temp_dir.hpp"
#include "test_strings.hpp"

using amberedit::app::runProgram;
using amberedit::test::contains;
using amberedit::test::errorOf;
using amberedit::test::TempDir;

namespace {

/// A program that writes its arguments to `output`, one to a line: what is
/// asserted about an exec is what the words on the way in came out as.
std::string aProgramWriting(const TempDir& dir, const std::string& output) {
    const std::string path = dir.path("say");
    std::ofstream file(path);
    file << "#!/bin/sh\nfor word in \"$@\"; do printf '%s\\n' \"$word\"; done > \""
         << output << "\"\n";
    file.close();
    REQUIRE(::chmod(path.c_str(), 0755) == 0);
    return path;
}

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
    const std::string program = aProgramWriting(dir, said);

    // Nothing goes through a shell, so an argument is one argument however many
    // spaces it holds and nothing in it is expanded.
    CHECK(runProgram({program, "two words", "$HOME", "*"}).has_value());
    CHECK(contentsOf(said) == "two words\n$HOME\n*\n");
}

TEST_CASE("A program is looked for on $PATH [runprogram]") {
    // `true` takes whatever it is handed and says it went well, and every Unix
    // has one somewhere on the path — which is the whole of what is asserted
    // here: the name was not a path and was found all the same.
    CHECK(runProgram({"true"}).has_value());
}

TEST_CASE("What a program exited with is nobody's business here [runprogram]") {
    // The failure is a program that did not start, and that alone: what it made
    // of what it was given is between it and the user.
    CHECK(runProgram({"false"}).has_value());
}

TEST_CASE("A program that cannot be run is said so, by name [runprogram]") {
    const std::string error = errorOf(runProgram({"amberedit-no-such-program"}));
    CHECK_MESSAGE(contains(error, "amberedit-no-such-program"), error);
}

TEST_CASE("An empty command runs nothing and is not a failure [runprogram]") {
    CHECK(runProgram({}).has_value());
}
