#include <doctest/doctest.h>

#include "domain/ftn_address.hpp"

using amberedit::domain::FtnAddress;

TEST_CASE("FtnAddress parses a full address with a point [ftn_address]") {
    const auto addr = FtnAddress::parse("2:5020/9999.1");
    REQUIRE(addr.has_value());
    CHECK(addr->zone == 2);
    CHECK(addr->net == 5020);
    CHECK(addr->node == 9999);
    CHECK(addr->point == 1);
    CHECK(addr->toString() == "2:5020/9999.1");
}

TEST_CASE("FtnAddress parses an address without a point [ftn_address]") {
    const auto addr = FtnAddress::parse("2:5020/715");
    REQUIRE(addr.has_value());
    CHECK(addr->point == 0);
    CHECK(addr->toString() == "2:5020/715");
}

TEST_CASE("FtnAddress parses an address with a domain [ftn_address]") {
    const auto addr = FtnAddress::parse("192:168/2@localnet");
    REQUIRE(addr.has_value());
    CHECK(addr->zone == 192);
    CHECK(addr->net == 168);
    CHECK(addr->node == 2);
    CHECK(addr->domain == "localnet");
    CHECK(addr->toString() == "192:168/2@localnet");
}

TEST_CASE("FtnAddress ignores surrounding whitespace [ftn_address]") {
    const auto addr = FtnAddress::parse("  2:5020/1  ");
    REQUIRE(addr.has_value());
    CHECK(addr->node == 1);
}

TEST_CASE("FtnAddress rejects non-addresses [ftn_address]") {
    CHECK_FALSE(FtnAddress::parse("").has_value());
    CHECK_FALSE(FtnAddress::parse("ru.linux").has_value());
    CHECK_FALSE(FtnAddress::parse("-b").has_value());
    CHECK_FALSE(FtnAddress::parse("2:5020").has_value());
    CHECK_FALSE(FtnAddress::parse("squish").has_value());
    CHECK_FALSE(FtnAddress::parse("2:5020/715junk").has_value());
}

TEST_CASE("FtnAddress::isValid spots an empty address [ftn_address]") {
    CHECK_FALSE(FtnAddress{}.isValid());
    CHECK(FtnAddress::parse("2:5020/715")->isValid());
}
