#include "app/user_shell.hpp"

#include <doctest/doctest.h>

#include <cstdlib>
#include <fstream>
#include <optional>
#include <string>

#include "sys/env.hpp"
#include "temp_dir.hpp"
#include "test_programs.hpp"
#include "test_strings.hpp"

using amberedit::app::runUserShell;
using amberedit::app::userShellPath;
using amberedit::test::contains;
using amberedit::test::errorOf;
using amberedit::test::TempDir;

namespace {

/// `$SHELL` said to be something else for as long as this lives, and put back
/// afterwards — an environment variable belongs to the whole process, so a test
/// that left one behind would be answering the next one's questions.
class WithShellEnv {
public:
    explicit WithShellEnv(const std::optional<std::string>& shell) {
        if (const char* was = ::getenv("SHELL")) previous_ = std::string(was);
        if (shell) {
            amberedit::sys::setEnvironment("SHELL", shell->c_str());
        } else {
            amberedit::sys::unsetEnvironment("SHELL");
        }
    }
    ~WithShellEnv() {
        if (previous_) {
            amberedit::sys::setEnvironment("SHELL", previous_->c_str());
        } else {
            amberedit::sys::unsetEnvironment("SHELL");
        }
    }

    WithShellEnv(const WithShellEnv&) = delete;
    WithShellEnv& operator=(const WithShellEnv&) = delete;

private:
    std::optional<std::string> previous_;
};

}  // namespace

TEST_CASE("The shell is the environment's, then the password file's [shell]") {
    const WithShellEnv shell(std::string("/some/where/ash"));
    CHECK(userShellPath() == "/some/where/ash");
}

TEST_CASE("Without $SHELL there is still a shell to run [shell]") {
    // What the password file gives this user, or /bin/sh where it gives
    // nothing. Which of the two it is belongs to the machine the test runs on;
    // what holds everywhere is that an absolute path comes back.
    const WithShellEnv none(std::nullopt);
    const std::string path = userShellPath();
    CHECK_FALSE(path.empty());
    // Absolute on POSIX, where the password file names a path and /bin/sh is
    // the fallback. On Windows the fallback is `cmd.exe` by name — %COMSPEC%
    // usually spells it out, but nothing says it must, and a name found on
    // %PATH% is as much a shell as a path is.
#ifndef _WIN32
    CHECK(path.front() == '/');
#endif

    // An empty $SHELL is the same as none at all: it names no shell either.
    const WithShellEnv blank(std::string(""));
    CHECK(userShellPath() == path);
}

TEST_CASE("A shell that runs and exits comes back with nothing to report "
          "[shell]") {
    const WithShellEnv shell(amberedit::test::stubProgram());

    // The helper with no mode named exits without doing anything, which is a
    // shell the user typed `exit` into as far as this is concerned.
    CHECK(runUserShell().has_value());
}

TEST_CASE("A shell that cannot be run is said so, by name [shell]") {
    // Nothing at that path at all, which is the failure that reads the same for
    // every user: a file without its execute bit is one root may run anyway.
    TempDir dir;
    const WithShellEnv shell(dir.path("no-such-shell"));

    const std::string error = errorOf(runUserShell());
    CHECK_MESSAGE(contains(error, dir.path("no-such-shell")), error);
}
