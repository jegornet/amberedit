#include "config/fidoconfig_parser.hpp"

#include <cstdlib>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>

#include "config/text_util.hpp"

namespace amberedit::config {

using domain::AreaConfig;
using domain::AreaKind;
using domain::FtnAddress;
using domain::MsgBaseType;

namespace {

/// Area options followed by exactly one value. Anything else starting with
/// '-' is a boolean flag and is skipped.
///
/// The list is every value-taking option in husky's own `parseAreaOption()`
/// (fidoconf/src/line.c) and nothing besides: an option listed here that the
/// tosser treats as a flag would make us eat the option after it. `-d` reads
/// its value through `getDescription()` rather than the usual token split, but
/// takes one all the same.
const std::set<std::string>& valueOptions() {
    static const std::set<std::string> options = {
        "-a",  "-b",         "-d",           "-g",    "-p",      "-lr",     "-lw",
        "-$m", "-dupecheck", "-dupehistory", "-scan", "-toonew", "-tooold",
    };
    return options;
}

/// What one line of a fidoconfig leaves behind for the lines after it: the
/// variables `set` defines and the template `echoareadefaults` states.
///
/// One of these is threaded through a whole parse, includes and all. An
/// included file neither starts from a clean slate nor gives its settings back
/// at the end — in husky the two live in the config being built, and a config
/// that sets a variable in `include`d common settings and uses it in the file
/// below is the ordinary way of writing one.
struct ParseState {
    /// Keys folded to lower case: fidoconfig looks a variable up without
    /// regard to case, `[BASE]` and `[base]` naming the same one.
    std::map<std::string, std::string> variables;

    /// The last `echoareadefaults`. A default-constructed one is what "no
    /// defaults" looks like, which is also what the statement leaves behind
    /// when it names nothing.
    AreaConfig defaults;

    /// Every `include` that named a file which is not there, as the path was
    /// looked for — mapped, expanded and resolved. Kept rather than complained
    /// about: a start goes on reading, and an area list short by an include is
    /// still better than no AmberEdit at all. What it is for is the caller who
    /// has somebody in front of it, `checkTosserConfig()` in the setup wizard,
    /// where "no areas in this file" and "this file includes one that is not
    /// there" are the same fact and only the second is worth reading.
    std::vector<std::string> missingIncludes;

    /// What the statements about the system said — the sysop, the addresses and
    /// the links — gathered as the areas are: they may stand in an included
    /// file as easily as in this one, and a second pass over the same text to
    /// find them would be the same work done twice.
    TosserSystem system;

    /// The `linkdefaults` template, and whether there is one: a `link` below
    /// such a block starts from a copy of it. husky clones the whole link
    /// structure and we clone the four fields we read, which comes to the same
    /// thing for them — a password the template set through `password` reaches
    /// both robots, and one it set through `areafixPwd` reaches the one.
    TosserLink linkDefaults;
    bool hasLinkDefaults{false};

    /// Whether the lines being read belong to that template rather than to a
    /// link. `linkdefaults` opens it, `linkdefaults end` and `destroy` close
    /// it, and so does a `link` line — husky stops describing defaults where a
    /// link begins, so the `end` is needed only before global statements.
    bool describingLinkDefaults{false};
};

/// The variables a config can use without setting them.
///
/// Three of them exist to write a character that would otherwise be read as
/// syntax — `[[]` for a literal `[` — and are why expansion has to be able to
/// answer with a bracket it does not then look at again. `OS` is what husky's
/// own configs test to pick a path spelling. husky's `[#]` is not among them: a
/// comment is taken off the line before a variable is looked at, so nothing a
/// variable expands to could put the `#` back.
std::map<std::string, std::string> initialVariables() {
    return {
        {"[", "["},
        {"\"", "\""},
        {"'", "'"},
#ifdef _WIN32
        {"os", "WIN"},
#else
        {"os", "UNIX"},
#endif
    };
}

/// The value of a variable: what `set` gave it, then the environment, then
/// nothing. The environment is looked up under the name as written, since that
/// is the only way a process environment can be read.
std::string variableValue(const ParseState& state, const std::string& name) {
    const auto it = state.variables.find(text::toLower(name));
    if (it != state.variables.end()) return it->second;
    if (name.empty()) return {};
    if (const char* fromEnvironment = std::getenv(name.c_str())) return fromEnvironment;
    return {};
}

/// Replaces every `[name]` with the variable's value, an undefined one with
/// nothing at all.
///
/// The result is not looked at again, so a value holding a bracket is text and
/// not another substitution — which is what makes `[[]` a way of writing a
/// literal `[`. A `[` with no `]` after it is a bracket like any other.
std::string expandVariables(std::string_view line, const ParseState& state) {
    if (line.find('[') == std::string_view::npos) return std::string(line);

    std::string expanded;
    expanded.reserve(line.size());
    for (size_t i = 0; i < line.size();) {
        if (line[i] != '[') {
            expanded += line[i++];
            continue;
        }
        const size_t close = line.find(']', i + 1);
        if (close == std::string_view::npos) {
            expanded += line[i++];
            continue;
        }
        expanded += variableValue(state, std::string(line.substr(i + 1, close - i - 1)));
        i = close + 1;
    }
    return expanded;
}

/// `set <name> = <value>`, the whole line after the keyword.
///
/// The name is everything up to the first `=`, the value everything after it,
/// both without the whitespace around them; a value in double quotes keeps the
/// spaces inside them. A value of nothing forgets the variable rather than
/// defining it as empty, so that what the environment says is heard again.
void parseSet(std::string_view rest, ParseState& state) {
    const size_t equals = rest.find('=');
    if (equals == std::string_view::npos) return;  // not a definition of anything

    const std::string name = text::toLower(text::trim(rest.substr(0, equals)));
    if (name.empty()) return;

    std::string_view value = rest.substr(equals + 1);
    while (!value.empty() && text::asciiIsSpace(value.front())) value.remove_prefix(1);

    std::string unquoted;
    if (!value.empty() && value.front() == '"') {
        // Everything up to the closing quote, which is the first one not
        // written as `\"`.
        for (size_t i = 1; i < value.size(); ++i) {
            if (value[i] == '"' && value[i - 1] != '\\') break;
            unquoted += value[i];
        }
    } else {
        unquoted.assign(value);
    }

    if (unquoted.empty()) {
        state.variables.erase(name);
        return;
    }
    state.variables[name] = std::move(unquoted);
}

/// Returns the area kind for a keyword, or nullopt if the line does not
/// declare an area at all.
std::optional<AreaKind> parseAreaKeyword(std::string_view keyword) {
    if (text::iequals(keyword, "echoarea")) return AreaKind::Echo;
    if (text::iequals(keyword, "netmailarea")) return AreaKind::Netmail;
    if (text::iequals(keyword, "localarea")) return AreaKind::Local;
    if (text::iequals(keyword, "badarea")) return AreaKind::Bad;
    if (text::iequals(keyword, "dupearea")) return AreaKind::Dupe;
    return std::nullopt;
}

/// Whether `echoareadefaults` speaks for an area of this kind.
///
/// Netmail is the one it does not: it is not echomail, and none of what the
/// statement sets — the group the echoes are filed under, the base type they
/// are all written in — is meant for it.
bool inheritsDefaults(AreaKind kind) {
    return kind != AreaKind::Netmail;
}

/// Takes off the pair of double quotes a value may be written in, which is what
/// husky's `stripRoundingChars()` does to every string keyword's value. A quote
/// at one end only is text like any other.
std::string unquote(std::string_view value) {
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
        value = value.substr(1, value.size() - 2);
    return std::string(value);
}

/// The link a link-scoped keyword speaks about: the `linkdefaults` template
/// while one is being described, and otherwise the last `link` opened — which
/// is husky's `getDescrLink()`. Null where neither is there, a line husky
/// answers with "you must define a link first" and we pass over.
TosserLink* describedLink(ParseState& state) {
    if (state.describingLinkDefaults) return &state.linkDefaults;
    return state.system.links.empty() ? nullptr : &state.system.links.back();
}

/// Strips a '#' comment and trailing whitespace.
std::string stripComment(std::string_view line) {
    const size_t hash = line.find('#');
    if (hash != std::string_view::npos) line = line.substr(0, hash);
    return std::string(text::trim(line));
}

tl::expected<void, ErrorPtr> parseInto(const std::string& content,
                                       std::vector<AreaConfig>& areas,
                                       const std::filesystem::path& baseDir,
                                       int includeDepth, ParseState& state,
                                       const PathMap& paths, const std::string& charset);

/// Reads the options an area line and an `echoareadefaults` line both take,
/// from `first` to the end of the line, into an area that already holds
/// whatever it inherited. An option that is given states what it states; one
/// that is absent leaves the inherited value alone, which is the whole of what
/// inheriting means here.
void applyAreaOptions(const std::vector<std::string>& tokens, size_t first,
                      AreaConfig& area) {
    // Decided over the whole line rather than where it is read: husky lets
    // `-pass` stand anywhere among the options and the word `passthrough`
    // where the base would be, and neither is undone by a `-b` after it.
    // Inherited too — `echoareadefaults passthrough` is a default like any
    // other, and an area that leaves its path out keeps it.
    bool passthrough = area.type == MsgBaseType::Passthrough;

    for (size_t i = first; i < tokens.size(); ++i) {
        const std::string& token = tokens[i];

        if (!token.empty() && token[0] == '-') {
            const std::string option = text::toLower(token);

            // `-pass` makes the area passthrough with the path and the base
            // type written out beside it (fidoconf/src/line.c), which is how a
            // config turns a base off for a while without losing the line.
            if (option == "-pass") {
                passthrough = true;
                continue;
            }

            if (valueOptions().count(option) == 0) continue;  // boolean flag
            if (i + 1 >= tokens.size()) break;
            // A value never starts with '-'. Should the list above ever fall
            // behind husky again, this keeps a flag mistaken for a value from
            // swallowing the option after it — losing `-b squish` to a stray
            // `-pack` would leave the area with no base type at all.
            if (!tokens[i + 1].empty() && tokens[i + 1][0] == '-') continue;
            const std::string& value = tokens[++i];

            if (option == "-a") {
                // The AKA the area is presented under, and one address only.
                // Links are the bare addresses further along the line — the
                // same division squish.cfg makes with -p.
                if (auto address = FtnAddress::parse(value)) area.address = *address;
            } else if (option == "-b") {
                // A word nobody knows leaves the type unstated, and the base is
                // then worked out from the files on disk.
                area.type =
                    domain::parseMsgBaseType(value).value_or(MsgBaseType::Unknown);
            } else if (option == "-g") {
                area.group = value;
            } else if (option == "-d") {
                area.description = value;
            }
            continue;
        }

        // `passthrough` names no base of its own wherever it stands, which on
        // an `echoareadefaults` line is the only place it can stand.
        if (text::iequals(token, "passthrough")) {
            passthrough = true;
            continue;
        }

        // A bare token after the path is a link if it looks like an FTN address.
        // The links the defaults named are already in the list, and these come
        // after them.
        if (auto addr = FtnAddress::parse(token)) area.links.push_back(*addr);
    }

    if (passthrough) {
        area.type = MsgBaseType::Passthrough;
        area.path.clear();
    }
}

/// Parses a single area declaration line.
std::optional<AreaConfig> parseAreaLine(const std::vector<std::string>& tokens,
                                        AreaKind kind, const ParseState& state,
                                        const PathMap& paths) {
    if (tokens.size() < 2) return std::nullopt;  // the tag is the least of it

    AreaConfig area = inheritsDefaults(kind) ? state.defaults : AreaConfig{};
    area.kind = kind;
    area.tag = tokens[1];

    // The token after the tag is the path — unless the defaults have already
    // made the area passthrough, and then husky lets it be left out and reads
    // that token as an option instead. It tells the two apart by looking for a
    // path separator, so `EchoArea x -g A` needs no path under passthrough
    // defaults while `EchoArea x /ftn/x` still has one.
    size_t next = 2;
    if (next < tokens.size()) {
        const std::string& token = tokens[next];
        const bool pathMayBeLeftOut = area.type == MsgBaseType::Passthrough;

        if (text::iequals(token, "passthrough")) {
            area.type = MsgBaseType::Passthrough;
            area.path.clear();
            ++next;
        } else if (!token.empty() && token[0] == '-') {
            // An option, whatever the defaults say: no path begins with '-'.
        } else if (!pathMayBeLeftOut || token.find_first_of("/\\") != std::string::npos) {
            // Mapped here and not where the base is opened: this is the one
            // place a path the tosser wrote becomes an area's, and what stands
            // in `token` has already had its `[name]` variables expanded.
            area.path = paths.apply(token);
            // A base of its own is not the passthrough the defaults meant, and
            // what it holds is then read off the files as for any other area.
            if (area.type == MsgBaseType::Passthrough) area.type = MsgBaseType::Unknown;
            ++next;
        }
    }

    // A line naming a tag and no path is an area only where the defaults have
    // already said it has no base of its own.
    if (next == 2 && area.type != MsgBaseType::Passthrough) return std::nullopt;

    applyAreaOptions(tokens, next, area);

    if (area.path.empty() && area.type == MsgBaseType::Unknown)
        area.type = MsgBaseType::Passthrough;
    return area;
}

tl::expected<void, ErrorPtr> parseInto(const std::string& content,
                                       std::vector<AreaConfig>& areas,
                                       const std::filesystem::path& baseDir,
                                       int includeDepth, ParseState& state,
                                       const PathMap& paths, const std::string& charset) {
    for (const auto& rawLine : text::splitLines(content)) {
        // The comment goes first and the variables second, which is the order
        // the format reads them in: what a variable expands to is text, and a
        // `#` it holds does not start a comment in the line it lands in.
        const std::string line = expandVariables(stripComment(rawLine), state);
        if (line.empty()) continue;

        const auto tokens = text::tokenize(line);
        if (tokens.empty()) continue;

        // set <name>=<value> — read off the line itself rather than the tokens,
        // since a value may hold the spaces the tokenizer splits on.
        if (text::iequals(tokens[0], "set")) {
            parseSet(std::string_view(line).substr(tokens[0].size()), state);
            continue;
        }

        // echoareadefaults [options] — what the echo, local, bad and dupe areas
        // below it start from. Each statement replaces the one before it whole,
        // so one naming nothing (or the `off` husky writes for readability) is
        // how a config stops inheriting.
        if (text::iequals(tokens[0], "echoareadefaults")) {
            state.defaults = AreaConfig{};
            applyAreaOptions(tokens, 1, state.defaults);
            continue;
        }

        // Everything after the keyword, which is what husky's `getRestOfLine()`
        // hands the string statements. Worked out here because six of them
        // want it and none of them wants the tokens.
        const auto restOfLine = [&line, &tokens]() {
            return std::string(
                text::trim(std::string_view(line).substr(tokens[0].size())));
        };

        // sysop <name> — the rest of the line, which is how husky reads it
        // (`fc_copyString(getRestOfLine())` in fidoconf/src/line.c): a name is
        // several words and none of them is an option. The quotes around a
        // value are taken off it, and a second statement wins over the first,
        // both of them husky's own behaviour for a keyword written twice. A
        // statement naming nobody leaves the name as it was, husky calling that
        // a missing parameter and keeping what it had.
        if (text::iequals(tokens[0], "sysop")) {
            std::string sysop = unquote(restOfLine());
            if (!sysop.empty()) state.system.sysop = std::move(sysop);
            continue;
        }

        // address <addr> — the first token after the keyword and nothing after
        // it, husky looking at one aka per statement. The statement repeats,
        // the first one naming the main address and the rest the other AKAs,
        // which is the order they are kept in here.
        if (text::iequals(tokens[0], "address") && tokens.size() >= 2) {
            if (auto address = FtnAddress::parse(tokens[1])) {
                // 5D as written, 4D as kept: fidoconfig's address is full 5D
                // and nothing AmberEdit compares an address against carries a
                // domain — a message base holds four numbers and no more.
                address->domain.clear();
                state.system.addresses.push_back(*address);
            }
            continue;
        }

        // linkdefaults [begin|end|destroy] — the template a `link` below it
        // starts from. A bare `linkdefaults` is `begin`; `end` closes the
        // block and keeps the template, `destroy` throws it away. A word that
        // is none of the three is husky's error and our silence.
        if (text::iequals(tokens[0], "linkdefaults")) {
            const std::string what = tokens.size() >= 2 ? text::toLower(tokens[1]) : "";
            if (what.empty() || what == "begin") {
                state.describingLinkDefaults = true;
                state.hasLinkDefaults = true;
            } else if (what == "end") {
                state.describingLinkDefaults = false;
            } else if (what == "destroy") {
                state.describingLinkDefaults = false;
                state.hasLinkDefaults = false;
                state.linkDefaults = TosserLink{};
            }
            continue;
        }

        // link <name> — a new link, starting from the template where there is
        // one. The name is the tosser's own label for it and is not kept: what
        // a netmail to this link's robot is addressed to is the robot's name,
        // which is a statement of its own.
        //
        // Opening a link closes the `linkdefaults` block, as it does in husky,
        // so the statements below belong to the link and not to the template.
        if (text::iequals(tokens[0], "link")) {
            state.describingLinkDefaults = false;
            state.system.links.push_back(state.hasLinkDefaults ? state.linkDefaults
                                                               : TosserLink{});
            continue;
        }

        // The statements inside a link, which the `linkdefaults` template takes
        // as readily. All of them are husky's: `aka` is the node the link is,
        // `password` is the one password of the whole link and reaches both
        // robots, `areafixPwd`/`filefixPwd` state one robot's on its own, and
        // `areafixName`/`filefixName` say what that robot is called.
        //
        // Order is what tells a `password` after an `areafixPwd` from one
        // before it, and it is kept: each statement is applied where it stands,
        // which is how husky reads them and the only reading under which
        // `password` overwriting what came before it means anything.
        if (text::iequals(tokens[0], "aka") || text::iequals(tokens[0], "password") ||
            text::iequals(tokens[0], "areafixpwd") ||
            text::iequals(tokens[0], "filefixpwd") ||
            text::iequals(tokens[0], "areafixname") ||
            text::iequals(tokens[0], "filefixname")) {
            TosserLink* link = describedLink(state);
            if (link == nullptr) continue;  // no link and no template to speak of
            const std::string keyword = text::toLower(tokens[0]);

            if (keyword == "aka") {
                if (tokens.size() >= 2) {
                    if (auto address = FtnAddress::parse(tokens[1])) {
                        address->domain.clear();  // 4D, for the reason above
                        link->aka = *address;
                    }
                }
            } else if (keyword == "password") {
                // One password for everything the link does, robots included.
                link->areafixPwd = restOfLine();
                link->filefixPwd = link->areafixPwd;
            } else if (keyword == "areafixpwd") {
                link->areafixPwd = restOfLine();
            } else if (keyword == "filefixpwd") {
                link->filefixPwd = restOfLine();
            } else if (keyword == "areafixname") {
                link->areafixName = unquote(restOfLine());
            } else {
                link->filefixName = unquote(restOfLine());
            }
            continue;
        }

        // include <file> — the path is relative to the including config.
        //
        // Mapped before it is resolved, since a `map_path` is what turns an
        // absolute path of the tosser's into one this machine has a root for:
        // `c:\fido\config\areas` is *relative* to std::filesystem here, and
        // resolving it against the including file's directory first would look
        // for it in a place nothing is.
        if (text::iequals(tokens[0], "include") && tokens.size() >= 2) {
            if (includeDepth <= 0) continue;  // guard against include cycles
            std::filesystem::path included(paths.apply(tokens[1]));
            if (included.is_relative()) included = baseDir / included;
            std::error_code ec;
            if (!std::filesystem::exists(included, ec)) {
                state.missingIncludes.push_back(included.string());
                continue;
            }
            auto text = text::readFileIn(included.string(), charset);
            if (!text) return tl::make_unexpected(std::move(text).error());
            auto read = parseInto(*text, areas, included.parent_path(), includeDepth - 1,
                                  state, paths, charset);
            if (!read) return tl::make_unexpected(std::move(read).error());
            continue;
        }

        if (auto kind = parseAreaKeyword(tokens[0])) {
            auto area = parseAreaLine(tokens, *kind, state, paths);
            // A passthrough area is not in the list at all. The tosser routes
            // the mail through it and writes nothing down, so there is no base
            // to open, nothing to read and nowhere to write — and a line in the
            // area list that answers every key with "passthrough" is one more
            // thing between the reader and the echoes they do carry.
            if (area && !area->isPassthrough()) areas.push_back(std::move(*area));
        }
    }
    return {};
}

/// One whole config — the file, its includes, and everything the parse left
/// behind. The areas go into `areas` and the rest is the state handed back, so
/// that the two things a caller may want out of a fidoconfig are read the same
/// way and read once each.
/// The state a parse starts from: the variables a config may use without
/// setting them, and nothing else said yet.
ParseState freshState() {
    ParseState state;
    state.variables = initialVariables();
    return state;
}

/// The name a robot answers to where the link named none. Both are in the list
/// of names husky's own robots accept — `robotNames` defaults to
/// `AreaFix AreaMgr hpt` and `FileFix FileMgr AllFix FileScan htick` — so they
/// are the ordinary spelling to write to a link that says nothing about it.
void nameUnnamedRobots(std::vector<TosserLink>& links) {
    for (TosserLink& link : links) {
        if (link.areafixName.empty()) link.areafixName = "AreaFix";
        if (link.filefixName.empty()) link.filefixName = "FileFix";
    }
}

tl::expected<ParseState, ErrorPtr> readConfig(const std::string& path,
                                              std::vector<AreaConfig>& areas,
                                              const PathMap& paths,
                                              const std::string& charset) {
    auto content = text::readFileIn(path, charset);
    if (!content) return tl::make_unexpected(std::move(content).error());
    ParseState state = freshState();
    auto read = parseInto(*content, areas, std::filesystem::path(path).parent_path(),
                          /*includeDepth=*/8, state, paths, charset);
    if (!read) return tl::make_unexpected(std::move(read).error());
    nameUnnamedRobots(state.system.links);
    return state;
}

}  // namespace

FidoconfigParser::FidoconfigParser(std::string path, PathMap paths, std::string charset)
    : path_(std::move(path)), paths_(std::move(paths)), charset_(std::move(charset)) {}

tl::expected<std::vector<AreaConfig>, ErrorPtr> FidoconfigParser::loadAreas() {
    missingIncludes_.clear();
    std::vector<AreaConfig> areas;
    auto state = readConfig(path_, areas, paths_, charset_);
    if (!state) return tl::make_unexpected(std::move(state).error());
    missingIncludes_ = std::move(state->missingIncludes);
    return areas;
}

tl::expected<TosserSystem, ErrorPtr> FidoconfigParser::loadSystem() {
    // The areas are read and dropped. Walking the file for these statements
    // alone would mean a second parser that has to know `include` and `set` and
    // everything else that decides what a line says — two readings of one format
    // to be kept in step with each other — and the whole of an HPT config is a
    // few hundred lines.
    //
    // `missingIncludes_` is left alone: it says what the last `loadAreas()` went
    // past, and this read is not that.
    std::vector<AreaConfig> ignored;
    auto state = readConfig(path_, ignored, paths_, charset_);
    if (!state) return tl::make_unexpected(std::move(state).error());
    return std::move(state->system);
}

std::vector<AreaConfig> FidoconfigParser::parseText(const std::string& content,
                                                    const PathMap& paths) {
    std::vector<AreaConfig> areas;
    ParseState state = freshState();
    // includeDepth 0, so the one thing parseInto can fail at — reading an
    // include — cannot happen and the answer is nothing to check.
    static_cast<void>(parseInto(content, areas, std::filesystem::current_path(),
                                /*includeDepth=*/0, state, paths, /*charset=*/""));
    return areas;
}

TosserSystem FidoconfigParser::parseSystemText(const std::string& content) {
    std::vector<AreaConfig> areas;
    ParseState state = freshState();
    static_cast<void>(parseInto(content, areas, std::filesystem::current_path(),
                                /*includeDepth=*/0, state, PathMap{}, /*charset=*/""));
    nameUnnamedRobots(state.system.links);
    return std::move(state.system);
}

}  // namespace amberedit::config
