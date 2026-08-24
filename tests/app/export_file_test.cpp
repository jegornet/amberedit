#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "app/export_file.hpp"
#include "app/import_file.hpp"
#include "config/text_util.hpp"
#include "domain/ftn_address.hpp"
#include "domain/message.hpp"
#include "temp_dir.hpp"
#include "test_strings.hpp"

using amberedit::app::exportedLines;
using amberedit::app::exportMessage;
using amberedit::app::ExportRequest;
using amberedit::app::saveUueFiles;
using amberedit::app::uueFiles;
using amberedit::app::uueFilesIn;
using amberedit::domain::FtnAddress;
using amberedit::domain::MessageBody;
using amberedit::domain::MessageDate;
using amberedit::domain::MessageHeader;
using amberedit::domain::MessageLine;
using amberedit::test::TempDir;
using amberedit::test::contains;
using amberedit::test::errorOf;

namespace {

/// "Привет" in CP866 and in UTF-8 — the pair the charset tests are written
/// round, here in the direction a message leaves in.
const std::string kPrivetCp866 = "\x8F\xE0\xA8\xA2\xA5\xE2";
const std::string kPrivetUtf8 = "Привет";

MessageHeader header() {
    MessageHeader head;
    head.number = 44;
    head.from = "Ivan Ivanov";
    head.to = "All";
    head.subject = "About the weather";
    head.date = MessageDate{2026, 8, 14, 20, 15, 0};
    head.utcOffset = "+0300";
    head.origAddr = *FtnAddress::parse("2:5020/1");
    return head;
}

MessageBody body() {
    MessageBody text;
    text.lines.push_back(MessageLine{"\x01MSGID: 2:5020/1 12345678", true, false});
    text.lines.push_back(MessageLine{"Hello, All!", false, false});
    text.lines.push_back(MessageLine{"", false, false});
    text.lines.push_back(MessageLine{"--- AmberEdit/linux 0.1", false, true});
    text.lines.push_back(MessageLine{"SEEN-BY: 5020/1", true, false});
    return text;
}

constexpr const char* kFormat = "%d %b %y %H:%M %z";

/// A file's worth of bytes with every value in it, zeros and high bits and all:
/// what a text export would ruin and what a uuencoded one carries whole.
std::string binary() {
    std::string bytes;
    for (int i = 0; i < 256; ++i) bytes += static_cast<char>(i);
    return bytes;
}

/// The lines a message carrying `bytes` under `name` holds, with a line of
/// somebody's text either side of the block — a file arrives in a message that
/// says something about it.
std::vector<std::string> messageCarrying(const std::string& bytes,
                                         const std::string& name) {
    std::vector<std::string> lines{"Here is the file you asked for.", ""};
    for (const auto& line : amberedit::app::uuencode(bytes, name)) lines.push_back(line);
    lines.emplace_back("");
    lines.emplace_back("--- AmberEdit/linux 0.1");
    return lines;
}

/// The same as a body, the service lines the reader hides among them.
MessageBody bodyOf(const std::vector<std::string>& lines) {
    MessageBody body;
    body.lines.push_back(MessageLine{"\x01MSGID: 2:5020/1 12345678", true, false});
    for (const auto& line : lines) body.lines.push_back(MessageLine{line, false, false});
    body.lines.push_back(MessageLine{"SEEN-BY: 5020/1", true, false});
    return body;
}

}  // namespace

TEST_CASE("exportedLines writes the header the reader draws [export]") {
    const auto lines = exportedLines(header(), body(), kFormat);

    REQUIRE(lines.size() >= 5);
    // The same labels and the same four rows the reader's block carries, so a
    // file reads the way the screen did — the address in brackets after the
    // name, where the base kept one.
    CHECK(lines[0] == "From : Ivan Ivanov (2:5020/1)");
    // A JAM echo keeps no sender address; a row with none is just the name.
    CHECK(lines[1] == "To   : All");
    CHECK(lines[2] == "Subj : About the weather");
    CHECK(lines[3] == "Date : 14 Aug 26 20:15 +0300");
    CHECK(lines[4] == std::string(72, '-'));

    // Then the message: its own lines, the service ones left out as the reader
    // leaves them out, and the tearline kept — it is a line of the message.
    CHECK(lines[5] == "Hello, All!");
    CHECK(lines[6].empty());
    CHECK(lines[7] == "--- AmberEdit/linux 0.1");
    CHECK(lines.size() == 8);
}

TEST_CASE("exportMessage writes the file in the charset it is given [export]") {
    const TempDir dir;
    const std::string path = dir.path("out.txt");

    MessageHeader head = header();
    head.subject = kPrivetUtf8;
    REQUIRE(
        exportMessage(ExportRequest{path, "CP866", kFormat}, head, body()).has_value());

    const std::string written =
        amberedit::test::valueOf(amberedit::config::text::readFile(path));
    // Above the adapter everything is UTF-8; what lands on disk is the charset
    // the file is to be read in.
    CHECK_MESSAGE(contains(written, "Subj : " + kPrivetCp866), written);
    CHECK_MESSAGE(contains(written, "Hello, All!\n"), written);
}

TEST_CASE("exportMessage adds to a file rather than writing over it [export]") {
    const TempDir dir;
    const std::string path = dir.path("out.txt");
    const ExportRequest request{path, "UTF-8", kFormat};

    REQUIRE(exportMessage(request, header(), body()).has_value());
    MessageHeader second = header();
    second.subject = "And another thing";
    REQUIRE(exportMessage(request, second, body()).has_value());

    // Exporting one message after another into one file is what an export is
    // usually for, so the second is added to the first rather than losing it.
    const auto lines = amberedit::config::text::splitLines(
        amberedit::test::valueOf(amberedit::config::text::readFile(path)));
    CHECK(lines.size() == 16);
    CHECK(lines[2] == "Subj : About the weather");
    CHECK(lines[10] == "Subj : And another thing");
}

TEST_CASE("uueFiles takes the file back out of the message [export][uue]") {
    const std::string bytes = binary();
    const auto files = uueFiles(bodyOf(messageCarrying(bytes, "report.zip")));

    REQUIRE(files.size() == 1);
    CHECK(files[0].name == "report.zip");
    // Byte for byte what went in: the import's own encoder run backwards, which
    // is the whole of what the two halves have to agree on.
    CHECK(files[0].bytes == bytes);
}

TEST_CASE("uueFiles finds every file in one message [export][uue]") {
    std::vector<std::string> lines = messageCarrying("first", "one.txt");
    for (const auto& line : messageCarrying("second", "two.txt")) lines.push_back(line);

    const auto files = uueFilesIn(lines);
    REQUIRE(files.size() == 2);
    CHECK(files[0].name == "one.txt");
    CHECK(files[0].bytes == "first");
    CHECK(files[1].name == "two.txt");
    CHECK(files[1].bytes == "second");
}

TEST_CASE("uueFiles finds nothing in an ordinary message [export][uue]") {
    // Words that begin a line and mean nothing of the kind: the block is a
    // shape, not a word, and a message about beginnings is not a file.
    CHECK(uueFilesIn({"begin at the beginning", "end of story", "beginning 644 x"})
              .empty());
    CHECK(uueFiles(body()).empty());
}

TEST_CASE("uueFiles refuses a file that is not all here [export][uue]") {
    // One section of a file split across several messages: there is no `end`,
    // and half a file decoded into a whole one is a file that will not open.
    std::vector<std::string> lines = messageCarrying("something", "part.zip");
    lines.erase(std::find(lines.begin(), lines.end(), "end"));
    CHECK(uueFilesIn(lines).empty());

    // And a block damaged in the middle — a line of somebody's typing where the
    // data should be — is not decoded as far as the damage.
    std::vector<std::string> broken = messageCarrying(binary(), "part.zip");
    broken[5] = "oops, sorry about that";
    CHECK(uueFilesIn(broken).empty());
}

TEST_CASE("uueFiles takes the name and not the path [export][uue]") {
    // What stands in the message came off somebody else's machine: a `../` in it
    // would write outside the directory the user picked, and a DOS path is what
    // FTN mail has carried names as since there was FTN mail.
    auto lines = messageCarrying("x", "../../etc/passwd");
    REQUIRE(uueFilesIn(lines).size() == 1);
    CHECK(uueFilesIn(lines)[0].name == "passwd");

    lines = messageCarrying("x", "C:\\DL\\FILE.ZIP");
    REQUIRE(uueFilesIn(lines).size() == 1);
    CHECK(uueFilesIn(lines)[0].name == "FILE.ZIP");

    // A name that is nothing but a path is no name at all.
    CHECK(uueFilesIn(messageCarrying("x", "..")).empty());
}

TEST_CASE("uueFiles decodes what the mail stripped [export][uue]") {
    // The encoding this reader writes puts a backquote where a zero goes, but
    // mail carries what other encoders wrote: a space, which whatever moved the
    // message has since taken off the end of the line. The bytes are zeros
    // either way, and the length character at the head of the line says how
    // many of them there are.
    const auto files = uueFilesIn({"begin 644 zeros.bin", "M", "`", "end"});
    REQUIRE(files.size() == 1);
    CHECK(files[0].bytes == std::string(45, '\0'));
}

TEST_CASE("saveUueFiles writes each file whole [export][uue]") {
    const TempDir dir;
    const std::string bytes = binary();
    const auto files = uueFilesIn(messageCarrying(bytes, "report.zip"));

    REQUIRE(saveUueFiles(dir.path(""), files).has_value());
    CHECK(amberedit::test::valueOf(
              amberedit::config::text::readFile(dir.path("report.zip"))) == bytes);
}

TEST_CASE("saveUueFiles writes over nothing [export][uue]") {
    const TempDir dir;
    {
        std::ofstream standing(dir.path("report.zip"), std::ios::binary);
        standing << "something of the user's own";
    }

    auto files = uueFilesIn(messageCarrying("first", "one.txt"));
    for (const auto& file : uueFilesIn(messageCarrying("second", "report.zip"))) {
        files.push_back(file);
    }

    // These names are the message's rather than the user's and there is nowhere
    // to change one, so a name already taken stops the export — and stops it
    // before anything is written, rather than leaving the directory half filled.
    const auto result = saveUueFiles(dir.path(""), files);
    CHECK_FALSE(result.has_value());
    const std::string error = errorOf(result);
    CHECK_MESSAGE(contains(error, "report.zip"), error);
    CHECK(amberedit::test::valueOf(amberedit::config::text::readFile(
              dir.path("report.zip"))) == "something of the user's own");
    CHECK_FALSE(std::filesystem::exists(dir.path("one.txt")));
}

TEST_CASE("exportMessage says where it could not write [export]") {
    const TempDir dir;
    // A directory that is not there: the name was given a moment ago and the
    // dialog is still up to be given another.
    const auto result = exportMessage(
        ExportRequest{dir.path("nowhere/out.txt"), "UTF-8", kFormat}, header(), body());
    CHECK_FALSE(result.has_value());
    const std::string error = errorOf(result);
    CHECK_MESSAGE(contains(error, "out.txt"), error);
}
