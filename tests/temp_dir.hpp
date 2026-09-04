#pragma once

#include <unistd.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>

#include "sys/env.hpp"

namespace amberedit::test {

/// An empty directory, taken away again with whatever was put in it.
///
/// Unlike TempSquishBase and the rest nothing is copied into it: it is for the
/// tests about a path where no base exists yet, which is what an area a tosser
/// config has just declared looks like on disk.
class TempDir {
public:
    TempDir() {
        namespace fs = std::filesystem;
        dir_ = fs::temp_directory_path() /
               ("amberedit-tmp-" + std::to_string(::getpid()) + "-" +
                std::to_string(counter_++));
        fs::create_directories(dir_);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(dir_, ec);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    /// A path inside it that nothing has made yet.
    [[nodiscard]] std::string path(const std::string& name) const {
        return (dir_ / name).string();
    }

private:
    std::filesystem::path dir_;
    static inline int counter_ = 0;
};

/// The system's temporary directory, said to be somewhere else for as long as
/// this lives — which is how a test stands in for the machine when the code
/// under test asks the system where to work.
///
/// All four names are set and put back, not just `TMPDIR`: which of them a
/// standard library reads for `temp_directory_path` is its own business, and a
/// test that set only the one libstdc++ reads would quietly go on using the real
/// /tmp somewhere else.
class WithTempDirEnv {
public:
    explicit WithTempDirEnv(const std::string& directory) {
        for (size_t i = 0; i < kNames.size(); ++i) {
            if (const char* was = ::getenv(kNames[i])) previous_[i] = std::string(was);
            amberedit::sys::setEnvironment(kNames[i], directory.c_str());
        }
    }
    ~WithTempDirEnv() {
        for (size_t i = 0; i < kNames.size(); ++i) {
            if (previous_[i]) {
                amberedit::sys::setEnvironment(kNames[i], previous_[i]->c_str());
            } else {
                amberedit::sys::unsetEnvironment(kNames[i]);
            }
        }
    }

    WithTempDirEnv(const WithTempDirEnv&) = delete;
    WithTempDirEnv& operator=(const WithTempDirEnv&) = delete;

private:
    static constexpr std::array<const char*, 4> kNames{"TMPDIR", "TMP", "TEMP",
                                                       "TEMPDIR"};
    std::array<std::optional<std::string>, 4> previous_;
};

}  // namespace amberedit::test
