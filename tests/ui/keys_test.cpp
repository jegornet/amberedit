#include "ui/keys.hpp"

#include <doctest/doctest.h>

#include <fstream>
#include <string>
#include <vector>

#include "config/text_util.hpp"
#include "temp_dir.hpp"
#include "test_paths.hpp"
#include "test_strings.hpp"
#include "ui/term/event.hpp"

using amberedit::config::Commands;
using amberedit::test::contains;
using amberedit::test::errorOf;
using amberedit::test::valueOf;
using amberedit::ui::Command;
using amberedit::ui::KeyMap;
using amberedit::ui::keyNamed;
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

    CHECK(keys.is(Event::Character('l'), Command::ReaderList));
    CHECK(keys.is(Event::F9, Command::ReaderList));
    CHECK(keys.is(Event::Character('q'), Command::ReaderReply));
    CHECK(keys.is(Event::F4, Command::ReaderReply));
    // The comment on the message, which is the reply addressed to whoever it
    // was written to. A chord of its own: a bare letter is not what a command
    // wanted now and then should be a slip of the finger away from.
    CHECK(keys.is(alt('q'), Command::ReaderCommentReply));
    CHECK_FALSE(keys.is(Event::Character('q'), Command::ReaderCommentReply));
    CHECK(keys.is(Event::Delete, Command::ReaderDelete));
    CHECK(keys.is(ctrl('n'), Command::ReaderNodelist));
    CHECK(keys.is(Event::F10, Command::ReaderNodelist));
    CHECK(keys.is(ctrl('q'), Command::AppQuit));
    // Quitting is one chord: Ctrl-C is left to whatever a layout wants of it.
    CHECK_FALSE(keys.is(ctrl('c'), Command::AppQuit));
    CHECK(keys.is(ctrl('w'), Command::ComposeDeleteWord));
    CHECK(keys.is(Event::Named(Event::Name::Backspace, false, true),
                  Command::ComposeDeleteWord));
    // A bare Backspace is still the key that takes out one character.
    CHECK_FALSE(keys.is(Event::Backspace, Command::ComposeDeleteWord));
    CHECK(keys.is(alt('b'), Command::ComposeWordLeft));
    CHECK(keys.is(Event::Named(Event::Name::ArrowLeft, false, true),
                  Command::ComposeWordLeft));

    // F2 is two commands, and the two screens never meet.
    CHECK(keys.is(Event::F2, Command::ReaderChange));
    CHECK(keys.is(Event::F2, Command::ComposeSave));

    // A key that runs nothing, and a mouse report, which is never a binding.
    CHECK_FALSE(keys.is(Event::Character('z'), Command::ReaderList));
    CHECK_FALSE(keys.is(Event::Mouse({}), Command::ReaderList));

    // Alt reaches the terminal only for the letters a layout binds, and these
    // are they.
    CHECK(keys.altLetters() == "bfhq");
    // And the ESC in front of Backspace is claimed for the same reason.
    CHECK(keys.altBackspace());
}

TEST_CASE("amberkeys.cfg.example is the defaults, written out [keys]") {
    // The example is what a user copies to start from, so a default it has
    // fallen behind on would be a layout that quietly changes as it is adopted.
    const std::string text = amberedit::test::valueOf(amberedit::config::text::readFile(
        amberedit::test::projectPath("amberkeys.cfg.example")));
    const KeyMap written = valueOf(KeyMap::parse(text, "amberkeys.cfg.example"));
    const KeyMap defaults = KeyMap::defaults();

    for (size_t i = 0; i < amberedit::ui::kCommandCount; ++i) {
        const auto command = static_cast<Command>(i);
        INFO(Commands::of(command).name);
        CHECK(written.keysOf(command) == defaults.keysOf(command));
    }
}

TEST_CASE("A layout is the whole of the layout [keys]") {
    const KeyMap keys =
        valueOf(KeyMap::parse("# what this system is used to\n"
                              "\n"
                              "F3   reader.find\n"
                              "F3   compose.save\n",
                              "keys"));

    CHECK(keys.is(Event::F3, Command::ReaderFind));
    // The letter the default layout had is gone with the rest of it: a file is
    // a layout and not a list of corrections.
    CHECK_FALSE(keys.is(Event::Character('f'), Command::ReaderFind));
    // And a command the file never names has no key at all.
    CHECK(keys.keysOf(Command::ReaderList).empty());
    CHECK(keys.keysOf(Command::AppQuit).empty());
    // Two screens sharing one key is what F2 does by default, and is allowed.
    CHECK(keys.is(Event::F3, Command::ComposeSave));
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
    CHECK(keyNamed("alt-backspace") == Event::Named(Event::Name::Backspace, false, true));
    CHECK(keyNamed("Alt-F1") == Event::Named(Event::Name::F1, false, true));
    CHECK(keyNamed("alt-f12") == Event::Named(Event::Name::F12, false, true));

    // Ctrl goes with a letter and Alt with a letter, an arrow, a function key
    // or Backspace: nothing else is a key a terminal can be made to report.
    CHECK_FALSE(keyNamed("Ctrl-F5"));
    CHECK_FALSE(keyNamed("Ctrl-+"));
    CHECK_FALSE(keyNamed("Ctrl-Backspace"));
    CHECK_FALSE(keyNamed("Alt-F13"));
    CHECK_FALSE(keyNamed("Alt-Home"));
    CHECK_FALSE(keyNamed("F13"));
    CHECK_FALSE(keyNamed("Ctrl"));
    CHECK_FALSE(keyNamed(""));

    // And what is written back out reads the same again.
    for (const char* spelling :
         {"g", "G", "Ctrl-N", "F10", "Del", "-", "Alt-Left", "Alt-Backspace", "Alt-F1"}) {
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
    // Bare only: Alt-Left is how a word is walked over, and Alt-Backspace how
    // one is taken out.
    CHECK_FALSE(amberedit::ui::isReservedKey("Alt-Left"));
    CHECK_FALSE(amberedit::ui::isReservedKey("Alt-Backspace"));

    const std::string error = errorOf(KeyMap::parse("Esc reader.list", "keys"));
    CHECK_MESSAGE(contains(error, "cannot be bound"), error);
}

TEST_CASE("A layout that cannot be read says which line is wrong [keys]") {
    const std::string error = errorOf(KeyMap::parse("l reader.lst", "keys"));
    CHECK_MESSAGE(contains(error, "keys:1: no command is called reader.lst"), error);
    const std::string error2 = errorOf(KeyMap::parse("\nF13 reader.list", "keys"));
    CHECK_MESSAGE(contains(error2, "keys:2: no key is called F13"), error2);
    const std::string error3 = errorOf(KeyMap::parse("l\n", "keys"));
    CHECK_MESSAGE(contains(error3, "a line is a key and a command"), error3);
    const std::string error4 = errorOf(KeyMap::parse("l reader.list now\n", "keys"));
    CHECK_MESSAGE(contains(error4, "a line is a key and a command"), error4);

    // One screen may not have a key twice, and the message names what it is
    // already doing.
    const std::string error5 =
        errorOf(KeyMap::parse("l reader.list\nl reader.find\n", "keys"));
    CHECK_MESSAGE(contains(error5, "l is already reader.list"), error5);
    // Quitting is answered before every screen, so it shares with nothing.
    const std::string error6 =
        errorOf(KeyMap::parse("x app.quit\nx compose.save\n", "keys"));
    CHECK_MESSAGE(contains(error6, "x is already app.quit"), error6);
}

TEST_CASE("A layout written with the older names is still read [keys]") {
    // Two-word commands were spelled with a `-` once — `compose.word-left` —
    // and a file that still spells them that way binds the same commands.
    const KeyMap keys = valueOf(KeyMap::parse(
        "Alt-J compose.word-left\nCtrl-Y compose.delete-line\n", "keys"));
    CHECK(keys.is(alt('j'), Command::ComposeWordLeft));
    CHECK(keys.is(ctrl('y'), Command::ComposeDeleteLine));
}

TEST_CASE("A merged layout keeps what the file did not move [keys]") {
    // `keys_mode merge`: the file is a handful of corrections, and everything
    // it says nothing about goes on working as it did.
    const KeyMap file = valueOf(KeyMap::parse("k reader.list\n", "keys"));
    const KeyMap keys = file.mergedOnto(KeyMap::defaults());

    CHECK(keys.is(Event::Character('k'), Command::ReaderList));
    // The keys the default layout had for it are keys the file took nothing
    // from, so the command answers to all three: merging adds a key and does
    // not move one — `keys_mode clear` is what moves a key.
    CHECK(keys.is(Event::Character('l'), Command::ReaderList));
    CHECK(keys.is(Event::F9, Command::ReaderList));
    CHECK(keys.keysOf(Command::ReaderList) ==
          std::vector<Event>{Event::Character('k'), Event::Character('l'), Event::F9});
    // And the file's own key is the one a hint is drawn from: it is the key the
    // user asked this command to be under.
    CHECK(keys.preferredKey(Command::ReaderList) == Event::Character('k'));
    // A command the file never names is untouched, whichever screen it is on.
    CHECK(keys.is(ctrl('q'), Command::AppQuit));
    CHECK(keys.is(Event::Character('i'), Command::ReaderInfo));
    CHECK(keys.keysOf(Command::ComposeSave) == std::vector<Event>{ctrl('s'), Event::F2});
}

TEST_CASE("A merged layout settles a clash in the file's favour [keys]") {
    // `k` was Kludges and `l` was List, and the file has given `k` to List: the
    // two are one screen, so the key leaves the command that had it rather than
    // running both.
    const KeyMap file = valueOf(KeyMap::parse("k reader.list\n", "keys"));
    const KeyMap keys = file.mergedOnto(KeyMap::defaults());

    CHECK_FALSE(keys.is(Event::Character('k'), Command::ReaderKludges));
    // Kludges had that one key and now has none — the file said what `k` does,
    // and nothing says what Kludges is under instead.
    CHECK(keys.keysOf(Command::ReaderKludges).empty());

    // A command answered before every screen meets all of them, so it loses a
    // key to any screen's command.
    const KeyMap quit = valueOf(KeyMap::parse("Ctrl-Q compose.import\n", "keys"))
                            .mergedOnto(KeyMap::defaults());
    CHECK(quit.is(ctrl('q'), Command::ComposeImport));
    CHECK(quit.keysOf(Command::AppQuit).empty());
}

TEST_CASE("A merged layout leaves the other screen its key [keys]") {
    // `F2` is Change in the reader and Save in the editor, and the two never
    // meet: a file moving one of them takes nothing from the other.
    const KeyMap keys =
        valueOf(KeyMap::parse("F2 reader.info\n", "keys")).mergedOnto(KeyMap::defaults());

    CHECK(keys.is(Event::F2, Command::ReaderInfo));
    CHECK_FALSE(keys.is(Event::F2, Command::ReaderChange));
    CHECK(keys.is(Event::F2, Command::ComposeSave));
    // Change keeps the key the file did not ask about.
    CHECK(keys.is(Event::Character('c'), Command::ReaderChange));
}

TEST_CASE("A merged layout writes no key twice [keys]") {
    // A file that copies a line out of the defaults and then changes another is
    // the ordinary way one is written, and the copied line must leave the
    // command answering to that key once rather than twice.
    const KeyMap keys =
        valueOf(KeyMap::parse("l reader.list\nAlt-J compose.word_left\n", "keys"))
            .mergedOnto(KeyMap::defaults());

    CHECK(keys.keysOf(Command::ReaderList) ==
          std::vector<Event>{Event::Character('l'), Event::F9});
    // The letters the terminal is told about are both layouts' — the file's
    // Alt-J and the defaults the file left alone.
    CHECK(keys.altLetters() == "bfhjq");
    CHECK(keys.altBackspace());
    // And the default key for the command the file moved is still there: a
    // chord and a letter on one command is what merging leaves.
    CHECK(keys.is(alt('j'), Command::ComposeWordLeft));
    CHECK(keys.is(alt('b'), Command::ComposeWordLeft));
}

TEST_CASE("A layout is read from the file the config names [keys]") {
    amberedit::test::TempDir dir;
    const std::string path = dir.path("amberkeys.cfg");
    {
        std::ofstream out(path);
        out << "  # indented comments count too\n\n";
        out << "F3\treader.find\n";
        out << "Alt-J compose.word_left\n";
    }

    const KeyMap keys = valueOf(KeyMap::loadFromFile(path));
    CHECK(keys.is(Event::F3, Command::ReaderFind));
    // The terminal is told about the letters this layout uses and no others,
    // and about an ESC in front of Backspace only where one is wanted.
    CHECK(keys.altLetters() == "j");
    CHECK_FALSE(keys.altBackspace());

    const std::string error = errorOf(KeyMap::loadFromFile(dir.path("gone.cfg")));
    CHECK_MESSAGE(contains(error, "keys file not found"), error);

    // The file's own name is what a mistake in it is reported against, since
    // that is what the reader of the message has to go and open.
    const std::string bad = dir.path("bad.cfg");
    {
        std::ofstream out(bad);
        out << "F3 reader.find\nEsc reader.list\n";
    }
    const std::string error2 = errorOf(KeyMap::loadFromFile(bad));
    CHECK_MESSAGE(contains(error2, bad + ":2: "), error2);
}
