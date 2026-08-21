#include "config/text_util.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "archive/zip_reader.hpp"
#include "echolist/echolist_db.hpp"
#include "nodelist/nodelist_db.hpp"
#include "temp_dir.hpp"
#include "test_strings.hpp"

using amberedit::config::text::readFile;
using amberedit::test::contains;
using amberedit::test::errorOf;

TEST_CASE("A whole file is read back as it was written [text_util]") {
    amberedit::test::TempDir dir;
    const std::string path = dir.path("plain.txt");
    {
        std::ofstream out(path, std::ios::binary);
        out << "one\ntwo\n";
    }
    CHECK(amberedit::test::valueOf(readFile(path)) == "one\ntwo\n");

    const std::string error = errorOf(readFile(dir.path("gone.txt")));
    CHECK_MESSAGE(contains(error, "gone.txt"), error);
}

TEST_CASE("A directory where a file was expected is refused by name [text_util]") {
    // Every system opens a directory as a stream without complaint, and then
    // they part company: libstdc++ throws out of the first read and libc++ hands
    // back an empty string. Both used to reach the caller — as an exception on
    // one and as an empty file that parsed cleanly on the other — so this is
    // asked of every whole-file read there is, and each of them is checked here.
    amberedit::test::TempDir dir;
    const std::string path = dir.path("a-directory");
    std::error_code ec;
    std::filesystem::create_directories(path, ec);

    const std::string error = errorOf(readFile(path));
    CHECK_MESSAGE(contains(error, "not a file"), error);
    CHECK_MESSAGE(contains(error, path), error);

    CHECK_MESSAGE(
        contains(errorOf(amberedit::nodelist::NodelistDb::open(path)), "not a file"),
        path);
    CHECK_MESSAGE(
        contains(errorOf(amberedit::echolist::EcholistDb::open(path)), "not a file"),
        path);
    CHECK_MESSAGE(
        contains(errorOf(amberedit::archive::ZipArchive::open(path)), "not a file"),
        path);
}
