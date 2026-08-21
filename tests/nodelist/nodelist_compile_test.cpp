#include "nodelist/nodelist_compiler.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "nodelist/nodelist_db.hpp"
#include "temp_dir.hpp"
#include "test_paths.hpp"
#include "test_strings.hpp"

using namespace amberedit;
using amberedit::test::contains;
namespace fs = std::filesystem;

TEST_CASE("the nodelist and the pointlist in testdata compile into one file "
          "[nodelist]") {
    test::TempDir dir;

    nodelist::CompileOptions options;
    options.sources = {test::projectPath("testdata/nodelist/Z2DAILY.999"),
                       test::projectPath("testdata/nodelist/Z2PNT.Z99")};
    options.dbPath = dir.path("nodelist.db");
    options.tempDir = dir.path("tmp");

    std::ostringstream log;
    const auto report = nodelist::compileNodelists(options, &log);

    REQUIRE(report.sources.size() == 2);
    CHECK_FALSE(report.sources[0].pointList);
    CHECK(report.sources[0].archive.empty());
    CHECK(report.sources[1].pointList);
    CHECK(fs::path(report.sources[1].archive).filename() == "Z2PNT.Z19");

    CHECK(report.nodes > 1000);
    CHECK(report.points > 100);
    CHECK(report.warnings == 0);
    CHECK(report.bytes > 0);

    // The two are one file, and each node is under the address its own list put
    // it at.
    const auto db = nodelist::NodelistDb::open(options.dbPath);
    CHECK(db.size() == report.nodes + report.points);
    REQUIRE(db.sources().size() == 2);
    // A node remembers the line the config wrote and not the file it happened
    // to resolve to today, which is a different file tomorrow.
    CHECK(db.sources()[1].spec == options.sources[1]);

    const auto node = db.find(*domain::FtnAddress::parse("2:221/6"));
    REQUIRE(node);
    CHECK(db.entry(*node).sysop == "Tommi Koivula");
    CHECK(db.sourceAt(*node) == 0);

    const auto point = db.find(*domain::FtnAddress::parse("2:221/6.66"));
    REQUIRE(point);
    CHECK(db.entry(*point).system == "FPoint");
    CHECK(db.sourceAt(*point) == 1);

    // The searches the format is for, over the real thing: a net, and a sysop
    // by part of their name.
    const auto prefix = nodelist::AddressPrefix::parse("2:221");
    REQUIRE(prefix);
    const auto range = db.findRange(*prefix);
    CHECK(range.second - range.first > 5);
    for (size_t i = range.first; i < range.second; ++i) {
        CHECK(db.addressAt(i).zone == 2);
        CHECK(db.addressAt(i).net == 221);
    }

    const auto found = db.findBySysop("koivula");
    REQUIRE_FALSE(found.empty());
    for (size_t index : found) {
        CHECK(db.entry(index).sysop.find("Koivula") != std::string::npos);
    }
    // The nodelist and the pointlist both name them, so the search reaches
    // across the two files that went in.
    bool sawNode = false;
    bool sawPoint = false;
    for (size_t index : found) {
        if (db.addressAt(index).point == 0) sawNode = true;
        if (db.addressAt(index).point != 0) sawPoint = true;
    }
    CHECK(sawNode);
    CHECK(sawPoint);

    // Nothing is left behind in the temporary directory.
    CHECK(fs::is_empty(options.tempDir));

    const std::string written = log.str();
    CHECK(written.find("Z2DAILY.225") != std::string::npos);
    CHECK(written.find("pointlist") != std::string::npos);
}

TEST_CASE("a nodelist that is not there is said out loud and never thrown "
          "[nodelist]") {
    test::TempDir dir;

    // A missing nodelist beside one that is there: the one that is there is
    // still compiled, and the compiled file is still written. AmberEdit starts
    // either way — that is the whole point of it not throwing.
    nodelist::CompileOptions options;
    options.sources = {dir.path("nothing.ndl"),
                       test::projectPath("testdata/nodelist/Z2DAILY.225")};
    options.dbPath = dir.path("nodelist.db");
    options.tempDir = dir.path("tmp");

    const auto report = nodelist::compileNodelists(options, nullptr);
    CHECK(report.written);
    CHECK(report.nodes > 1000);
    REQUIRE(report.problems.size() == 1);
    CHECK_MESSAGE(contains(report.problems[0], "nodelist not found"), report.problems[0]);
    REQUIRE(report.sources.size() == 2);
    CHECK_MESSAGE(contains(report.sources[0].problem,
                           "not found"),
                  report.sources[0].problem);
    CHECK(report.sources[0].state.path.empty());
    CHECK(report.sources[1].problem.empty());

    // The one that would not read is in the compiled file all the same, as the
    // nothing it was: that is what stops the next start compiling again.
    const auto db = nodelist::NodelistDb::open(options.dbPath);
    REQUIRE(db.sources().size() == 2);
    CHECK(db.sources()[0].spec == options.sources[0]);
    CHECK(db.sources()[0].path.empty());
    CHECK_FALSE(nodelist::nodelistNeedsCompiling(options));
}

TEST_CASE("a config with nowhere to compile to says so and writes nothing "
          "[nodelist]") {
    test::TempDir dir;

    nodelist::CompileOptions options;
    options.sources = {test::projectPath("testdata/nodelist/Z2DAILY.225")};

    const auto report = nodelist::compileNodelists(options, nullptr);
    CHECK_FALSE(report.written);
    REQUIRE(report.problems.size() == 1);
    CHECK_MESSAGE(contains(report.problems[0],
                           "nodelist_db is not set"),
                  report.problems[0]);

    // And a config naming no nodelist at all is not a mistake: it is what most
    // configs are, and there is nothing for it to say.
    nodelist::CompileOptions none;
    none.dbPath = dir.path("nodelist.db");
    const auto quiet = nodelist::compileNodelists(none, nullptr);
    CHECK_FALSE(quiet.written);
    CHECK(quiet.problems.empty());
    CHECK_FALSE(nodelist::nodelistNeedsCompiling(none));
}

TEST_CASE("the nodelists are compiled again when, and only when, they change "
          "[nodelist]") {
    test::TempDir dir;
    const std::string nodelistPath = dir.path("NODELIST.225");
    {
        std::ofstream out(nodelistPath, std::ios::binary);
        out << "Zone,2,Europe,Somewhere,Nobody,-Unpublished-,300\r\n"
            << ",1,A_BBS,Somewhere,Some_Sysop,-Unpublished-,300\r\n";
    }

    nodelist::CompileOptions options;
    options.sources = {dir.path("NODELIST.999")};
    options.dbPath = dir.path("nodelist.db");
    options.tempDir = dir.path("tmp");

    // Nothing compiled yet, so there is everything to do.
    CHECK(nodelist::nodelistNeedsCompiling(options));
    CHECK(nodelist::refreshNodelist(options, /*force=*/false, nullptr).written);
    CHECK_FALSE(nodelist::nodelistNeedsCompiling(options));

    // And now nothing, however often it is asked.
    CHECK_FALSE(nodelist::refreshNodelist(options, /*force=*/false, nullptr).written);
    // Unless it is asked for outright, which is what --compile is.
    CHECK(nodelist::refreshNodelist(options, /*force=*/true, nullptr).written);

    // A nodelist rewritten in place: a different length, and a different stamp.
    {
        std::ofstream out(nodelistPath, std::ios::binary);
        out << "Zone,2,Europe,Somewhere,Nobody,-Unpublished-,300\r\n"
            << ",1,A_BBS,Somewhere,Some_Sysop,-Unpublished-,300\r\n"
            << ",2,Another,Somewhere,Other_Sysop,-Unpublished-,300\r\n";
    }
    CHECK(nodelist::nodelistNeedsCompiling(options));
    CHECK(nodelist::refreshNodelist(options, /*force=*/false, nullptr).written);
    CHECK_FALSE(nodelist::nodelistNeedsCompiling(options));

    // A new day's nodelist beside the old one: a different file under the same
    // line, which is the case the day number pattern exists for.
    {
        std::ofstream out(dir.path("NODELIST.226"), std::ios::binary);
        out << "Zone,2,Europe,Somewhere,Nobody,-Unpublished-,300\r\n";
    }
    CHECK(nodelist::nodelistNeedsCompiling(options));
    CHECK(nodelist::refreshNodelist(options, /*force=*/false, nullptr).written);
    CHECK_FALSE(nodelist::nodelistNeedsCompiling(options));

    // The nodelist gone altogether. It is compiled once more — what is there
    // has changed — and then left alone: a missing file is a state like any
    // other, and every start trying again would be a start reading a directory
    // for nothing.
    fs::remove(nodelistPath);
    fs::remove(dir.path("NODELIST.226"));
    CHECK(nodelist::nodelistNeedsCompiling(options));
    const auto gone = nodelist::refreshNodelist(options, /*force=*/false, nullptr);
    CHECK(gone.written);
    CHECK(gone.nodes == 0);
    REQUIRE(gone.problems.size() == 1);
    CHECK_FALSE(nodelist::nodelistNeedsCompiling(options));

    // A config line added is a config that no longer matches what was compiled.
    options.sources.push_back(test::projectPath("testdata/nodelist/Z2DAILY.225"));
    CHECK(nodelist::nodelistNeedsCompiling(options));
}

TEST_CASE("a compiled nodelist that cannot be read is compiled again [nodelist]") {
    test::TempDir dir;

    nodelist::CompileOptions options;
    options.sources = {test::projectPath("testdata/nodelist/Z2DAILY.225")};
    options.dbPath = dir.path("nodelist.db");
    CHECK(nodelist::refreshNodelist(options, /*force=*/false, nullptr).written);

    // Whatever is wrong with it — a truncated file, one from another version of
    // the format, one that is not a compiled nodelist at all — the answer is
    // the same, and it is never a failure the user has to do something about.
    {
        std::ofstream out(options.dbPath, std::ios::binary | std::ios::trunc);
        out << "not a compiled nodelist";
    }
    CHECK(nodelist::nodelistNeedsCompiling(options));
    CHECK(nodelist::refreshNodelist(options, /*force=*/false, nullptr).written);
    CHECK_FALSE(nodelist::nodelistNeedsCompiling(options));

    // And a compiled file that cannot be written is a problem and not a throw:
    // a directory where the file should be is as close as a test can come to a
    // disk that will not take it.
    nodelist::CompileOptions blocked = options;
    blocked.dbPath = dir.path("in-the-way");
    fs::create_directories(blocked.dbPath);
    const auto report = nodelist::refreshNodelist(blocked, /*force=*/false, nullptr);
    CHECK_FALSE(report.written);
    CHECK_FALSE(report.problems.empty());
}
