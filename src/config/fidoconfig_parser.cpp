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
                                       const PathMap& paths);

/// Reads the options an area line and an `echoareadefaults` line both take,
/// from `first` to the end of the line, into an area that already holds
/// whatever it inherited. An option that is given states what it states; one
/// that is absent leaves the inherited value alone, which is the whole of what
/// inheriting means here.
void applyAreaOptions(const std::vector<std::string>& tokens, size_t first,
                      AreaConfig& area) {
    for (size_t i = first; i < tokens.size(); ++i) {
        const std::string& token = tokens[i];

        if (!token.empty() && token[0] == '-') {
            const std::string option = text::toLower(token);

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
            area.type = MsgBaseType::Passthrough;
            area.path.clear();
            continue;
        }

        // A bare token after the path is a link if it looks like an FTN address.
        // The links the defaults named are already in the list, and these come
        // after them.
        if (auto addr = FtnAddress::parse(token)) area.links.push_back(*addr);
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
                                       const PathMap& paths) {
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
            if (!std::filesystem::exists(included, ec)) continue;
            auto text = text::readFile(included.string());
            if (!text) return tl::make_unexpected(std::move(text).error());
            auto read = parseInto(*text, areas, included.parent_path(), includeDepth - 1,
                                  state, paths);
            if (!read) return tl::make_unexpected(std::move(read).error());
            continue;
        }

        if (auto kind = parseAreaKeyword(tokens[0])) {
            if (auto area = parseAreaLine(tokens, *kind, state, paths))
                areas.push_back(std::move(*area));
        }
    }
    return {};
}

}  // namespace

FidoconfigParser::FidoconfigParser(std::string path, PathMap paths)
    : path_(std::move(path)), paths_(std::move(paths)) {}

tl::expected<std::vector<AreaConfig>, ErrorPtr> FidoconfigParser::loadAreas() {
    auto content = text::readFile(path_);
    if (!content) return tl::make_unexpected(std::move(content).error());
    std::vector<AreaConfig> areas;
    ParseState state{initialVariables(), AreaConfig{}};
    auto read = parseInto(*content, areas, std::filesystem::path(path_).parent_path(),
                          /*includeDepth=*/8, state, paths_);
    if (!read) return tl::make_unexpected(std::move(read).error());
    return areas;
}

std::vector<AreaConfig> FidoconfigParser::parseText(const std::string& content,
                                                    const PathMap& paths) {
    std::vector<AreaConfig> areas;
    ParseState state{initialVariables(), AreaConfig{}};
    // includeDepth 0, so the one thing parseInto can fail at — reading an
    // include — cannot happen and the answer is nothing to check.
    static_cast<void>(parseInto(content, areas, std::filesystem::current_path(),
                                /*includeDepth=*/0, state, paths));
    return areas;
}

}  // namespace amberedit::config
