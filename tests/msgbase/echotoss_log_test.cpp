#include <doctest/doctest.h>

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "domain/ftn_address.hpp"
#include "domain/message.hpp"
#include "msgbase/ftn_msgbase.hpp"
#include "msgbase/echotoss_log.hpp"
#include "temp_dir.hpp"
#include "temp_squish_base.hpp"
#include "test_strings.hpp"

using amberedit::domain::AreaConfig;
using amberedit::domain::MsgBaseType;
using amberedit::msgbase::appendEchotossLog;
using amberedit::msgbase::FtnMsgBase;
using amberedit::test::TempDir;
using amberedit::test::TempSquishBase;
using amberedit::test::valueOf;

namespace {

/// The whole file as bytes, empty where nothing made it. Not read by lines:
/// what a tosser takes the file apart by is the byte between two names, and
/// that is the half of this worth checking.
std::string bytesOf(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

AreaConfig areaAt(const std::string& tag, const std::string& path) {
    AreaConfig area;
    area.tag = tag;
    area.path = path;
    area.type = MsgBaseType::Squish;
    return area;
}

/// A message as the compose screen leaves one: Loc and not Snt, which is what
/// `app::startingAttributes()` gives everything written here — and what decides
/// whether the area is named in the log.
amberedit::domain::MessageDraft helloDraft() {
    amberedit::domain::MessageDraft draft;
    draft.from = "Yegor Gluhov";
    draft.to = "All";
    draft.subject = "Hello";
    draft.origAddr = *amberedit::domain::FtnAddress::parse("2:382/736");
    draft.charset = "CP866";
    draft.lines = {"Hello!"};
    draft.attributes = amberedit::domain::attr::kLocal;
    return draft;
}

/// One that came off the network: neither attribute, which is every message a
/// tosser put in a base and the user then copied somewhere.
amberedit::domain::MessageDraft arrivedDraft() {
    amberedit::domain::MessageDraft draft = helloDraft();
    draft.attributes = 0;
    return draft;
}

/// One of the user's own that has already gone out.
amberedit::domain::MessageDraft sentDraft() {
    amberedit::domain::MessageDraft draft = helloDraft();
    draft.attributes = amberedit::domain::attr::kLocal | amberedit::domain::attr::kSent;
    return draft;
}

}  // namespace

TEST_CASE("A toss log that was never named writes nowhere [tosslog]") {
    const TempDir dir;
    const std::string path = dir.path("echotoss.log");

    appendEchotossLog("", "ru.linux");

    // Not "an empty file": a config naming no echotosslog leaves nothing on the
    // disk at all, which is what says the setting is off and not merely quiet.
    CHECK_FALSE(std::ifstream(path).good());
}

TEST_CASE("An area with no tag has no name to write [tosslog]") {
    const TempDir dir;
    const std::string path = dir.path("echotoss.log");

    appendEchotossLog(path, "");

    CHECK_FALSE(std::ifstream(path).good());
}

TEST_CASE("One area is one line, ended with LF and never deduplicated [tosslog]") {
    const TempDir dir;
    const std::string path = dir.path("echotoss.log");

    appendEchotossLog(path, "ru.linux");
    appendEchotossLog(path, "ru.fidonet.today");
    appendEchotossLog(path, "ru.linux");

    // The same area twice is two lines: this file holds what has happened since
    // the tosser last emptied it, and nothing here reads it back to find out
    // what is already in it.
    CHECK(bytesOf(path) == "ru.linux\nru.fidonet.today\nru.linux\n");
}

TEST_CASE("A toss log is added to and never rewritten [tosslog]") {
    const TempDir dir;
    const std::string path = dir.path("echotoss.log");
    {
        std::ofstream out(path, std::ios::binary);
        out << "ru.blah\n";
    }

    appendEchotossLog(path, "ru.linux");

    CHECK(bytesOf(path) == "ru.blah\nru.linux\n");
}

TEST_CASE("A message written into an area names it in the toss log [tosslog][squish]") {
    const TempDir dir;
    const std::string path = dir.path("echotoss.log");
    TempSquishBase base;
    FtnMsgBase msgbase("CP866", /*fieldLimits=*/true, /*ucsKludges=*/true, path);
    REQUIRE(msgbase.open(areaAt("localnet", base.path())).has_value());

    const uint32_t number = valueOf(msgbase.write(helloDraft()));
    REQUIRE(number != 0);
    CHECK(bytesOf(path) == "localnet\n");

    // Changing a message already in the base writes no second line: the area was
    // named when the message first reached it.
    REQUIRE(msgbase.replace(number, helloDraft()).has_value());
    CHECK(bytesOf(path) == "localnet\n");
}

TEST_CASE("Only a message that has to go out names its area [tosslog][squish]") {
    const TempDir dir;
    const std::string path = dir.path("echotoss.log");
    TempSquishBase base;
    FtnMsgBase msgbase("CP866", /*fieldLimits=*/true, /*ucsKludges=*/true, path);
    REQUIRE(msgbase.open(areaAt("localnet", base.path())).has_value());

    SUBCASE("mail that arrived from the network says nothing") {
        // A Copy or a Move of somebody else's message: it is in the base, and a
        // tosser told about it would send out mail this node never wrote.
        REQUIRE(valueOf(msgbase.write(arrivedDraft())) != 0);
        CHECK_FALSE(std::ifstream(path).good());
    }
    SUBCASE("the user's own that has already gone out says nothing") {
        REQUIRE(valueOf(msgbase.write(sentDraft())) != 0);
        CHECK_FALSE(std::ifstream(path).good());
    }
    SUBCASE("the user's own, unsent, names the area") {
        REQUIRE(valueOf(msgbase.write(helloDraft())) != 0);
        CHECK(bytesOf(path) == "localnet\n");
    }
}

TEST_CASE("A set names the area once, and only if something in it goes out "
          "[tosslog][squish]") {
    const TempDir dir;
    const std::string path = dir.path("echotoss.log");
    TempSquishBase base;
    FtnMsgBase msgbase("CP866", /*fieldLimits=*/true, /*ucsKludges=*/true, path);
    REQUIRE(msgbase.open(areaAt("localnet", base.path())).has_value());

    SUBCASE("a set of mail that arrived says nothing") {
        const std::vector<amberedit::domain::MessageDraft> carried = {
            arrivedDraft(), sentDraft(), arrivedDraft()};
        REQUIRE(msgbase.writeAll(carried).written == 3);
        CHECK_FALSE(std::ifstream(path).good());
    }
    SUBCASE("one message of the user's own among them names it, once") {
        // Three messages, one line: what the line says is that the area has
        // something new in it, and it says that no better for being written
        // once a message.
        const std::vector<amberedit::domain::MessageDraft> carried = {
            arrivedDraft(), helloDraft(), arrivedDraft()};
        REQUIRE(msgbase.writeAll(carried).written == 3);
        CHECK(bytesOf(path) == "localnet\n");
    }
}

TEST_CASE("A base with no toss log named writes none [tosslog][squish]") {
    const TempDir dir;
    const std::string path = dir.path("echotoss.log");
    TempSquishBase base;
    FtnMsgBase msgbase("CP866");
    REQUIRE(msgbase.open(areaAt("localnet", base.path())).has_value());

    REQUIRE(valueOf(msgbase.write(helloDraft())) != 0);
    CHECK_FALSE(std::ifstream(path).good());
}
