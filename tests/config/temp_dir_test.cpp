#include "config/temp_dir.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "sys/env.hpp"
#include "temp_dir.hpp"
#include "test_strings.hpp"

using namespace amberedit;
using amberedit::test::contains;
namespace fs = std::filesystem;

namespace {

/// What the fallback directory is called on this machine: the system's own
/// temporary directory is where it lives, and the user is what tells it apart
/// from the one belonging to whoever else is logged in.
std::string oursUnder(const std::string& system) {
    // Built the way `defaultTempDir()` builds it, separator and all: joined
    // through fs::path so the answer is spelled the way the platform spells a
    // path, and qualified by the user only where the system has not already
    // given them a temporary directory of their own.
    const std::string tag = amberedit::sys::userTag();
    const std::string name = tag.empty() ? "amberedit" : "amberedit-" + tag;
    return (std::filesystem::path(system) / name).string();
}

}  // namespace

TEST_CASE("a config naming no tmpdir works under the system's own [temp_dir]") {
    // $TMPDIR is what says where the system's temporary directory is, so
    // pointing it at a directory of the test's own is the whole of standing in
    // for a machine here.
    test::TempDir dir;
    fs::create_directories(dir.path("system"));
    test::WithTempDirEnv env(dir.path("system"));

    const std::string made = test::valueOf(config::makeTempDir(""));
    CHECK(made == oursUnder(dir.path("system")));
    CHECK(fs::is_directory(made));

    // Nobody else's to look into or to write to, the directory above it being
    // one the whole machine shares.
    //
    // POSIX only, and not because Windows is careless about it: there the
    // directory this one is made under is already the account's own, under its
    // profile, so there is nobody to be shut out. That is the same fact
    // `sys::userTag()` reports by answering with nothing there. And the mode
    // bits these read do not exist on Windows — `fs::permissions` cannot set
    // them and `fs::status` reports them as granted to everyone.
#ifndef _WIN32
    const auto permissions = fs::status(made).permissions();
    CHECK((permissions & fs::perms::group_all) == fs::perms::none);
    CHECK((permissions & fs::perms::others_all) == fs::perms::none);
#endif

    // Asked for twice is the same directory and not an error: it is made when it
    // is not there, and what is there already is what it was.
    CHECK(test::valueOf(config::makeTempDir("")) == made);
}

TEST_CASE("a tmpdir the config names is used as the user made it [temp_dir]") {
    test::TempDir dir;
    const std::string named = dir.path("work");
    fs::create_directories(named);
    fs::permissions(named,
                    fs::perms::owner_all | fs::perms::group_read | fs::perms::others_read,
                    fs::perm_options::replace);

    CHECK(test::valueOf(config::makeTempDir(named)) == named);

    // Where the user put it and who else may look in is their business: a
    // directory named in the config is not one to be tidied up after.
    const auto permissions = fs::status(named).permissions();
    CHECK((permissions & fs::perms::others_read) == fs::perms::others_read);

    // And one that is not there yet is made, a config being read long before
    // anything works in it.
    const std::string deeper = dir.path("work/further/down");
    CHECK(test::valueOf(config::makeTempDir(deeper)) == deeper);
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

    // Making one is a privilege on Windows — an administrator, or an account
    // with developer mode turned on — so where it cannot be made there is
    // nothing here to assert against. The check it stands for is in
    // `makeTempDir` either way, and runs wherever a link can be made at all.
    std::error_code ec;
    fs::create_directory_symlink(dir.path("elsewhere"), oursUnder(dir.path("system")), ec);
    if (ec) {
        MESSAGE("no symbolic link could be made here: " << ec.message());
        return;
    }

    const std::string error = test::errorOf(config::makeTempDir(""));
    CHECK_MESSAGE(contains(error, "symbolic link"), error);
}

TEST_CASE("a machine with no temporary directory at all says so [temp_dir]") {
    // $TMPDIR pointing at something that is not a directory leaves the system
    // with no answer to give, and then only the config can name one.
    test::TempDir dir;
    test::WithTempDirEnv env(dir.path("not-a-directory"));

    const std::string error = test::errorOf(config::makeTempDir(""));
    CHECK_MESSAGE(contains(error, "tmpdir has to name one"), error);
}

TEST_CASE("a tmpdir that cannot be made says which one [temp_dir]") {
    test::TempDir dir;
    {
        std::ofstream out(dir.path("file"));
        out << "not a directory";
    }

    const std::string under = dir.path("file/under-a-file");
    const std::string error = test::errorOf(config::makeTempDir(under));
    CHECK_MESSAGE(contains(error, "not one that can be made"), error);
    const std::string error2 = test::errorOf(config::makeTempDir(under));
    CHECK_MESSAGE(contains(error2, under), error2);
}
