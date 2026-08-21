#include <doctest/doctest.h>

#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "app/area_manager.hpp"
#include "msgbase/fido_lastread_store.hpp"
#include "msgbase/jam_lastread_store.hpp"
#include "msgbase/lastread_file.hpp"
#include "msgbase/msgbase_lastread_store.hpp"
#include "msgbase/ftn_msgbase.hpp"
#include "msgbase/squish_lastread_store.hpp"
#include "temp_squish_base.hpp"

using amberedit::domain::AreaConfig;
using amberedit::domain::MsgBaseType;
using amberedit::msgbase::FidoLastReadStore;
using amberedit::msgbase::JamLastReadStore;
using amberedit::msgbase::MsgBaseLastReadStore;
using amberedit::msgbase::SquishLastReadStore;

namespace lastread_file = amberedit::msgbase::lastread_file;
namespace fs = std::filesystem;

namespace {

/// An empty directory that cleans itself up, for the files the stores write.
class TempDir {
public:
    TempDir() {
        dir_ = fs::temp_directory_path() /
               ("amberedit-lastread-" + std::to_string(::getpid()) + "-" +
                std::to_string(counter_++));
        fs::create_directories(dir_);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    [[nodiscard]] std::string basePath() const { return (dir_ / "area").string(); }
    [[nodiscard]] fs::path dir() const { return dir_; }

private:
    fs::path dir_;
    static inline int counter_ = 0;
};

/// Hands the manager a fixed area list, so no tosser config is needed.
class StubAreaSource final : public amberedit::ports::IAreaConfigSource {
public:
    explicit StubAreaSource(std::vector<AreaConfig> areas) : areas_(std::move(areas)) {}
    amberedit::Result<std::vector<AreaConfig>> loadAreas() override { return areas_; }

private:
    std::vector<AreaConfig> areas_;
};

AreaConfig areaAt(const std::string& path, MsgBaseType type) {
    AreaConfig area;
    area.tag = "test.area";
    area.path = path;
    area.type = type;
    return area;
}

std::vector<unsigned char> readFile(const std::string& path) {
    std::vector<unsigned char> out(static_cast<size_t>(lastread_file::fileSize(path)));
    if (!out.empty()) {
        REQUIRE(lastread_file::readBytes(path, 0, out.data(), out.size()));
    }
    return out;
}

}  // namespace

TEST_CASE("The JAM name CRC matches the published algorithm [lastread]") {
    // CRC-32 with the edb88320H polynomial seeded with ffffffffH and left
    // uninverted — the check value of the usual CRC-32 is cbf43926H, and this
    // is its complement.
    CHECK(lastread_file::nameCrc32("123456789") == 0x340bc6d9u);
    // Lowercased before hashing, so the case a name is written in does not
    // decide whose lastread record it is.
    CHECK(lastread_file::nameCrc32("All") == lastread_file::nameCrc32("all"));
    CHECK(lastread_file::nameCrc32("all") == 0xc4e78e22u);
}

TEST_CASE("Squish marks go to <base>.sql at the user's index [lastread]") {
    TempDir temp;
    const AreaConfig area = areaAt(temp.basePath(), MsgBaseType::Squish);

    SquishLastReadStore user0(0);
    SquishLastReadStore user2(2);

    // Nothing read yet, and no file at all — the same answer either way.
    CHECK(user0.getLastRead(area) == 0);

    user2.setLastRead(area, 0x11223344);
    CHECK(user2.getLastRead(area) == 0x11223344);
    // Each user has their own slot; writing one must not touch another's.
    CHECK(user0.getLastRead(area) == 0);

    // Four little-endian bytes per user, so user 2 starts at offset 8.
    const auto bytes = readFile(SquishLastReadStore::pathFor(area));
    REQUIRE(bytes.size() == 12);
    CHECK(bytes[8] == 0x44);
    CHECK(bytes[9] == 0x33);
    CHECK(bytes[10] == 0x22);
    CHECK(bytes[11] == 0x11);

    user0.setLastRead(area, 7);
    CHECK(user0.getLastRead(area) == 7);
    CHECK(user2.getLastRead(area) == 0x11223344);
}

TEST_CASE("A cleared Squish record reads as unread [lastread]") {
    TempDir temp;
    const AreaConfig area = areaAt(temp.basePath(), MsgBaseType::Squish);

    SquishLastReadStore store(0);
    store.setLastRead(area, 0xffffffffu);
    CHECK(store.getLastRead(area) == 0);
}

TEST_CASE("Fido marks go to a lastread file in the area directory [lastread]") {
    TempDir temp;
    const std::string dir = (temp.dir() / "netmail").string();
    fs::create_directories(dir);
    const AreaConfig area = areaAt(dir, MsgBaseType::Sdm);

    FidoLastReadStore user1(1);
    CHECK(user1.getLastRead(area) == 0);

    user1.setLastRead(area, 4242);
    CHECK(user1.getLastRead(area) == 4242);
    CHECK(fs::exists(fs::path(dir) / "lastread"));

    // Two little-endian bytes per user: user 1 sits at offset 2.
    const auto bytes = readFile(FidoLastReadStore::pathFor(area));
    REQUIRE(bytes.size() == 4);
    CHECK(lastread_file::readU16(bytes.data() + 2) == 4242);
}

TEST_CASE("A Fido mark past 16 bits is dropped rather than wrapped [lastread]") {
    TempDir temp;
    const std::string dir = (temp.dir() / "big").string();
    fs::create_directories(dir);
    const AreaConfig area = areaAt(dir, MsgBaseType::Sdm);

    FidoLastReadStore store(0);
    store.setLastRead(area, 1000);
    store.setLastRead(area, 70000);  // would come back as 4464
    CHECK(store.getLastRead(area) == 1000);
}

TEST_CASE("JAM records are found by the CRC of the user's name [lastread]") {
    TempDir temp;
    const AreaConfig area = areaAt(temp.basePath(), MsgBaseType::Jam);

    JamLastReadStore ivan(1, "Ivan Petrov");
    JamLastReadStore maria(2, "Maria Sidorova");

    CHECK(ivan.getLastRead(area) == 0);

    ivan.setLastRead(area, 100);
    maria.setLastRead(area, 200);
    CHECK(ivan.getLastRead(area) == 100);
    CHECK(maria.getLastRead(area) == 200);

    // Two records of sixteen bytes, the second appended rather than laid at an
    // index of its own — the file is searched, not indexed.
    const auto bytes = readFile(JamLastReadStore::pathFor(area));
    REQUIRE(bytes.size() == 32);
    CHECK(lastread_file::readU32(bytes.data()) ==
          lastread_file::nameCrc32("Ivan Petrov"));
    CHECK(lastread_file::readU32(bytes.data() + 4) == 1u);   // UserID
    CHECK(lastread_file::readU32(bytes.data() + 8) == 100u); // LastReadMsg

    // A record already in the file is rewritten in place.
    ivan.setLastRead(area, 150);
    CHECK(readFile(JamLastReadStore::pathFor(area)).size() == 32);
    CHECK(ivan.getLastRead(area) == 150);
    CHECK(maria.getLastRead(area) == 200);
}

TEST_CASE("The JAM high-read mark only ever moves forward [lastread]") {
    TempDir temp;
    const AreaConfig area = areaAt(temp.basePath(), MsgBaseType::Jam);
    JamLastReadStore store(0, "Ivan Petrov");

    store.setLastRead(area, 500);
    store.setLastRead(area, 300);  // read something older again

    const auto bytes = readFile(JamLastReadStore::pathFor(area));
    REQUIRE(bytes.size() == 16);
    CHECK(lastread_file::readU32(bytes.data() + 8) == 300u);   // LastReadMsg follows
    CHECK(lastread_file::readU32(bytes.data() + 12) == 500u);  // HighReadMsg does not
}

TEST_CASE("JAM without a user name in the config stores nothing [lastread]") {
    // There is no key to search the file on, and the CRC of the empty string
    // would claim whichever record happens to carry it.
    TempDir temp;
    const AreaConfig area = areaAt(temp.basePath(), MsgBaseType::Jam);

    JamLastReadStore store(0, "");
    store.setLastRead(area, 42);
    CHECK(store.getLastRead(area) == 0);
    CHECK_FALSE(fs::exists(JamLastReadStore::pathFor(area)));
}

TEST_CASE("The dispatcher writes each base type to its own file [lastread]") {
    TempDir temp;
    MsgBaseLastReadStore store(0, "Ivan Petrov");

    const AreaConfig squish = areaAt((temp.dir() / "sq").string(), MsgBaseType::Squish);
    const AreaConfig jam = areaAt((temp.dir() / "jm").string(), MsgBaseType::Jam);
    const std::string sdmDir = (temp.dir() / "msg").string();
    fs::create_directories(sdmDir);
    const AreaConfig sdm = areaAt(sdmDir, MsgBaseType::Sdm);

    store.setLastRead(squish, 11);
    store.setLastRead(jam, 22);
    store.setLastRead(sdm, 33);

    CHECK(fs::exists(temp.dir() / "sq.sql"));
    CHECK(fs::exists(temp.dir() / "jm.jlr"));
    CHECK(fs::exists(fs::path(sdmDir) / "lastread"));

    CHECK(store.getLastRead(squish) == 11);
    CHECK(store.getLastRead(jam) == 22);
    CHECK(store.getLastRead(sdm) == 33);
}

TEST_CASE("A passthrough area keeps no marks [lastread]") {
    TempDir temp;
    MsgBaseLastReadStore store(0, "Ivan Petrov");

    AreaConfig area = areaAt(temp.basePath(), MsgBaseType::Passthrough);
    store.setLastRead(area, 5);
    CHECK(store.getLastRead(area) == 0);
    CHECK(fs::is_empty(temp.dir()));
}

// --- end to end, on the Squish base in the repository ------------------------

TEST_CASE("Reading a message leaves a mark the next run resumes from "
          "[lastread][squish]") {
    amberedit::test::TempSquishBase base;
    const AreaConfig area = areaAt(base.path(), MsgBaseType::Squish);

    amberedit::config::AppConfig config;
    config.tosserConfigPath = "/dev/null";

    const auto makeManager = [&] {
        std::vector<AreaConfig> areas{area};
        return amberedit::app::AreaManager(
            std::make_unique<StubAreaSource>(std::move(areas)),
            std::make_unique<MsgBaseLastReadStore>(0, "Ivan Petrov"), config);
    };

    uint32_t total = 0;
    {
        auto manager = makeManager();
        static_cast<void>(manager.reload());
        amberedit::ports::IMsgBase* msgbase = manager.openArea(area);
        REQUIRE(msgbase != nullptr);
        total = msgbase->count();
        REQUIRE(total >= 3);

        manager.markRead(total - 2);
        // The area list is brought up to date without a reload, so leaving the
        // area shows the reading that was just done.
        REQUIRE(manager.areas().size() == 1);
        CHECK(manager.areas().front().unread == 2);
        manager.closeCurrentArea();
    }

    // A second run, reading the mark back off disk rather than out of memory.
    auto manager = makeManager();
    static_cast<void>(manager.reload());
    CHECK(manager.areas().front().unread == 2);

    REQUIRE(manager.openArea(area) != nullptr);
    // The mark is on total - 2 and the reader resumes on the message after it,
    // that being the first one not read yet.
    CHECK(manager.startingMessage(area, total) == total - 1);
}

TEST_CASE("Where an area resumes is reader_lastread_auto_next's "
          "[lastread][squish]") {
    amberedit::test::TempSquishBase base;
    const AreaConfig area = areaAt(base.path(), MsgBaseType::Squish);

    amberedit::config::AppConfig config;
    config.tosserConfigPath = "/dev/null";

    const auto makeManager = [&](bool autoNext) {
        config.lastreadAutoNext = autoNext;
        std::vector<AreaConfig> areas{area};
        return amberedit::app::AreaManager(
            std::make_unique<StubAreaSource>(std::move(areas)),
            std::make_unique<MsgBaseLastReadStore>(0, "Ivan Petrov"), config);
    };

    uint32_t total = 0;
    {
        auto manager = makeManager(true);
        static_cast<void>(manager.reload());
        amberedit::ports::IMsgBase* msgbase = manager.openArea(area);
        REQUIRE(msgbase != nullptr);
        total = msgbase->count();
        REQUIRE(total >= 3);
        manager.markRead(total - 2);
    }

    {
        // Off, the area opens on the message the mark names — the last one read.
        auto manager = makeManager(false);
        static_cast<void>(manager.reload());
        REQUIRE(manager.openArea(area) != nullptr);
        CHECK(manager.startingMessage(area, total) == total - 2);
    }

    // A mark on the newest message has nothing after it to move to, so both
    // settings open the area there.
    {
        auto manager = makeManager(true);
        static_cast<void>(manager.reload());
        REQUIRE(manager.openArea(area) != nullptr);
        manager.markRead(total);
        CHECK(manager.startingMessage(area, total) == total);
    }
}

TEST_CASE("An area with no mark opens at its first message "
          "[lastread][squish]") {
    amberedit::test::TempSquishBase base;
    const AreaConfig area = areaAt(base.path(), MsgBaseType::Squish);
    // The base in testdata comes with marks of its own; this is the area as
    // somebody who has never opened it finds it.
    fs::remove(base.path() + ".sql");

    amberedit::config::AppConfig config;
    config.tosserConfigPath = "/dev/null";
    // Nothing read is nothing read whichever way this stands: there is no
    // marked message to open on, and no message before the first one either.
    SUBCASE("with lastread_auto_next on") { config.lastreadAutoNext = true; }
    SUBCASE("and with it off") { config.lastreadAutoNext = false; }

    std::vector<AreaConfig> areas{area};
    amberedit::app::AreaManager manager(
        std::make_unique<StubAreaSource>(std::move(areas)),
        std::make_unique<MsgBaseLastReadStore>(0, "Ivan Petrov"), config);
    static_cast<void>(manager.reload());

    amberedit::ports::IMsgBase* msgbase = manager.openArea(area);
    REQUIRE(msgbase != nullptr);
    const uint32_t total = msgbase->count();
    REQUIRE(total >= 3);

    CHECK(manager.startingMessage(area, total) == 1);
    // Which is the whole of the area unread, and the area list says so.
    CHECK(manager.areas().front().unread == total);
}

TEST_CASE("A mark is stored as a UID, not as a position [lastread][squish]") {
    // What is on disk has to survive the base being packed, so it must be the
    // message's UID — and for this base the two differ.
    amberedit::test::TempSquishBase base;
    const AreaConfig area = areaAt(base.path(), MsgBaseType::Squish);

    amberedit::msgbase::FtnMsgBase msgbase;
    REQUIRE(msgbase.open(area));
    const uint32_t total = msgbase.count();
    REQUIRE(total >= 2);
    const uint32_t uid = msgbase.uidOf(total - 1);
    msgbase.close();

    amberedit::config::AppConfig config;
    config.tosserConfigPath = "/dev/null";
    std::vector<AreaConfig> areas{area};
    amberedit::app::AreaManager manager(
        std::make_unique<StubAreaSource>(std::move(areas)),
        std::make_unique<MsgBaseLastReadStore>(0, "Ivan Petrov"), config);
    static_cast<void>(manager.reload());
    REQUIRE(manager.openArea(area) != nullptr);
    manager.markRead(total - 1);
    manager.closeCurrentArea();

    CHECK(SquishLastReadStore(0).getLastRead(area) == uid);
}
