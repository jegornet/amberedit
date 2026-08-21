#include "ui/keys.hpp"

#include <algorithm>

#include "config/text_util.hpp"

namespace amberedit::ui {
namespace {

using term::Event;

/// One command: what the file calls it, which screen answers it, and the keys
/// it runs on when no `keys` file has been named.
///
/// The defaults are written as spellings rather than as events so that this
/// table is the one place the layout is stated. `amberkeys.cfg.example` is the
/// same table written out, and a test reads that file back through here to keep
/// the two saying the same thing.
struct CommandInfo {
    KeyCommand command;
    const char* name;
    KeyScreen screen;
    /// Blank separated, in the order they are to be offered.
    const char* keys;
};

constexpr CommandInfo kCommands[] = {
    {KeyCommand::AppQuit, "app.quit", KeyScreen::Anywhere, "Ctrl-Q"},
    {KeyCommand::AreaListNextUnread, "arealist.next-unread", KeyScreen::AreaList, "/"},
    {KeyCommand::AreaListRescan, "arealist.rescan", KeyScreen::AreaList, "Ctrl-R"},
    {KeyCommand::ReaderReply, "reader.reply", KeyScreen::Reader, "q F4"},
    {KeyCommand::ReaderReplyTo, "reader.reply-to", KeyScreen::Reader, "n F5"},
    {KeyCommand::ReaderNew, "reader.new", KeyScreen::Reader, "e"},
    {KeyCommand::ReaderForward, "reader.forward", KeyScreen::Reader, "m"},
    {KeyCommand::ReaderChange, "reader.change", KeyScreen::Reader, "c F2"},
    {KeyCommand::ReaderDelete, "reader.delete", KeyScreen::Reader, "d Del"},
    {KeyCommand::ReaderExport, "reader.export", KeyScreen::Reader, "w"},
    {KeyCommand::ReaderFind, "reader.find", KeyScreen::Reader, "f"},
    {KeyCommand::ReaderList, "reader.list", KeyScreen::Reader, "l F9"},
    {KeyCommand::ReaderInfo, "reader.info", KeyScreen::Reader, "i"},
    {KeyCommand::ReaderNodelist, "reader.nodelist", KeyScreen::Reader, "Ctrl-N F10"},
    {KeyCommand::ReaderKludges, "reader.kludges", KeyScreen::Reader, "k"},
    {KeyCommand::ReaderScrollbar, "reader.scrollbar", KeyScreen::Reader, "b"},
    {KeyCommand::ReaderThreadUp, "reader.thread-up", KeyScreen::Reader, "-"},
    {KeyCommand::ReaderThreadDown, "reader.thread-down", KeyScreen::Reader, "+ ="},
    {KeyCommand::ComposeSave, "compose.save", KeyScreen::Compose, "Ctrl-S F2"},
    {KeyCommand::ComposeAttributes, "compose.attributes", KeyScreen::Compose, "Ctrl-F"},
    {KeyCommand::ComposeImport, "compose.import", KeyScreen::Compose, "Ctrl-O"},
    {KeyCommand::ComposeHeaderBack, "compose.header-back", KeyScreen::Compose, "Ctrl-U"},
    {KeyCommand::ComposeDeleteLine, "compose.delete-line", KeyScreen::Compose, "Ctrl-Y"},
    {KeyCommand::ComposeDeleteQuote, "compose.delete-quote", KeyScreen::Compose,
     "Ctrl-D"},
    {KeyCommand::ComposeDeleteWord, "compose.delete-word", KeyScreen::Compose, "Ctrl-W"},
    {KeyCommand::ComposeWordLeft, "compose.word-left", KeyScreen::Compose,
     "Alt-B Alt-Left"},
    {KeyCommand::ComposeWordRight, "compose.word-right", KeyScreen::Compose,
     "Alt-F Alt-Right"},
    {KeyCommand::ComposeLineStart, "compose.line-start", KeyScreen::Compose, "Ctrl-A"},
    {KeyCommand::ComposeLineEnd, "compose.line-end", KeyScreen::Compose, "Ctrl-E"},
};

static_assert(sizeof(kCommands) / sizeof(kCommands[0]) == kKeyCommandCount,
              "every command needs a name, a screen and its defaults");

const CommandInfo& infoOf(KeyCommand command) {
    const auto at = static_cast<size_t>(command);
    // The table is written in the order the enumeration is, which is what lets
    // a command be looked up by its own value.
    return kCommands[at];
}

/// A key with a name of its own, and whether AmberEdit keeps it for moving
/// about. The first spelling of each is the one written back out.
struct NamedKey {
    const char* spelling;
    Event::Name name;
    bool reserved;
};

constexpr NamedKey kNamedKeys[] = {
    {"F1", Event::Name::F1, false},         {"F2", Event::Name::F2, false},
    {"F3", Event::Name::F3, false},         {"F4", Event::Name::F4, false},
    {"F5", Event::Name::F5, false},         {"F6", Event::Name::F6, false},
    {"F7", Event::Name::F7, false},         {"F8", Event::Name::F8, false},
    {"F9", Event::Name::F9, false},         {"F10", Event::Name::F10, false},
    {"F11", Event::Name::F11, false},       {"F12", Event::Name::F12, false},
    {"Del", Event::Name::Delete, false},    {"Delete", Event::Name::Delete, false},
    {"Enter", Event::Name::Return, true},   {"Return", Event::Name::Return, true},
    {"Esc", Event::Name::Escape, true},     {"Escape", Event::Name::Escape, true},
    {"Tab", Event::Name::Tab, true},        {"Backspace", Event::Name::Backspace, true},
    {"Home", Event::Name::Home, true},      {"End", Event::Name::End, true},
    {"PgUp", Event::Name::PageUp, true},    {"PageUp", Event::Name::PageUp, true},
    {"PgDn", Event::Name::PageDown, true},  {"PageDown", Event::Name::PageDown, true},
    {"Up", Event::Name::ArrowUp, true},     {"Down", Event::Name::ArrowDown, true},
    {"Left", Event::Name::ArrowLeft, true}, {"Right", Event::Name::ArrowRight, true},
};

const NamedKey* namedKey(std::string_view spelling) {
    for (const NamedKey& key : kNamedKeys) {
        if (config::text::iequals(spelling, key.spelling)) return &key;
    }
    return nullptr;
}

bool isArrow(Event::Name name) {
    return name == Event::Name::ArrowUp || name == Event::Name::ArrowDown ||
           name == Event::Name::ArrowLeft || name == Event::Name::ArrowRight;
}

bool isAsciiLetter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/// What is left of a spelling once its modifiers have been taken off the front.
struct Spelling {
    std::string_view body;
    bool ctrl{false};
    bool alt{false};
};

Spelling withoutModifiers(std::string_view spelling) {
    Spelling out{spelling, false, false};
    // In either order and in either case, and greedily: what is left over is
    // the key itself, which may be a `-` and so must not be split on.
    for (bool more = true; more;) {
        more = false;
        if (config::text::iequals(out.body.substr(0, 5), "ctrl-") &&
            out.body.size() > 5) {
            out.ctrl = true;
            out.body.remove_prefix(5);
            more = true;
        } else if (config::text::iequals(out.body.substr(0, 4), "alt-") &&
                   out.body.size() > 4) {
            out.alt = true;
            out.body.remove_prefix(4);
            more = true;
        }
    }
    return out;
}

/// How near the front of the queue a key stands when one of several has to be
/// shown: a bare key, then Ctrl, then Alt, then a key with a name of its own.
int reachOf(const Event& key) {
    if (key.is_character() && !key.ctrl() && !key.alt()) return 0;
    if (key.ctrl()) return 1;
    if (key.alt()) return 2;
    return 3;
}

}  // namespace

const char* nameOf(KeyCommand command) {
    return infoOf(command).name;
}

KeyScreen screenOf(KeyCommand command) {
    return infoOf(command).screen;
}

std::optional<KeyCommand> commandNamed(std::string_view name) {
    for (const CommandInfo& info : kCommands) {
        if (config::text::iequals(name, info.name)) return info.command;
    }
    return std::nullopt;
}

bool isReservedKey(std::string_view spelling) {
    const Spelling parsed = withoutModifiers(spelling);
    // Only bare: Alt-Left is how a word is walked over, and Home with Ctrl on it
    // is not the Home that moves the cursor.
    if (parsed.ctrl || parsed.alt) return false;
    if (config::text::iequals(parsed.body, "Space")) return true;
    const NamedKey* named = namedKey(parsed.body);
    return named != nullptr && named->reserved;
}

std::optional<Event> keyNamed(std::string_view spelling) {
    if (spelling.empty()) return std::nullopt;
    if (isReservedKey(spelling)) return std::nullopt;

    const Spelling parsed = withoutModifiers(spelling);
    if (parsed.body.empty()) return std::nullopt;

    if (const NamedKey* named = namedKey(parsed.body)) {
        // Alt with an arrow is a key a terminal can be asked to report; nothing
        // else carries a modifier here, and a spelling that asks for one is a
        // key that would never arrive.
        if (parsed.ctrl) return std::nullopt;
        if (parsed.alt && !isArrow(named->name)) return std::nullopt;
        return Event::Named(named->name, false, parsed.alt);
    }
    // A character, which is one byte: what is bound is what the terminal reports
    // a key as, and every key AmberEdit can be asked about reports as ASCII.
    if (parsed.body.size() != 1) return std::nullopt;
    const char c = parsed.body.front();
    if (static_cast<unsigned char>(c) <= ' ' || static_cast<unsigned char>(c) > '~') {
        return std::nullopt;
    }
    if ((parsed.ctrl || parsed.alt) && !isAsciiLetter(c)) return std::nullopt;
    if (parsed.ctrl || parsed.alt) {
        // The letter as the terminal reports it under a modifier, which is the
        // lower case one whichever case the file wrote.
        return Event::Character(std::string(1, config::text::asciiLower(c)), parsed.ctrl,
                                parsed.alt);
    }
    return Event::Character(std::string(1, c));
}

std::string spellingOf(const Event& key) {
    std::string out;
    if (key.ctrl()) out += "Ctrl-";
    if (key.alt()) out += "Alt-";
    if (key.kind() == Event::Kind::Key) {
        for (const NamedKey& named : kNamedKeys) {
            if (named.name == key.name()) return out + named.spelling;
        }
        return out + "?";
    }
    if (key.input() == " ") return out + "Space";
    // Upper case under a modifier and as written without one: `Ctrl-N` is how a
    // chord is read, and `G` is a key of its own.
    if (key.ctrl() || key.alt()) {
        return out + std::string(1, config::text::asciiUpper(key.input()[0]));
    }
    return out + key.input();
}

std::string briefSpellingOf(const Event& key) {
    const std::string full = spellingOf(key);
    // A modifier is spelled the same however it is written, so it may as well be
    // quiet; a bare key is not — `G` is a key of its own, and `F9` and `Del` are
    // names rather than letters.
    if (!key.ctrl() && !key.alt()) return full;
    return config::text::toLower(full);
}

KeyMap KeyMap::defaults() {
    KeyMap map;
    for (const CommandInfo& info : kCommands) {
        for (const std::string& spelling : config::text::tokenize(info.keys)) {
            const auto key = keyNamed(spelling);
            // The table above is ours, so a spelling it cannot read is a
            // mistake in this file rather than in anybody's config.
            if (key) map.bind(info.command, *key);
        }
    }
    return map;
}

Result<KeyMap> KeyMap::parse(std::string_view text, const std::string& origin) {
    KeyMap map;
    // What each key is already doing, to answer for a key written twice. The
    // command is kept rather than only the key: what makes the second line
    // wrong is the first one, and saying so is the whole of the message.
    std::vector<std::pair<Event, KeyCommand>> taken;

    const std::vector<std::string> lines = config::text::splitLines(text);
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string_view line = config::text::trim(lines[i]);
        const std::string where = origin + ":" + std::to_string(i + 1) + ": ";
        // A comment is a `#` where the line begins, which is what makes `#` the
        // one printable character a layout cannot bind: a key stands at the
        // beginning of its line, and that is where a comment starts.
        if (line.empty() || line.front() == '#') continue;

        const std::vector<std::string> tokens = config::text::tokenize(line);
        if (tokens.size() != 2) {
            return failure(where +
                           "a line is a key and a command, and nothing else "
                           "— as in `l reader.list`");
        }

        const auto command = commandNamed(tokens[1]);
        if (!command) {
            return failure(where + "no command is called " + tokens[1]);
        }
        if (isReservedKey(tokens[0])) {
            return failure(
                where + tokens[0] +
                " moves about and cannot be bound — the arrows, PgUp and PgDn, Home "
                "and End, Space, Enter, Esc, Backspace and Tab mean the same thing on "
                "every screen");
        }
        const auto key = keyNamed(tokens[0]);
        if (!key) return failure(where + "no key is called " + tokens[0]);

        for (const auto& [earlier, other] : taken) {
            if (!(earlier == *key)) continue;
            // Two screens may share a key and never meet; one screen may not.
            const KeyScreen a = screenOf(*command);
            const KeyScreen b = screenOf(other);
            if (a != b && a != KeyScreen::Anywhere && b != KeyScreen::Anywhere) continue;
            return failure(where + tokens[0] + " is already " + nameOf(other));
        }

        taken.emplace_back(*key, *command);
        map.bind(*command, *key);
    }
    return map;
}

Result<KeyMap> KeyMap::loadFromFile(const std::string& path) {
    const auto text = config::text::readFile(path);
    if (!text) return failure("keys file not found: " + path);
    return parse(*text, path);
}

void KeyMap::bind(KeyCommand command, Event key) {
    bound_[static_cast<size_t>(command)].push_back(std::move(key));
}

bool KeyMap::is(const Event& event, KeyCommand command) const {
    // A mouse report is never a binding: what a click means is where it landed,
    // and every screen asks that of the pointer rather than of the layout.
    if (event.is_mouse()) return false;
    const auto& keys = bound_[static_cast<size_t>(command)];
    return std::any_of(keys.begin(), keys.end(),
                       [&event](const Event& key) { return event == key; });
}

const std::vector<Event>& KeyMap::keysOf(KeyCommand command) const {
    return bound_[static_cast<size_t>(command)];
}

std::optional<Event> KeyMap::preferredKey(KeyCommand command) const {
    const auto& keys = bound_[static_cast<size_t>(command)];
    if (keys.empty()) return std::nullopt;
    // The first of the nearest kind: two keys of one kind are as good as each
    // other, and the layout wrote one of them first.
    const auto best = std::min_element(
        keys.begin(), keys.end(),
        [](const Event& a, const Event& b) { return reachOf(a) < reachOf(b); });
    return *best;
}

std::string KeyMap::altLetters() const {
    std::string letters;
    for (const auto& keys : bound_) {
        for (const Event& key : keys) {
            if (!key.alt() || !key.is_character() || key.input().size() != 1) continue;
            const char letter = key.input()[0];
            if (!isAsciiLetter(letter)) continue;
            if (letters.find(letter) == std::string::npos) letters += letter;
        }
    }
    std::sort(letters.begin(), letters.end());
    return letters;
}

}  // namespace amberedit::ui
