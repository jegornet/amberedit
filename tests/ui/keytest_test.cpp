#include "ui/keytest.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "temp_dir.hpp"
#include "test_paths.hpp"
#include "test_strings.hpp"

using amberedit::test::contains;
using amberedit::test::errorOf;
using amberedit::ui::keytest::Checker;
using amberedit::ui::keytest::escapeBytes;
using amberedit::ui::keytest::keys;
using amberedit::ui::keytest::nameOf;
using amberedit::ui::keytest::readLimits;
using amberedit::ui::keytest::unescapeBytes;
namespace fs = std::filesystem;

TEST_CASE("bytes are written so that a line holds them, and read back [keys]") {
    const std::string bytes = "\x1b[1;5D \\-\x7f";
    CHECK(escapeBytes(bytes) == "\\e[1;5D\\x20\\x5c\\x2d\\x7f");
    CHECK(unescapeBytes(escapeBytes(bytes)) == bytes);
    // A lone '-' is how a file says nothing arrived, so the byte never is one.
    CHECK(escapeBytes("-") == "\\x2d");
}

TEST_CASE("every key a run asks for has a name of its own [keys]") {
    std::vector<std::string> names;
    for (const auto& key : keys()) names.push_back(nameOf(key));
    std::vector<std::string> sorted = names;
    std::sort(sorted.begin(), sorted.end());
    CHECK(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end());
    for (const char* expected : {"Shift-Tab", "Space", "Shift-Space", "Ctrl-Home",
                                 "Alt-F12", "Ctrl-A", "Alt-Z", "Shift-Ins"}) {
        CHECK_MESSAGE(std::find(names.begin(), names.end(), expected) != names.end(),
                      expected);
    }
}

TEST_CASE("a limit names a key, its bytes and what they are read as [keys]") {
    amberedit::test::TempDir dir;
    const std::string path = dir.path("limits.lst");
    std::ofstream(path) << "# comment\n\nCtrl-H  \\x08  Backspace  # why\n"
                           "Alt-Up  \\e\\e[A  Esc Up\n";
    const auto limits = readLimits(path);
    REQUIRE(limits.has_value());
    REQUIRE(limits->size() == 2);
    CHECK((*limits)[0].bytes == "\x08");
    CHECK((*limits)[1].readAs == "Esc Up");

    std::ofstream(path) << "Ctrl-H \\x08\n";
    const auto broken = readLimits(path);
    CHECK_MESSAGE(contains(errorOf(broken), "limits.lst:1"), errorOf(broken));
}

#ifndef _WIN32

// The bytes real terminals sent, taken by `amberedit --keytest` and kept under
// testdata/keys/, played back into the Terminal over a pty. A key read as
// anything but itself fails here unless testdata/keys/limits.lst says the
// terminal sent bytes some other key is sent as too. A file whose TERM this
// machine has no terminfo for is passed over: its bytes mean what that entry
// says they mean, and without it there is nothing to play them against.
TEST_CASE("keys taken from real terminals read as the keys they were taken for "
          "[keys][pty]") {
    auto limits = readLimits(amberedit::test::projectPath("testdata/keys/limits.lst"));
    REQUIRE_MESSAGE(limits.has_value(), errorOf(limits));

    std::vector<std::string> files;
    for (const auto& entry :
         fs::directory_iterator(amberedit::test::projectPath("testdata/keys"))) {
        if (entry.path().extension() == ".txt") files.push_back(entry.path().string());
    }
    std::sort(files.begin(), files.end());
    REQUIRE(!files.empty());

    Checker checker(std::move(*limits));
    std::size_t played = 0;
    for (const std::string& path : files) {
        const auto result = checker.check(path);
        REQUIRE_MESSAGE(result.has_value(), errorOf(result));
        if (!result->skipped.empty()) {
            MESSAGE(fs::path(path).filename().string() << ": " << result->skipped);
            continue;
        }
        ++played;
        for (const auto& wrong : result->wrong) {
            FAIL_CHECK(fs::path(path).filename().string()
                       << ": " << wrong.key << " " << escapeBytes(wrong.bytes)
                       << " read as '" << wrong.readAs << "'");
        }
    }
    MESSAGE(played << " of " << files.size() << " files played back");
}

#endif
