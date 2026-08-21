#pragma once

#include <string>

#ifndef AMBEREDIT_PROJECT_ROOT
#error "AMBEREDIT_PROJECT_ROOT must be defined by CMake"
#endif

namespace amberedit::test {

/// A path relative to the repository root, so the tests can be run from any
/// working directory.
inline std::string projectPath(const std::string& relative) {
    return std::string(AMBEREDIT_PROJECT_ROOT) + "/" + relative;
}

}  // namespace amberedit::test
