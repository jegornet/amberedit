#include "echolist/echolist_area_source.hpp"

#include <catch2/catch.hpp>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "app/area_manager.hpp"
#include "echolist/echolist_writer.hpp"
#include "temp_dir.hpp"

using namespace amberedit;

namespace {

/// An area list handed over as it stands — the tosser config's part, with
/// nothing here reading a file.
class FixedAreas final : public ports::IAreaConfigSource {
public:
    explicit FixedAreas(std::vector<domain::AreaConfig> areas)
        : areas_(std::move(areas)) {}

    std::vector<domain::AreaConfig> loadAreas() override { return areas_; }

private:
    std::vector<domain::AreaConfig> areas_;
};

domain::AreaConfig areaOf(const std::string& tag, const std::string& description) {
    domain::AreaConfig area;
    area.tag = tag;
    area.description = description;
    return area;
}

std::unique_ptr<ports::IAreaConfigSource> inner() {
    return std::make_unique<FixedAreas>(std::vector<domain::AreaConfig>{
        areaOf("RU.LINUX", "what the tosser says"), areaOf("Su.Music", ""),
        areaOf("Local.Mail", "a base no echolist knows")});
}

/// A compiled echolist with something to say about two of those three.
std::string compiledEcholist(const test::TempDir& dir) {
    echolist::DbSource source;
    source.state.spec = "echo50.lst";
    source.entries = {{"Ru.Linux", "what the echolist says"}, {"SU.MUSIC", "Музыка!"}};
    const std::string path = dir.path("echolist.db");
    echolist::writeEcholistDb(path, {source}, 1);
    return path;
}

}  // namespace

TEST_CASE("with priority area the tosser's own description wins", "[echolist]") {
    test::TempDir dir;
    echolist::EcholistAreaSource source(inner(), compiledEcholist(dir),
                                        config::DescriptionPriority::Area);

    const auto areas = source.loadAreas();
    REQUIRE(areas.size() == 3);
    CHECK(areas[0].description == "what the tosser says");
    // Only a description that says something counts: an echo the tosser config
    // is silent about is described by the echolist whichever way round the
    // setting stands. The tag is matched folded.
    CHECK(areas[1].description == "Музыка!");
    CHECK(areas[2].description == "a base no echolist knows");
}

TEST_CASE("with priority echolist the echolist's description wins", "[echolist]") {
    test::TempDir dir;
    echolist::EcholistAreaSource source(inner(), compiledEcholist(dir),
                                        config::DescriptionPriority::Echolist);

    const auto areas = source.loadAreas();
    REQUIRE(areas.size() == 3);
    CHECK(areas[0].description == "what the echolist says");
    CHECK(areas[1].description == "Музыка!");
    // An echo no echolist names keeps what it came with, both ways round.
    CHECK(areas[2].description == "a base no echolist knows");
}

TEST_CASE("a compiled echolist that will not open leaves every area as it was",
          "[echolist]") {
    test::TempDir dir;

    // It is a convenience, like the nodelist: there is no version of "your
    // echolist is missing" worth an area list without descriptions in it.
    echolist::EcholistAreaSource missing(inner(), dir.path("gone.db"),
                                         config::DescriptionPriority::Echolist);
    const auto areas = missing.loadAreas();
    REQUIRE(areas.size() == 3);
    CHECK(areas[0].description == "what the tosser says");
    CHECK(areas[1].description.empty());
}

TEST_CASE("a config naming no compiled echolist is left unwrapped", "[echolist]") {
    config::AppConfig cfg;
    auto source = echolist::withEcholistDescriptions(inner(), cfg);
    CHECK(dynamic_cast<echolist::EcholistAreaSource*>(source.get()) == nullptr);

    test::TempDir dir;
    cfg.echolistDbPath = compiledEcholist(dir);
    cfg.areaDescriptionPriority = config::DescriptionPriority::Echolist;
    auto wrapped = echolist::withEcholistDescriptions(inner(), cfg);
    REQUIRE(dynamic_cast<echolist::EcholistAreaSource*>(wrapped.get()) != nullptr);
    CHECK(wrapped->loadAreas()[0].description == "what the echolist says");
}

TEST_CASE("the area list main() builds carries the echolist's descriptions",
          "[echolist]") {
    // The chain as `main()` puts it together: whatever the areas came from,
    // wrapped in this. Areas declared here rather than in a tosser config, so
    // that the test is about the wiring and not about a parser.
    test::TempDir dir;

    config::AppConfig cfg;
    config::ManualArea described;
    described.area.tag = "SU.MUSIC";
    described.area.type = domain::MsgBaseType::Passthrough;
    cfg.manualAreas.push_back(described);
    cfg.echolistDbPath = compiledEcholist(dir);

    auto source = echolist::withEcholistDescriptions(app::makeAreaSource(cfg), cfg);
    const auto areas = source->loadAreas();
    REQUIRE(areas.size() == 1);
    CHECK(areas[0].description == "Музыка!");
}
