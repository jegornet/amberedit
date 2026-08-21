#include "echolist/echolist_db.hpp"

#include <doctest/doctest.h>

#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "echolist/echolist_format.hpp"
#include "echolist/echolist_writer.hpp"
#include "temp_dir.hpp"

using namespace amberedit;

namespace {

echolist::DbSource sourceOf(const std::string& spec,
                            std::vector<echolist::EchoEntry> entries) {
    echolist::DbSource source;
    source.state.spec = spec;
    source.state.path = spec;
    source.state.modified = 1000;
    source.state.size = 10;
    source.entries = std::move(entries);
    return source;
}

}  // namespace

TEST_CASE("a compiled echolist reads back what was written into it [echolist]") {
    test::TempDir dir;
    const std::string path = dir.path("echolist.db");

    const std::vector<echolist::DbSource> sources = {
        sourceOf("echo50.lst", {{"Ru.Linux", "Linux and the rest of it"},
                                {"SU.MUSIC", "Музыка!"},
                                {"AARP_FRAUD", "AARP Fraud Warning Network news"}})};

    const auto written = echolist::writeEcholistDb(path, sources, 1234);
    CHECK(written.areas == 3);
    CHECK(written.duplicates == 0);
    CHECK(written.bytes > 0);

    const auto db = echolist::EcholistDb::open(path);
    CHECK(db.size() == 3);
    CHECK(db.builtAt() == 1234);
    REQUIRE(db.sources().size() == 1);
    CHECK(db.sources()[0].spec == "echo50.lst");

    // The tag is looked up folded, so however the tosser config spells it.
    const auto found = db.descriptionOf("ru.linux");
    REQUIRE(found);
    CHECK(*found == "Linux and the rest of it");
    CHECK(db.descriptionOf("RU.LINUX") == db.descriptionOf("Ru.Linux"));
    // Nothing decoded here: the text went in as UTF-8 and comes back as it was.
    CHECK(db.descriptionOf("su.music") == std::optional<std::string>("Музыка!"));

    CHECK_FALSE(db.descriptionOf("ru.unix"));
    CHECK_FALSE(db.descriptionOf(""));

    // The records stand in folded-tag order, which is the only order there is.
    CHECK(db.tagAt(0) == "AARP_FRAUD");
    CHECK(db.tagAt(1) == "Ru.Linux");
    CHECK(db.tagAt(2) == "SU.MUSIC");
}

TEST_CASE("the first echolist to name an echo is the one that keeps it [echolist]") {
    test::TempDir dir;
    const std::string path = dir.path("echolist.db");

    const std::vector<echolist::DbSource> sources = {
        sourceOf("first.lst", {{"Ru.Linux", "from the first list"},
                               {"Ru.Linux", "from the first list, said twice"}}),
        sourceOf("second.na", {{"RU.LINUX", "from the second list"},
                               {"Ru.Unix", "only the second list has this"}})};

    const auto written = echolist::writeEcholistDb(path, sources, 1);
    CHECK(written.areas == 2);
    CHECK(written.duplicates == 2);

    const auto db = echolist::EcholistDb::open(path);
    // The config's order of `echolist` lines is the only statement of
    // precedence anybody has made, and inside one file the first line wins.
    CHECK(db.descriptionOf("ru.linux") ==
          std::optional<std::string>("from the first list"));
    CHECK(db.descriptionOf("ru.unix") ==
          std::optional<std::string>("only the second list has this"));
}

TEST_CASE("an echolist that was not there is written as the nothing it was "
          "[echolist]") {
    test::TempDir dir;
    const std::string path = dir.path("echolist.db");

    // A source with no entries is meaningful and is written like any other: it
    // is what stops every start from trying the missing file again.
    echolist::DbSource missing;
    missing.state.spec = "/ftn/echolist/gone.lst";
    missing.state.charset = "CP866";

    const auto written = echolist::writeEcholistDb(path, {missing}, 7);
    CHECK(written.areas == 0);

    const auto db = echolist::EcholistDb::open(path);
    CHECK(db.empty());
    REQUIRE(db.sources().size() == 1);
    CHECK(db.sources()[0].spec == "/ftn/echolist/gone.lst");
    CHECK(db.sources()[0].charset == "CP866");
    CHECK(db.sources()[0].path.empty());
}

TEST_CASE("a file that is not a compiled echolist is refused by name [echolist]") {
    test::TempDir dir;

    CHECK_THROWS_AS(echolist::EcholistDb::open(dir.path("nothing.db")),
                    std::runtime_error);

    const std::string wrong = dir.path("wrong.db");
    {
        std::ofstream out(wrong, std::ios::binary);
        out << "not an echolist at all, whatever else it may be";
    }
    CHECK_THROWS_AS(echolist::EcholistDb::open(wrong), std::runtime_error);

    // A file written by another version of the format is refused too, which
    // `echolistNeedsCompiling` reads as "compile it again".
    const std::string path = dir.path("echolist.db");
    echolist::writeEcholistDb(path, {sourceOf("a.lst", {{"Ru.Linux", "Linux"}})}, 1);
    {
        std::fstream out(path, std::ios::binary | std::ios::in | std::ios::out);
        out.seekp(8);
        const char bumped[2] = {static_cast<char>(echolist::format::kVersion + 1), 0};
        out.write(bumped, sizeof(bumped));
    }
    CHECK_THROWS_AS(echolist::EcholistDb::open(path), std::runtime_error);
}
