#include <catch2/catch.hpp>

#include "ui/quick_search.hpp"

using namespace amberedit;

namespace {

std::vector<app::AreaEntry> areas(const std::vector<std::string>& tags) {
    std::vector<app::AreaEntry> out;
    out.reserve(tags.size());
    for (const auto& tag : tags) {
        app::AreaEntry entry;
        entry.config.tag = tag;
        out.push_back(std::move(entry));
    }
    return out;
}

}  // namespace

TEST_CASE("findAreaByPrefix finds the first area the query starts") {
    const auto list = areas({"Bad.News", "LocalNet", "Local.Talk", "Ru.Fido"});

    REQUIRE(ui::findAreaByPrefix(list, "lo") == 1);
    REQUIRE(ui::findAreaByPrefix(list, "local.") == 2);
    REQUIRE(ui::findAreaByPrefix(list, "B") == 0);
}

TEST_CASE("findAreaByPrefix ignores ASCII case in either direction") {
    const auto list = areas({"LocalNet"});

    REQUIRE(ui::findAreaByPrefix(list, "LOCAL") == 0);
    REQUIRE(ui::findAreaByPrefix(list, "lOcAl") == 0);
}

TEST_CASE("findAreaByPrefix matches the start of a tag only") {
    const auto list = areas({"Ru.LocalNet"});

    REQUIRE_FALSE(ui::findAreaByPrefix(list, "local").has_value());
}

TEST_CASE("findAreaByPrefix answers nothing for an empty query") {
    const auto list = areas({"LocalNet"});

    REQUIRE_FALSE(ui::findAreaByPrefix(list, "").has_value());
}

TEST_CASE("findAreaByPrefix answers nothing when no tag matches") {
    const auto list = areas({"LocalNet", "Ru.Fido"});

    REQUIRE_FALSE(ui::findAreaByPrefix(list, "zz").has_value());
    REQUIRE_FALSE(ui::findAreaByPrefix(areas({}), "lo").has_value());
}

TEST_CASE("findAreaByPrefix takes a query longer than the tag as no match") {
    const auto list = areas({"Lo"});

    REQUIRE_FALSE(ui::findAreaByPrefix(list, "local").has_value());
}
