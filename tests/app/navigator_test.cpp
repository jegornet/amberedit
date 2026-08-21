#include <doctest/doctest.h>

#include "app/navigator.hpp"

using amberedit::app::Navigator;
using amberedit::app::ScreenId;

TEST_CASE("Navigator starts on the area list [navigator]") {
    const Navigator nav;
    CHECK(nav.current() == ScreenId::AreaList);
    CHECK(nav.depth() == 1);
}

TEST_CASE("Navigator walks all three screens and back [navigator]") {
    Navigator nav;

    nav.push(ScreenId::MessageList);
    CHECK(nav.current() == ScreenId::MessageList);

    nav.push(ScreenId::MessageRead);
    CHECK(nav.current() == ScreenId::MessageRead);
    CHECK(nav.depth() == 3);

    CHECK(nav.pop());
    CHECK(nav.current() == ScreenId::MessageList);
    CHECK(nav.pop());
    CHECK(nav.current() == ScreenId::AreaList);
}

TEST_CASE("Navigator does not pop past the root screen [navigator]") {
    Navigator nav;
    // false at the root tells the caller it is time to quit the application.
    CHECK_FALSE(nav.pop());
    CHECK(nav.current() == ScreenId::AreaList);
    CHECK(nav.depth() == 1);
}

TEST_CASE("Navigator::reset returns to the root from any depth [navigator]") {
    Navigator nav;
    nav.push(ScreenId::MessageList);
    nav.push(ScreenId::MessageRead);

    nav.reset();
    CHECK(nav.current() == ScreenId::AreaList);
    CHECK(nav.depth() == 1);
}
