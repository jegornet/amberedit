#include "config/commands.hpp"

#include <iterator>

#include "config/text_util.hpp"
#include "i18n/i18n.hpp"

namespace amberedit::config {
namespace {

/// The table itself. Everything AmberEdit offers is a row here and nothing is a
/// row anywhere else: the keyboard, the two menus and the four hint bars all
/// read it, so a command is added by writing one line and asking for it in the
/// screen that answers it.
///
/// A glyph is written only for the commands a menu may offer, a menu being the
/// one place a glyph is drawn. The rest carry a word alone, which is what a hint
/// is written with.
constexpr Commands::Info kCommands[] = {
    {Command::AppQuit, "app.quit", CommandScreen::Anywhere, N_("Quit"), "", "Ctrl-Q",
     false},
    {Command::AreaListNextUnread, "arealist.next_unread", CommandScreen::AreaList,
     N_("Next unread"), "", "/", false},
    {Command::AreaListRescan, "arealist.rescan", CommandScreen::AreaList, N_("Rescan"),
     "", "Ctrl-R", false},
    {Command::ReaderReply, "reader.reply", CommandScreen::Reader, N_("Reply"), "↩",
     "q F4", true},
    {Command::ReaderReplyElsewhere, "reader.reply_elsewhere", CommandScreen::Reader,
     N_("Reply elsewhere"), "↪", "n F5", true},
    {Command::ReaderCommentReply, "reader.comment_reply", CommandScreen::Reader,
     N_("Comment-reply"), "⇄", "Alt-Q", true},
    {Command::ReaderNew, "reader.new", CommandScreen::Reader, N_("New"), "✎", "e", true},
    {Command::ReaderForward, "reader.forward", CommandScreen::Reader, N_("Fwd / Copy"),
     "↗", "m", true},
    {Command::ReaderChange, "reader.change", CommandScreen::Reader, N_("Change"), "⚠︎",
     "c F2", true},
    {Command::ReaderDelete, "reader.delete", CommandScreen::Reader, N_("Delete"), "",
     "d Del", false},
    {Command::ReaderExport, "reader.export", CommandScreen::Reader, N_("Export"), "⌲",
     "w F7", true},
    {Command::ReaderFind, "reader.find", CommandScreen::Reader, N_("Find"), "⌕",
     "Ctrl-F F6", true},
    {Command::ReaderList, "reader.list", CommandScreen::Reader, N_("List"), "≔", "l F9",
     true},
    {Command::ReaderInfo, "reader.info", CommandScreen::Reader, N_("Info"), "𝒊", "i",
     true},
    {Command::ReaderNodelist, "reader.nodelist", CommandScreen::Reader, N_("Nodelist"),
     "⚲", "Ctrl-N F10", true},
    {Command::ReaderKludges, "reader.kludges", CommandScreen::Reader, N_("Kludges"), "",
     "k", false},
    {Command::ReaderScrollbar, "reader.scrollbar", CommandScreen::Reader, N_("Scrollbar"),
     "", "b", false},
    {Command::ReaderThreadUp, "reader.thread_up", CommandScreen::Reader, N_("Thread up"),
     "", "-", false},
    {Command::ReaderThreadDown, "reader.thread_down", CommandScreen::Reader,
     N_("Thread down"), "", "+ =", false},
    {Command::ComposeSave, "compose.save", CommandScreen::Compose, N_("Save"), "✓",
     "Ctrl-S F2", true},
    {Command::ComposeAttributes, "compose.attributes", CommandScreen::Compose,
     N_("Attributes"), "", "Ctrl-F", false},
    {Command::ComposeImport, "compose.import", CommandScreen::Compose, N_("Import"), "+",
     "Ctrl-O", true},
    {Command::ComposeHeaderBack, "compose.header_back", CommandScreen::Compose,
     N_("Header"), "", "Alt-H", false},
    {Command::ComposeDeleteLine, "compose.delete_line", CommandScreen::Compose,
     N_("Delete line"), "", "Ctrl-Y", false},
    {Command::ComposeRestoreLine, "compose.restore_line", CommandScreen::Compose,
     N_("Restore line"), "", "Ctrl-U", false},
    {Command::ComposeDeleteQuote, "compose.delete_quote", CommandScreen::Compose,
     N_("Delete quote"), "", "Ctrl-D", false},
    {Command::ComposeDeleteWord, "compose.delete_word", CommandScreen::Compose,
     N_("Delete word"), "", "Ctrl-W Alt-Backspace", false},
    {Command::ComposeWordLeft, "compose.word_left", CommandScreen::Compose,
     N_("Word left"), "", "Alt-B Alt-Left", false},
    {Command::ComposeWordRight, "compose.word_right", CommandScreen::Compose,
     N_("Word right"), "", "Alt-F Alt-Right", false},
    {Command::ComposeLineStart, "compose.line_start", CommandScreen::Compose,
     N_("Line start"), "", "Ctrl-A", false},
    {Command::ComposeLineEnd, "compose.line_end", CommandScreen::Compose, N_("Line end"),
     "", "Ctrl-E", false},
};

static_assert(sizeof(kCommands) / sizeof(kCommands[0]) == kCommandCount,
              "every command needs a name, a screen, a label and its defaults");

/// The table as a list, built once: what `all()` hands out.
const std::vector<Commands::Info>& table() {
    static const std::vector<Commands::Info> kAll(std::begin(kCommands),
                                                  std::end(kCommands));
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

std::string Commands::offeredNamesOn(CommandScreen screen, In where) {
    std::string names;
    for (const Command command : offeredOn(screen, where)) {
        if (!names.empty()) names += ", ";
        names.append(shortNameOf(command));
    }
    return names;
}

}  // namespace amberedit::config
