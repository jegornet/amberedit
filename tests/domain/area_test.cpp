#include <doctest/doctest.h>

#include "domain/area.hpp"

using amberedit::domain::AreaConfig;
using amberedit::domain::AreaKind;
using amberedit::domain::MsgBaseType;

namespace {

AreaConfig areaOfKind(AreaKind kind) {
    AreaConfig area;
    area.tag = "test";
    area.path = "/ftn/test";
    area.type = MsgBaseType::Squish;
    area.kind = kind;
    return area;
}

}  // namespace

TEST_CASE("Only netmail addresses an actual recipient [area]") {
    CHECK(areaOfKind(AreaKind::Netmail).hasAddressedRecipient());

    // Echomail is broadcast to the area, so its destination address names
    // nobody; the same goes for the local, bad and dupe bases.
    CHECK_FALSE(areaOfKind(AreaKind::Echo).hasAddressedRecipient());
    CHECK_FALSE(areaOfKind(AreaKind::Local).hasAddressedRecipient());
    CHECK_FALSE(areaOfKind(AreaKind::Bad).hasAddressedRecipient());
    CHECK_FALSE(areaOfKind(AreaKind::Dupe).hasAddressedRecipient());
}

TEST_CASE("An area with no path is passthrough whatever its type [area]") {
    AreaConfig area = areaOfKind(AreaKind::Echo);
    CHECK_FALSE(area.isPassthrough());

    area.path.clear();
    CHECK(area.isPassthrough());

    area = areaOfKind(AreaKind::Echo);
    area.type = MsgBaseType::Passthrough;
    CHECK(area.isPassthrough());
}

TEST_CASE("Base and area kinds render as their config spellings [area]") {
    CHECK(nameOf(MsgBaseType::Squish) == "squish");
    CHECK(nameOf(MsgBaseType::Jam) == "jam");
    CHECK(nameOf(MsgBaseType::Sdm) == "msg");
    CHECK(nameOf(MsgBaseType::Passthrough) == "passthrough");
    CHECK(nameOf(MsgBaseType::Unknown) == "unknown");

    CHECK(nameOf(AreaKind::Echo) == "echo");
    CHECK(nameOf(AreaKind::Netmail) == "netmail");
}
