#pragma once

#include <unistd.h>

#include <filesystem>
#include <string>
#include <system_error>

#include "test_paths.hpp"

namespace amberedit::test {

/// A copy of testdata/msgbase/area2 (JAM) in a temporary directory.
///
/// Same reason as TempSquishBase: the write tests change the base and the
/// lastread stores write beside it, so the fixture in the repository must
/// never be the one under test.
class TempJamBase {
public:
    TempJamBase() {
        namespace fs = std::filesystem;
        dir_ =
            fs::temp_directory_path() / ("amberedit-jam-" + std::to_string(::getpid()) +
                                         "-" + std::to_string(counter_++));
        fs::create_directories(dir_);
        for (const char* extension : {".jhr", ".jdt", ".jdx", ".jlr"}) {
            const fs::path source = projectPath("testdata/msgbase/area2") + extension;
            if (fs::exists(source))
                fs::copy_file(source, dir_ / ("area2" + std::string(extension)));
        }
    }
    ~TempJamBase() {
        std::error_code ec;
        std::filesystem::remove_all(dir_, ec);
    }

    TempJamBase(const TempJamBase&) = delete;
    TempJamBase& operator=(const TempJamBase&) = delete;

    /// The base path, extension excluded — what AreaConfig::path holds.
    [[nodiscard]] std::string path() const { return (dir_ / "area2").string(); }
    [[nodiscard]] std::filesystem::path dir() const { return dir_; }

private:
    std::filesystem::path dir_;
    static inline int counter_ = 0;
};

/// A copy of testdata/msgbase/netmail (Fido *.msg) in a temporary directory.
///
/// Unlike the other formats the base is the directory itself: one file per
/// message, named after its number.
class TempSdmBase {
public:
    TempSdmBase() {
        namespace fs = std::filesystem;
        dir_ =
            fs::temp_directory_path() / ("amberedit-sdm-" + std::to_string(::getpid()) +
                                         "-" + std::to_string(counter_++));
        fs::create_directories(dir_);
        const fs::path source = projectPath("testdata/msgbase/netmail");
        for (const auto& entry : fs::directory_iterator(source)) {
            if (entry.path().extension() == ".msg")
                fs::copy_file(entry.path(), dir_ / entry.path().filename());
        }
    }
    ~TempSdmBase() {
        std::error_code ec;
        std::filesystem::remove_all(dir_, ec);
    }

    TempSdmBase(const TempSdmBase&) = delete;
    TempSdmBase& operator=(const TempSdmBase&) = delete;

    /// The base path — for Fido *.msg that is the directory itself.
    [[nodiscard]] std::string path() const { return dir_.string(); }
    [[nodiscard]] std::filesystem::path dir() const { return dir_; }

private:
    std::filesystem::path dir_;
    static inline int counter_ = 0;
};

}  // namespace amberedit::test
