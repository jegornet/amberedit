#include "config/temp_dir.hpp"

#include <doctest/doctest.h>

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "temp_dir.hpp"
#include "test_strings.hpp"

using namespace amberedit;
using amberedit::test::contains;
using amberedit::test::errorFrom;
namespace fs = std::filesystem;

namespace {

/// What the fallback directory is called on this machine: the system's own
/// temporary directory is where it lives, and the user is what tells it apart
/// from the one belonging to whoever else is logged in.
std::string oursUnder(const std::string& system) {
    return system + "/amberedit-" + std::to_string(::getuid());
}

}  // namespace

TEST_CASE("a config naming no tmpdir works under the system's own [temp_dir]") {
    // $TMPDIR is what says where the system's temporary directory is, so
    // pointing it at a directory of the test's own is the whole of standing in
    // for a machine here.
    test::TempDir dir;
    fs::create_directories(dir.path("system"));
    test::WithTempDirEnv env(dir.path("system"));

    const std::string made = config::makeTempDir("");
    CHECK(made == oursUnder(dir.path("system")));
    CHECK(fs::is_directory(made));

    // Nobody else's to look into or to write to, the directory above it being
    // one the whole machine shares.
    const auto permissions = fs::status(made).permissions();
    CHECK((permissions & fs::perms::group_all) == fs::perms::none);
    CHECK((permissions & fs::perms::others_all) == fs::perms::none);

    // Asked for twice is the same directory and not an error: it is made when it
    // is not there, and what is there already is what it was.
    CHECK(config::makeTempDir("") == made);
}

TEST_CASE("a tmpdir the config names is used as the user made it [temp_dir]") {
    test::TempDir dir;
    const std::string named = dir.path("work");
    fs::create_directories(named);
    fs::permissions(named,
                    fs::perms::owner_all | fs::perms::group_read | fs::perms::others_read,
                    fs::perm_options::replace);

    CHECK(config::makeTempDir(named) == named);

    // Where the user put it and who else may look in is their business: a
    // directory named in the config is not one to be tidied up after.
    const auto permissions = fs::status(named).permissions();
    CHECK((permissions & fs::perms::others_read) == fs::perms::others_read);

    // And one that is not there yet is made, a config being read long before
    // anything works in it.
    const std::string deeper = dir.path("work/further/down");
    CHECK(config::makeTempDir(deeper) == deeper);
    CHECK(fs::is_directory(deeper));
}

TEST_CASE("a temporary directory of ours that is not ours is refused [temp_dir]") {
    // Somebody else got to the name first. A link is the case that matters —
    // working through it would write wherever it points — and it is the case a
    // test can make without a second user to make it with.
    test::TempDir dir;
    fs::create_directories(dir.path("system"));
    fs::create_directories(dir.path("elsewhere"));
    test::WithTempDirEnv env(dir.path("system"));
    fs::create_directory_symlink(dir.path("elsewhere"), oursUnder(dir.path("system")));

    const std::string error = errorFrom([&] { config::makeTempDir(""); });
    CHECK_MESSAGE(contains(error, "symbolic link"), error);
}

TEST_CASE("a machine with no temporary directory at all says so [temp_dir]") {
    // $TMPDIR pointing at something that is not a directory leaves the system
    // with no answer to give, and then only the config can name one.
    test::TempDir dir;
    test::WithTempDirEnv env(dir.path("not-a-directory"));

    const std::string error = errorFrom([&] { config::makeTempDir(""); });
    CHECK_MESSAGE(contains(error, "tmpdir has to name one"), error);
}

TEST_CASE("a tmpdir that cannot be made says which one [temp_dir]") {
    test::TempDir dir;
    {
        std::ofstream out(dir.path("file"));
        out << "not a directory";
    }

    const std::string under = dir.path("file/under-a-file");
    const std::string error = errorFrom([&] { config::makeTempDir(under); });
    CHECK_MESSAGE(contains(error, "not one that can be made"), error);
    const std::string error2 = errorFrom([&] { config::makeTempDir(under); });
    CHECK_MESSAGE(contains(error2, under), error2);
}
