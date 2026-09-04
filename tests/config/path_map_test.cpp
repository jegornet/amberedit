#include <doctest/doctest.h>

#include <string>

#include "config/path_map.hpp"

using amberedit::config::PathMap;

namespace {

PathMap mapping(const std::string& source, const std::string& target) {
    PathMap map;
    map.add(source, target);
    return map;
}

}  // namespace

TEST_CASE("map_path rewrites the tail onto the target [path_map]") {
    const PathMap map = mapping("c:\\fido", "/mnt/fido");

    CHECK(map.apply("c:\\fido\\msgbase\\test") == "/mnt/fido/msgbase/test");
    // The path is the source itself and there is no tail to join on.
    CHECK(map.apply("c:\\fido") == "/mnt/fido");
    // A trailing separator on either side changes nothing.
    CHECK(map.apply("c:\\fido\\") == "/mnt/fido");
    CHECK(mapping("c:\\fido\\", "/mnt/fido/").apply("c:\\fido\\msgbase") ==
          "/mnt/fido/msgbase");
}

TEST_CASE("map_path matches whole components, in either spelling [path_map]") {
    const PathMap map = mapping("c:\\fido", "/mnt/fido");

    // One rule covers a config written with either separator and in any case:
    // both are the same path, and a DOS config may hold both in one file.
    CHECK(map.apply("C:/FIDO/msgbase") == "/mnt/fido/msgbase");
    CHECK(map.apply("c:/fido\\msgbase") == "/mnt/fido/msgbase");

    // And never half of a name.
    CHECK(map.apply("c:\\fidonet\\msgbase") == "c:\\fidonet\\msgbase");
    // Nor a path that merely spells the same words without the drive.
    CHECK(map.apply("\\fido\\msgbase") == "\\fido\\msgbase");
}

TEST_CASE("map_path leaves a path no rule covers alone [path_map]") {
    const PathMap map = mapping("c:\\fido", "/mnt/fido");

    CHECK(map.apply("/home/ftn/msg/localnet") == "/home/ftn/msg/localnet");
    CHECK(map.apply("d:\\fido\\msgbase") == "d:\\fido\\msgbase");
    // An area with no base of its own has no path, and no rule invents one.
    CHECK(map.apply("").empty());
    CHECK(PathMap{}.apply("c:\\fido\\msgbase") == "c:\\fido\\msgbase");
}

TEST_CASE("map_path takes the rule that pins down the most [path_map]") {
    PathMap map;
    map.add("c:\\fido", "/mnt/fido");
    map.add("c:\\fido\\msgbase", "/var/spool/ftn");

    CHECK(map.apply("c:\\fido\\msgbase\\test") == "/var/spool/ftn/test");
    CHECK(map.apply("c:\\fido\\nodelist") == "/mnt/fido/nodelist");

    // And the order the two are written in does not decide it.
    PathMap reversed;
    reversed.add("c:\\fido\\msgbase", "/var/spool/ftn");
    reversed.add("c:\\fido", "/mnt/fido");
    CHECK(reversed.apply("c:\\fido\\msgbase\\test") == "/var/spool/ftn/test");
}

TEST_CASE("map_path writes the tail in the target's separator [path_map]") {
    // The ordinary case: a DOS tail arrives spelled the way this machine spells
    // a path, since that is what will be opened.
    CHECK(mapping("c:\\fido", "/mnt/fido").apply("c:\\fido\\msg\\test") ==
          "/mnt/fido/msg/test");
    // A target written in backslashes keeps them.
    CHECK(mapping("c:\\fido", "d:\\fido").apply("c:\\fido\\msg\\test") ==
          "d:\\fido\\msg\\test");
    // A root target leaves no doubled separator behind it.
    CHECK(mapping("c:\\fido", "/").apply("c:\\fido\\msg") == "/msg");
    CHECK(mapping("c:\\fido", "/").apply("c:\\fido") == "/");
}

TEST_CASE("map_path knows the source it already maps [path_map]") {
    const PathMap map = mapping("c:\\fido", "/mnt/fido");

    // Asked of the path and not of the text: these are all one source, and a
    // config stating it twice is stating two answers to one question.
    CHECK(map.maps("c:\\fido"));
    CHECK(map.maps("C:/FIDO/"));
    CHECK_FALSE(map.maps("c:\\fido\\msgbase"));
    CHECK_FALSE(map.maps("\\fido"));
}
