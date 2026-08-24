#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "domain/message.hpp"
#include "msgbase/ftn_msgbase.hpp"
#include "temp_dir.hpp"
#include "temp_squish_base.hpp"
#include "test_strings.hpp"

using amberedit::domain::AreaConfig;
using amberedit::domain::MessageDraft;
using amberedit::domain::MsgBaseType;
using amberedit::msgbase::FtnMsgBase;
using amberedit::test::TempDir;
using amberedit::test::TempSquishBase;
using amberedit::test::valueOf;

namespace fs = std::filesystem;

namespace {

/// Whether anything stands at `path`. The error_code overload throughout: one
/// of the tests asks about a path whose parent is a regular file, and what that
/// answers is an errno rather than an exception worth throwing.
bool present(const std::string& path) {
    std::error_code ec;
    return fs::exists(path, ec);
}

AreaConfig areaAt(const std::string& path, MsgBaseType type) {
    AreaConfig area;
    area.tag = "new.area";
    area.path = path;
    area.type = type;
    return area;
}

/// A message to put into a base that has just been made, which is the whole
/// point of making one: an area is created so that a first message can go in.
MessageDraft firstMessage() {
    MessageDraft draft;
    draft.from = "Yegor Gluhov";
    draft.to = "All";
    draft.subject = "The first message";
    draft.origAddr = *amberedit::domain::FtnAddress::parse("192:168/2");
    draft.destAddr = *amberedit::domain::FtnAddress::parse("192:168/2");
    draft.charset = "CP866";
    draft.kludges = {"MSGID: 192:168/2 68a1b2c3", "CHRS: CP866 2"};
    draft.lines = {"Hello from a base nothing had written to."};
    return draft;
}

/// Makes the base, opens it, writes one message and reads it back through a
/// second open — the round trip every format has to pass alike.
void checkCreatedBaseTakesAMessage(const AreaConfig& area) {
    FtnMsgBase created;
    REQUIRE(created.create(area).has_value());
    // Creating leaves the base on disk and the adapter closed on it: what is
    // read afterwards is the base as it stands, not as create() imagined it.
    CHECK_FALSE(created.isOpen());
    CHECK(FtnMsgBase::probeType(area.path) == area.type);
    CHECK_FALSE(FtnMsgBase::isAbsent(area));

    FtnMsgBase base;
    REQUIRE(base.open(area).has_value());
    CHECK(base.count() == 0);
    REQUIRE(valueOf(base.write(firstMessage())) == 1);
    base.close();

    FtnMsgBase again;
    REQUIRE(again.open(area).has_value());
    REQUIRE(again.count() == 1);
    CHECK(again.header(1).subject == "The first message");
    CHECK(again.header(1).from == "Yegor Gluhov");
    // A UID of its own, which is what a lastread mark will hold.
    CHECK(again.uidOf(1) != 0);
}

}  // namespace

TEST_CASE("A created Squish base opens empty and takes a message [create]") {
    TempDir dir;
    checkCreatedBaseTakesAMessage(areaAt(dir.path("fresh"), MsgBaseType::Squish));
}

TEST_CASE("A created JAM base opens empty and takes a message [create][jam]") {
    TempDir dir;
    checkCreatedBaseTakesAMessage(areaAt(dir.path("fresh"), MsgBaseType::Jam));
}

TEST_CASE("A created Fido *.msg base opens empty and takes a message "
          "[create][sdm]") {
    TempDir dir;
    checkCreatedBaseTakesAMessage(areaAt(dir.path("fresh"), MsgBaseType::Sdm));
}

TEST_CASE("Creating a base makes the files its format is read through "
          "[create]") {
    TempDir dir;

    SUBCASE("Squish") {
        const std::string path = dir.path("fresh");
        REQUIRE(FtnMsgBase().create(areaAt(path, MsgBaseType::Squish)).has_value());
        CHECK(fs::exists(path + ".sqd"));
        CHECK(fs::exists(path + ".sqi"));
        // The index holds one record per message, and there are none.
        CHECK(fs::file_size(path + ".sqi") == 0);
    }
    SUBCASE("JAM") {
        const std::string path = dir.path("fresh");
        REQUIRE(FtnMsgBase().create(areaAt(path, MsgBaseType::Jam)).has_value());
        CHECK(fs::exists(path + ".jhr"));
        CHECK(fs::exists(path + ".jdx"));
        CHECK(fs::exists(path + ".jdt"));
        CHECK(fs::file_size(path + ".jdx") == 0);
        CHECK(fs::file_size(path + ".jdt") == 0);
    }
    SUBCASE("Fido *.msg") {
        const std::string path = dir.path("fresh");
        REQUIRE(FtnMsgBase().create(areaAt(path, MsgBaseType::Sdm)).has_value());
        CHECK(fs::is_directory(path));
    }
}

TEST_CASE("A base that is already there is never created over [create]") {
    // The one thing creating must not do: an area someone is reading is an area
    // with messages in it, and an empty base written over it would take them.
    TempSquishBase existing;
    const AreaConfig area = areaAt(existing.path(), MsgBaseType::Squish);

    CHECK_FALSE(FtnMsgBase::isAbsent(area));

    FtnMsgBase base;
    const auto made = base.create(area);
    CHECK_FALSE(made.has_value());
    CHECK_FALSE(made.error()->message().empty());

    // And it is still the base it was.
    FtnMsgBase reader;
    REQUIRE(reader.open(area).has_value());
    CHECK(reader.count() > 0);
}

TEST_CASE("An area with no stated type is not one to create [create]") {
    // Nothing is on disk to work the format out from, and guessing at one would
    // write a base of the wrong kind that a tosser then refuses.
    TempDir dir;
    const AreaConfig area = areaAt(dir.path("fresh"), MsgBaseType::Unknown);

    CHECK_FALSE(FtnMsgBase::isAbsent(area));
    FtnMsgBase base;
    const auto made = base.create(area);
    CHECK_FALSE(made.has_value());
    CHECK_FALSE(made.error()->message().empty());
}

TEST_CASE("A passthrough area has no base to create [create]") {
    AreaConfig area;
    area.tag = "pass.through";
    area.type = MsgBaseType::Passthrough;

    CHECK_FALSE(FtnMsgBase::isAbsent(area));
    FtnMsgBase base;
    CHECK_FALSE(base.create(area).has_value());
}

TEST_CASE("A base that cannot be created says so and leaves nothing behind "
          "[create]") {
    TempDir dir;
    // A regular file where a directory would have to be: every format has to
    // put something under it, so none of them can.
    const std::string blocked = dir.path("a-file");
    { std::ofstream(blocked) << "not a directory"; }

    const std::string path = blocked + "/fresh";

    SUBCASE("Squish") {
        FtnMsgBase base;
        const auto made = base.create(areaAt(path, MsgBaseType::Squish));
        CHECK_FALSE(made.has_value());
        CHECK_FALSE(made.error()->message().empty());
        CHECK_FALSE(present(path + ".sqd"));
        CHECK_FALSE(present(path + ".sqi"));
    }
    SUBCASE("JAM") {
        FtnMsgBase base;
        const auto made = base.create(areaAt(path, MsgBaseType::Jam));
        CHECK_FALSE(made.has_value());
        CHECK_FALSE(made.error()->message().empty());
        CHECK_FALSE(present(path + ".jhr"));
        CHECK_FALSE(present(path + ".jdx"));
        CHECK_FALSE(present(path + ".jdt"));
    }
    SUBCASE("Fido *.msg") {
        FtnMsgBase base;
        const auto made = base.create(areaAt(path, MsgBaseType::Sdm));
        CHECK_FALSE(made.has_value());
        CHECK_FALSE(made.error()->message().empty());
        CHECK_FALSE(present(path));
    }
}
