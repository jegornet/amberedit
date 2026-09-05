#include "config/commands.hpp"

#include <array>
#include <iterator>
#include <string>

#include "config/text_util.hpp"
#include "i18n/i18n.hpp"

namespace amberedit::config {
namespace {

/// The table itself. Everything AmberEdit offers is a row here and nothing is a
/// row anywhere else: the keyboard, the three menus and the four hint bars all
/// read it, so a command is added by writing one line and asking for it in the
/// screen that answers it.
///
/// A glyph is written only for the commands a menu may offer, a menu being the
/// one place a glyph is drawn. The rest carry a word alone, which is what a hint
/// is written with.
constexpr Commands::Info kCommands[] = {
    {Command::AppQuit, "app.quit", CommandScreen::Anywhere, N_("Quit"),
     N_("Leave AmberEdit"), "", "Ctrl-Q", false},
    {Command::AppHelp, "app.help", CommandScreen::Anywhere, N_("Help"),
     N_("This list of keys"), "", "F1", false},
    {Command::AreaListNextUnread, "arealist.next_unread", CommandScreen::AreaList,
     N_("Next unread"), N_("Go to the next area with unread mail"), "⇩", "/", true},
    {Command::AreaListToggleUnread, "arealist.toggle_unread", CommandScreen::AreaList,
     N_("Toggle unread"), N_("Show the areas with unread mail, or all of them"), "✱",
     "Ctrl-U", true},
    {Command::AreaListRescan, "arealist.rescan", CommandScreen::AreaList, N_("Rescan"),
     N_("Read the message bases again"), "⟳", "Ctrl-R", true},
    {Command::MessageListMarkToggle, "msglist.mark_toggle", CommandScreen::MessageList,
     N_("Mark"), N_("Mark the message, or take the mark off"), "", "t", false},
    {Command::ReaderReply, "reader.reply", CommandScreen::Reader, N_("Reply"),
     N_("Reply in this area"), "↩", "q F4", true},
    {Command::ReaderReplyElsewhere, "reader.reply_elsewhere", CommandScreen::Reader,
     N_("Reply elsewhere"), N_("Reply in another area"), "↪", "n F5", true},
    {Command::ReaderCommentReply, "reader.comment_reply", CommandScreen::Reader,
     N_("Comment-reply"), N_("Reply to whoever the message was written to"), "⇄", "Alt-Q",
     true},
    {Command::ReaderNew, "reader.new", CommandScreen::Reader, N_("New"),
     N_("Write a new message"), "✎", "e", true},
    {Command::ReaderForward, "reader.forward", CommandScreen::Reader, N_("Fwd / Copy"),
     N_("Forward, move or copy into another area"), "↗", "m", true},
    {Command::ReaderChange, "reader.change", CommandScreen::Reader, N_("Change"),
     N_("Change the message"), "⚠︎", "c F2", true},
    {Command::ReaderDelete, "reader.delete", CommandScreen::Reader, N_("Delete"),
     N_("Delete the message"), "", "d Del", false},
    {Command::ReaderExport, "reader.export", CommandScreen::Reader, N_("Export"),
     N_("Write the message, or a file in it, to disk"), "⌲", "w F7", true},
    {Command::ReaderFind, "reader.find", CommandScreen::Reader, N_("Find"),
     N_("Look for a message in this area"), "⌕", "Ctrl-F F6", true},
    {Command::ReaderList, "reader.list", CommandScreen::Reader, N_("List"),
     N_("Back to the list of messages"), "≔", "l F9", true},
    {Command::ReaderInfo, "reader.info", CommandScreen::Reader, N_("Info"),
     N_("What the message base holds about it"), "𝒊", "i", true},
    {Command::ReaderNodelist, "reader.nodelist", CommandScreen::Reader, N_("Nodelist"),
     N_("Look an address or a sysop up"), "⚲", "Ctrl-N F10", true},
    {Command::ReaderKludges, "reader.kludges", CommandScreen::Reader, N_("Kludges"),
     N_("Show or hide the service lines"), "", "k", false},
    {Command::ReaderScrollbar, "reader.scrollbar", CommandScreen::Reader, N_("Scrollbar"),
     N_("Show or hide the scrollbar"), "", "b", false},
    {Command::ReaderThreadUp, "reader.thread_up", CommandScreen::Reader, N_("Thread up"),
     N_("The message this one answers"), "", "-", false},
    {Command::ReaderThreadDown, "reader.thread_down", CommandScreen::Reader,
     N_("Thread down"), N_("The next answer to this message"), "", "+ =", false},
    {Command::ReaderMarkToggle, "reader.mark_toggle", CommandScreen::Reader, N_("Mark"),
     N_("Mark the message, or take the mark off"), "★", "t", true},
    {Command::ReaderMarkMenu, "reader.mark_menu", CommandScreen::Reader, N_("Marks"),
     N_("Mark a run of messages at once"), "☆", "s", true},
    {Command::ReaderShell, "reader.shell", CommandScreen::Reader, N_("Shell"),
     N_("Run your own shell"), "❯", "Ctrl-X", true},
    {Command::ComposeSave, "compose.save", CommandScreen::Compose, N_("Save"),
     N_("Save the message"), "✓", "Ctrl-S F2", true},
    {Command::ComposeAttributes, "compose.attributes", CommandScreen::Compose,
     N_("Attributes"), N_("Change the message attributes"), "", "Ctrl-F", false},
    {Command::ComposeImport, "compose.import", CommandScreen::Compose, N_("Import"),
     N_("Read a file into the message"), "+", "Ctrl-O F3", true},
    {Command::ComposeHeaderBack, "compose.header_back", CommandScreen::Compose,
     N_("Header"), N_("Back up into the header fields"), "", "Alt-H", false},
    {Command::ComposeDeleteLine, "compose.delete_line", CommandScreen::Compose,
     N_("Delete line"), N_("Delete the line the cursor is on"), "", "Ctrl-Y", false},
    {Command::ComposeRestoreLine, "compose.restore_line", CommandScreen::Compose,
     N_("Restore line"), N_("Put the last deleted line back"), "", "Ctrl-U", false},
    {Command::ComposeDeleteQuote, "compose.delete_quote", CommandScreen::Compose,
     N_("Delete quote"), N_("Delete the quoted text after the cursor"), "", "Ctrl-D",
     false},
    {Command::ComposeDeleteWord, "compose.delete_word", CommandScreen::Compose,
     N_("Delete word"), N_("Delete the word before the cursor"), "",
     "Ctrl-W Alt-Backspace", false},
    {Command::ComposeWordLeft, "compose.word_left", CommandScreen::Compose,
     N_("Word left"), N_("A word to the left"), "", "Alt-B Alt-Left", false},
    {Command::ComposeWordRight, "compose.word_right", CommandScreen::Compose,
     N_("Word right"), N_("A word to the right"), "", "Alt-F Alt-Right", false},
    {Command::ComposeLineStart, "compose.line_start", CommandScreen::Compose,
     N_("Line start"), N_("To the start of the line"), "", "Ctrl-A", false},
    {Command::ComposeLineEnd, "compose.line_end", CommandScreen::Compose, N_("Line end"),
     N_("To the end of the line"), "", "Ctrl-E", false},
};

/// The screens an external utility can be run from, in the order their commands
/// stand at the end of the enumeration, and what each writes in front of the
/// name — `arealist.extern_util0` and the two beside it.
///
/// The message list is not among them: a screen one passes through on the way to
/// a message is not where a program is reached for, whatever else it answers.
constexpr struct ExternUtilScreen {
    CommandScreen screen;
    std::string_view prefix;
} kExternUtilScreens[] = {
    {CommandScreen::AreaList, "arealist"},
    {CommandScreen::Reader, "reader"},
    {CommandScreen::Compose, "compose"},
};

constexpr size_t kExternUtilScreenCount =
    sizeof(kExternUtilScreens) / sizeof(kExternUtilScreens[0]);

/// How many utility commands there are in all: a slot on each of those screens.
constexpr size_t kExternUtilCommandCount = kExternUtilScreenCount * kExternUtilCount;

static_assert(
    (sizeof(kCommands) / sizeof(kCommands[0])) + kExternUtilCommandCount == kCommandCount,
    "every command needs a name, a screen, a label, a help line and its defaults");

/// The utility command that many places past the first of them, the thirty
/// standing together at the end of the enumeration in exactly this order.
Command externUtilCommandAt(size_t at) {
    return static_cast<Command>(static_cast<size_t>(Command::AreaListExternUtil0) + at);
}

/// The table as a list, built once: what `all()` hands out.
///
/// The utilities are put together here rather than written out above, because
/// they are one row said thirty times over: ten slots on each of three screens,
/// differing in nothing but the name. Their names are built beside the table and
/// live as long as it does, the rows holding views of them.
///
/// A utility carries the same word as every other one — its own is the `title`
/// the config gives it, which `AppConfig::labelOf()` answers with. This is what
/// is left when a utility has no title, and nothing that draws reaches it: a
/// menu or a hint naming a slot no `extern_utilN` line sets is refused where it
/// is written.
const std::vector<Commands::Info>& table() {
    static const std::array<std::string, kExternUtilCommandCount> kNames = [] {
        std::array<std::string, kExternUtilCommandCount> names;
        size_t at = 0;
        for (const ExternUtilScreen& on : kExternUtilScreens) {
            for (size_t slot = 0; slot < kExternUtilCount; ++slot) {
                names[at++] =
                    std::string(on.prefix) + ".extern_util" + std::to_string(slot);
            }
        }
        return names;
    }();

    static const std::vector<Commands::Info> kAll = [] {
        std::vector<Commands::Info> all;
        all.reserve(kCommandCount);
        all.insert(all.end(), std::begin(kCommands), std::end(kCommands));
        size_t at = 0;
        for (const ExternUtilScreen& on : kExternUtilScreens) {
            for (size_t slot = 0; slot < kExternUtilCount; ++slot, ++at) {
                all.push_back({externUtilCommandAt(at), kNames[at], on.screen,
                               N_("Utility"), N_("Run the program the config names"), "⚒",
                               "", true});
            }
        }
        return all;
    }();
    return kAll;
}

/// Whether that screen's list may name the command: its own commands, and the
/// ones answered before every screen where a hint is what is being written — a
/// hint is a key with its name beside it, and a key answered everywhere is one
/// every screen has.
///
/// A menu button is not that: what it runs, the screen underneath runs on its
/// own, so a menu holds the screen's own commands and only those a button can
/// stand for.
bool offers(const Commands::Info& info, CommandScreen screen, Commands::In where) {
    if (where == Commands::In::Menu) return info.screen == screen && info.inMenu;
    return info.screen == screen || info.screen == CommandScreen::Anywhere;
}

/// Whether a config wrote that name for this command, `-` and `_` being the same
/// character to a reader of names.
///
/// A command whose name is two words is written `delete_line`, and was written
/// `delete-line` before. The older spelling is still read — a config is a file a
/// user wrote once and is not asked to rewrite — but it is not offered anywhere:
/// the table holds one spelling, and that is the one every message and every
/// generated file says.
bool nameIs(std::string_view written, std::string_view name) {
    if (written.size() != name.size()) return false;
    for (size_t i = 0; i < name.size(); ++i) {
        const char c = written[i] == '-' ? '_' : text::asciiLower(written[i]);
        if (c != text::asciiLower(name[i])) return false;
    }
    return true;
}

}  // namespace

const std::vector<Commands::Info>& Commands::all() {
    return table();
}

const Commands::Info& Commands::of(Command command) {
    // The table is written in the order the enumeration is, which is what lets a
    // command be looked up by its own value.
    return table()[static_cast<size_t>(command)];
}

const char* Commands::labelOf(Command command) {
    return _(of(command).labelId);
}

const char* Commands::helpOf(Command command) {
    return _(of(command).helpId);
}

std::string_view Commands::shortNameOf(Command command) {
    const std::string_view name = of(command).name;
    const size_t dot = name.find('.');
    return dot == std::string_view::npos ? name : name.substr(dot + 1);
}

const Commands::Info* Commands::named(std::string_view name) {
    for (const Info& info : table()) {
        if (nameIs(name, info.name)) return &info;
    }
    return nullptr;
}

const Commands::Info* Commands::namedOn(CommandScreen screen, std::string_view name,
                                        In where) {
    for (const Info& info : table()) {
        if (!offers(info, screen, where)) continue;
        if (nameIs(name, shortNameOf(info.command))) return &info;
    }
    return nullptr;
}

std::vector<Command> Commands::offeredOn(CommandScreen screen, In where) {
    std::vector<Command> commands;
    for (const Info& info : table()) {
        if (offers(info, screen, where)) commands.push_back(info.command);
    }
    return commands;
}

std::optional<size_t> Commands::externUtilOf(Command command) {
    const auto at = static_cast<size_t>(command);
    const auto first = static_cast<size_t>(Command::AreaListExternUtil0);
    if (at < first) return std::nullopt;
    // The screen is the block it falls in and the slot is its place inside one,
    // which is the whole of why the thirty stand together.
    return (at - first) % kExternUtilCount;
}

std::optional<Command> Commands::externUtilOn(CommandScreen screen, size_t slot) {
    if (slot >= kExternUtilCount) return std::nullopt;
    for (size_t block = 0; block < kExternUtilScreenCount; ++block) {
        if (kExternUtilScreens[block].screen != screen) continue;
        return externUtilCommandAt((block * kExternUtilCount) + slot);
    }
    return std::nullopt;
}

std::string Commands::offeredNamesOn(CommandScreen screen, In where) {
    std::string names;
    for (const Command command : offeredOn(screen, where)) {
        if (!names.empty()) names += ", ";
        names.append(shortNameOf(command));
    }
    return names;
}

}  // namespace amberedit::config
