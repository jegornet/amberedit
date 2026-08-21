#include "config/fidoconfig_parser.hpp"

#include <filesystem>
#include <optional>
#include <set>
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
        "-a",  "-b",  "-d",         "-g",           "-p",     "-lr",
        "-lw", "-$m", "-dupecheck", "-dupehistory", "-scan",  "-toonew",
        "-tooold",
    };
    return options;
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

/// Strips a '#' comment and trailing whitespace.
std::string stripComment(std::string_view line) {
    const size_t hash = line.find('#');
    if (hash != std::string_view::npos) line = line.substr(0, hash);
    return std::string(text::trim(line));
}

void parseInto(const std::string& content, std::vector<AreaConfig>& areas,
               const std::filesystem::path& baseDir, int includeDepth);

/// Parses a single area declaration line.
std::optional<AreaConfig> parseAreaLine(const std::vector<std::string>& tokens,
                                        AreaKind kind) {
    if (tokens.size() < 3) return std::nullopt;  // a tag and a path are the minimum

    AreaConfig area;
    area.kind = kind;
    area.tag = tokens[1];
    area.path = tokens[2];

    if (text::iequals(area.path, "passthrough")) {
        area.type = MsgBaseType::Passthrough;
        area.path.clear();
    }

    for (size_t i = 3; i < tokens.size(); ++i) {
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
                area.type = domain::parseMsgBaseType(value).value_or(
                    MsgBaseType::Unknown);
            } else if (option == "-g") {
                area.group = value;
            } else if (option == "-d") {
                area.description = value;
            }
            continue;
        }

        // A bare token after the path is a link if it looks like an FTN address.
        if (auto addr = FtnAddress::parse(token)) area.links.push_back(*addr);
    }

    if (area.path.empty() && area.type == MsgBaseType::Unknown)
        area.type = MsgBaseType::Passthrough;
    return area;
}

void parseInto(const std::string& content, std::vector<AreaConfig>& areas,
               const std::filesystem::path& baseDir, int includeDepth) {
    for (const auto& rawLine : text::splitLines(content)) {
        const std::string line = stripComment(rawLine);
        if (line.empty()) continue;

        const auto tokens = text::tokenize(line);
        if (tokens.empty()) continue;

        // include <file> — the path is relative to the including config.
        if (text::iequals(tokens[0], "include") && tokens.size() >= 2) {
            if (includeDepth <= 0) continue;  // guard against include cycles
            std::filesystem::path included(tokens[1]);
            if (included.is_relative()) included = baseDir / included;
            std::error_code ec;
            if (!std::filesystem::exists(included, ec)) continue;
            parseInto(text::readFile(included.string()), areas, included.parent_path(),
                      includeDepth - 1);
            continue;
        }

        if (auto kind = parseAreaKeyword(tokens[0])) {
            if (auto area = parseAreaLine(tokens, *kind))
                areas.push_back(std::move(*area));
        }
    }
}

}  // namespace

FidoconfigParser::FidoconfigParser(std::string path) : path_(std::move(path)) {}

std::vector<AreaConfig> FidoconfigParser::loadAreas() {
    const std::string content = text::readFile(path_);
    std::vector<AreaConfig> areas;
    parseInto(content, areas, std::filesystem::path(path_).parent_path(),
              /*includeDepth=*/8);
    return areas;
}

std::vector<AreaConfig> FidoconfigParser::parseText(const std::string& content) {
    std::vector<AreaConfig> areas;
    parseInto(content, areas, std::filesystem::current_path(), /*includeDepth=*/0);
    return areas;
}

}  // namespace amberedit::config
