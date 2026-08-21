#include <doctest/doctest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "app/area_manager.hpp"
#include "msgbase/null_lastread_store.hpp"
#include "temp_dir.hpp"
#include "test_paths.hpp"
#include "test_strings.hpp"

using amberedit::app::AreaManager;
using amberedit::config::AppConfig;
using amberedit::config::TosserConfigFormat;
using amberedit::domain::AreaConfig;
using amberedit::domain::FtnAddress;
using amberedit::domain::MsgBaseType;
using amberedit::test::TempDir;

namespace {

/// Hands back a fixed list, so the manager can be exercised without a tosser
/// config on disk.
class StubAreaSource final : public amberedit::ports::IAreaConfigSource {
public:
    explicit StubAreaSource(std::vector<AreaConfig> areas) : areas_(std::move(areas)) {}
    amberedit::Result<std::vector<AreaConfig>> loadAreas() override { return areas_; }

private:
    std::vector<AreaConfig> areas_;
};

AreaConfig areaNamed(const std::string& tag) {
    AreaConfig area;
    area.tag = tag;
    // Passthrough keeps reload() away from the disk: the AKA is filled in
    // before any base is opened, which is what these tests are about.
    area.type = MsgBaseType::Passthrough;
    return area;
}

AreaManager makeManager(std::vector<AreaConfig> areas, const AppConfig& config) {
    return AreaManager(std::make_unique<StubAreaSource>(std::move(areas)),
                       std::make_unique<amberedit::msgbase::NullLastReadStore>(), config);
}

AppConfig configWithAddress(const std::string& address) {
    AppConfig config;
    config.tosserConfigPath = "/dev/null";
    config.tosserConfigFormat = TosserConfigFormat::Fidoconfig;
    if (!address.empty()) config.userAddress = FtnAddress::parse(address);
    return config;
}

/// A config read from text, which is the only way to write the area groups the
/// tests below are about. The body states its own default_charset: which
/// charset an area is read in is half of what they check.
AppConfig configFrom(const std::string& body) {
    return amberedit::test::valueOf(
        AppConfig::loadFromString("tosser_config /dev/null\n"
                                  "tosser_config_format fidoconfig\n"
                                  "compose_charset CP866\n" +
                                  body));
}

const amberedit::app::AreaEntry& entryFor(const AreaManager& manager,
                                          const std::string& tag) {
    for (const auto& entry : manager.areas()) {
        if (entry.config.tag == tag) return entry;
    }
    FAIL("no area named " << tag);
    return manager.areas().front();
}

}  // namespace

TEST_CASE("An area with no AKA of its own takes the user's address [areamanager]") {
    // areas.bbs can never state one, and in the other two formats the option is
    // optional, so this is the common case rather than the exception.
    auto manager = makeManager({areaNamed("no.aka")}, configWithAddress("2:5020/1"));
    static_cast<void>(manager.reload());

    REQUIRE(manager.areas().size() == 1);
    CHECK(manager.areas()[0].config.address.toString() == "2:5020/1");
}

TEST_CASE("An area that states its own AKA keeps it [areamanager]") {
    AreaConfig area = areaNamed("has.aka");
    area.address = *FtnAddress::parse("192:168/1");

    auto manager = makeManager({area}, configWithAddress("2:5020/1"));
    static_cast<void>(manager.reload());

    REQUIRE(manager.areas().size() == 1);
    CHECK(manager.areas()[0].config.address.toString() == "192:168/1");
}

TEST_CASE("With no address anywhere the AKA stays unset [areamanager]") {
    auto manager = makeManager({areaNamed("no.aka")}, configWithAddress(""));
    static_cast<void>(manager.reload());

    REQUIRE(manager.areas().size() == 1);
    CHECK_FALSE(manager.areas()[0].config.address.isValid());
}

TEST_CASE("An area group's address outranks the tosser's [areamanager]") {
    // Both areas are presented under the same AKA by the tosser config. The
    // group says otherwise about one of them, and a group is the answer written
    // about that area in particular.
    AreaConfig sysop = areaNamed("r50.sysop");
    sysop.address = *FtnAddress::parse("192:168/1");
    AreaConfig linux = areaNamed("ru.linux");
    linux.address = *FtnAddress::parse("192:168/1");

    auto manager = makeManager({sysop, linux}, configFrom("default_charset CP866\n"
                                                          "address 2:5020/1\n"
                                                          "group\n"
                                                          "  member r50.sysop\n"
                                                          "  address 2:5020/9999\n"
                                                          "endgroup\n"));
    static_cast<void>(manager.reload());

    CHECK(entryFor(manager, "r50.sysop").config.address.toString() == "2:5020/9999");
    CHECK(entryFor(manager, "ru.linux").config.address.toString() == "192:168/1");
}

TEST_CASE("An area group decides the charset its area is read in [areamanager]") {
    // testdata/msgbase/charsets holds "Привет" three times: in KOI8-R with a
    // matching CHRS, in CP866 with its own, and once with no CHRS at all. The
    // third message is the one a default has any say over.
    AreaConfig charsets;
    charsets.tag = "esp.charsets";
    charsets.path = amberedit::test::projectPath("testdata/msgbase/charsets");
    charsets.type = MsgBaseType::Squish;

    const std::string expected = "Привет";
    const auto config = configFrom(
        "default_charset KOI8-R\n"
        "group\n"
        "  member esp.*\n"
        "  default_charset CP866\n"
        "endgroup\n");

    auto grouped = makeManager({charsets}, config);
    auto* base = amberedit::test::valueOf(grouped.openArea(charsets));
    REQUIRE(base != nullptr);
    CHECK(base->header(3).subject == expected);

    // The same base under a tag no group covers is read in the config's own
    // charset, and the word comes out as something else.
    AreaConfig outside = charsets;
    outside.tag = "ru.charsets";
    auto ungrouped = makeManager({outside}, config);
    auto* plain = amberedit::test::valueOf(ungrouped.openArea(outside));
    REQUIRE(plain != nullptr);
    CHECK(plain->header(3).subject != expected);
}

namespace {

using amberedit::app::AreaEntry;
using amberedit::app::sortAreas;
using amberedit::config::AreaSortCriterion;
using amberedit::config::AreaSortKey;
using amberedit::domain::AreaKind;

AreaEntry entryOf(const std::string& tag, uint32_t unread = 0,
                  AreaKind kind = AreaKind::Echo, const std::string& group = "") {
    AreaEntry entry;
    entry.config.tag = tag;
    entry.config.kind = kind;
    entry.config.group = group;
    entry.unread = unread;
    return entry;
}

std::vector<std::string> tagsOf(const std::vector<AreaEntry>& areas) {
    std::vector<std::string> tags;
    tags.reserve(areas.size());
    for (const auto& entry : areas) tags.push_back(entry.config.tag);
    return tags;
}

}  // namespace

TEST_CASE("sortAreas orders the list by echoid [areamanager][sort]") {
    std::vector<AreaEntry> areas{entryOf("Ru.Linux"), entryOf("alt.test"),
                                 entryOf("ru.fido")};

    sortAreas(areas, {{AreaSortKey::Echoid, false}});
    CHECK(tagsOf(areas) == std::vector<std::string>{"alt.test", "ru.fido", "Ru.Linux"});

    sortAreas(areas, {{AreaSortKey::Echoid, true}});
    CHECK(tagsOf(areas) == std::vector<std::string>{"Ru.Linux", "ru.fido", "alt.test"});
}

TEST_CASE("sortAreas orders the types net, echo, local [areamanager][sort]") {
    std::vector<AreaEntry> areas{
        entryOf("dupes", 0, AreaKind::Dupe), entryOf("local.notes", 0, AreaKind::Local),
        entryOf("ru.linux", 0, AreaKind::Echo), entryOf("netmail", 0, AreaKind::Netmail)};

    sortAreas(areas, {{AreaSortKey::Type, false}});
    CHECK(tagsOf(areas) ==
          std::vector<std::string>{"netmail", "ru.linux", "local.notes", "dupes"});
}

TEST_CASE("sortAreas breaks a tie with the next criterion [areamanager][sort]") {
    // "-u+e", the example the config documents: the unread areas first, and
    // alphabetical among those holding as much.
    std::vector<AreaEntry> areas{entryOf("ru.fido", 3), entryOf("alt.test", 0),
                                 entryOf("ru.linux", 7), entryOf("a.quiet.one", 3)};

    sortAreas(areas, {{AreaSortKey::Unread, true}, {AreaSortKey::Echoid, false}});
    CHECK(tagsOf(areas) ==
          std::vector<std::string>{"ru.linux", "a.quiet.one", "ru.fido", "alt.test"});
}

TEST_CASE("sortAreas leaves areas the criteria cannot tell apart alone "
          "[areamanager][sort]") {
    // Every one of them is an ungrouped echo, so the config's own order stands.
    const std::vector<std::string> written{"ru.fido", "alt.test", "ru.linux"};
    std::vector<AreaEntry> areas{entryOf("ru.fido"), entryOf("alt.test"),
                                 entryOf("ru.linux")};

    sortAreas(areas, {{AreaSortKey::Group, false}, {AreaSortKey::Type, true}});
    CHECK(tagsOf(areas) == written);

    // And an empty order is the way to ask for exactly that.
    sortAreas(areas, {});
    CHECK(tagsOf(areas) == written);
}

TEST_CASE("reload() sorts the list the config asks for [areamanager][sort]") {
    AppConfig config = configWithAddress("2:5020/1");
    config.areaListSort = {{AreaSortKey::Echoid, true}};

    auto manager = makeManager({areaNamed("alt.test"), areaNamed("ru.linux")}, config);
    static_cast<void>(manager.reload());

    CHECK(tagsOf(manager.areas()) == std::vector<std::string>{"ru.linux", "alt.test"});
}

TEST_CASE("reload() lists every area, available or not [areamanager]") {
    auto manager =
        makeManager({areaNamed("one"), areaNamed("two")}, configWithAddress("2:5020/1"));
    static_cast<void>(manager.reload());

    REQUIRE(manager.areas().size() == 2);
    for (const auto& entry : manager.areas()) {
        INFO(entry.config.tag);
        // Passthrough areas have no base, so they carry a reason instead.
        CHECK_FALSE(entry.isAvailable());
        CHECK(entry.config.address.toString() == "2:5020/1");
    }
}

TEST_CASE("reload() names each area before it opens it [areamanager]") {
    // What the rescan modal shows. The order is the config's, since the list is
    // only sorted once every base has been read.
    std::vector<std::string> reached;
    auto manager =
        makeManager({areaNamed("one"), areaNamed("two")}, configWithAddress("2:5020/1"));
    manager.reload([&reached](const std::string& tag) { reached.push_back(tag); });

    CHECK(reached == std::vector<std::string>{"one", "two"});
}

TEST_CASE("Entering an area with no base on disk makes one [areamanager][create]") {
    // The ordinary state of an area a tosser config has just declared: nothing
    // has written into it, so there is nothing to open. Entering it is what
    // brings the base into being, which is where the first message goes.
    const TempDir dir;
    AreaConfig area;
    area.tag = "new.echo";
    area.path = dir.path("new-echo");
    area.type = MsgBaseType::Squish;

    auto manager = makeManager({area}, configWithAddress("2:5020/1"));
    static_cast<void>(manager.reload());
    REQUIRE(manager.areas().size() == 1);
    // Nothing is created at startup: reload() opens every base there is, and a
    // rescan that wrote a spool would be a rescan nobody asked for.
    CHECK_FALSE(manager.areas()[0].isAvailable());
    CHECK_FALSE(std::filesystem::exists(area.path + ".sqd"));

    REQUIRE(manager.openArea(area).has_value());
    CHECK(std::filesystem::exists(area.path + ".sqd"));

    // And the row that said the area could not be read is brought up to date.
    manager.refreshArea(area);
    CHECK(manager.areas()[0].isAvailable());
    CHECK(manager.areas()[0].total == 0);
}

TEST_CASE("An area whose base cannot be created reports why [areamanager][create]") {
    const TempDir dir;
    // A regular file where the base's directory would have to be.
    const std::string blocked = dir.path("a-file");
    {
        std::ofstream(blocked) << "not a directory";
    }

    AreaConfig area;
    area.tag = "new.echo";
    area.path = blocked + "/new-echo";
    area.type = MsgBaseType::Squish;

    auto manager = makeManager({area}, configWithAddress("2:5020/1"));
    static_cast<void>(manager.reload());

    const auto opened = manager.openArea(area);
    CHECK_FALSE(opened.has_value());
    CHECK_FALSE(opened.error().empty());
}

TEST_CASE("A base that is there and unreadable is never created over "
          "[areamanager][create]") {
    // Half a base, or a broken one, holds something: an empty one written over
    // it would take that with it. Only "nothing at all is there" is answered by
    // making one.
    const TempDir dir;
    const std::string path = dir.path("broken");
    {
        std::ofstream(path + ".sqd") << std::string(300, '\0');
    }

    AreaConfig area;
    area.tag = "broken.echo";
    area.path = path;
    area.type = MsgBaseType::Squish;

    auto manager = makeManager({area}, configWithAddress("2:5020/1"));
    static_cast<void>(manager.reload());

    const auto opened = manager.openArea(area);
    CHECK_FALSE(opened.has_value());
    CHECK_FALSE(opened.error().empty());
    // Untouched: the file is exactly as long as it was.
    CHECK(std::filesystem::file_size(path + ".sqd") == 300);
}

TEST_CASE("An area declared in the config is read like any other [areamanager]") {
    // makeAreaSource() with no tosser config at all: the whole list is the
    // `area ... endarea` blocks, and everything reload() does to an area — the
    // AKA it fills in, the passthrough it marks, the group it resolves — it does
    // to these as well.
    const auto config =
        amberedit::test::valueOf(AppConfig::loadFromString("default_charset CP866\n"
                                                           "compose_charset CP866\n"
                                                           "address 2:5020/9999\n"
                                                           "area NOTES\n"
                                                           "  type passthrough\n"
                                                           "endarea\n"
                                                           "group\n"
                                                           "  member notes\n"
                                                           "  default_charset UTF-8\n"
                                                           "endgroup\n"));

    AreaManager manager(amberedit::test::valueOf(amberedit::app::makeAreaSource(config)),
                        std::make_unique<amberedit::msgbase::NullLastReadStore>(),
                        config);
    static_cast<void>(manager.reload());

    REQUIRE(manager.areas().size() == 1);
    const auto& entry = manager.areas().front();
    CHECK(entry.config.tag == "NOTES");
    CHECK(entry.config.address.toString() == "2:5020/9999");
    CHECK(entry.error == "passthrough");
    CHECK(config.effectiveFor(entry.config).defaultCharset == "UTF-8");
}
