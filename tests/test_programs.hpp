#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "sys/program.hpp"

namespace amberedit::test {

/// The helper program that stands in for the things a test needs to run: an
/// editor, a URL handler, a program whose arguments are then looked at. Built
/// beside the test binary, and found beside it rather than through a path
/// compiled in — the tests are cross-built as readily as native, and a path from
/// the build host means nothing on the machine that runs them.
[[nodiscard]] inline std::string stubProgram() {
    std::filesystem::path beside =
        sys::executablePath().parent_path() / "amberedit_test_stub";
#ifdef _WIN32
    beside += ".exe";
#endif
    return beside.string();
}

/// A program the system is certain to have on its path, given arguments that
/// make it succeed — for the one thing being asserted, which is that a bare name
/// was looked for on the path and found.
///
/// `true` on POSIX. Windows has no such program, and the one command every
/// installation has is the command interpreter itself; `cmd` is a bare name and
/// is found the same way, through %PATH% and %PATHEXT%.
[[nodiscard]] inline std::vector<std::string> aProgramOnThePath() {
#ifdef _WIN32
    return {"cmd", "/c", "exit", "0"};
#else
    return {"true"};
#endif
}

/// The same, given arguments that make it exit unhappily — for asserting that
/// what a program exited with is nobody's business.
[[nodiscard]] inline std::vector<std::string> aFailingProgramOnThePath() {
#ifdef _WIN32
    return {"cmd", "/c", "exit", "1"};
#else
    return {"false"};
#endif
}

}  // namespace amberedit::test
