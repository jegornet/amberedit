#include "app/external_editor.hpp"

#include <doctest/doctest.h>

#include <sys/stat.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "temp_dir.hpp"
#include "test_strings.hpp"

using amberedit::app::externalEditorCommand;
using amberedit::app::externalEditPath;
using amberedit::app::ExternalEdit;
using amberedit::app::runExternalEditor;
using amberedit::test::contains;
using amberedit::test::errorOf;
using amberedit::test::TempDir;
using amberedit::test::WithTempDirEnv;

namespace {

/// A stand-in for the user's editor: a shell script that writes `text` over
/// whatever file it was handed. An editor is a program that leaves a file
/// behind, and that is the whole of what AmberEdit asks of one.
std::string anEditorWriting(const TempDir& dir, const std::string& name,
                            const std::string& text) {
    const std::string path = dir.path(name);
    std::ofstream file(path);
    file << "#!/bin/sh\nprintf '%s' '" << text << "' > \"$1\"\n";
    file.close();
    REQUIRE(::chmod(path.c_str(), 0755) == 0);
    return path;
}

/// One that writes nothing at all — every editor's way of saying the message
/// was not wanted.
std::string anEditorLeavingItAlone(const TempDir& dir) {
    const std::string path = dir.path("quit");
    std::ofstream file(path);
    file << "#!/bin/sh\nexit 0\n";
    file.close();
    REQUIRE(::chmod(path.c_str(), 0755) == 0);
    return path;
}

std::string contentsOf(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

}  // namespace

TEST_CASE("The file goes wherever $msg stands [externaleditor]") {
    CHECK(externalEditorCommand({"mcedit", "$msg"}, "/tmp/m.msg") ==
          std::vector<std::string>{"mcedit", "/tmp/m.msg"});
    // Inside an argument as readily as alone in one, and every time it is
    // written.
    CHECK(externalEditorCommand({"vi", "+1", "--file=$msg", "--also=$msg"}, "/t/x") ==
          std::vector<std::string>{"vi", "+1", "--file=/t/x", "--also=/t/x"});
    // A path that spells the placeholder itself is left as it is: what is put
    // in its place is not looked at again.
    CHECK(externalEditorCommand({"vi", "$msg"}, "/tmp/$msg") ==
          std::vector<std::string>{"vi", "/tmp/$msg"});
}

TEST_CASE("The message is handed over as the file holds it [externaleditor]") {
    TempDir dir;
    const std::string file = dir.path("msg");
    // `cat` leaves the file exactly as it found it, which makes it the editor
    // to ask what was written into it.
    const auto edited =
        runExternalEditor({"true", "$msg"}, file, {"Hello, Michiel", "", "Bye"}, "UTF-8");
    REQUIRE(edited.has_value());
    CHECK(contentsOf(file) == "Hello, Michiel\n\nBye\n");
}

TEST_CASE("A file that came back untouched changed nothing [externaleditor]") {
    TempDir dir;
    const std::string file = dir.path("msg");
    const std::vector<std::string> lines{"Hello, Michiel", "", "Bye"};

    const auto edited =
        runExternalEditor({anEditorLeavingItAlone(dir), "$msg"}, file, lines, "UTF-8");
    REQUIRE(edited.has_value());
    CHECK_FALSE(edited->changed);
    // And the message is the one that was handed over, not an empty one read
    // back off a file nobody wrote to.
    CHECK(edited->lines == lines);
}

TEST_CASE("What the editor wrote is what comes back [externaleditor]") {
    TempDir dir;
    const std::string file = dir.path("msg");
    const std::string editor = anEditorWriting(dir, "write", "one\ntwo\n");

    const auto edited = runExternalEditor({editor, "$msg"}, file, {"nothing"}, "UTF-8");
    REQUIRE(edited.has_value());
    CHECK(edited->changed);
    CHECK(edited->lines == std::vector<std::string>{"one", "two"});
}

TEST_CASE("A message written back byte for byte is not a change [externaleditor]") {
    TempDir dir;
    const std::string file = dir.path("msg");
    // What `:wq` in vi comes to: the file is written again holding what it
    // held. The message was not wanted any less for the editor having saved it,
    // and it must not be read as an answer either way.
    const std::string editor = anEditorWriting(dir, "rewrite", "Hello\n");

    const auto edited = runExternalEditor({editor, "$msg"}, file, {"Hello"}, "UTF-8");
    REQUIRE(edited.has_value());
    CHECK_FALSE(edited->changed);
}

TEST_CASE("DOS line endings and tabs do not reach the message [externaleditor]") {
    TempDir dir;
    const std::string file = dir.path("msg");
    const std::string editor = anEditorWriting(dir, "dos", "one\r\n\tindented\r\n");

    const auto edited = runExternalEditor({editor, "$msg"}, file, {"x"}, "UTF-8");
    REQUIRE(edited.has_value());
    CHECK(edited->changed);
    // The tab is opened out to the next eight-column stop, exactly as the
    // import opens one out, and the carriage return is gone.
    CHECK(edited->lines == std::vector<std::string>{"one", "        indented"});
}

TEST_CASE("The file is written in the charset it was asked for [externaleditor]") {
    TempDir dir;
    const std::string file = dir.path("msg");

    // "Привет", written as bytes so that the source's own encoding is nothing
    // the test depends on.
    const std::string greeting = "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82";
    const auto edited =
        runExternalEditor({"true", "$msg"}, file, {greeting}, "CP866");
    REQUIRE(edited.has_value());
    // "Привет" in CP866 is six bytes, one per letter.
    CHECK(contentsOf(file) == "\x8F\xE0\xA8\xA2\xA5\xE2\n");
}

TEST_CASE("A charset nothing can write the message in is a failure [externaleditor]") {
    TempDir dir;
    const std::string error = errorOf(
        runExternalEditor({"true", "$msg"}, dir.path("msg"), {"Hello"}, "NO-SUCH-SET"));
    CHECK_MESSAGE(contains(error, "NO-SUCH-SET"), error);
}

TEST_CASE("An editor that will not start is said so, by name [externaleditor]") {
    TempDir dir;
    const std::string error = errorOf(runExternalEditor(
        {"amberedit-no-such-editor", "$msg"}, dir.path("msg"), {"Hello"}, "UTF-8"));
    CHECK_MESSAGE(contains(error, "amberedit-no-such-editor"), error);
}

TEST_CASE("The file is one of ours under the temporary directory [externaleditor]") {
    TempDir dir;
    // Where the config names one, it is used as it stands.
    const auto named = externalEditPath(dir.path("work"));
    REQUIRE(named.has_value());
    CHECK(contains(*named, dir.path("work")));
    CHECK(contains(*named, "amberedit-"));

    // Where it names none, the system's own answers — which is what a config
    // with no `tmpdir` line asks for.
    const std::string system = dir.path("system");
    std::filesystem::create_directories(system);
    const WithTempDirEnv pretending(system);
    const auto fallen = externalEditPath("");
    REQUIRE(fallen.has_value());
    CHECK(contains(*fallen, system));
}
