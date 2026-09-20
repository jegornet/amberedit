#include <doctest/doctest.h>

#include <filesystem>
#include <string>

#include "sys/program.hpp"

// An answer at all is what everything asking this needs. `tests/test_programs.hpp`
// looks for the stub program beside the test binary, and i18n looks for the
// message catalogs beside the installed one when the path compiled in is not
// where the tree ended up. An empty path is what a system whose branch is
// missing hands back — FreeBSD did, reading a `/proc/self/exe` that is not
// there on a machine mounting no procfs — and both of those lookups then have
// nowhere to look and say nothing about it.
TEST_CASE("The program's own path comes back, and is this binary [program]") {
    const std::filesystem::path program = amberedit::sys::executablePath();

    REQUIRE_FALSE(program.empty());
    CHECK(program.is_absolute());
    CHECK(std::filesystem::exists(program));
    CHECK(program.stem().string() == "amberedit_tests");
}
