#include <doctest/doctest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "config/manual_area_source.hpp"
#include "test_strings.hpp"

using amberedit::config::ManualArea;
using amberedit::config::ManualAreaSource;
using amberedit::domain::AreaConfig;
using amberedit::domain::MsgBaseType;
using amberedit::test::contains;
using amberedit::test::errorFrom;

namespace {

/// A tosser config source that hands back what it was made with, or throws:
/// what the real ones do is read a file, and this test is about neither file.
class FakeTosser final : public amberedit::ports::IAreaConfigSource {
public:
    explicit FakeTosser(std::vector<AreaConfig> areas) : areas_(std::move(areas)) {}
    explicit FakeTosser(std::string error) : error_(std::move(error)) {}

    std::vector<AreaConfig> loadAreas() override {
        if (!error_.empty()) throw std::runtime_error(error_);
        return areas_;
    }

private:
    std::vector<AreaConfig> areas_;
    std::string error_;
};

AreaConfig area(const std::string& tag, const std::string& path) {
    AreaConfig config;
    config.tag = tag;
    config.path = path;
    return config;
}

ManualArea declared(const std::string& tag, const std::string& path, int line) {
    ManualArea manual;
    manual.area = area(tag, path);
    manual.area.type = MsgBaseType::Jam;
    manual.line = line;
    return manual;
}

}  // namespace

TEST_CASE("ManualAreaSource puts the declared areas after the tosser's "
          "[manual_area]") {
    auto tosser = std::make_unique<FakeTosser>(
        std::vector<AreaConfig>{area("NETMAIL", "/ftn/msg/netmail"),
                                area("ru.linux", "/ftn/msg/ru.linux")});
    ManualAreaSource source({declared("NOTES", "/ftn/msg/notes", 12)}, std::move(tosser));

    const auto areas = source.loadAreas();
    REQUIRE(areas.size() == 3);
    // Each list in the order it was written, and the tosser's first — which is
    // what `arealist_sort ""` shows and what everything else sorts anyway.
    CHECK(areas[0].tag == "NETMAIL");
    CHECK(areas[1].tag == "ru.linux");
    CHECK(areas[2].tag == "NOTES");
    CHECK(areas[2].type == MsgBaseType::Jam);
}

TEST_CASE("ManualAreaSource works with no tosser at all [manual_area]") {
    ManualAreaSource source({declared("NOTES", "/ftn/msg/notes", 4)}, nullptr);

    const auto areas = source.loadAreas();
    REQUIRE(areas.size() == 1);
    CHECK(areas.front().tag == "NOTES");
}

TEST_CASE("A tag declared twice over is refused, naming the line [manual_area]") {
    auto tosser =
        std::make_unique<FakeTosser>(std::vector<AreaConfig>{area("RU.LINUX", "/ftn/a")});
    ManualAreaSource source({declared("ru.linux", "/ftn/b", 17)}, std::move(tosser));

    // Case-insensitively: an echotag is the same tag whichever case it is
    // written in, here as in a group's member pattern.
    const std::string error = errorFrom([&] { source.loadAreas(); });
    CHECK_MESSAGE(contains(error, "line 17"), error);
}

TEST_CASE("An unreadable tosser config still stops the load [manual_area]") {
    auto tosser = std::make_unique<FakeTosser>(std::string("cannot read /etc/areas"));
    ManualAreaSource source({declared("NOTES", "/ftn/msg/notes", 4)}, std::move(tosser));

    const std::string error = errorFrom([&] { source.loadAreas(); });
    CHECK_MESSAGE(contains(error, "cannot read"), error);
}
