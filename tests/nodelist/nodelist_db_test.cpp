#include "nodelist/nodelist_db.hpp"

#include <catch2/catch.hpp>

#include <fstream>

#include "nodelist/nodelist_writer.hpp"
#include "temp_dir.hpp"

using namespace amberedit;

namespace {

nodelist::NodeEntry node(const std::string& address, const std::string& system,
                         const std::string& sysop) {
    nodelist::NodeEntry entry;
    entry.address = *domain::FtnAddress::parse(address);
    entry.system = system;
    entry.sysop = sysop;
    entry.location = "Somewhere";
    entry.phone = "-Unpublished-";
    entry.speed = 300;
    return entry;
}

/// The addresses a run of the db names, as strings, so that a failing search
/// says which nodes it found.
std::vector<std::string> addresses(const nodelist::NodelistDb& db,
                                   const std::vector<size_t>& indexes) {
    std::vector<std::string> out;
    out.reserve(indexes.size());
    for (size_t index : indexes) out.push_back(db.addressAt(index).toString());
    return out;
}

std::vector<std::string> addresses(const nodelist::NodelistDb& db,
                                   std::pair<size_t, size_t> range) {
    std::vector<size_t> indexes;
    for (size_t i = range.first; i < range.second; ++i) indexes.push_back(i);
    return addresses(db, indexes);
}

std::vector<size_t> bySysop(const nodelist::NodelistDb& db, const std::string& query) {
    return db.findBySysop(query);
}

}  // namespace

TEST_CASE("a compiled nodelist reads back what went into it", "[nodelist]") {
    test::TempDir dir;
    const std::string path = dir.path("nodelist.db");

    nodelist::DbSource source;
    source.state.spec = "~/ftn/nodelist/nodelist.ndl";
    source.entries = {node("2:5020/999", "A_BBS", "Vasiliy Pupkin"),
                      node("2:5020/999.1", "A Point", "Vasiliy Pupkin")};
    source.entries[0].flags = "CM,IBN:24554";

    const auto report = nodelist::writeNodelistDb(path, {source}, 1234567890);
    CHECK(report.nodes == 1);
    CHECK(report.points == 1);
    CHECK(report.duplicates == 0);

    const auto db = nodelist::NodelistDb::open(path);
    REQUIRE(db.size() == 2);
    CHECK(db.builtAt() == 1234567890);
    REQUIRE(db.sources().size() == 1);
    CHECK(db.sources()[0].spec == "~/ftn/nodelist/nodelist.ndl");
    CHECK(db.sourceAt(0) == 0);

    // The node stands before its own point, which is what the key's order says.
    CHECK(db.addressAt(0).toString() == "2:5020/999");
    CHECK(db.addressAt(1).toString() == "2:5020/999.1");

    const auto entry = db.entry(0);
    CHECK(entry.system == "A_BBS");
    CHECK(entry.sysop == "Vasiliy Pupkin");
    CHECK(entry.phone == "-Unpublished-");
    CHECK(entry.speed == 300);
    CHECK(entry.flags == "CM,IBN:24554");
}

TEST_CASE("a node is found by its whole address and by any part of one", "[nodelist]") {
    test::TempDir dir;
    const std::string path = dir.path("nodelist.db");

    nodelist::DbSource source;
    source.state.spec = "nodelist";
    source.entries = {
        node("1:102/401", "Techware", "Lee Green"),
        node("2:221/1", "MXO", "Tommi Koivula"),
        node("2:221/6", "KCO", "Tommi Koivula"),
        node("2:221/6.66", "FPoint", "Tommi Koivula"),
        node("2:5020/999", "A_BBS", "Vasiliy Pupkin"),
        node("2:5020/999.1", "A Point", "Petr Petrov"),
        node("3:640/1384", "Another", "Somebody Else"),
    };
    nodelist::writeNodelistDb(path, {source}, 0);
    const auto db = nodelist::NodelistDb::open(path);
    REQUIRE(db.size() == 7);

    const auto range = [&db](const std::string& text) {
        const auto prefix = nodelist::AddressPrefix::parse(text);
        REQUIRE(prefix);
        return addresses(db, db.findRange(*prefix));
    };

    // A whole address, with and without a point on it.
    CHECK(db.find(*domain::FtnAddress::parse("2:221/6")) == std::optional<size_t>(2));
    CHECK(db.find(*domain::FtnAddress::parse("2:221/6.66")) == std::optional<size_t>(3));
    CHECK_FALSE(db.find(*domain::FtnAddress::parse("2:221/7")));

    // A zone, a net, a node — each a run of the same sorted index.
    CHECK(range("1") == std::vector<std::string>{"1:102/401"});
    CHECK(range("2") == std::vector<std::string>{"2:221/1", "2:221/6", "2:221/6.66",
                                                 "2:5020/999", "2:5020/999.1"});
    CHECK(range("2:221") == std::vector<std::string>{"2:221/1", "2:221/6", "2:221/6.66"});
    CHECK(range("2:221/6") == std::vector<std::string>{"2:221/6", "2:221/6.66"});
    CHECK(range("2:221/6.66") == std::vector<std::string>{"2:221/6.66"});
    CHECK(range("2:5020") == std::vector<std::string>{"2:5020/999", "2:5020/999.1"});
    CHECK(range("4").empty());
    CHECK(range("2:222").empty());

    // Half-typed is the prefix that stands before the separator, so that a
    // search field can be read while it is still being filled in.
    CHECK(range("2:") == range("2"));
    CHECK(range("2:221/") == range("2:221"));
}

TEST_CASE("a node is found by the whole of a sysop's name or by part of one",
          "[nodelist]") {
    test::TempDir dir;
    const std::string path = dir.path("nodelist.db");

    nodelist::DbSource source;
    source.state.spec = "nodelist";
    source.entries = {
        node("2:221/1", "MXO", "Tommi Koivula"),
        node("2:221/6", "KCO", "Tommi Koivula"),
        node("2:240/2188.13", "Kruemel", "Joerg Walther"),
        node("2:5020/999", "A_BBS", "Vasiliy Pupkin"),
        node("2:5020/1042", "Another", "Andrew Leary"),
    };
    nodelist::writeNodelistDb(path, {source}, 0);
    const auto db = nodelist::NodelistDb::open(path);

    // The whole name, either way it is spelled, and either case.
    CHECK(addresses(db, bySysop(db, "Tommi Koivula")) ==
          std::vector<std::string>{"2:221/1", "2:221/6"});
    CHECK(addresses(db, bySysop(db, "tommi_koivula")) ==
          std::vector<std::string>{"2:221/1", "2:221/6"});
    CHECK(addresses(db, bySysop(db, "TOMMI KOIVULA")) ==
          std::vector<std::string>{"2:221/1", "2:221/6"});

    // A surname on its own, a forename on its own, and a run of characters out
    // of the middle of one — which is what a suffix array buys over a sorted
    // list of names.
    CHECK(addresses(db, bySysop(db, "Koivula")) ==
          std::vector<std::string>{"2:221/1", "2:221/6"});
    CHECK(addresses(db, bySysop(db, "walther")) ==
          std::vector<std::string>{"2:240/2188.13"});
    CHECK(addresses(db, bySysop(db, "oivul")) ==
          std::vector<std::string>{"2:221/1", "2:221/6"});
    CHECK(addresses(db, bySysop(db, "ear")) == std::vector<std::string>{"2:5020/1042"});

    // One letter of a name several share answers with all of them, in address
    // order, and once each however often it stands in the name.
    CHECK(addresses(db, bySysop(db, "o")) ==
          std::vector<std::string>{"2:221/1", "2:221/6", "2:240/2188.13"});

    CHECK(bySysop(db, "nobody at all").empty());
    // Nothing typed is not everything: it is a search field before anything has
    // been put in it.
    CHECK(bySysop(db, "").empty());
    CHECK(bySysop(db, "   ").empty());

    CHECK(db.findBySysop("o", 2).size() == 2);
}

TEST_CASE("a point nobody lists is answered for by its boss", "[nodelist]") {
    test::TempDir dir;
    const std::string path = dir.path("nodelist.db");

    nodelist::DbSource source;
    source.state.spec = "nodelist";
    source.entries = {
        node("2:5020/999", "A_BBS", "Vasiliy Pupkin"),
        node("2:5020/999.1", "A Point", "Petr Petrov"),
        node("2:5020/1000", "Another", "Ivan Ivanov"),
    };
    nodelist::writeNodelistDb(path, {source}, 0);
    const auto db = nodelist::NodelistDb::open(path);

    const auto at = [&db](const std::string& address) {
        const auto found = db.findOrBoss(*domain::FtnAddress::parse(address));
        return found ? db.addressAt(*found).toString() : std::string{};
    };

    // A point that is listed is itself, and so is every node.
    CHECK(at("2:5020/999.1") == "2:5020/999.1");
    CHECK(at("2:5020/999") == "2:5020/999");

    // A point that is not — a pointlist nobody compiled, most often — is
    // answered for by the node it hangs off: it is that node's own client.
    CHECK(at("2:5020/999.2") == "2:5020/999");
    CHECK(at("2:5020/1000.7") == "2:5020/1000");

    // Nothing falls back from a node, and nothing falls back to a boss that is
    // not there either.
    CHECK(at("2:5020/1001").empty());
    CHECK(at("2:5020/1001.1").empty());
    // And the exact search still answers exactly, which is what the compose
    // screen and every other caller that means one address asks.
    CHECK_FALSE(db.find(*domain::FtnAddress::parse("2:5020/999.2")));
}

TEST_CASE("a sysop search can answer closest first", "[nodelist]") {
    test::TempDir dir;
    const std::string path = dir.path("nodelist.db");

    nodelist::DbSource source;
    source.state.spec = "nodelist";
    source.entries = {
        node("2:1/1", "A", "Andrew Leary"),    // the query is a word of it
        node("2:1/2", "B", "Leary"),           // and the whole of it
        node("2:1/3", "C", "Learyson Smith"),  // which it begins
        node("2:1/4", "D", "Bill O'Leary"),    // and stands inside a word of
        node("2:1/5", "E", "Zoe Leary"),       // a word of it, and a shorter name
    };
    nodelist::writeNodelistDb(path, {source}, 0);
    const auto db = nodelist::NodelistDb::open(path);

    // Address order is the order the nodelist itself is in, and says nothing
    // about which of them the query meant.
    CHECK(addresses(db, db.findBySysop("leary")) ==
          std::vector<std::string>{"2:1/1", "2:1/2", "2:1/3", "2:1/4", "2:1/5"});

    // Closest first: the whole name, the name it begins, then the two it is a
    // word of — shorter first, the query being more of it — and last the one
    // where it is buried in a word.
    CHECK(addresses(db, db.findBySysop("leary", 0,
                                       nodelist::NodelistDb::SysopOrder::Relevance)) ==
          std::vector<std::string>{"2:1/2", "2:1/3", "2:1/5", "2:1/1", "2:1/4"});

    // A limit takes the best of them and not the first of them found.
    CHECK(addresses(db, db.findBySysop("leary", 2,
                                       nodelist::NodelistDb::SysopOrder::Relevance)) ==
          std::vector<std::string>{"2:1/2", "2:1/3"});

    // The whole name, whichever way it is spelled, is closest of all.
    CHECK(addresses(db, db.findBySysop("zoe_leary", 0,
                                       nodelist::NodelistDb::SysopOrder::Relevance))
              .front() == "2:1/5");
}

TEST_CASE("the first nodelist to name an address is the one that keeps it",
          "[nodelist]") {
    test::TempDir dir;
    const std::string path = dir.path("nodelist.db");

    nodelist::DbSource first{{"first.ndl"},
                             {node("2:5020/999", "The first", "First Op")}};
    nodelist::DbSource second{{"second.ndl"},
                              {node("2:5020/999", "The second", "Second Op"),
                               node("2:5020/1000", "Only here", "Third Op")}};

    const auto report = nodelist::writeNodelistDb(path, {first, second}, 0);
    CHECK(report.duplicates == 1);

    const auto db = nodelist::NodelistDb::open(path);
    REQUIRE(db.size() == 2);
    CHECK(db.entry(0).system == "The first");
    CHECK(db.sourceAt(0) == 0);
    CHECK(db.sourceAt(1) == 1);
    // The one that lost is gone with its sysop: a name search must not find a
    // node that is no longer in the file.
    CHECK(db.findBySysop("Second Op").empty());
}

TEST_CASE("a compiled nodelist that is not one is refused by name", "[nodelist]") {
    test::TempDir dir;

    const std::string missing = dir.path("nothing.db");
    CHECK_THROWS_WITH(nodelist::NodelistDb::open(missing),
                      Catch::Matchers::Contains(missing));

    const std::string wrong = dir.path("wrong.db");
    {
        std::ofstream out(wrong, std::ios::binary);
        out << "Zone,2,Europe,Somewhere,Nobody,-Unpublished-,300\r\n";
    }
    CHECK_THROWS_WITH(nodelist::NodelistDb::open(wrong),
                      Catch::Matchers::Contains("not a compiled nodelist"));

    // The version marker is what a file compiled by another AmberEdit is caught
    // by, and the complaint says what to do about it.
    const std::string old = dir.path("old.db");
    nodelist::writeNodelistDb(old, {{{"nodelist"}, {node("2:5020/999", "A", "B")}}}, 0);
    {
        std::fstream out(old, std::ios::binary | std::ios::in | std::ios::out);
        out.seekp(8);
        const char version[2] = {0x63, 0x00};
        out.write(version, 2);
    }
    CHECK_THROWS_WITH(nodelist::NodelistDb::open(old),
                      Catch::Matchers::Contains("format version 99"));
}

TEST_CASE("an empty nodelist compiles and answers nothing", "[nodelist]") {
    test::TempDir dir;
    const std::string path = dir.path("nodelist.db");
    nodelist::writeNodelistDb(path, {{{"nodelist"}, {}}}, 0);

    const auto db = nodelist::NodelistDb::open(path);
    CHECK(db.empty());
    CHECK_FALSE(db.find(*domain::FtnAddress::parse("2:5020/999")));
    CHECK(db.findBySysop("anybody").empty());
    const auto prefix = nodelist::AddressPrefix::parse("2");
    REQUIRE(prefix);
    CHECK(db.findRange(*prefix) == std::pair<size_t, size_t>{0, 0});
}

TEST_CASE("what is not the beginning of an address is not a prefix", "[nodelist]") {
    CHECK_FALSE(nodelist::AddressPrefix::parse(""));
    CHECK_FALSE(nodelist::AddressPrefix::parse("Vasiliy"));
    CHECK_FALSE(nodelist::AddressPrefix::parse(":5020"));
    CHECK_FALSE(nodelist::AddressPrefix::parse("2:5020/999.1.2"));
    CHECK_FALSE(nodelist::AddressPrefix::parse("2:5020/999x"));
    CHECK_FALSE(nodelist::AddressPrefix::parse("70000"));

    const auto full = nodelist::AddressPrefix::parse("2:5020/999.1");
    REQUIRE(full);
    CHECK(full->depth == 4);
    CHECK(full->lowKey() == full->highKey());

    // A zone on its own covers every address in it, up to the last one.
    const auto zone = nodelist::AddressPrefix::parse("65535");
    REQUIRE(zone);
    CHECK(zone->highKey() == 0xffffffffffffffffULL);
}
