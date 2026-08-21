#include "echolist/echolist_compiler.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

#include "echolist/echolist_db.hpp"
#include "temp_dir.hpp"
#include "test_paths.hpp"

using namespace amberedit;
namespace fs = std::filesystem;

TEST_CASE("the echolists in testdata compile into one file [echolist]") {
    test::TempDir dir;

    echolist::CompileOptions options;
    options.sources = {{test::projectPath("testdata/echolist/echo50.lst"), "CP866"},
                       {test::projectPath("testdata/echolist/elst2601.zip"), "CP866"}};
    options.dbPath = dir.path("echolist.db");
    options.tempDir = dir.path("tmp");

    std::ostringstream log;
    const auto report = echolist::compileEcholists(options, &log);

    REQUIRE(report.sources.size() == 2);
    CHECK(report.sources[0].problem.empty());
    CHECK(report.sources[0].files == 1);
    CHECK(report.sources[0].archive.empty());
    // The archive's three .na lists, read under the one `echolist` line.
    CHECK(report.sources[1].files == 3);
    CHECK(fs::path(report.sources[1].archive).filename() == "elst2601.zip");

    // 106 echoes out of the .lst and 372 out of the archive's three lists; the
    // two hub lists in it overlap, and where they do the first one named keeps
    // the echo.
    CHECK(report.areas == 360);
    CHECK(report.duplicates == 118);
    CHECK(report.warnings == 0);
    CHECK(report.written);
    CHECK(report.problems.empty());

    const auto db = echolist::EcholistDb::open(options.dbPath);
    CHECK(db.size() == report.areas);
    REQUIRE(db.sources().size() == 2);
    CHECK(db.sources()[0].charset == "CP866");

    // One from the .lst, decoded out of CP866, and one from each of the two
    // .na lists an archive holds.
    CHECK(db.descriptionOf("su.music") == std::optional<std::string>("Музыка!"));
    CHECK(db.descriptionOf("aarp_fraud") ==
          std::optional<std::string>("AARP Fraud Warning Network news"));
    CHECK(db.descriptionOf("FSX_ADS") == std::optional<std::string>("Ads + ANSI Art"));

    // Whatever was unpacked on the way is gone again.
    CHECK(fs::is_empty(options.tempDir));
}

TEST_CASE("an echolist is compiled again only once it has changed [echolist]") {
    test::TempDir dir;
    const std::string list = dir.path("echo.lst");
    {
        std::ofstream out(list);
        out << ",Ru.Linux,Linux,Some Body,2:5020/113,\n";
    }

    echolist::CompileOptions options;
    options.sources = {{list, "UTF-8"}};
    options.dbPath = dir.path("echolist.db");
    options.tempDir = dir.path("tmp");

    CHECK(echolist::echolistNeedsCompiling(options));
    CHECK(echolist::refreshEcholist(options, false, nullptr).written);
    // Nothing has changed, so nothing is done and the report says so.
    CHECK_FALSE(echolist::echolistNeedsCompiling(options));
    CHECK_FALSE(echolist::refreshEcholist(options, false, nullptr).written);
    // `--compile` is what asks for it anyway.
    CHECK(echolist::refreshEcholist(options, true, nullptr).written);

    // A charset corrected on the line is a file to be read again, though the
    // file itself has not moved.
    options.sources[0].charset = "CP866";
    CHECK(echolist::echolistNeedsCompiling(options));
    CHECK(echolist::refreshEcholist(options, false, nullptr).written);

    // And so is a file written over in place.
    {
        std::ofstream out(list, std::ios::app);
        out << ",Ru.Unix,Unix,Some Body,2:5020/113,\n";
    }
    CHECK(echolist::echolistNeedsCompiling(options));
    const auto again = echolist::refreshEcholist(options, false, nullptr);
    CHECK(again.written);
    CHECK(again.areas == 2);
}

TEST_CASE("a wildcard is recompiled when it comes to stand for a newer file "
          "[echolist]") {
    test::TempDir dir;
    const auto write = [&dir](const std::string& name, const std::string& description) {
        std::ofstream out(dir.path(name), std::ios::binary);
        out << ",Ru.Linux," << description << ",Some Body,2:5020/113,\n";
    };
    write("echo2512.lst", "what December said");

    echolist::CompileOptions options;
    options.sources = {{dir.path("echo*.lst"), "UTF-8"}};
    options.dbPath = dir.path("echolist.db");
    options.tempDir = dir.path("tmp");

    CHECK(echolist::refreshEcholist(options, false, nullptr).written);
    CHECK_FALSE(echolist::echolistNeedsCompiling(options));
    {
        const auto db = echolist::EcholistDb::open(options.dbPath);
        CHECK(db.descriptionOf("ru.linux") ==
              std::optional<std::string>("what December said"));
        // The compiled file records the pattern the config wrote and the file it
        // stood for, which is what the next start compares.
        REQUIRE(db.sources().size() == 1);
        CHECK(db.sources()[0].spec == dir.path("echo*.lst"));
        CHECK(fs::path(db.sources()[0].path).filename() == "echo2512.lst");
    }

    // Next month's echolist arrives beside the old one, and the pattern now
    // stands for a different file — which is a change like any other.
    write("echo2601.lst", "what January said");
    CHECK(echolist::echolistNeedsCompiling(options));
    CHECK(echolist::refreshEcholist(options, false, nullptr).written);

    const auto db = echolist::EcholistDb::open(options.dbPath);
    CHECK(db.descriptionOf("ru.linux") ==
          std::optional<std::string>("what January said"));
    CHECK(fs::path(db.sources()[0].path).filename() == "echo2601.lst");
}

TEST_CASE("an echolist that will not read stops nothing [echolist]") {
    test::TempDir dir;

    echolist::CompileOptions options;
    options.sources = {{dir.path("gone.lst"), ""}};
    options.dbPath = dir.path("echolist.db");
    options.tempDir = dir.path("tmp");

    // The whole contract: a missing echolist is a line in `problems`, the
    // compiled file is written anyway, and the state of the file that was not
    // there is written into it — so the next start tries again only once
    // something has appeared.
    const auto report = echolist::compileEcholists(options, nullptr);
    REQUIRE(report.problems.size() == 1);
    CHECK(report.written);
    CHECK(report.areas == 0);
    CHECK_FALSE(echolist::echolistNeedsCompiling(options));

    {
        std::ofstream out(dir.path("gone.lst"));
        out << ",Ru.Linux,Linux,Some Body,2:5020/113,\n";
    }
    CHECK(echolist::echolistNeedsCompiling(options));
}

TEST_CASE("an echolist with nowhere to compile it to is said out loud [echolist]") {
    echolist::CompileOptions options;
    options.sources = {{"/ftn/echolist/echo50.lst", ""}};

    const auto report = echolist::compileEcholists(options, nullptr);
    CHECK_FALSE(report.written);
    REQUIRE(report.problems.size() == 1);
    CHECK(report.problems[0].find("echolist_db") != std::string::npos);

    // A config with no echolist lines at all does nothing and says nothing.
    CHECK_FALSE(echolist::echolistNeedsCompiling({}));
    CHECK_FALSE(echolist::refreshEcholist({}, true, nullptr).written);
}
