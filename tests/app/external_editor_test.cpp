#include "app/external_editor.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "temp_dir.hpp"
#include "test_programs.hpp"
#include "test_strings.hpp"

using amberedit::app::commandWithMessageFile;
using amberedit::app::externalEditPath;
using amberedit::app::ExternalEdit;
using amberedit::app::externUtilDetachedPath;
using amberedit::app::externUtilMsgPath;
using amberedit::app::namesMessageFile;
using amberedit::app::runExternalEditor;
using amberedit::app::runUtilOnMessage;
using amberedit::test::contains;
using amberedit::test::errorOf;
using amberedit::test::TempDir;
using amberedit::test::WithTempDirEnv;
using Run = amberedit::config::ExternUtil::Run;

namespace {

/// A stand-in for the user's editor: the helper program, told to write `text`
/// over whatever file it is handed. An editor is a program that leaves a file
/// behind, and that is the whole of what AmberEdit asks of one.
///
/// `text` is written with `\n` and `\r` as two characters each; the helper puts
/// the real ones in. That keeps every newline in this file out of a command line
/// and makes the DOS-line-ending case say what it means.
std::vector<std::string> anEditorWriting(const std::string& text) {
    return {amberedit::test::stubProgram(), "write", "$msg", text};
}

/// One that writes nothing at all — every editor's way of saying the message
/// was not wanted.
std::vector<std::string> anEditorLeavingItAlone() {
    return {amberedit::test::stubProgram(), "nothing", "$msg"};
}

std::string contentsOf(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

}  // namespace

TEST_CASE("The file goes wherever $msg stands [externaleditor]") {
    CHECK(commandWithMessageFile({"mcedit", "$msg"}, "/tmp/m.msg") ==
          std::vector<std::string>{"mcedit", "/tmp/m.msg"});
    // Inside an argument as readily as alone in one, and every time it is
    // written.
    CHECK(commandWithMessageFile({"vi", "+1", "--file=$msg", "--also=$msg"}, "/t/x") ==
          std::vector<std::string>{"vi", "+1", "--file=/t/x", "--also=/t/x"});
    // A path that spells the placeholder itself is left as it is: what is put
    // in its place is not looked at again.
    CHECK(commandWithMessageFile({"vi", "$msg"}, "/tmp/$msg") ==
          std::vector<std::string>{"vi", "/tmp/$msg"});

    // Nothing put in its place is what the area list hands a utility: there is
    // no message on that screen, so the word stands for one that is not there
    // and the argument is left empty rather than left saying `$msg`.
    CHECK(commandWithMessageFile({"less", "$msg"}, "") ==
          std::vector<std::string>{"less", ""});
    CHECK(commandWithMessageFile({"wc", "-l", "$msg$msg"}, "") ==
          std::vector<std::string>{"wc", "-l", ""});
    // And a line that never writes it down is the line itself, filled in or not.
    CHECK(commandWithMessageFile({"mc"}, "/tmp/m.msg") == std::vector<std::string>{"mc"});
}

TEST_CASE("A command says for itself whether it wants the message [externaleditor]") {
    CHECK(namesMessageFile({"hunspell", "$msg"}));
    CHECK(namesMessageFile({"sh", "-c", "wc -l <$msg"}));
    // A utility that only wants the terminal — which is what every one of them
    // was until now.
    CHECK_FALSE(namesMessageFile({"mc"}));
    CHECK_FALSE(namesMessageFile({}));
}

TEST_CASE("The message is handed over as the file holds it [externaleditor]") {
    TempDir dir;
    const std::string file = dir.path("msg");
    // `cat` leaves the file exactly as it found it, which makes it the editor
    // to ask what was written into it.
    const auto edited =
        runExternalEditor(anEditorLeavingItAlone(), file,
                          {"Hello, Michiel", "", "Bye"}, "UTF-8");
    REQUIRE(edited.has_value());
    CHECK(contentsOf(file) == "Hello, Michiel\n\nBye\n");
}

TEST_CASE("A file that came back untouched changed nothing [externaleditor]") {
    TempDir dir;
    const std::string file = dir.path("msg");
    const std::vector<std::string> lines{"Hello, Michiel", "", "Bye"};

    const auto edited =
        runExternalEditor(anEditorLeavingItAlone(), file, lines, "UTF-8");
    REQUIRE(edited.has_value());
    CHECK_FALSE(edited->changed);
    // And the message is the one that was handed over, not an empty one read
    // back off a file nobody wrote to.
    CHECK(edited->lines == lines);
}

TEST_CASE("What the editor wrote is what comes back [externaleditor]") {
    TempDir dir;
    const std::string file = dir.path("msg");
    const auto edited = runExternalEditor(anEditorWriting("one\\ntwo\\n"), file,
                                          {"nothing"}, "UTF-8");
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
    const auto edited =
        runExternalEditor(anEditorWriting("Hello\\n"), file, {"Hello"}, "UTF-8");
    REQUIRE(edited.has_value());
    CHECK_FALSE(edited->changed);
}

TEST_CASE("DOS line endings and tabs do not reach the message [externaleditor]") {
    TempDir dir;
    const std::string file = dir.path("msg");
    const auto edited = runExternalEditor(anEditorWriting("one\\r\\n\\tindented\\r\\n"),
                                          file, {"x"}, "UTF-8");
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
        runExternalEditor(anEditorLeavingItAlone(), file, {greeting}, "CP866");
    REQUIRE(edited.has_value());
    // "Привет" in CP866 is six bytes, one per letter.
    CHECK(contentsOf(file) == "\x8F\xE0\xA8\xA2\xA5\xE2\n");
}

TEST_CASE("A charset nothing can write the message in is a failure [externaleditor]") {
    TempDir dir;
    // The editor is never reached: recoding the message is what fails, and it
    // happens before anything is started.
    const std::string error = errorOf(
        runExternalEditor(anEditorLeavingItAlone(), dir.path("msg"), {"Hello"},
                          "NO-SUCH-SET"));
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

TEST_CASE("A utility writes its copy of the message somewhere else [externaleditor]") {
    TempDir dir;
    const auto editors = externalEditPath(dir.path("work"));
    const auto utilitys = externUtilMsgPath(dir.path("work"));
    REQUIRE(editors.has_value());
    REQUIRE(utilitys.has_value());
    // Two files and not one: a utility is reached from the editor as readily as
    // from the reader, and the message being written is in the editor's for as
    // long as it is being written.
    CHECK(*editors != *utilitys);
    CHECK(contains(*utilitys, dir.path("work")));
}

TEST_CASE("What a utility left is read back and the file goes [externaleditor]") {
    TempDir dir;
    const std::string file = dir.path("util.msg");
    const auto left = runUtilOnMessage(anEditorWriting("Hello, Michiel\\n"),
                                       Run::Terminal, file, {"Helo, Michiel"}, "UTF-8");
    REQUIRE(left.has_value());
    CHECK(left->changed);
    CHECK(left->lines == std::vector<std::string>{"Hello, Michiel"});
    // Nothing is kept: there is no later moment to take it away in — the reader
    // drops what came back and the editor has already made a message of it.
    CHECK_FALSE(std::filesystem::exists(file));
}

TEST_CASE("A utility that would not start leaves no file behind [externaleditor]") {
    TempDir dir;
    const std::string file = dir.path("util.msg");
    const std::string error = errorOf(runUtilOnMessage(
        {"amberedit-no-such-utility", "$msg"}, Run::Terminal, file, {"Hello"}, "UTF-8"));
    CHECK_MESSAGE(contains(error, "amberedit-no-such-utility"), error);
    CHECK_FALSE(std::filesystem::exists(file));
}

TEST_CASE("A utility run without the terminal makes the same trip [externaleditor]") {
    TempDir dir;
    const std::string file = dir.path("util.msg");
    const auto left = runUtilOnMessage(anEditorWriting("Hello, Michiel\\n"),
                                       Run::NoTerminal, file, {"Helo, Michiel"}, "UTF-8");
    REQUIRE(left.has_value());
    CHECK(left->changed);
    CHECK(left->lines == std::vector<std::string>{"Hello, Michiel"});
    CHECK_FALSE(std::filesystem::exists(file));
}

TEST_CASE(
    "A detached utility is left the file and nothing is read back [externaleditor]") {
    TempDir dir;
    const std::string file = dir.path("util.msg");
    const auto left = runUtilOnMessage(anEditorWriting("Hello, Michiel\\n"),
                                       Run::Detached, file, {"Helo, Michiel"}, "UTF-8");
    REQUIRE(left.has_value());
    // What was handed over, whatever the program goes on to write: it has not
    // finished when the call comes back, so there is nothing to read yet.
    CHECK_FALSE(left->changed);
    CHECK(left->lines == std::vector<std::string>{"Helo, Michiel"});
    // And the file is not taken away from under it: it is the program's now.
    bool written = false;
    for (int tries = 0; tries < 500 && !written; ++tries) {
        written = contentsOf(file) == "Hello, Michiel\n";
        if (!written) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(written);

    // One that never started has nobody to leave a file to.
    const std::string gone = dir.path("gone.msg");
    const std::string error = errorOf(runUtilOnMessage(
        {"amberedit-no-such-utility", "$msg"}, Run::Detached, gone, {"Hello"}, "UTF-8"));
    CHECK_MESSAGE(contains(error, "amberedit-no-such-utility"), error);
    CHECK_FALSE(std::filesystem::exists(gone));
}

TEST_CASE("A detached utility gets a file no other run has [externaleditor]") {
    TempDir dir;
    const auto first = externUtilDetachedPath(dir.path("work"));
    REQUIRE(first.has_value());
    CHECK(contains(*first, dir.path("work")));
    CHECK(*first != *externUtilMsgPath(dir.path("work")));
    // A file left by an earlier run is passed over rather than written into:
    // the program it was handed to may be reading it still.
    std::ofstream(*first, std::ios::binary) << "still read\n";
    const auto second = externUtilDetachedPath(dir.path("work"));
    REQUIRE(second.has_value());
    CHECK(*second != *first);
    CHECK(contentsOf(*first) == "still read\n");
}

TEST_CASE(
    "A utility run for its output makes the same trip and hands it back "
    "[externaleditor]") {
    TempDir dir;
    // Forward slashes, so that a Windows path's `\tmp` does not reach the helper
    // as one of the escapes it reads its text with.
    const std::string file = std::filesystem::path(dir.path("util.msg")).generic_string();
    const auto left =
        runUtilOnMessage(anEditorWriting("Hello, Michiel\\n"), Run::PrintResult, file,
                         {"Helo, Michiel"}, "UTF-8");
    REQUIRE(left.has_value());
    CHECK(left->changed);
    CHECK(left->lines == std::vector<std::string>{"Hello, Michiel"});
    CHECK(left->output.bytes.empty());
    CHECK_FALSE(std::filesystem::exists(file));

    const auto said = runUtilOnMessage(
        {amberedit::test::stubProgram(), "both", "checked $msg\\n", "2 errors\\n"},
        Run::PrintResult, file, {"Hello"}, "UTF-8");
    REQUIRE(said.has_value());
    CHECK(said->output.bytes == "checked " + file + "\n2 errors\n");
}
