#include <catch2/catch.hpp>

#include <fstream>
#include <string>
#include <vector>

#include "app/import_file.hpp"
#include "temp_dir.hpp"

using amberedit::app::importFile;
using amberedit::app::ImportMode;
using amberedit::app::ImportRequest;
using amberedit::app::uuencode;
using amberedit::test::TempDir;

namespace {

/// "Привет" in CP866 and in UTF-8 — the same pair the charset tests are
/// written round.
const std::string kPrivetCp866 = "\x8F\xE0\xA8\xA2\xA5\xE2";
const std::string kPrivetUtf8 = "Привет";

std::string write(const TempDir& dir, const std::string& name,
                  const std::string& content) {
    const std::string path = dir.path(name);
    std::ofstream out(path, std::ios::binary);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    return path;
}

/// The request for a text import with the default cut lines round it.
ImportRequest textRequest(const std::string& path, const std::string& charset) {
    return ImportRequest{path, ImportMode::Text, charset, "=== Cut ===", "=== Cut ==="};
}

/// uudecode, as far as the tests need one: the bytes a `begin`/`end` block
/// stands for. Written here rather than shelled out to so that what AmberEdit
/// writes is checked against the encoding itself and not against whichever
/// uudecode a machine happens to have.
std::string uudecode(const std::vector<std::string>& lines) {
    const auto six = [](char c) -> unsigned {
        return c == '`' ? 0u : static_cast<unsigned>(c - 0x20) & 0x3Fu;
    };

    std::string out;
    for (size_t i = 1; i + 1 < lines.size(); ++i) {
        const std::string& line = lines[i];
        if (line == "`") break;
        size_t left = six(line[0]);
        for (size_t at = 1; at + 4 <= line.size() && left > 0; at += 4) {
            const unsigned a = six(line[at]);
            const unsigned b = six(line[at + 1]);
            const unsigned c = six(line[at + 2]);
            const unsigned d = six(line[at + 3]);
            const unsigned char bytes[3] = {
                static_cast<unsigned char>((a << 2) | (b >> 4)),
                static_cast<unsigned char>(((b & 0x0Fu) << 4) | (c >> 2)),
                static_cast<unsigned char>(((c & 0x03u) << 6) | d)};
            for (size_t k = 0; k < 3 && left > 0; ++k, --left) {
                out += static_cast<char>(bytes[k]);
            }
        }
    }
    return out;
}

}  // namespace

TEST_CASE("importFile reads text in its own charset", "[import]") {
    const TempDir dir;
    const std::string path = write(dir, "note.txt", kPrivetCp866 + "\nsecond line\n");

    const auto result = importFile(textRequest(path, "CP866"));
    REQUIRE(result.ok());
    CHECK(result.lines == std::vector<std::string>{"=== Cut ===", kPrivetUtf8,
                                                   "second line", "=== Cut ==="});
}

TEST_CASE("importFile understands the FTN spelling of a charset", "[import]") {
    const TempDir dir;
    // +7_FIDO is CP866 under the name FTN gave it, and a user typing it into
    // the dialog means by it what a CHRS kludge means by it.
    const auto result =
        importFile(textRequest(write(dir, "note.txt", kPrivetCp866), "+7_FIDO"));
    REQUIRE(result.ok());
    CHECK(result.lines ==
          std::vector<std::string>{"=== Cut ===", kPrivetUtf8, "=== Cut ==="});
}

TEST_CASE("importFile writes no cut line the config leaves empty", "[import]") {
    const TempDir dir;
    const std::string path = write(dir, "note.txt", "one\n");

    ImportRequest request = textRequest(path, "UTF-8");
    request.beginLine.clear();
    request.endLine.clear();
    const auto result = importFile(request);
    REQUIRE(result.ok());
    CHECK(result.lines == std::vector<std::string>{"one"});
}

TEST_CASE("importFile makes a text file safe to carry", "[import]") {
    const TempDir dir;
    // A tab is opened out to the next stop, a CRLF is one line ending like any
    // other, and the control bytes go: a NUL among them would end the message
    // where FTS-0001 finds it rather than where it was written.
    const std::string content("a\tb\r\nc\0\ad\n", 10);
    const auto result = importFile(textRequest(write(dir, "note.txt", content), "UTF-8"));
    REQUIRE(result.ok());
    CHECK(result.lines ==
          std::vector<std::string>{"=== Cut ===", "a       b", "cd", "=== Cut ==="});
}

TEST_CASE("importFile disarms the commands a file carries", "[import]") {
    const TempDir dir;
    // A `CC:` line in a file being read into the message was written by
    // whoever wrote the file, and carrying it out would send copies the writer
    // of the message never asked for.
    const auto result = importFile(
        textRequest(write(dir, "note.txt", "CC: Ivan Ivanov\nplain\n"), "UTF-8"));
    REQUIRE(result.ok());
    CHECK(result.lines == std::vector<std::string>{"=== Cut ===", "!CC: Ivan Ivanov",
                                                   "plain", "=== Cut ==="});
}

TEST_CASE("importFile says what it could not read", "[import]") {
    const TempDir dir;

    const auto missing = importFile(textRequest(dir.path("nothing.txt"), "UTF-8"));
    CHECK_FALSE(missing.ok());
    CHECK(missing.lines.empty());
    CHECK_THAT(missing.error, Catch::Matchers::Contains("nothing.txt"));

    // A charset iconv does not know is an error rather than the bytes handed
    // back as they stand: the name was typed a moment ago, and mojibake in the
    // message is not something the user could undo afterwards.
    const auto unknown =
        importFile(textRequest(write(dir, "note.txt", kPrivetCp866), "CP8666"));
    CHECK_FALSE(unknown.ok());
    CHECK_THAT(unknown.error, Catch::Matchers::Contains("CP8666"));
}

TEST_CASE("uuencode writes a block uudecode reads back", "[import]") {
    std::string bytes;
    // Two full lines and a part of a third, so that the length character and
    // the padding of an incomplete group are both exercised.
    for (int i = 0; i < 100; ++i) bytes += static_cast<char>(i * 7 % 256);

    const std::vector<std::string> lines = uuencode(bytes, "blob.bin");
    REQUIRE(lines.size() >= 4);
    CHECK(lines.front() == "begin 644 blob.bin");
    CHECK(lines[lines.size() - 2] == "`");
    CHECK(lines.back() == "end");
    // 45 bytes to the line, and the length character in front of each.
    CHECK(lines[1].size() == 1 + (15 * 4));
    CHECK(lines[1][0] == 'M');
    CHECK(uudecode(lines) == bytes);
}

TEST_CASE("uuencode writes a zero as a backquote", "[import]") {
    // A space would be stripped off the end of a line somewhere between here
    // and whoever decodes it, and the line would come out a byte short.
    const std::vector<std::string> lines = uuencode(std::string(3, '\0'), "zero.bin");
    REQUIRE(lines.size() == 4);
    CHECK(lines[1] == "#````");
    CHECK(uudecode(lines) == std::string(3, '\0'));
}

TEST_CASE("importFile as UUE carries the file and no cut lines", "[import]") {
    const TempDir dir;
    const std::string bytes(
        "GIF89a\0\xFF"
        "binary",
        14);
    const std::string path = write(dir, "picture.gif", bytes);

    ImportRequest request = textRequest(path, "CP866");
    request.mode = ImportMode::Uue;
    const auto result = importFile(request);
    REQUIRE(result.ok());
    // The file's own name and nothing of the path it was read from, and a block
    // with nothing round it: what a decoder at the other end looks for is
    // `begin`, and a cut line in front of it is one more thing to trip over.
    CHECK(result.lines.front() == "begin 644 picture.gif");
    CHECK(result.lines.back() == "end");
    CHECK(uudecode(result.lines) == bytes);
}

TEST_CASE("importFile as UUE takes an empty file", "[import]") {
    const TempDir dir;
    const auto result = importFile(
        ImportRequest{write(dir, "empty.bin", ""), ImportMode::Uue, "", "", ""});
    REQUIRE(result.ok());
    CHECK(result.lines == std::vector<std::string>{"begin 644 empty.bin", "`", "end"});
}
