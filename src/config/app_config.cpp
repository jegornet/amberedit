#include "config/app_config.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "config/cfg_file.hpp"
#include "config/text_util.hpp"
#include "domain/message.hpp"

namespace amberedit::config {
namespace {

TosserConfigFormat parseFormat(const CfgEntry& entry) {
    const std::string value = text::toLower(entry.one());
    if (value == "fidoconfig" || value == "hpt") return TosserConfigFormat::Fidoconfig;
    if (value == "areas.bbs" || value == "areasbbs" || value == "bbs")
        return TosserConfigFormat::AreasBbs;
    if (value == "squish" || value == "squish.cfg" || value == "squishcfg")
        return TosserConfigFormat::SquishCfg;
    entry.fail("unknown tosser_config_format: '" + value +
               "' (expected fidoconfig | areas.bbs | squish.cfg)");
}

/// The `arealist_sort` letters: a criterion each, optionally preceded by `-` for
/// descending or `+` for ascending. The letters are read case-insensitively and
/// the spaces between them mean nothing, so `-u+e`, `-U +E` and `- u + e` are
/// one and the same order.
///
/// An empty value is not an empty setting: it asks for the tosser config's own
/// order, which is the only way to say "leave the list as it is written" now
/// that the default sorts it.
std::vector<AreaSortCriterion> parseAreaSort(const CfgEntry& entry) {
    if (entry.values.empty()) {
        entry.fail(
            "arealist_sort needs the letters to sort by — write arealist_sort \"\" for "
            "the tosser config's own order");
    }

    std::vector<AreaSortCriterion> criteria;
    bool descending = false;
    bool modifierSeen = false;

    for (const std::string& value : entry.values) {
        for (const char raw : value) {
            if (raw == '+' || raw == '-') {
                if (modifierSeen) {
                    entry.fail("arealist_sort: '" + std::string(1, raw) +
                               "' follows another +/- with no criterion between them");
                }
                descending = raw == '-';
                modifierSeen = true;
                continue;
            }

            AreaSortKey key{};
            switch (text::asciiLower(raw)) {
                case 'a': key = AreaSortKey::Address; break;
                case 'e': key = AreaSortKey::Echoid; break;
                case 'g': key = AreaSortKey::Group; break;
                case 't': key = AreaSortKey::Type; break;
                case 'u': key = AreaSortKey::Unread; break;
                default:
                    entry.fail("arealist_sort: '" + std::string(1, raw) +
                               "' is not a sort criterion (a address, e echoid, "
                               "g group, t type, u unread, each optionally "
                               "preceded by - or +)");
            }
            // A criterion written twice can only be a slip: the second one
            // sorts what the first has already left in order, so it does
            // nothing whichever way round it was meant.
            const auto same = [key](const AreaSortCriterion& c) { return c.key == key; };
            if (std::any_of(criteria.begin(), criteria.end(), same)) {
                entry.fail("arealist_sort: '" + std::string(1, text::asciiLower(raw)) +
                           "' is named twice");
            }
            criteria.push_back(AreaSortCriterion{key, descending});
            descending = false;
            modifierSeen = false;
        }
    }

    if (modifierSeen) entry.fail("arealist_sort: a trailing +/- names no criterion");
    return criteria;
}

/// The letters `arealist_format` is written with, what each shows, and how wide
/// it stands when no width is written after it.
const ListFormatSpec& areaFormatSpec() {
    static const ListFormatSpec spec{
        "arealist_format",
        {{'a', 4}, {'e', 0}, {'d', 0}, {'g', 5}, {'c', 4}, {'u', 4}, {'n', 1}},
        "a number, e echoid, d description, g group, c messages, u unread, "
        "n unread marker",
        "e c u\\nd n",
        "e d c un",
    };
    return spec;
}

AreaFieldKind areaFieldOf(char letter) {
    switch (letter) {
        case 'a': return AreaFieldKind::Number;
        case 'e': return AreaFieldKind::Echoid;
        case 'd': return AreaFieldKind::Description;
        case 'g': return AreaFieldKind::Group;
        case 'c': return AreaFieldKind::Total;
        case 'u': return AreaFieldKind::Unread;
        case 'n': return AreaFieldKind::UnreadFlag;
        default: return AreaFieldKind::Space;  // ' ', the only letter left
    }
}

/// The letters `msglist_format` is written with. The number and the date stand
/// at `kAutoWidth`: how wide they want to be is a question about the area and
/// the stamps in it rather than a number the table could name here.
const ListFormatSpec& msgFormatSpec() {
    static const ListFormatSpec spec{
        "msglist_format",
        {{'a', kAutoWidth}, {'f', 20}, {'t', 20}, {'s', 0}, {'d', kAutoWidth}},
        "a number, f from, t to, s subject, d date",
        "a f0 t0 d15\\ns",
        "a f t s d",
    };
    return spec;
}

MsgFieldKind msgFieldOf(char letter) {
    switch (letter) {
        case 'a': return MsgFieldKind::Number;
        case 'f': return MsgFieldKind::From;
        case 't': return MsgFieldKind::To;
        case 's': return MsgFieldKind::Subject;
        case 'd': return MsgFieldKind::Date;
        default: return MsgFieldKind::Space;  // ' ', the only letter left
    }
}

/// The lines the shared reader came back with, under the kinds this list knows
/// its letters by. What a letter shows is the only thing either list decides
/// for itself; the shape of the value is `config/list_format.*`'s.
template <typename Format, typename Kind, typename KindOf>
Format formatOf(const ListFormatRow& lines, KindOf kindOf) {
    Format format;
    format.reserve(lines.size());
    for (const auto& line : lines) {
        typename Format::value_type fields;
        fields.reserve(line.size());
        for (const auto& field : line) {
            fields.push_back({kindOf(field.letter), field.width});
        }
        format.push_back(std::move(fields));
    }
    return format;
}

/// The commands one menu key may name, in the order the default writes them.
/// Each key has its own table, which is what makes `reader_menu save` a mistake
/// the config catches rather than a button that does nothing.
using MenuNames = std::vector<std::pair<std::string_view, MenuCommand>>;

const MenuNames& readerCommands() {
    static const MenuNames names{
        {"list", MenuCommand::List},        {"reply", MenuCommand::Reply},
        {"reply_to", MenuCommand::ReplyTo}, {"new", MenuCommand::New},
        {"forward", MenuCommand::Forward},  {"find", MenuCommand::Find},
        {"change", MenuCommand::Change},    {"info", MenuCommand::Info},
        {"export", MenuCommand::Export},    {"nodelist", MenuCommand::Nodelist},
    };
    return names;
}

const MenuNames& composeCommands() {
    static const MenuNames names{{"save", MenuCommand::Save},
                                 {"import", MenuCommand::Import}};
    return names;
}

/// What a menu key says about a word that is not one of its commands. Its own
/// function so that the message is built where it is thrown rather than once per
/// value read — `fail` throws, so it is built at most once.
[[noreturn]] void failUnknownCommand(const CfgEntry& entry, const std::string& value,
                                     const std::string& offered) {
    entry.fail(entry.key + ": '" + value + "' is not one of its commands (" + offered +
               ")");
}

/// The commands a menu key names, as the key writes them.
///
/// Whether the button that opens the menu is on the screen at all is
/// `menu_button`, so the list here is only ever what the menu holds. Everything
/// malformed stops AmberEdit rather than being skipped: a dropped button is one
/// the user wrote down and cannot see, which is exactly the shape of a mistake
/// nobody finds.
std::vector<MenuCommand> parseMenu(const CfgEntry& entry, const MenuNames& known) {
    std::string offered;
    for (const auto& command : known) {
        if (!offered.empty()) offered += ", ";
        offered.append(command.first);
    }

    if (entry.values.empty()) {
        entry.fail(entry.key + " needs the commands to put in the menu (" + offered +
                   ") — write menu_button off for no menu at all");
    }

    std::vector<MenuCommand> commands;
    for (const std::string& value : entry.values) {
        const std::string name = text::toLower(value);
        // `none` was how a screen used to be left without a toolbar, and the
        // menus took the toolbars' place: it is said here rather than left to
        // read as an unknown command, since the setting moved and did not go
        // away.
        if (name == "none") {
            entry.fail(entry.key + ": 'none' is no longer one of its values — write " +
                       "menu_button off for no menu at all, and this key for what the "
                       "menu holds");
        }

        const auto found =
            std::find_if(known.begin(), known.end(),
                         [&name](const auto& pair) { return pair.first == name; });
        if (found == known.end()) failUnknownCommand(entry, value, offered);
        // Written twice can only be a slip: two buttons doing the same thing
        // one under the other is not something anyone asks for on purpose.
        if (std::find(commands.begin(), commands.end(), found->second) !=
            commands.end()) {
            entry.fail(entry.key + ": '" + name + "' is named twice");
        }
        commands.push_back(found->second);
    }
    return commands;
}

Visibility parseVisibility(const CfgEntry& entry) {
    const std::string value = text::toLower(entry.one());
    if (value == "on") return Visibility::On;
    if (value == "off") return Visibility::Off;
    if (value == "when_narrow") return Visibility::WhenNarrow;
    if (value == "when_wide") return Visibility::WhenWide;
    entry.fail(entry.key + ": '" + entry.one() +
               "' does not say whether it is shown (on | off | when_narrow | "
               "when_wide)");
}

/// A strftime format for a date or a time, checked as far as it can be: it has
/// to say something — a blank is not something, the stamp being trimmed — and
/// what it writes for a date that is certainly one has to be a single line:
/// `%n` and `%t` are the two specifiers that are not, and a newline in a stamp
/// would take the header table's columns with it. What a
/// specifier means beyond that is the C library's business, and one it does not
/// know comes out as it was written, where the user can see it.
std::string readTimeFormat(const CfgEntry& entry) {
    const std::string value = entry.text();
    if (value.empty()) {
        entry.fail(entry.key +
                   " needs a strftime format, e.g. \"%d %b %y\" — there is no way to "
                   "ask for no date at all");
    }

    // The sample is given a zone, so that a format asking for one — `%z` and
    // nothing else, say — is judged on what it writes for a message that
    // states one rather than on the blank it leaves for a message that does
    // not.
    const std::string written =
        domain::MessageDate{2026, 8, 10, 21, 19, 36}.format(value, "+0300");
    // A stamp is trimmed, so a format that writes nothing but blank writes
    // nothing at all — which is the empty format above under another spelling.
    if (written.empty()) {
        entry.fail(entry.key + ": '" + value +
                   "' writes no stamp at all, and there is no way to ask for no "
                   "date");
    }
    const auto isControl = [](char c) { return static_cast<unsigned char>(c) < 0x20; };
    if (std::any_of(written.begin(), written.end(), isControl)) {
        entry.fail(entry.key + ": '" + value +
                   "' writes a stamp over more than one line, which a date on a "
                   "header row cannot be");
    }
    return value;
}

/// Expands a leading "~/" to the home directory.
std::string expandTilde(std::string path) {
    if (path.size() >= 2 && path[0] == '~' && path[1] == '/') {
        if (const char* home = std::getenv("HOME")) {
            return std::string(home) + path.substr(1);
        }
    }
    return path;
}

/// A path setting: the value with a leading `~/` expanded, and never the empty
/// one. An empty path reads back as "the setting was never written", and a line
/// that is there was meant to do something.
std::string readPath(const CfgEntry& entry, const char* what) {
    const std::string path = expandTilde(entry.text());
    if (path.empty()) entry.fail(entry.key + " needs the path of " + what);
    return path;
}

/// An `echolist` line: the path, and the charset that file is written in where
/// the line says. Two values at most — the charset is one word, and a path with
/// a space in it is written in quotes like every other value.
EcholistSource readEcholist(const CfgEntry& entry) {
    if (entry.values.empty() || entry.values.size() > 2) {
        entry.fail(
            "echolist takes the path of an echolist, and after it the charset it is "
            "written in where that is not the locale's — e.g. echolist "
            "~/ftn/echolist/echo50.lst CP866");
    }
    EcholistSource source;
    source.path = expandTilde(entry.values[0]);
    if (source.path.empty()) entry.fail("echolist needs the path of an echolist");
    if (entry.values.size() == 2) source.charset = entry.values[1];
    return source;
}

/// The `arealist_description_priority` word: which of the two descriptions an
/// area with both is shown by.
DescriptionPriority parseDescriptionPriority(const CfgEntry& entry) {
    const std::string value = text::toLower(entry.one());
    if (value == "area") return DescriptionPriority::Area;
    if (value == "echolist") return DescriptionPriority::Echolist;
    entry.fail("unknown arealist_description_priority: '" + value +
               "' (expected area | echolist)");
}

domain::FtnAddress readAddress(const CfgEntry& entry, const std::string& value) {
    const auto address = domain::FtnAddress::parse(value);
    if (!address) entry.fail(entry.key + " is not an FTN address: '" + value + "'");
    return *address;
}

/// The fields of an `address_macro` line: the commas are what separates them,
/// and the blanks around each one mean nothing — `af,AreaFix,2:382/736` and
/// `af, AreaFix, 2:382/736` are the same line.
///
/// A field left empty is a field the line did not state, which is how a macro
/// names attributes without a subject: `af,AreaFix,2:382/736,,k/s`. A comma is
/// therefore always a separator and never text, quoted or not — a subject with
/// one in it cannot be written here, and nothing else on the line could hold one.
std::vector<std::string> macroFields(const CfgEntry& entry) {
    // The line as it was written, the quotes gone and the runs of blank between
    // words collapsed: a quoted subject arrives as one value with its spaces
    // kept, and an unquoted one as several that join back into what was typed.
    const std::string line = entry.text();

    std::vector<std::string> fields;
    size_t start = 0;
    while (true) {
        const size_t comma = line.find(',', start);
        const size_t end = comma == std::string::npos ? line.size() : comma;
        const std::string_view field = std::string_view(line).substr(start, end - start);
        fields.emplace_back(text::trim(field));
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return fields;
}

/// What an `address_macro` says about a word that is not an attribute. Its own
/// function, as `failUnknownCommand` is and for the same reason: the list of
/// what was on offer is built where it is thrown rather than once per word read.
[[noreturn]] void failUnknownAttribute(const CfgEntry& entry, const std::string& word) {
    // Uns is the one word that looks like an attribute and is none: it is shown
    // when Loc is set and Snt is not, and there is no bit to set for it. Saying
    // so beats "not an attribute" about a word every screen in AmberEdit prints.
    if (text::iequals(word, "Uns")) {
        entry.fail(
            "address_macro: 'Uns' is not an attribute a message carries — it is shown "
            "for Loc set with Snt clear, which is what a message written here "
            "already is");
    }

    std::string offered;
    for (const auto& name : domain::messageAttributeNames()) {
        if (!offered.empty()) offered += ", ";
        offered += name;
    }
    entry.fail("address_macro: '" + word + "' is not a message attribute (" + offered +
               ")");
}

/// The attributes an `address_macro` writes its message with: the short forms
/// the screens show, separated by blanks — `k/s`, `pvt k/s`, `Cra Imm`.
///
/// Blanks rather than another punctuation mark, because one of the names is
/// `K/s` and a slash between attributes could not then be told from the slash
/// inside one.
uint32_t macroAttributes(const CfgEntry& entry, const std::string& field) {
    uint32_t attributes = 0;
    for (const std::string& word : text::tokenize(field)) {
        const auto bit = domain::messageAttributeBit(word);
        if (!bit) failUnknownAttribute(entry, word);
        attributes |= *bit;
    }
    return attributes;
}

/// One `address_macro` line, read onto the macro it describes.
AddressMacro readAddressMacro(const CfgEntry& entry) {
    const std::vector<std::string> fields = macroFields(entry);
    if (fields.size() < 3 || fields.size() > 5) {
        entry.fail(
            "address_macro takes a macro, a name and an address, and may take a "
            "subject and the attributes after them, e.g. address_macro "
            "af,AreaFix,2:382/736,\"PASSWORD\",k/s");
    }

    AddressMacro macro;
    macro.macro = fields[0];
    macro.name = fields[1];
    if (macro.macro.empty()) entry.fail("address_macro needs the word that is typed");
    if (macro.name.empty()) {
        entry.fail("address_macro needs the name the message is addressed to");
    }
    macro.address = readAddress(entry, fields[2]);

    // Empty is "the line said nothing about it" rather than "make it empty":
    // that is what lets the attributes be named without a subject, and a macro
    // that wanted to blank a subject would be blanking one it had just put
    // there itself.
    if (fields.size() > 3 && !fields[3].empty()) macro.subject = fields[3];
    if (fields.size() > 4 && !fields[4].empty()) {
        macro.attributes = macroAttributes(entry, fields[4]);
    }
    return macro;
}

/// The value of `compose_cc_list`: what a message keeps of the `CC:` lines it
/// was written with.
CarbonList parseCarbonList(const CfgEntry& entry) {
    const std::string value = text::toLower(entry.one());
    if (value == "keep") return CarbonList::Keep;
    if (value == "names") return CarbonList::Names;
    if (value == "visible") return CarbonList::Visible;
    if (value == "hidden") return CarbonList::Hidden;
    if (value == "remove") return CarbonList::Remove;
    entry.fail("compose_cc_list: '" + entry.one() +
               "' is not one of its values (keep | names | visible | hidden | remove)");
}

/// The value of `compose_xc_list`, the same for the `XC:`/`XP:` lines. The
/// words are the ones GoldED writes them with, which is why they are not the
/// five above with one taken away.
CrosspostList parseCrosspostList(const CfgEntry& entry) {
    const std::string value = text::toLower(entry.one());
    if (value == "raw") return CrosspostList::Raw;
    if (value == "verbose") return CrosspostList::Verbose;
    if (value == "yes") return CrosspostList::Yes;
    if (value == "none") return CrosspostList::None;
    entry.fail("compose_xc_list: '" + entry.one() +
               "' is not one of its values (raw | verbose | yes | none)");
}

TwitMode parseTwitMode(const CfgEntry& entry) {
    const std::string value = text::toLower(entry.one());
    if (value == "show") return TwitMode::Show;
    if (value == "blank") return TwitMode::Blank;
    if (value == "skip") return TwitMode::Skip;
    if (value == "ignore") return TwitMode::Ignore;
    if (value == "kill") return TwitMode::Kill;
    entry.fail("twit_mode: '" + entry.one() +
               "' is not one of its values (show | blank | skip | ignore | kill)");
}

/// One `twit` line: an FTN address pattern, or a name to match whole.
///
/// The address is tried first and only where the line holds a ':' — every FTN
/// address states a zone, and without that a bare `*` would parse as "every
/// address there is" and quietly stop being the name glob it was written as.
/// Nothing that is not an address is refused: a name is what a name looks like,
/// and there is no third thing a `twit` line could be.
TwitRule readTwit(const CfgEntry& entry) {
    // Asked before `text()`, which would say only that the key needs a value:
    // what a `twit` line takes is worth saying where somebody has written one
    // and left it empty, and `twit ""` is the same mistake as a bare `twit`.
    constexpr const char* kNeeds =
        "twit needs a name or an FTN address, e.g. twit \"Ivan Ivanov\"";
    if (entry.values.empty()) entry.fail(kNeeds);

    // The line as it was written, quoted or not: `twit Ivan Ivanov` and
    // `twit "Ivan Ivanov"` are the same name, as they are for `name`.
    const std::string value = entry.text();
    if (value.empty()) entry.fail(kNeeds);

    TwitRule rule;
    if (value.find(':') != std::string::npos) {
        rule.address = domain::AddressPattern::parse(value);
        if (rule.address) return rule;
    }
    rule.name = value;
    return rule;
}

/// The entry for `aka`, adding it if this is the first line to name it. The two
/// keys write into the same list, so an AKA may be declared on its own and then
/// given destinations, or given them straight away.
AkaMatch& akaEntryFor(AppConfig& cfg, const domain::FtnAddress& aka) {
    for (auto& entry : cfg.akaMatches) {
        if (entry.aka == aka) return entry;
    }
    cfg.akaMatches.push_back(AkaMatch{aka, {}});
    return cfg.akaMatches.back();
}

/// The `aka` and `akamatch` lines, read once the whole file has been: they lean
/// on `address`, which may be written under them. Everything malformed stops
/// AmberEdit here rather than being skipped — a dropped entry would show up as
/// netmail from the wrong address, which is what the lines are there to
/// prevent.
void readAkas(const std::vector<const CfgEntry*>& akas, AppConfig& cfg) {
    for (const CfgEntry* entry : akas) {
        if (entry->values.empty()) entry->fail(entry->key + " needs an AKA of yours");
        const auto aka = readAddress(*entry, entry->values.front());

        if (entry->key == "aka") {
            if (entry->values.size() != 1) {
                entry->fail(
                    "aka takes one address — write the destinations it is used for "
                    "on an akamatch line");
            }
            akaEntryFor(cfg, aka);
            continue;
        }

        if (entry->values.size() < 2) {
            entry->fail(
                "akamatch takes an AKA and the address patterns it is used for, "
                "e.g. akamatch 2:5020/9999.1 2:5020/9999.*");
        }
        if (!cfg.userAddress) {
            entry->fail(
                "akamatch needs the address line — it is the address used when no "
                "pattern matches");
        }

        AkaMatch& into = akaEntryFor(cfg, aka);
        for (size_t i = 1; i < entry->values.size(); ++i) {
            const auto pattern = domain::AddressPattern::parse(entry->values[i]);
            if (!pattern) {
                entry->fail("akamatch pattern for '" + aka.toString() +
                            "' is not an FTN address pattern: '" + entry->values[i] +
                            "'");
            }
            into.patterns.push_back(*pattern);
        }
    }
}

/// One setting, read onto a config. False when the key is not a setting at all,
/// which is the caller's to complain about: the same line is refused with a
/// different message at the top level and inside a group.
///
/// This is the one place a setting is read, and an area group resolves by
/// running its own lines through it again — which is what keeps "adding a
/// setting is a branch in fromEntries()" true for groups as well, with no second
/// table of overrides to be kept in step with this one.
bool applySetting(AppConfig& cfg, const CfgEntry& entry) {
    const std::string& key = entry.key;

    if (key == "name") {
        cfg.userName = entry.text();
    } else if (key == "address") {
        cfg.userAddress = readAddress(entry, entry.one());
    } else if (key == "tosser_config") {
        cfg.tosserConfigPath = expandTilde(entry.text());
    } else if (key == "tosser_config_format") {
        cfg.tosserConfigFormat = parseFormat(entry);
    } else if (key == "nodelist") {
        cfg.nodelistSources.push_back(readPath(entry, "a nodelist file"));
    } else if (key == "address_macro") {
        AddressMacro macro = readAddressMacro(entry);
        // The same word twice is a contradiction, and the line that lost would
        // be an invisible one — exactly what any other key written twice is
        // stopped for.
        const auto same = [&macro](const AddressMacro& earlier) {
            return text::iequals(earlier.macro, macro.macro);
        };
        if (std::any_of(cfg.addressMacros.begin(), cfg.addressMacros.end(), same)) {
            entry.fail("address_macro '" + macro.macro + "' is defined twice");
        }
        cfg.addressMacros.push_back(std::move(macro));
    } else if (key == "nodelist_db") {
        cfg.nodelistDbPath = readPath(entry, "the compiled nodelist");
    } else if (key == "echolist") {
        cfg.echolistSources.push_back(readEcholist(entry));
    } else if (key == "echolist_db") {
        cfg.echolistDbPath = readPath(entry, "the compiled echolist");
    } else if (key == "keys") {
        cfg.keysPath = readPath(entry, "a keyboard layout");
    } else if (key == "tmpdir") {
        cfg.tempDirPath = readPath(entry, "a directory to work in");
    } else if (key == "default_charset") {
        cfg.defaultCharset = entry.one();
    } else if (key == "compose_charset") {
        cfg.composeCharset = entry.one();
    } else if (key == "lastread_user") {
        // The number is an index into an array on disk, and the array is
        // grown by seeking past its end — so a mistyped one would silently
        // create a sparse file megabytes long. 65535 is the ceiling the
        // Fido *.msg lastread file imposes anyway, its records being two
        // bytes wide.
        cfg.lastreadUser = static_cast<int>(entry.numberIn(0, 65535));
    } else if (key == "arealist_sort") {
        cfg.areaListSort = parseAreaSort(entry);
    } else if (key == "arealist_format") {
        const ListFormats formats = parseListFormats(entry, areaFormatSpec());
        cfg.areaListFormatNarrow =
            formatOf<AreaListFormat, AreaFieldKind>(formats.narrow, areaFieldOf);
        cfg.areaListFormatWide =
            formatOf<AreaListFormat, AreaFieldKind>(formats.wide, areaFieldOf);
    } else if (key == "msglist_format") {
        const ListFormats formats = parseListFormats(entry, msgFormatSpec());
        cfg.messageListFormatNarrow =
            formatOf<MsgListFormat, MsgFieldKind>(formats.narrow, msgFieldOf);
        cfg.messageListFormatWide =
            formatOf<MsgListFormat, MsgFieldKind>(formats.wide, msgFieldOf);
    } else if (key == "arealist_description_priority") {
        cfg.areaDescriptionPriority = parseDescriptionPriority(entry);
    } else if (key == "arealist_description_default") {
        // Written with no value at all the line says nothing, and what it would
        // silently mean — the blank column — is the one thing worth being sure
        // was meant. `arealist_description_default ""` says it out loud.
        if (entry.values.empty()) {
            entry.fail(
                "arealist_description_default needs the text to show for an area with "
                "no description — write arealist_description_default \"\" to leave the "
                "column blank");
        }
        cfg.areaDescriptionDefault = entry.text();
    } else if (key == "arealist_scrollbar") {
        cfg.areaListScrollbar = entry.flag();
    } else if (key == "msglist_scrollbar") {
        cfg.messageListScrollbar = entry.flag();
    } else if (key == "highlight_unread") {
        cfg.highlightUnread = entry.flag();
    } else if (key == "reader_scrollbar") {
        cfg.showScrollbar = entry.flag();
    } else if (key == "reader_underline_links") {
        cfg.underlineLinks = entry.flag();
    } else if (key == "reader_stylecodes") {
        cfg.styleCodes = entry.flag();
    } else if (key == "bbs_codes_renegade") {
        cfg.bbsCodesRenegade = entry.flag();
    } else if (key == "reader_edge_exit") {
        cfg.edgeExit = entry.flag();
    } else if (key == "reader_lastread_auto_next") {
        cfg.lastreadAutoNext = entry.flag();
    } else if (key == "areareplydirect") {
        cfg.areaReplyDirect = entry.flag();
    } else if (key == "compose_cc_list") {
        cfg.carbonList = parseCarbonList(entry);
    } else if (key == "compose_xc_list") {
        cfg.crosspostList = parseCrosspostList(entry);
    } else if (key == "reply_to_area") {
        // The tag is taken as it is written and checked against nothing: the
        // area list is not built yet when the config is read, and the dialog
        // that uses it opens at the top of the list where the tag names no
        // area it holds.
        cfg.replyToArea = entry.text();
    } else if (key == "twit") {
        cfg.twits.push_back(readTwit(entry));
    } else if (key == "twit_subj") {
        // A subject is one string, spaces and all, and an empty pattern would
        // cover every message carrying no subject at all — which is a great
        // many of them, and not something anyone writes a line to ask for.
        constexpr const char* kNeeds =
            "twit_subj needs a subject to ignore, e.g. twit_subj \"*SPAM*\"";
        if (entry.values.empty()) entry.fail(kNeeds);
        std::string pattern = entry.text();
        if (pattern.empty()) entry.fail(kNeeds);
        cfg.twitSubjects.push_back(std::move(pattern));
    } else if (key == "twit_to") {
        cfg.twitTo = entry.flag();
    } else if (key == "twit_mode") {
        cfg.twitMode = parseTwitMode(entry);
    } else if (key == "adaptive_ui_threshold") {
        // The floor is where a window still has room for the things
        // `when_narrow` puts on the screen — under twenty columns a menu
        // button has no corner left to stand in — and the ceiling is a window
        // nothing sane
        // is dragged past, where every window-led setting would be stuck on
        // the one side of the line for good.
        cfg.adaptiveUiThreshold = static_cast<int>(entry.numberIn(20, 1000));
    } else if (key == "back_button") {
        cfg.backButton = parseVisibility(entry);
    } else if (key == "hint_bar") {
        cfg.hintBar = parseVisibility(entry);
    } else if (key == "compose_delete_line_button") {
        cfg.composeDeleteLineButton = parseVisibility(entry);
    } else if (key == "show_location") {
        cfg.showLocation = entry.flag();
    } else if (key == "reader_show_message_size") {
        cfg.readerShowMessageSize = entry.flag();
    } else if (key == "show_recd_date") {
        cfg.showRecdDate = parseVisibility(entry);
    } else if (key == "menu_button") {
        cfg.menuButton = parseVisibility(entry);
    } else if (key == "reader_menu") {
        cfg.readerMenu = parseMenu(entry, readerCommands());
    } else if (key == "compose_menu") {
        cfg.composeMenu = parseMenu(entry, composeCommands());
    } else if (key == "menu_buttons_width") {
        // The floor is a frame with a column of label left inside it, under
        // which a button says nothing at all; the ceiling is wider than any
        // window a menu would be read in.
        cfg.menuButtonsWidth = static_cast<int>(entry.numberIn(4, 100));
    } else if (key == "click_animation_ms") {
        // Zero is meaningful — it is how the animation is turned off — and
        // the ceiling is a second, past which a click stops reading as
        // feedback and starts reading as the program having hung.
        cfg.clickAnimationMs = static_cast<int>(entry.numberIn(0, 1000));
    } else if (key == "reader_datetime_format") {
        cfg.readerDateTimeFormat = readTimeFormat(entry);
    } else if (key == "template_date_format") {
        cfg.templateDateFormat = readTimeFormat(entry);
    } else if (key == "template_time_format") {
        cfg.templateTimeFormat = readTimeFormat(entry);
    } else if (key == "theme") {
        cfg.themePath = expandTilde(entry.text());
    } else if (key == "template") {
        cfg.templatePath = expandTilde(entry.text());
    } else if (key == "quote_string") {
        // One '>' and no more: it is what quote levels are counted in, so a
        // second one would send a first-level quote out looking like a
        // second-level one — to us and to every other reader alike.
        const std::string value = entry.text();
        if (std::count(value.begin(), value.end(), '>') != 1) {
            entry.fail(
                "quote_string must contain exactly one '>', which is what marks "
                "a quoted line: '" +
                value + "'");
        }
        cfg.quoteString = value;
    } else if (key == "quote_margin") {
        // Below 20 the prefix leaves nothing worth wrapping, and past 255
        // the line stops being one another reader will show as it was
        // written.
        cfg.quoteMargin = static_cast<int>(entry.numberIn(20, 255));
    } else if (key == "import_begin") {
        // Empty is a value like any other here: it is how a file goes into a
        // message with no line in front of it, and `entry.text()` of a key
        // written with nothing after it is exactly that.
        cfg.importBegin = entry.text();
    } else if (key == "import_end") {
        cfg.importEnd = entry.text();
    } else if (key == "tearline") {
        cfg.tearline = entry.text();
    } else if (key == "origin") {
        cfg.origin = entry.text();
    } else {
        return false;
    }
    return true;
}

/// The settings an area group may state — everything a message carries out into
/// the network or is read back in, and nothing that decides what the screen
/// looks like or which areas there are.
///
/// A whitelist rather than a list of what is refused: a setting added to
/// applySetting() and not to this table comes out as "not a per-area setting",
/// which is a message, where the other way round it would come out as a layout
/// key silently overridable per area.
[[nodiscard]] bool isGroupSetting(const std::string& key) {
    static const std::set<std::string> kGroupSettings{"name",
                                                      "address",
                                                      "default_charset",
                                                      "compose_charset",
                                                      "origin",
                                                      "tearline",
                                                      "template",
                                                      "quote_string",
                                                      "quote_margin",
                                                      "import_begin",
                                                      "import_end",
                                                      "template_date_format",
                                                      "template_time_format",
                                                      "reader_stylecodes",
                                                      "bbs_codes_renegade",
                                                      "areareplydirect",
                                                      "reply_to_area",
                                                      "compose_cc_list",
                                                      "compose_xc_list",
                                                      "twit",
                                                      "twit_subj",
                                                      "twit_to",
                                                      "twit_mode"};
    return kGroupSettings.count(key) != 0;
}

/// The keys a config may write more than once, each line adding to a list
/// rather than putting itself in place of the last one. Anything else written
/// twice is a contradiction, and the line that lost would be an invisible one.
///
/// The same answer inside a `group ... endgroup` block as outside it: a group
/// that names three people it does not read is three `twit` lines, exactly as
/// the file itself would be.
[[nodiscard]] bool isRepeatable(const std::string& key) {
    return key == "aka" || key == "akamatch" || key == "nodelist" || key == "echolist" ||
           key == "address_macro" || key == "twit" || key == "twit_subj";
}

/// Whether the key is a setting at all — which only applySetting() can say, so
/// it is asked by reading the line onto a config that is thrown away.
///
/// A line that throws was recognised: only a key the chain knows reads its value
/// at all, and complaining about the value is what that reading does. The
/// complaint is swallowed here because the caller is not asking whether the line
/// is right, only whether the key exists — a `theme` with no value belongs
/// outside a group whether or not it also needs a value.
[[nodiscard]] bool isKnownSetting(const CfgEntry& entry) {
    try {
        AppConfig probe;
        return applySetting(probe, entry);
    } catch (const std::exception&) {
        return true;
    }
}

/// One `group ... endgroup` or `area ... endarea` block as it stood in the file:
/// the line that opened it, for the message a malformed one throws, and every
/// line between the two.
///
/// One struct for both because a block is the same shape whichever word opens
/// it; what the lines inside mean is the business of the two functions that read
/// them.
struct Block {
    const CfgEntry* opener{nullptr};
    std::vector<const CfgEntry*> lines;
};

/// Takes the blocks out of the flat list and answers with what is left — the
/// lines written at the top level, which are read exactly as they always were.
///
/// A pass of its own rather than a flag threaded through the reading loop: with
/// a flag every one of the thirty-odd branches would implicitly be asking which
/// scope it was in, and the set of keys already seen would have to be swapped
/// out and back by hand at exactly the right two lines. Three vectors say it
/// once.
///
/// Neither block nests, in itself or in the other: a group states settings for
/// the areas it covers and an area states what an area is, and there is nothing
/// one of them could mean inside the other.
std::vector<const CfgEntry*> splitBlocks(const std::vector<CfgEntry>& entries,
                                         std::vector<Block>& groups,
                                         std::vector<Block>& areas) {
    std::vector<const CfgEntry*> globals;
    Block open;
    bool openIsArea = false;

    for (const auto& entry : entries) {
        const bool opensArea = entry.key == "area";
        if (opensArea || entry.key == "group") {
            if (open.opener != nullptr) {
                entry.fail(entry.key + " inside a " + open.opener->key +
                           " block, opened at line " +
                           std::to_string(open.opener->line) +
                           " — the blocks do not nest");
            }
            if (opensArea) {
                if (entry.values.size() != 1) {
                    entry.fail(
                        "area takes the echotag of the area it declares and nothing "
                        "else, e.g. area ru.linux");
                }
            } else if (!entry.values.empty()) {
                entry.fail(
                    "group takes no values — write the areas it covers on member "
                    "lines inside it");
            }
            open = Block{&entry, {}};
            openIsArea = opensArea;
            continue;
        }

        const bool closesArea = entry.key == "endarea";
        if (closesArea || entry.key == "endgroup") {
            if (open.opener == nullptr) {
                entry.fail(entry.key + " with no " +
                           std::string(closesArea ? "area" : "group") + " above it");
            }
            if (closesArea != openIsArea) {
                entry.fail(entry.key + " closes the " + open.opener->key +
                           " block opened at line " + std::to_string(open.opener->line) +
                           " — write end" + open.opener->key);
            }
            if (!entry.values.empty()) entry.fail(entry.key + " takes no values");
            (closesArea ? areas : groups).push_back(std::move(open));
            open = Block{};
            continue;
        }

        if (open.opener != nullptr) {
            open.lines.push_back(&entry);
        } else if (entry.key == "member") {
            entry.fail("member is only written inside a group ... endgroup block");
        } else {
            globals.push_back(&entry);
        }
    }

    if (open.opener != nullptr) {
        open.opener->fail(open.opener->key + " is never closed by an end" +
                          open.opener->key);
    }
    return globals;
}

/// Whether the two groups may be told apart for some area they both cover. They
/// may not when a member of each pins the tag down exactly as far as the other's
/// and some tag matches both — and then, for any setting they both state, there
/// is no answer to which of them wins.
///
/// Checked against the patterns rather than against the areas the tosser
/// declares: an ambiguous config is ambiguous whether or not an echo that trips
/// it has been subscribed yet, and a config error should be one at startup and
/// not on the day a new area arrives.
void checkUnambiguous(const AreaGroup& first, const AreaGroup& second,
                      const CfgEntry& opener) {
    std::string shared;
    for (const auto& setting : first.settings) {
        if (second.states(setting.key)) {
            shared = setting.key;
            break;
        }
    }
    if (shared.empty()) return;

    for (const auto& a : first.members) {
        for (const auto& b : second.members) {
            if (a.specificity() != b.specificity() || !a.overlaps(b)) continue;
            opener.fail(
                "the groups at line " + std::to_string(first.line) + " and line " +
                std::to_string(second.line) + " both set " + shared + " for the areas '" +
                a.toString() + "' and '" + b.toString() +
                "' both cover, and neither pattern is the more particular — write "
                "one of them so that it says more about the tag than the other");
        }
    }
}

/// The `group ... endgroup` blocks, read once the whole file has been: a group
/// is laid over the file's own settings, so what it lays them over has to be
/// finished first.
void readGroups(const std::vector<Block>& blocks, AppConfig& cfg) {
    for (const auto& block : blocks) {
        AreaGroup group;
        group.line = block.opener->line;
        std::set<std::string> seen;

        for (const CfgEntry* entry : block.lines) {
            if (entry->key == "member") {
                if (entry->values.empty()) {
                    entry->fail("member needs an area tag, e.g. member esp.*");
                }
                for (const auto& value : entry->values) {
                    const auto pattern = AreaTagPattern::parse(value);
                    if (!pattern) entry->fail("member pattern is empty");
                    group.members.push_back(*pattern);
                }
                continue;
            }
            // Twice in one group is the contradiction it is anywhere else,
            // barring the keys that are a list. The set is the group's own: the
            // same key outside the group is another setting entirely, and the
            // two never see each other.
            if (!isRepeatable(entry->key) && !seen.insert(entry->key).second) {
                entry->fail(entry->key + " is set twice in this group");
            }
            if (!isGroupSetting(entry->key)) {
                // A key the top level knows is refused for what it is; one
                // nobody knows gets the message it would have got anywhere.
                if (isKnownSetting(*entry)) {
                    entry->fail("'" + entry->key +
                                "' is a setting for the whole config and not for one "
                                "area — write it outside the group ... endgroup block");
                }
                entry->fail("unknown setting '" + entry->key + "'");
            }
            group.settings.push_back(*entry);
        }

        if (group.members.empty()) {
            block.opener->fail("a group with no member line covers no area");
        }
        if (group.settings.empty()) {
            block.opener->fail("a group that sets nothing changes nothing");
        }

        // Read here and thrown away: it is the same code the resolution runs,
        // over the same lines, so a group that applies cleanly now applies
        // cleanly for ever after. A charset misspelled in a group should stop
        // AmberEdit at startup, as one misspelled outside a group does, and not
        // at the moment somebody opens the one area that group covers.
        AppConfig probe = cfg;
        for (const auto& setting : group.settings) applySetting(probe, setting);

        // An address a group states is an AKA of ours wherever it turns up, so
        // that a message written under it is still recognised as one's own. It
        // is added with no patterns, which is what an `aka` line on its own
        // already means: an address one has and one that is never picked by
        // destination.
        if (probe.userAddress && group.states("address")) {
            akaEntryFor(cfg, *probe.userAddress);
        }

        for (const auto& earlier : cfg.areaGroups) {
            checkUnambiguous(earlier, group, *block.opener);
        }
        cfg.areaGroups.push_back(std::move(group));
    }
}

/// The `area ... endarea` blocks: the areas the config declares itself, in the
/// fields a tosser config states its own with — a tag, a path, the base type,
/// the kind, a description, the label the area list's group column shows, the
/// AKA the area is presented under, and its links.
///
/// Read once the whole file has been, as the groups are: an address a block
/// states becomes an AKA of ours, and the `address` line it is added beside may
/// be written below the block.
///
/// Nothing here looks at the disk. Whether the base a block names is there is a
/// question for the moment the area is opened, and an area declared before its
/// base exists is exactly what someone writes a block for.
void readManualAreas(const std::vector<Block>& blocks, AppConfig& cfg) {
    for (const auto& block : blocks) {
        ManualArea manual;
        manual.line = block.opener->line;
        domain::AreaConfig& area = manual.area;
        area.tag = block.opener->values.front();
        if (area.tag.empty()) {
            block.opener->fail("area needs an echotag, e.g. area ru.linux");
        }

        // `link` is the one field that is a list; anything else written twice is
        // the contradiction it is anywhere else in the config.
        std::set<std::string> seen;

        for (const CfgEntry* entry : block.lines) {
            if (entry->key != "link" && !seen.insert(entry->key).second) {
                entry->fail(entry->key + " is set twice in this area");
            }

            if (entry->key == "path") {
                area.path = readPath(*entry, "the message base");
            } else if (entry->key == "type") {
                const auto type = domain::parseMsgBaseType(entry->one());
                if (!type) {
                    entry->fail("'" + entry->one() +
                                "' is not a base type "
                                "(squish | jam | msg | passthrough)");
                }
                area.type = *type;
            } else if (entry->key == "kind") {
                const auto kind = domain::parseAreaKind(entry->one());
                if (!kind) {
                    entry->fail("'" + entry->one() +
                                "' is not an area kind "
                                "(echo | netmail | local | bad | dupe)");
                }
                area.kind = *kind;
            } else if (entry->key == "description") {
                area.description = entry->text();
            } else if (entry->key == "group_label") {
                area.group = entry->one();
            } else if (entry->key == "address") {
                area.address = readAddress(*entry, entry->one());
            } else if (entry->key == "link") {
                if (entry->values.empty()) {
                    entry->fail("link needs the address of a downlink");
                }
                for (const auto& value : entry->values) {
                    area.links.push_back(readAddress(*entry, value));
                }
            } else if (isKnownSetting(*entry)) {
                // A block declares an area; how it is read is a group's to say,
                // and a group matches an area declared here like any other.
                entry->fail("'" + entry->key +
                            "' is a setting and not a field of an area — write it in a "
                            "group ... endgroup block whose member covers this area");
            } else {
                entry->fail("unknown area field '" + entry->key +
                            "' (path, type, kind, description, group_label, "
                            "address, link)");
            }
        }

        // A passthrough area is one there is no base for, and a path is exactly
        // what such an area does not have. Neither is guessed from the other:
        // fidoconfig reads an area with no path as a passthrough because the
        // word stands in the path's own place there, and a hand-written block
        // that says nothing about either is likelier a line left out than an
        // area deliberately without a base.
        if (area.type == domain::MsgBaseType::Passthrough) {
            if (!area.path.empty()) {
                block.opener->fail("area '" + area.tag +
                                   "' is passthrough and names a path — a "
                                   "passthrough area has no base on disk");
            }
        } else if (area.path.empty()) {
            block.opener->fail("area '" + area.tag +
                               "' states no path — write `type passthrough` "
                               "where there is deliberately no base");
        }

        for (const auto& earlier : cfg.manualAreas) {
            if (!text::iequals(earlier.area.tag, area.tag)) continue;
            block.opener->fail("area '" + area.tag +
                               "' is declared twice, here and at line " +
                               std::to_string(earlier.line));
        }

        // The AKA an area is presented under is one of ours wherever it turns
        // up, the same way a group's `address` is: a message written under it is
        // still the user's own. With no patterns, so it is never picked by
        // destination — the area it belongs to is what picks it.
        if (area.address.isValid()) akaEntryFor(cfg, area.address);

        cfg.manualAreas.push_back(std::move(manual));
    }
}

AppConfig fromEntries(const std::vector<CfgEntry>& entries,
                      const std::string& originName) {
    AppConfig cfg;
    std::set<std::string> seen;
    std::vector<const CfgEntry*> akas;
    std::vector<Block> groupBlocks;
    std::vector<Block> areaBlocks;

    const std::vector<const CfgEntry*> globals =
        splitBlocks(entries, groupBlocks, areaBlocks);

    for (const CfgEntry* entry : globals) {
        const std::string& key = entry->key;

        // The only two keys a config may write more than once: they are a list,
        // and the list is what makes them useful.
        if (key == "aka" || key == "akamatch") {
            akas.push_back(entry);
            continue;
        }
        // A `nodelist` line is a list as well — several of them compile into
        // one file, and their order is what settles which one keeps an address
        // two of them both name — and so are `address_macro`, one line per
        // macro, and `twit`/`twit_subj`, one line per person or subject not
        // worth reading. They differ from the AKAs only in needing nothing said
        // once the whole config has been read, so they are applied where they
        // stand rather than being collected. What a repeated `address_macro`
        // may not do is name the same word twice, which `applySetting` refuses
        // where it reads the line.
        //
        // Anything else said twice is a contradiction, and the line that lost
        // would be an invisible one. Which of them was meant is not ours to
        // guess.
        if (!isRepeatable(key) && !seen.insert(key).second) {
            entry->fail(key + " is set twice");
        }

        if (!applySetting(cfg, *entry)) entry->fail("unknown setting '" + key + "'");
    }

    // Asked of the lines rather than of the config: the format has a value
    // whether or not it was stated, so the file itself is the only thing that
    // can say whether it was.
    const bool formatStated = std::any_of(
        globals.begin(), globals.end(),
        [](const CfgEntry* entry) { return entry->key == "tosser_config_format"; });
    // The area list has to come from somewhere, and there are two somewheres:
    // the tosser's config and the blocks this config writes itself. Either will
    // do, both together are the ordinary case of a system with one local base
    // beside the echoes, and neither is a config that opens on an empty screen.
    if (cfg.tosserConfigPath.empty()) {
        if (areaBlocks.empty()) {
            throw std::runtime_error(
                originName +
                ": there is no area list — set tosser_config to the tosser's "
                "areas/areas.bbs, or declare the areas here in area ... endarea "
                "blocks");
        }
        if (formatStated) {
            throw std::runtime_error(
                originName +
                ": tosser_config_format names the format of a config that is not "
                "set — write tosser_config as well, or take the line out");
        }
    } else if (!formatStated) {
        throw std::runtime_error(
            originName +
            ": tosser_config_format is not set — state fidoconfig, areas.bbs or "
            "squish.cfg explicitly");
    }
    // Both charsets are stated, and neither stands in for the other: what the
    // echoes one reads are written in says nothing about what one wants to
    // write in, and a default guessed here would be a silent mojibake in
    // whichever of the two directions it guessed wrong.
    if (cfg.defaultCharset.empty()) {
        throw std::runtime_error(
            originName +
            ": default_charset is not set — it is the charset a message is read "
            "in when its CHRS kludge names none");
    }
    if (cfg.composeCharset.empty()) {
        throw std::runtime_error(
            originName +
            ": compose_charset is not set — it is the charset a message is "
            "written in");
    }
    // A nodelist with nowhere to compile it to is a line that does nothing, and
    // the other way round is not: a config may read a compiled nodelist that
    // something else keeps up to date, and then it names only the file.
    if (!cfg.nodelistSources.empty() && cfg.nodelistDbPath.empty()) {
        throw std::runtime_error(
            originName +
            ": nodelist_db is not set — it is the file AmberEdit compiles the "
            "nodelist lines into, and the file AmberEdit reads them back from");
    }
    // The echolist stands on the same terms, and for the same reason: a config
    // may read a compiled echolist something else keeps up to date, and then it
    // names only the file.
    if (!cfg.echolistSources.empty() && cfg.echolistDbPath.empty()) {
        throw std::runtime_error(
            originName +
            ": echolist_db is not set — it is the file AmberEdit compiles the "
            "echolist lines into, and the file AmberEdit reads them back from");
    }

    readAkas(akas, cfg);
    readManualAreas(areaBlocks, cfg);
    readGroups(groupBlocks, cfg);
    return cfg;
}

}  // namespace

bool TwitRule::matches(std::string_view who, const domain::FtnAddress& addr) const {
    // A pattern over addresses says nothing about a message that carries none —
    // which is every echomail message in a JAM base, the format keeping no
    // address subfields there. Matching the zeroes such a message hands back
    // would make `twit 2:*` an area nobody could read.
    if (address) return addr.isValid() && address->matches(addr);
    return text::globMatches(name, who);
}

bool AppConfig::isTwit(const domain::MessageHeader& header) const {
    for (const std::string& pattern : twitSubjects) {
        if (text::globMatches(pattern, header.subject)) return true;
    }
    for (const TwitRule& rule : twits) {
        if (rule.matches(header.from, header.origAddr)) return true;
        // The other end of the message, where `twit_to` asks for it: an answer
        // to somebody one does not read quotes the whole of what they said.
        if (twitTo && rule.matches(header.to, header.destAddr)) return true;
    }
    return false;
}

bool AreaGroup::states(std::string_view key) const {
    return std::any_of(settings.begin(), settings.end(),
                       [key](const CfgEntry& entry) { return entry.key == key; });
}

std::optional<std::tuple<int, int, bool>> AreaGroup::specificityFor(
    std::string_view tag) const {
    std::optional<std::tuple<int, int, bool>> best;
    for (const auto& member : members) {
        if (!member.matches(tag)) continue;
        const auto rank = member.specificity();
        if (!best || rank > *best) best = rank;
    }
    return best;
}

std::vector<const AreaGroup*> AppConfig::groupsFor(const domain::AreaConfig& area) const {
    // Kept beside the pointer so that the sort has something to compare without
    // working the patterns out again for every comparison.
    std::vector<std::pair<std::tuple<int, int, bool>, const AreaGroup*>> matched;
    for (const auto& group : areaGroups) {
        if (const auto rank = group.specificityFor(area.tag)) {
            matched.emplace_back(*rank, &group);
        }
    }

    // Least specific first, which is the order they are laid over one another
    // in: the last one applied is the one that says the most about this tag.
    // Two that say exactly as much are refused while the config is read, so the
    // stable sort's tie — the group written first — is a case that cannot arise
    // for any setting they both state.
    std::stable_sort(matched.begin(), matched.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });

    std::vector<const AreaGroup*> groups;
    groups.reserve(matched.size());
    for (const auto& entry : matched) groups.push_back(entry.second);
    return groups;
}

AppConfig AppConfig::effectiveFor(const domain::AreaConfig& area) const {
    AppConfig resolved = *this;
    for (const AreaGroup* group : groupsFor(area)) {
        // Every one of these lines was read once already, onto a copy, while the
        // config was parsed — so nothing here can throw, and a setting a group
        // never mentions keeps whatever the group before it, or the file itself,
        // gave it.
        for (const auto& setting : group->settings) applySetting(resolved, setting);
    }
    return resolved;
}

std::optional<domain::FtnAddress> AppConfig::akaFor(
    const domain::FtnAddress& dest) const {
    if (auto matched = akaMatching(dest)) return matched;
    return userAddress;
}

const AddressMacro* AppConfig::addressMacroFor(std::string_view typed) const {
    const std::string_view word = text::trim(typed);
    if (word.empty()) return nullptr;

    for (const auto& macro : addressMacros) {
        if (text::iequals(macro.macro, word)) return &macro;
    }
    return nullptr;
}

bool AppConfig::isOwnAddress(const domain::FtnAddress& addr) const {
    if (userAddress && userAddress->same4D(addr)) return true;
    return std::any_of(akaMatches.begin(), akaMatches.end(),
                       [&addr](const AkaMatch& entry) { return entry.aka.same4D(addr); });
}

std::optional<domain::FtnAddress> AppConfig::akaMatching(
    const domain::FtnAddress& dest) const {
    const domain::FtnAddress* best = nullptr;
    int bestDepth = -1;

    // The most specific matching pattern wins; `>` leaves a tie with the AKA
    // written first, the entries standing in the order of the file.
    for (const auto& entry : akaMatches) {
        for (const auto& pattern : entry.patterns) {
            if (!pattern.matches(dest)) continue;
            if (pattern.depth() > bestDepth) {
                bestDepth = pattern.depth();
                best = &entry.aka;
            }
        }
    }
    if (best != nullptr) return *best;
    return std::nullopt;
}

AppConfig AppConfig::loadFromString(const std::string& text,
                                    const std::string& originName) {
    return fromEntries(parseCfg(text, originName), originName);
}

AppConfig AppConfig::loadFromFile(const std::string& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        throw std::runtime_error("config not found: " + path);
    }
    AppConfig cfg = loadFromString(text::readFile(path), path);
    // Where a file named without a path is looked for. Settled here rather than
    // in `loadFromString`, which parses a config that need not have come off a
    // disk at all.
    cfg.configDir = std::filesystem::path(path).parent_path().string();

    // The template is required, and it is read here rather than when the editor
    // is first reached: a config that cannot compose should say so at startup,
    // not at the moment someone sits down to write. Reading it is the only
    // answer that counts — a path in the config says nothing about a file on
    // disk. `loadFromString` skips this: it parses a config without standing in
    // for the machine one would run on.
    if (cfg.templatePath.empty()) {
        throw std::runtime_error(path +
                                 ": template is not set — it must point at the file "
                                 "a new message starts from");
    }
    try {
        static_cast<void>(text::readFile(cfg.templatePath));
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("message template: ") + e.what());
    }

    // And the same for a template an area group names, for the same reason: a
    // config that cannot compose in one of its groups should say so at startup
    // rather than at the area that group covers.
    for (const auto& group : cfg.areaGroups) {
        if (!group.states("template")) continue;
        AppConfig probe = cfg;
        for (const auto& setting : group.settings) applySetting(probe, setting);
        try {
            static_cast<void>(text::readFile(probe.templatePath));
        } catch (const std::exception& e) {
            throw std::runtime_error("message template of the group at line " +
                                     std::to_string(group.line) + ": " + e.what());
        }
    }
    return cfg;
}

std::optional<std::string> AppConfig::findDefaultConfigPath() {
    std::vector<std::string> candidates;
    if (const char* env = std::getenv("AMBEREDIT_CONFIG")) candidates.emplace_back(env);
    candidates.emplace_back("amberedit.cfg");
    if (const char* home = std::getenv("HOME")) {
        candidates.emplace_back(std::string(home) + "/.ambereditrc");
    }

    std::error_code ec;
    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate, ec)) return candidate;
    }
    return std::nullopt;
}

}  // namespace amberedit::config
