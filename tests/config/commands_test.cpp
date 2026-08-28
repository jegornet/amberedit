#include "config/commands.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <set>
#include <string>
#include <string_view>
#include <utility>

using amberedit::config::Command;
using amberedit::config::Commands;
using amberedit::config::CommandScreen;
using amberedit::config::kCommandCount;

TEST_CASE("Every command is one row of the one table [commands]") {
    // The table is written in the order the enumeration is, which is what lets
    // a command be looked up by its own value: a row out of order would hand
    // every reader of it somebody else's command.
    REQUIRE(Commands::all().size() == kCommandCount);
    for (size_t i = 0; i < kCommandCount; ++i) {
        const auto command = static_cast<Command>(i);
        const Commands::Info& info = Commands::of(command);
        INFO(info.name);
        CHECK(info.command == command);

        // A name, a word to draw it with, and a name that says which screen
        // answers it: everything a menu, a hint bar and a `keys` file need of a
        // command, and nothing any of them keeps a second list of.
        CHECK_FALSE(info.name.empty());
        CHECK_FALSE(std::string_view(info.labelId).empty());
        const size_t dot = info.name.find('.');
        REQUIRE(dot != std::string_view::npos);
        CHECK_FALSE(Commands::shortNameOf(command).empty());

        // A glyph is the menu's, which is the one place one is drawn.
        CHECK(info.icon.empty() != info.inMenu);
    }
}

TEST_CASE("A command is found by the name it is written under [commands]") {
    // The whole name is what a `keys` file names it by, without regard to case.
    REQUIRE(Commands::named("reader.reply_elsewhere") != nullptr);
    CHECK(Commands::named("reader.reply_elsewhere")->command ==
          Command::ReaderReplyElsewhere);
    CHECK(Commands::named("READER.REPLY_ELSEWHERE")->command ==
          Command::ReaderReplyElsewhere);
    CHECK(Commands::named("reply_elsewhere") == nullptr);
    CHECK(Commands::named("reader.nothing") == nullptr);

    // A menu and a hint list name the part after the dot, the config key
    // already saying which screen is meant.
    const auto* hint = Commands::namedOn(CommandScreen::Reader, "reply_elsewhere",
                                         Commands::In::HintBar);
    REQUIRE(hint != nullptr);
    CHECK(hint->command == Command::ReaderReplyElsewhere);
    CHECK(Commands::shortNameOf(hint->command) == "reply_elsewhere");

    // One screen's commands are not another's, whichever list is being read.
    CHECK(Commands::namedOn(CommandScreen::Compose, "reply_elsewhere",
                            Commands::In::HintBar) == nullptr);
    CHECK(Commands::namedOn(CommandScreen::AreaList, "save", Commands::In::Menu) ==
          nullptr);

    // The three screens that carry a menu each hold their own commands in it,
    // and the message list holds none: marking the message under the cursor is
    // its one command, and one button is no menu.
    CHECK(Commands::namedOn(CommandScreen::AreaList, "rescan", Commands::In::Menu) !=
          nullptr);
    CHECK(Commands::namedOn(CommandScreen::MessageList, "mark_toggle",
                            Commands::In::HintBar) != nullptr);
    CHECK(Commands::namedOn(CommandScreen::MessageList, "mark_toggle",
                            Commands::In::Menu) == nullptr);
    CHECK(Commands::offeredOn(CommandScreen::MessageList, Commands::In::Menu).empty());

    // A hint is a key with its name beside it, so any command of the screen may
    // be one; a menu holds what a button can stand for, which is fewer.
    CHECK(Commands::namedOn(CommandScreen::Compose, "delete_line",
                            Commands::In::HintBar) != nullptr);
    CHECK(Commands::namedOn(CommandScreen::Compose, "delete_line", Commands::In::Menu) ==
          nullptr);

    // `app.quit` is answered before every screen, so every screen's row may
    // name it — and no menu does, a menu being the screen's own commands.
    for (const CommandScreen screen :
         {CommandScreen::AreaList, CommandScreen::MessageList, CommandScreen::Reader,
          CommandScreen::Compose}) {
        CHECK(Commands::namedOn(screen, "quit", Commands::In::HintBar) != nullptr);
        CHECK(Commands::namedOn(screen, "quit", Commands::In::Menu) == nullptr);
    }
}

TEST_CASE("The older spelling of a name is still read [commands]") {
    // Two-word names were written with a `-` once, and a config or a `keys`
    // file that still writes one is read as it stands — the `-` folds into the
    // `_` wherever a name is looked up, case folding as it always did.
    REQUIRE(Commands::named("reader.reply-elsewhere") != nullptr);
    CHECK(Commands::named("reader.reply-elsewhere")->command ==
          Command::ReaderReplyElsewhere);
    CHECK(Commands::named("READER.REPLY-ELSEWHERE")->command ==
          Command::ReaderReplyElsewhere);

    const auto* hint =
        Commands::namedOn(CommandScreen::Compose, "delete-line", Commands::In::HintBar);
    REQUIRE(hint != nullptr);
    CHECK(hint->command == Command::ComposeDeleteLine);

    // Only the one spelling is ever offered back, whichever was written.
    CHECK(Commands::shortNameOf(hint->command) == "delete_line");
    CHECK(Commands::offeredNamesOn(CommandScreen::Compose, Commands::In::HintBar)
              .find('-') == std::string::npos);

    // The fold is between those two characters and nothing else: a name is not
    // a pattern, and a wrong character is still a wrong name.
    CHECK(Commands::named("reader reply_elsewhere") == nullptr);
    CHECK(Commands::named("reader.replyelsewhere") == nullptr);
}

TEST_CASE("What a screen offers is named once and told the same way [commands]") {
    // A screen's own short names, all of them different: two commands one
    // config line could not tell apart would be a setting nobody could write.
    for (const CommandScreen screen :
         {CommandScreen::AreaList, CommandScreen::MessageList, CommandScreen::Reader,
          CommandScreen::Compose}) {
        std::set<std::string_view> seen;
        for (const Command command : Commands::offeredOn(screen, Commands::In::HintBar)) {
            INFO(Commands::of(command).name);
            CHECK(seen.insert(Commands::shortNameOf(command)).second);
        }
        // Every one of them is what the setting's error message would list.
        const std::string offered =
            Commands::offeredNamesOn(screen, Commands::In::HintBar);
        for (const Command command : Commands::offeredOn(screen, Commands::In::HintBar)) {
            INFO(offered);
            CHECK(offered.find(Commands::shortNameOf(command)) != std::string::npos);
        }
    }

    // Everything a menu may hold is something a hint bar may hold too.
    for (const CommandScreen screen :
         {CommandScreen::AreaList, CommandScreen::Reader, CommandScreen::Compose}) {
        for (const Command command : Commands::offeredOn(screen, Commands::In::Menu)) {
            INFO(Commands::of(command).name);
            CHECK(Commands::namedOn(screen, Commands::shortNameOf(command),
                                    Commands::In::HintBar) != nullptr);
        }
    }
}

TEST_CASE("An external utility is one slot on each of three screens [commands]") {
    using amberedit::config::kExternUtilCount;

    // Ten of them, and each named on the area list, in the reader and in the
    // editor — the screen in front of the dot being where the key is pressed
    // and the digit after it being which utility runs.
    for (size_t slot = 0; slot < kExternUtilCount; ++slot) {
        const std::string digit = std::to_string(slot);
        for (const auto& [screen, prefix] :
             {std::pair<CommandScreen, std::string>{CommandScreen::AreaList, "arealist"},
              {CommandScreen::Reader, "reader"},
              {CommandScreen::Compose, "compose"}}) {
            const std::string name = prefix + ".extern_util" + digit;
            INFO(name);
            const Commands::Info* found = Commands::named(name);
            REQUIRE(found != nullptr);
            CHECK(found->screen == screen);
            // The slot is the digit and nothing else: one utility however many
            // commands reach it.
            CHECK(Commands::externUtilOf(found->command) == slot);
            CHECK(Commands::externUtilOn(screen, slot) == found->command);

            // A hint may name it, and so may a menu — a program is exactly what
            // a button stands for.
            const std::string shortName = "extern_util" + digit;
            CHECK(Commands::shortNameOf(found->command) == shortName);
            CHECK(Commands::namedOn(screen, shortName, Commands::In::HintBar) != nullptr);
            CHECK(Commands::namedOn(screen, shortName, Commands::In::Menu) != nullptr);
        }
    }

    // The message list runs none: every key on it moves the cursor or searches
    // the names.
    CHECK_FALSE(Commands::externUtilOn(CommandScreen::MessageList, 0));
    CHECK(Commands::namedOn(CommandScreen::MessageList, "extern_util0",
                            Commands::In::HintBar) == nullptr);

    // Ten and no more: the slot is one digit.
    CHECK(Commands::named("reader.extern_util10") == nullptr);
    CHECK_FALSE(Commands::externUtilOn(CommandScreen::Reader, kExternUtilCount));

    // Nothing else is one, and none of them is bound by default: a utility is
    // reached only where something was written down to reach it by.
    CHECK_FALSE(Commands::externUtilOf(Command::ReaderShell));
    CHECK(Commands::of(Command::ReaderExternUtil0).keys.empty());
    CHECK(Commands::of(Command::AreaListExternUtil9).icon == "⚒");
}
