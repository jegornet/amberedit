#include <doctest/doctest.h>

#include <array>
#include <fstream>
#include <string>

#include "msgbase/binary_file.hpp"
#include "temp_dir.hpp"
#include "test_strings.hpp"

using amberedit::msgbase::BinaryFile;
using amberedit::msgbase::IoStatus;
using amberedit::test::contains;
using amberedit::test::TempDir;

namespace {

void writeBytes(const std::string& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

}  // namespace

/// The distinction the old `bool` could not draw. A base a half-finished tosser
/// left truncated and a base on a disk that will not read said exactly the same
/// nothing, and the `errno` behind the second was dropped where it happened.
TEST_CASE("A short file is told from a refused read [binary_file]") {
    const TempDir dir;
    const std::string path = dir.path("short.sqd");
    writeBytes(path, "only eight");  // ten bytes

    BinaryFile file;
    REQUIRE(file.open(path, false));

    SUBCASE("what is there reads") {
        std::array<unsigned char, 4> raw{};
        const IoStatus io = file.readAt(0, raw.data(), raw.size());
        CHECK(io.ok());
        CHECK_FALSE(io.failed());
        CHECK(io.message().empty());
    }

    SUBCASE("a record past the end is short, and says so") {
        std::array<unsigned char, 64> raw{};
        const IoStatus io = file.readAt(0, raw.data(), raw.size());
        REQUIRE(io.failed());
        CHECK_MESSAGE(contains(io.message(), "shorter than the record"), io.message());
    }

    SUBCASE("a read wholly past the end is short as well") {
        std::array<unsigned char, 4> raw{};
        const IoStatus io = file.readAt(4096, raw.data(), raw.size());
        REQUIRE(io.failed());
        CHECK_MESSAGE(contains(io.message(), "shorter than the record"), io.message());
    }
}

/// A file opened for reading is not one to write, and the reason is the
/// operating system's rather than a sentence of ours.
TEST_CASE("Writing where writing is not allowed names the reason [binary_file]") {
    const TempDir dir;
    const std::string path = dir.path("read_only.sqd");
    writeBytes(path, "some bytes");

    BinaryFile file;
    REQUIRE(file.open(path, false));
    REQUIRE_FALSE(file.writable());

    const std::string bytes = "no";
    const IoStatus io = file.writeAt(0, bytes.data(), bytes.size());
    REQUIRE(io.failed());
    // Whatever the platform calls EACCES — the point is that it is the system's
    // own words and not an empty failure.
    CHECK_FALSE(io.message().empty());
    CHECK_FALSE(contains(io.message(), "shorter than the record"));
}

/// Nothing is open, so there is nothing to blame the file for.
TEST_CASE("A closed file refuses rather than reads short [binary_file]") {
    BinaryFile file;
    std::array<unsigned char, 4> raw{};

    const IoStatus read = file.readAt(0, raw.data(), raw.size());
    REQUIRE(read.failed());
    CHECK_FALSE(contains(read.message(), "shorter than the record"));

    const IoStatus written = file.writeAt(0, raw.data(), raw.size());
    REQUIRE(written.failed());
    CHECK_FALSE(written.message().empty());
}
