#include <doctest/doctest.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "temp_dir.hpp"
#include "ui/error_log.hpp"

using amberedit::test::TempDir;

namespace error_log = amberedit::ui::error_log;

namespace {

/// The lines the log holds, or none where the file was never made.
std::vector<std::string> linesOf(const std::string& path) {
    std::ifstream in(path);
    std::vector<std::string> lines;
    for (std::string line; std::getline(in, line);) lines.push_back(line);
    return lines;
}

/// Every test here sets the path, so putting it back is what keeps one from
/// leaving the log on for the next.
struct LogOff {
    ~LogOff() { error_log::open(""); }
};

}  // namespace

TEST_CASE("A log that was never named writes nowhere [error_log]") {
    const LogOff off;
    const TempDir dir;
    const std::string path = dir.path("amberr.log");

    error_log::open("");
    CHECK(error_log::path().empty());
    error_log::write("reader", "something went wrong");

    // Not "an empty file": the file is never made at all, which is what says a
    // session that logs nothing leaves nothing behind.
    CHECK(linesOf(path).empty());
    CHECK_FALSE(std::ifstream(path).good());
}

TEST_CASE("Each error is one line, stamped and named [error_log]") {
    const LogOff off;
    const TempDir dir;
    const std::string path = dir.path("amberr.log");
    error_log::open(path);
    CHECK(error_log::path() == path);

    error_log::write("reader", "F9: base is not open");
    error_log::write("message list", "drawing the screen: bad header");

    const std::vector<std::string> lines = linesOf(path);
    REQUIRE(lines.size() == 2);
    CHECK(lines[0].find("reader: F9: base is not open") != std::string::npos);
    CHECK(lines[1].find("message list: drawing the screen: bad header") !=
          std::string::npos);

    // The stamp sorts, which is the whole of what is asked of it: four digits, a
    // dash, and a time behind it.
    CHECK(lines[0].size() > 19);
    CHECK(lines[0][4] == '-');
    CHECK(lines[0][7] == '-');
    CHECK(lines[0][13] == ':');
}

TEST_CASE(
    "A second session adds to the log rather than starting it again "
    "[error_log]") {
    const LogOff off;
    const TempDir dir;
    const std::string path = dir.path("amberr.log");

    error_log::open(path);
    error_log::write("reader", "first");
    error_log::open("");
    error_log::open(path);
    error_log::write("reader", "second");

    const std::vector<std::string> lines = linesOf(path);
    REQUIRE(lines.size() == 2);
    CHECK(lines[0].find("first") != std::string::npos);
    CHECK(lines[1].find("second") != std::string::npos);
}

TEST_CASE("An error that speaks in paragraphs still takes one line [error_log]") {
    const LogOff off;
    const TempDir dir;
    const std::string path = dir.path("amberr.log");
    error_log::open(path);

    error_log::write("compose", "cannot write:\nno room on the device\r\ngiving up");

    const std::vector<std::string> lines = linesOf(path);
    REQUIRE(lines.size() == 1);
    // One space for the newline, two for the CR and the newline of the pair —
    // every break becomes a blank, and none of them is dropped.
    CHECK(lines[0].find("cannot write: no room on the device  giving up") !=
          std::string::npos);
}

TEST_CASE("A log that cannot be written is not an error of its own [error_log]") {
    const LogOff off;
    const TempDir dir;

    // A directory that does not exist: nothing here makes one, the setting
    // naming a file the user is expected to have somewhere to put.
    error_log::open(dir.path("nowhere/amberr.log"));
    error_log::write("reader", "something went wrong");

    CHECK(linesOf(dir.path("nowhere/amberr.log")).empty());
}
