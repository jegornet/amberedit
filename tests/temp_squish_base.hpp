#pragma once

#include <unistd.h>

#include <filesystem>
#include <string>
#include <system_error>

#include "test_paths.hpp"

namespace amberedit::test {

/// A copy of testdata/msgbase/localnet in a temporary directory.
///
/// The write tests need a base they may change, and the lastread stores write
/// beside it, so the fixture in the repository must never be the one under
/// test.
class TempSquishBase {
public:
    TempSquishBase() {
        namespace fs = std::filesystem;
        dir_ =
            fs::temp_directory_path() / ("amberedit-test-" + std::to_string(::getpid()) +
                                         "-" + std::to_string(counter_++));
        fs::create_directories(dir_);
        for (const char* extension : {".sqd", ".sqi", ".sql"}) {
            const fs::path source = projectPath("testdata/msgbase/localnet") + extension;
            if (fs::exists(source))
                fs::copy_file(source, dir_ / ("localnet" + std::string(extension)));
        }
    }
    ~TempSquishBase() {
        std::error_code ec;
        std::filesystem::remove_all(dir_, ec);
    }

    TempSquishBase(const TempSquishBase&) = delete;
    TempSquishBase& operator=(const TempSquishBase&) = delete;

    /// The base path, extension excluded — what AreaConfig::path holds.
    [[nodiscard]] std::string path() const { return (dir_ / "localnet").string(); }
    [[nodiscard]] std::filesystem::path dir() const { return dir_; }

private:
    std::filesystem::path dir_;
    static inline int counter_ = 0;
};

}  // namespace amberedit::test
