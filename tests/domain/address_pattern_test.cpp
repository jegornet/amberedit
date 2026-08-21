#include <catch2/catch.hpp>

#include <string>

#include "domain/address_pattern.hpp"
#include "domain/ftn_address.hpp"

using amberedit::domain::AddressPattern;
using amberedit::domain::FtnAddress;

namespace {

/// The pattern written back out, or "-" when the text is not a pattern at all.
/// Comparing the canonical form checks the parse without spelling out four
/// optionals per case.
std::string roundTrip(const std::string& text) {
    const auto pattern = AddressPattern::parse(text);
    return pattern ? pattern->toString() : "-";
}

/// Whether the pattern matches the address, both given as text.
bool hits(const std::string& pattern, const std::string& address) {
    const auto parsed = AddressPattern::parse(pattern);
    const auto addr = FtnAddress::parse(address);
    REQUIRE(parsed);
    REQUIRE(addr);
    return parsed->matches(*addr);
}

int depthOf(const std::string& text) {
    const auto pattern = AddressPattern::parse(text);
    REQUIRE(pattern);
    return pattern->depth();
}

}  // namespace

TEST_CASE("AddressPattern parses the shapes an akamatch list uses", "[address_pattern]") {
    CHECK(roundTrip("192:*") == "192:*");
    CHECK(roundTrip("172:16/*") == "172:16/*");
    CHECK(roundTrip("2:382/736.*") == "2:382/736.*");
    CHECK(roundTrip("2:382/736.120") == "2:382/736.120");
    CHECK(roundTrip("*") == "*");

    // A pattern that simply stops names the node itself, point zero — which is
    // how an address without a point is written, so it comes back out that way.
    CHECK(roundTrip("2:382/736") == "2:382/736");
    CHECK(roundTrip("2:382/736.0") == "2:382/736");

    // Surrounding space is the config file's, not the pattern's.
    CHECK(roundTrip("  2:382/736.*  ") == "2:382/736.*");
}

TEST_CASE("AddressPattern rejects what is not a pattern", "[address_pattern]") {
    CHECK(roundTrip("") == "-");
    CHECK(roundTrip("abc") == "-");
    CHECK(roundTrip("2") == "-");
    CHECK(roundTrip("2:382") == "-");
    CHECK(roundTrip("2:382/") == "-");
    CHECK(roundTrip("2:382/736.") == "-");
    CHECK(roundTrip("2:**") == "-");
    CHECK(roundTrip("2:*/46.*x") == "-");
    CHECK(roundTrip("2:382/736.120x") == "-");
}

TEST_CASE("AddressPattern is 4D and refuses a domain", "[address_pattern]") {
    // Nothing AmberEdit reads carries a domain, so a pattern naming one could only
    // fail to match. Better said out loud at startup than silently never used.
    CHECK(roundTrip("2:*@fidonet") == "-");
    CHECK(roundTrip("2:382/736.120@fidonet") == "-");
    CHECK(roundTrip("2:382/736@") == "-");

    // A 5D destination still matches on the four numbers it has.
    CHECK(hits("2:*", "2:5020/1@fidonet"));
    CHECK(hits("2:5020/1", "2:5020/1@fidonet"));
}

TEST_CASE("AddressPattern: a trailing star covers everything after it",
          "[address_pattern]") {
    CHECK(hits("192:*", "192:168/2"));
    CHECK(hits("192:*", "192:1/1.1"));
    CHECK_FALSE(hits("192:*", "2:5020/1"));

    CHECK(hits("172:16/*", "172:16/9.3"));
    CHECK_FALSE(hits("172:16/*", "172:17/9"));

    CHECK(hits("*", "2:5020/1"));
    CHECK(hits("*", "192:168/2.4"));
}

TEST_CASE("AddressPattern: a boss and his points", "[address_pattern]") {
    // ".*" is the boss too: a point of zero is a point like another, and the
    // config example says so in as many words.
    CHECK(hits("2:382/736.*", "2:382/736"));
    CHECK(hits("2:382/736.*", "2:382/736.120"));
    CHECK_FALSE(hits("2:382/736.*", "2:382/737.120"));

    // Without the star the node alone matches.
    CHECK(hits("2:382/736", "2:382/736"));
    CHECK_FALSE(hits("2:382/736", "2:382/736.120"));
}

TEST_CASE("AddressPattern: a wildcard in the middle stands for one component",
          "[address_pattern]") {
    CHECK(roundTrip("*:382/736") == "*:382/736");
    CHECK(hits("*:382/736", "2:382/736"));
    CHECK(hits("*:382/736", "3:382/736"));
    CHECK_FALSE(hits("*:382/736", "2:382/737"));

    CHECK(roundTrip("2:*/46.*") == "2:*/46.*");
    CHECK(hits("2:*/46.*", "2:5020/46.1"));
    CHECK_FALSE(hits("2:*/46.*", "3:5020/46.1"));
}

TEST_CASE("AddressPattern: depth counts components up to the first wildcard",
          "[address_pattern]") {
    CHECK(depthOf("*") == 0);
    CHECK(depthOf("*:382/736") == 0);
    CHECK(depthOf("192:*") == 1);
    CHECK(depthOf("172:16/*") == 2);
    CHECK(depthOf("2:382/736.*") == 3);
    CHECK(depthOf("2:382/736") == 4);
    CHECK(depthOf("2:382/736.120") == 4);
}
