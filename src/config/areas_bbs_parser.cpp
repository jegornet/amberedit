#include "config/areas_bbs_parser.hpp"

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

/// Strips the base-type prefix off the path field.
/// `$` is Squish, `!` is JAM, no prefix means Fido *.msg (FTS-0001).
std::pair<MsgBaseType, std::string> splitTypePrefix(const std::string& field) {
    if (field.empty()) return {MsgBaseType::Unknown, field};
    switch (field[0]) {
        case '$': return {MsgBaseType::Squish, field.substr(1)};
        case '!': return {MsgBaseType::Jam, field.substr(1)};
        default: return {MsgBaseType::Sdm, field};
    }
}

std::optional<AreaConfig> parseLine(const std::string& rawLine) {
    const std::string_view line = text::trim(rawLine);
    if (line.empty() || line.front() == ';') return std::nullopt;

    const auto tokens = text::tokenize(line);
    // A path field and an echo tag are the minimum; an area with no links is
    // perfectly legal.
    if (tokens.size() < 2) return std::nullopt;

    AreaConfig area;
    area.kind = AreaKind::Echo;

    // A "P" field (in any case) means passthrough: there is no base on disk.
    if (text::iequals(tokens[0], "P")) {
        area.type = MsgBaseType::Passthrough;
    } else {
        auto [type, path] = splitTypePrefix(tokens[0]);
        if (path.empty()) return std::nullopt;
        area.type = type;
        area.path = std::move(path);
    }

    area.tag = tokens[1];

    for (size_t i = 2; i < tokens.size(); ++i) {
        if (auto addr = FtnAddress::parse(tokens[i])) area.links.push_back(*addr);
    }
    return area;
}

}  // namespace

AreasBbsParser::AreasBbsParser(std::string path) : path_(std::move(path)) {}

Result<std::vector<AreaConfig>> AreasBbsParser::loadAreas() {
    auto content = text::readFile(path_);
    if (!content) return tl::make_unexpected(std::move(content).error());
    return parseText(*content);
}

std::vector<AreaConfig> AreasBbsParser::parseText(const std::string& content) {
    std::vector<AreaConfig> areas;
    for (const auto& line : text::splitLines(content)) {
        if (auto area = parseLine(line)) areas.push_back(std::move(*area));
    }
    return areas;
}

}  // namespace amberedit::config
