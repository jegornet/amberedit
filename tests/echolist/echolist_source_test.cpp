#include "echolist/echolist_source.hpp"

#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include "temp_dir.hpp"
#include "test_paths.hpp"

using namespace amberedit;
namespace fs = std::filesystem;

TEST_CASE("a .zip is an archive of echolists and anything else is one echolist",
          "[echolist]") {
    CHECK(echolist::isArchiveName("/ftn/echolist/elst2601.zip"));
    // The archives come spelled both ways.
    CHECK(echolist::isArchiveName("/ftn/echolist/ELST2601.ZIP"));
    CHECK_FALSE(echolist::isArchiveName("/ftn/echolist/echo50.lst"));
    CHECK_FALSE(echolist::isArchiveName("/ftn/echolist/BACKBONE.NA"));
    CHECK_FALSE(echolist::isArchiveName("echolist"));
}

TEST_CASE("the state of an echolist is a stat and nothing more", "[echolist]") {
    test::TempDir dir;
    const std::string path = dir.path("echo.lst");

    // A line naming a file that is not there has a state like any other: it is
    // what stops the next start from trying to compile it again.
    const auto missing = echolist::stateOf(path, "CP866");
    CHECK(missing.spec == path);
    CHECK(missing.charset == "CP866");
    CHECK(missing.path.empty());
    CHECK(missing.size == 0);

    {
        std::ofstream out(path);
        out << ",Ru.Linux,Linux,Some Body,2:5020/113,\n";
    }
    const auto present = echolist::stateOf(path, "CP866");
    CHECK(present.path == path);
    CHECK(present.size > 0);
    CHECK(present != missing);

    // The charset is part of the state: a line whose charset has been corrected
    // is a line that has to be read again, though the file has not moved.
    CHECK(echolist::stateOf(path, "KOI8-R") != present);
}

TEST_CASE("an echolist is read in the charset the line states", "[echolist]") {
    test::TempDir dir;
    const std::string path = dir.path("echo.lst");
    {
        // "Музыка!" in CP866, which is what a Russian echolist is written in and
        // what nothing but the config line can say.
        std::ofstream out(path, std::ios::binary);
        out << ",Su.Music,\x8C\xE3\xA7\xEB\xAA\xA0!,Some Body,2:5023/24,\n";
    }

    echolist::EcholistSources sources(dir.path("tmp"));
    const auto loaded = sources.read(path, "CP866");
    REQUIRE(loaded.parts.size() == 1);
    CHECK(loaded.archive.empty());
    CHECK(loaded.parts[0].name == "echo.lst");
    // Everything above the file is UTF-8, here as everywhere else.
    CHECK(loaded.parts[0].text.find("Музыка!") != std::string::npos);
}

TEST_CASE("a zipped echolist is unpacked without paths and taken away again",
          "[echolist]") {
    test::TempDir dir;
    const std::string workDir = dir.path("tmp");

    {
        echolist::EcholistSources sources(workDir);
        const auto loaded =
            sources.read(test::projectPath("testdata/echolist/elst2601.zip"), "CP866");

        CHECK(fs::path(loaded.archive).filename() == "elst2601.zip");
        // The three .na lists in it and nothing else: the archive also carries
        // reports, a readme and two further archives, and none of that is an
        // echolist.
        REQUIRE(loaded.parts.size() == 3);
        CHECK(loaded.parts[0].name == "BACKBONE.NA");
        CHECK(loaded.parts[1].name == "ELIST.NA");
        CHECK(loaded.parts[2].name == "FSXNET.NA");

        for (const auto& part : loaded.parts) {
            // Without paths: the name it was unpacked under is its last
            // component and nothing else.
            CHECK(fs::path(part.readFrom).parent_path() == fs::path(workDir));
            CHECK(fs::exists(part.readFrom));
            CHECK_FALSE(part.text.empty());
        }
        CHECK(loaded.parts[2].text.find("FSX_ADS") != std::string::npos);
    }

    // Everything it unpacked is gone with it, and the directory itself stays.
    REQUIRE(fs::is_directory(workDir));
    CHECK(fs::is_empty(workDir));
}

TEST_CASE("a wildcard picks the newest echolist it covers", "[echolist]") {
    test::TempDir dir;

    const auto write = [&dir](const std::string& name, int minutesAgo) {
        const std::string path = dir.path(name);
        {
            std::ofstream out(path, std::ios::binary);
            out << ",Ru.Linux," << name << ",Some Body,2:5020/113,\n";
        }
        std::error_code ec;
        fs::last_write_time(
            path, fs::file_time_type::clock::now() - std::chrono::minutes(minutesAgo),
            ec);
        REQUIRE_FALSE(ec);
        return path;
    };

    write("echo2510.lst", 60);
    const std::string newest = write("echo2601.lst", 5);
    write("echo2512.lst", 30);
    // Not covered by the pattern below, sharing the directory as they do.
    write("elist.na", 1);

    const auto state = echolist::stateOf(dir.path("echo*.lst"), "UTF-8");
    // The line stays the pattern, since that is what the next start looks up
    // again; the path is what it stood for today.
    CHECK(state.spec == dir.path("echo*.lst"));
    CHECK(state.path == newest);
    CHECK(state.size > 0);

    // A path with nothing to match in it is still that file and nothing else.
    CHECK(echolist::stateOf(dir.path("echo2512.lst"), "").path ==
          dir.path("echo2512.lst"));

    // On a tie the later name wins, so that an echolist carrying its month in
    // its name is answered for in the order such names sort in — and so that
    // one directory read twice answers the same twice.
    const auto together = fs::file_time_type::clock::now() - std::chrono::minutes(2);
    for (const char* name : {"echo2510.lst", "echo2601.lst", "echo2512.lst"}) {
        std::error_code ec;
        fs::last_write_time(dir.path(name), together, ec);
        REQUIRE_FALSE(ec);
    }
    CHECK(fs::path(echolist::stateOf(dir.path("echo*.lst"), "").path).filename() ==
          "echo2601.lst");
}

TEST_CASE("a wildcard reads whichever kind of file it landed on", "[echolist]") {
    test::TempDir dir;
    echolist::EcholistSources sources(dir.path("tmp"));

    // The archive from testdata beside a plain list, and a pattern covering
    // both: what is read is the newest of them, and whether it is unpacked is
    // its own name's to say rather than the pattern's.
    const std::string plain = dir.path("echo50.lst");
    {
        std::ofstream out(plain, std::ios::binary);
        out << ",Ru.Linux,Linux,Some Body,2:5020/113,\n";
    }
    const std::string zipped = dir.path("elst2601.zip");
    fs::copy_file(test::projectPath("testdata/echolist/elst2601.zip"), zipped);

    std::error_code ec;
    const auto now = fs::file_time_type::clock::now();
    fs::last_write_time(plain, now - std::chrono::minutes(60), ec);
    fs::last_write_time(zipped, now - std::chrono::minutes(1), ec);

    const auto archive = sources.read(dir.path("e*.*"), "CP866");
    CHECK(fs::path(archive.archive).filename() == "elst2601.zip");
    CHECK(archive.parts.size() == 3);

    // The other way round, and the same pattern reads a plain list instead.
    fs::last_write_time(plain, now, ec);
    const auto list = sources.read(dir.path("e*.*"), "CP866");
    CHECK(list.archive.empty());
    REQUIRE(list.parts.size() == 1);
    CHECK(list.parts[0].name == "echo50.lst");
}

TEST_CASE("an echolist that is not there says so", "[echolist]") {
    test::TempDir dir;
    echolist::EcholistSources sources(dir.path("tmp"));

    CHECK_THROWS_WITH(sources.read(dir.path("gone.lst"), ""),
                      Catch::Matchers::Contains("echolist not found"));
    // A pattern that covers nothing names the pattern rather than a file, since
    // no file of that name was ever written down.
    CHECK_THROWS_WITH(sources.read(dir.path("gone*.lst"), ""),
                      Catch::Matchers::Contains("no echolist matching"));

    // An archive holding no echolist at all is the same kind of nothing, said
    // in its own words.
    const std::string archive = dir.path("empty.zip");
    {
        std::ofstream out(archive, std::ios::binary);
        // An empty zip: the end-of-directory record and nothing before it.
        const char end[] = {'P', 'K', 5, 6, 0, 0, 0, 0, 0, 0, 0,
                            0,   0,   0, 0, 0, 0, 0, 0, 0, 0, 0};
        out.write(end, sizeof(end));
    }
    CHECK_THROWS_AS(sources.read(archive, ""), std::runtime_error);
}
