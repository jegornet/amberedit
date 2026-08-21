#include "ui/keys.hpp"

#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "config/text_util.hpp"
#include "temp_dir.hpp"
#include "test_paths.hpp"
#include "test_strings.hpp"
#include "ui/term/event.hpp"

using amberedit::test::contains;
using amberedit::test::errorFrom;
using amberedit::ui::KeyCommand;
using amberedit::ui::KeyMap;
using amberedit::ui::keyNamed;
using amberedit::ui::nameOf;
using amberedit::ui::spellingOf;
using amberedit::ui::term::Event;

namespace {

Event ctrl(char letter) {
    return Event::Character(std::string(1, letter), true);
}
Event alt(char letter) {
    return Event::Character(std::string(1, letter), false, true);
}

}  // namespace

TEST_CASE("The defaults are the layout AmberEdit has always had [keys]") {
    const KeyMap keys = KeyMap::defaults();

    CHECK(keys.is(Event::Character('l'), KeyCommand::ReaderList));
    CHECK(keys.is(Event::F9, KeyCommand::ReaderList));
    CHECK(keys.is(Event::Character('q'), KeyCommand::ReaderReply));
    CHECK(keys.is(Event::F4, KeyCommand::ReaderReply));
    CHECK(keys.is(Event::Delete, KeyCommand::ReaderDelete));
    CHECK(keys.is(ctrl('n'), KeyCommand::ReaderNodelist));
    CHECK(keys.is(Event::F10, KeyCommand::ReaderNodelist));
    CHECK(keys.is(ctrl('q'), KeyCommand::AppQuit));
    // Quitting is one chord: Ctrl-C is left to whatever a layout wants of it.
    CHECK_FALSE(keys.is(ctrl('c'), KeyCommand::AppQuit));
    CHECK(keys.is(ctrl('w'), KeyCommand::ComposeDeleteWord));
    CHECK(keys.is(alt('b'), KeyCommand::ComposeWordLeft));
    CHECK(keys.is(Event::Named(Event::Name::ArrowLeft, false, true),
                  KeyCommand::ComposeWordLeft));

    // F2 is two commands, and the two screens never meet.
    CHECK(keys.is(Event::F2, KeyCommand::ReaderChange));
    CHECK(keys.is(Event::F2, KeyCommand::ComposeSave));

    // A key that runs nothing, and a mouse report, which is never a binding.
    CHECK_FALSE(keys.is(Event::Character('z'), KeyCommand::ReaderList));
    CHECK_FALSE(keys.is(Event::Mouse({}), KeyCommand::ReaderList));

    // Alt reaches the terminal only for the letters a layout binds, and these
    // are they.
    CHECK(keys.altLetters() == "bf");
}

TEST_CASE("amberkeys.cfg.example is the defaults, written out [keys]") {
    // The example is what a user copies to start from, so a default it has
    // fallen behind on would be a layout that quietly changes as it is adopted.
    const std::string text = amberedit::config::text::readFile(
        amberedit::test::projectPath("amberkeys.cfg.example"));
    const KeyMap written = KeyMap::parse(text, "amberkeys.cfg.example");
    const KeyMap defaults = KeyMap::defaults();

    for (size_t i = 0; i < amberedit::ui::kKeyCommandCount; ++i) {
        const auto command = static_cast<KeyCommand>(i);
        INFO(nameOf(command));
        CHECK(written.keysOf(command) == defaults.keysOf(command));
    }
}

TEST_CASE("A layout is the whole of the layout [keys]") {
    const KeyMap keys = KeyMap::parse(
        "# what this system is used to\n"
        "\n"
        "F3   reader.find\n"
        "F3   compose.save\n",
        "keys");

    CHECK(keys.is(Event::F3, KeyCommand::ReaderFind));
    // The letter the default layout had is gone with the rest of it: a file is
    // a layout and not a list of corrections.
    CHECK_FALSE(keys.is(Event::Character('f'), KeyCommand::ReaderFind));
    // And a command the file never names has no key at all.
    CHECK(keys.keysOf(KeyCommand::ReaderList).empty());
    CHECK(keys.keysOf(KeyCommand::AppQuit).empty());
    // Two screens sharing one key is what F2 does by default, and is allowed.
    CHECK(keys.is(Event::F3, KeyCommand::ComposeSave));
}

TEST_CASE("A key is read the way it is written [keys]") {
    // Case tells two letters apart, and does not tell two spellings of a
    // modifier apart.
    CHECK(keyNamed("g") == Event::Character('g'));
    CHECK(keyNamed("G") == Event::Character('G'));
    CHECK(keyNamed("ctrl-n") == ctrl('n'));
    CHECK(keyNamed("CTRL-N") == ctrl('n'));
    CHECK(keyNamed("f10") == Event::F10);
    CHECK(keyNamed("Del") == Event::Delete);
    CHECK(keyNamed("Delete") == Event::Delete);
    CHECK(keyNamed("-") == Event::Character('-'));
    CHECK(keyNamed("Alt-Left") == Event::Named(Event::Name::ArrowLeft, false, true));

    // Ctrl goes with a letter and Alt with a letter or an arrow: nothing else
    // is a key a terminal can be made to report.
    CHECK_FALSE(keyNamed("Ctrl-F5"));
    CHECK_FALSE(keyNamed("Ctrl-+"));
    CHECK_FALSE(keyNamed("Alt-F5"));
    CHECK_FALSE(keyNamed("F13"));
    CHECK_FALSE(keyNamed("Ctrl"));
    CHECK_FALSE(keyNamed(""));

    // And what is written back out reads the same again.
    for (const char* spelling : {"g", "G", "Ctrl-N", "F10", "Del", "-", "Alt-Left"}) {
        const auto key = keyNamed(spelling);
        REQUIRE(key);
        CHECK(keyNamed(spellingOf(*key)) == key);
    }
}

TEST_CASE("The keys that move about cannot be bound [keys]") {
    for (const char* spelling :
         {"Esc", "escape", "Enter", "Return", "Tab", "Backspace", "Space", "Home", "End",
          "PgUp", "PageDown", "Up", "Down", "Left", "Right"}) {
        INFO(spelling);
        CHECK(amberedit::ui::isReservedKey(spelling));
        CHECK_FALSE(keyNamed(spelling));
    }
    // Bare only: Alt-Left is how a word is walked over.
    CHECK_FALSE(amberedit::ui::isReservedKey("Alt-Left"));

    const std::string error = errorFrom([&] {
        KeyMap::parse("Esc reader.list", "keys");
    });
    CHECK_MESSAGE(contains(error, "cannot be bound"), error);
}

TEST_CASE("A layout that cannot be read says which line is wrong [keys]") {
    const std::string error = errorFrom([&] { KeyMap::parse("l reader.lst", "keys"); });
    CHECK_MESSAGE(contains(error, "keys:1: no command is called reader.lst"), error);
    const std::string error2 = errorFrom([&] {
        KeyMap::parse("\nF13 reader.list", "keys");
    });
    CHECK_MESSAGE(contains(error2, "keys:2: no key is called F13"), error2);
    const std::string error3 = errorFrom([&] { KeyMap::parse("l\n", "keys"); });
    CHECK_MESSAGE(contains(error3, "a line is a key and a command"), error3);
    const std::string error4 = errorFrom([&] {
        KeyMap::parse("l reader.list now\n", "keys");
    });
    CHECK_MESSAGE(contains(error4, "a line is a key and a command"), error4);

    // One screen may not have a key twice, and the message names what it is
    // already doing.
    const std::string error5 = errorFrom([&] {
        KeyMap::parse("l reader.list\nl reader.find\n", "keys");
    });
    CHECK_MESSAGE(contains(error5, "l is already reader.list"), error5);
    // Quitting is answered before every screen, so it shares with nothing.
    const std::string error6 = errorFrom([&] {
        KeyMap::parse("x app.quit\nx compose.save\n", "keys");
    });
    CHECK_MESSAGE(contains(error6, "x is already app.quit"), error6);
}

TEST_CASE("A layout is read from the file the config names [keys]") {
    amberedit::test::TempDir dir;
    const std::string path = dir.path("amberkeys.cfg");
    {
        std::ofstream out(path);
        out << "  # indented comments count too\n\n";
        out << "F3\treader.find\n";
        out << "Alt-J compose.word-left\n";
    }

    const KeyMap keys = KeyMap::loadFromFile(path);
    CHECK(keys.is(Event::F3, KeyCommand::ReaderFind));
    // The terminal is told about the letters this layout uses and no others.
    CHECK(keys.altLetters() == "j");

    const std::string error = errorFrom([&] {
        KeyMap::loadFromFile(dir.path("gone.cfg"));
    });
    CHECK_MESSAGE(contains(error, "keys file not found"), error);

    // The file's own name is what a mistake in it is reported against, since
    // that is what the reader of the message has to go and open.
    const std::string bad = dir.path("bad.cfg");
    {
        std::ofstream out(bad);
        out << "F3 reader.find\nEsc reader.list\n";
    }
    const std::string error2 = errorFrom([&] { KeyMap::loadFromFile(bad); });
    CHECK_MESSAGE(contains(error2, bad + ":2: "), error2);
}
