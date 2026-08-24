#include "config/squish_cfg_parser.hpp"

#include <optional>
#include <stdexcept>
#include <utility>

#include "config/text_util.hpp"

namespace amberedit::config {

using domain::AreaConfig;
using domain::AreaKind;
using domain::FtnAddress;
using domain::MsgBaseType;

namespace {

/// Returns the area kind for a keyword, or nullopt if the line does not
/// declare an area at all.
std::optional<AreaKind> parseAreaKeyword(std::string_view keyword) {
    if (text::iequals(keyword, "echoarea")) return AreaKind::Echo;
    if (text::iequals(keyword, "netarea")) return AreaKind::Netmail;
    if (text::iequals(keyword, "localarea")) return AreaKind::Local;
    if (text::iequals(keyword, "badarea")) return AreaKind::Bad;
    if (text::iequals(keyword, "dupearea")) return AreaKind::Dupe;
    return std::nullopt;
}

/// Strips a ';' comment and the surrounding whitespace.
std::string stripComment(std::string_view line) {
    const size_t semicolon = line.find(';');
    if (semicolon != std::string_view::npos) line = line.substr(0, semicolon);
    return std::string(text::trim(line));
}

/// Parses a single area declaration line.
std::optional<AreaConfig> parseAreaLine(const std::vector<std::string>& tokens,
                                        AreaKind kind) {
    if (tokens.size() < 3) return std::nullopt;  // a tag and a path are the minimum

    AreaConfig area;
    area.kind = kind;
    area.tag = tokens[1];
    area.path = tokens[2];

    // Without -$ the base is Fido *.msg; the flag below upgrades it.
    area.type = MsgBaseType::Sdm;
    if (text::iequals(area.path, "passthrough")) {
        area.type = MsgBaseType::Passthrough;
        area.path.clear();
    }

    for (size_t i = 3; i < tokens.size(); ++i) {
        const std::string& token = tokens[i];

        if (!token.empty() && token[0] == '-') {
            // Values are attached to their option, so these are prefix tests
            // rather than lookups: -$gA is one token, not two.
            if (text::startsWith(token, "-$g")) {
                area.group = token.substr(3);
            } else if (token == "-$") {
                if (area.type != MsgBaseType::Passthrough)
                    area.type = MsgBaseType::Squish;
            } else if (text::startsWith(token, "-p")) {
                if (auto address = FtnAddress::parse(token.substr(2))) {
                    area.address = *address;
                }
            }
            // Everything else on the line configures the tosser, not the
            // reader: dupe history, packing, message limits and the like.
            continue;
        }

        // A bare token after the options is a link if it looks like an address.
        if (auto address = FtnAddress::parse(token)) area.links.push_back(*address);
    }
    return area;
}

}  // namespace

SquishCfgParser::SquishCfgParser(std::string path) : path_(std::move(path)) {}

Result<std::vector<AreaConfig>> SquishCfgParser::loadAreas() {
    auto content = text::readFile(path_);
    if (!content) return tl::make_unexpected(std::move(content).error());
    return parseText(*content);
}

std::vector<AreaConfig> SquishCfgParser::parseText(const std::string& content) {
    std::vector<AreaConfig> areas;
    for (const auto& rawLine : text::splitLines(content)) {
        const std::string line = stripComment(rawLine);
        if (line.empty()) continue;

        const auto tokens = text::tokenize(line);
        if (tokens.empty()) continue;

        if (auto kind = parseAreaKeyword(tokens[0])) {
            if (auto area = parseAreaLine(tokens, *kind))
                areas.push_back(std::move(*area));
        }
    }
    return areas;
}

}  // namespace amberedit::config
