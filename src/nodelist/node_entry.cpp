#include "nodelist/node_entry.hpp"

#include <array>

#include "config/text_util.hpp"

namespace amberedit::nodelist {
namespace {

/// The keyword table, in the order of the enumeration so that the name of a
/// keyword is an index and not a search.
constexpr std::array<std::string_view, 9> kKeywordNames{
    "", "Zone", "Region", "Host", "Hub", "Pvt", "Hold", "Down", "Point"};

/// The spaces a nodelist writes as underscores, put back the way it wrote them.
std::string underscored(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        if (c == ' ') c = '_';
    }
    return out;
}

/// Walks the comma-separated flag field, handing each flag to `visit` until it
/// answers true.
template <typename Visitor>
bool eachFlag(std::string_view flags, Visitor visit) {
    size_t start = 0;
    while (start <= flags.size()) {
        const size_t comma = flags.find(',', start);
        const size_t end = comma == std::string_view::npos ? flags.size() : comma;
        if (end > start && visit(flags.substr(start, end - start))) return true;
        if (comma == std::string_view::npos) break;
        start = comma + 1;
    }
    return false;
}

}  // namespace

std::string_view keywordName(NodeKeyword keyword) {
    const auto index = static_cast<size_t>(keyword);
    return index < kKeywordNames.size() ? kKeywordNames[index] : std::string_view{};
}

std::optional<NodeKeyword> parseKeyword(std::string_view text) {
    for (size_t i = 0; i < kKeywordNames.size(); ++i) {
        if (config::text::iequals(text, kKeywordNames[i])) {
            return static_cast<NodeKeyword>(i);
        }
    }
    return std::nullopt;
}

std::string NodeEntry::toLine() const {
    // The number field is the last part of the address the line itself names:
    // the point for a point, and the node for everything else. A Zone, Region
    // or Host line names its net there, which is the same number as its node
    // field is zero — hence the net, and not the node, for those three.
    uint16_t number = address.node;
    if (address.point != 0) {
        number = address.point;
    } else if (keyword == NodeKeyword::Zone || keyword == NodeKeyword::Region ||
               keyword == NodeKeyword::Host) {
        number = address.net;
    }

    std::string line(keywordName(keyword));
    line += ',';
    line += std::to_string(number);
    line += ',' + underscored(system);
    line += ',' + underscored(location);
    line += ',' + underscored(sysop);
    line += ',' + phone;
    line += ',' + std::to_string(speed);
    if (!flags.empty()) line += ',' + flags;
    return line;
}

bool NodeEntry::hasFlag(std::string_view flag) const {
    return eachFlag(flags, [flag](std::string_view word) {
        const size_t colon = word.find(':');
        const std::string_view name =
            colon == std::string_view::npos ? word : word.substr(0, colon);
        return config::text::iequals(name, flag);
    });
}

std::optional<std::string> NodeEntry::flagValue(std::string_view flag) const {
    std::optional<std::string> found;
    eachFlag(flags, [flag, &found](std::string_view word) {
        const size_t colon = word.find(':');
        if (colon == std::string_view::npos) return false;
        if (!config::text::iequals(word.substr(0, colon), flag)) return false;
        found = std::string(word.substr(colon + 1));
        return true;
    });
    return found;
}

}  // namespace amberedit::nodelist
