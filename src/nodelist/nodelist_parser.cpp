#include "nodelist/nodelist_parser.hpp"

#include <optional>
#include <utility>

#include "config/text_util.hpp"

namespace amberedit::nodelist {
namespace {

/// The end-of-file mark DOS wrote after the last line, and that a great many
/// nodelists still carry. Everything from it on is not the nodelist.
constexpr char kDosEof = '\x1a';

/// Splits a nodelist line on commas into at most `limit` fields, the last of
/// which keeps every remaining comma. A nodelist line is exactly seven fields
/// and then the flags, and the flags carry commas of their own.
std::vector<std::string> splitFields(std::string_view line, size_t limit) {
    std::vector<std::string> fields;
    size_t start = 0;
    while (fields.size() + 1 < limit) {
        const size_t comma = line.find(',', start);
        if (comma == std::string_view::npos) break;
        fields.emplace_back(line.substr(start, comma - start));
        start = comma + 1;
    }
    fields.emplace_back(line.substr(start));
    return fields;
}

/// The spaces a nodelist writes as underscores, back the way they are meant.
std::string spaced(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        if (c == '_') c = ' ';
    }
    return out;
}

/// A field that has to be a whole number within `max`. Nullopt for anything
/// else, the empty field included.
std::optional<uint32_t> readNumber(std::string_view text, uint32_t max) {
    if (text.empty()) return std::nullopt;
    uint32_t value = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return std::nullopt;
        value = (value * 10) + static_cast<uint32_t>(c - '0');
        if (value > max) return std::nullopt;
    }
    return value;
}

/// Where the lines being read are addressed from: what the last `Zone`,
/// `Region`, `Host` or `Hub` line put them in, and — in a pointlist — the boss
/// the last `Boss` line named.
struct Context {
    uint16_t zone{0};
    uint16_t net{0};
    uint16_t node{0};
    domain::FtnAddress boss;
    bool pointMode{false};
};

/// One line that is neither blank nor a comment, onto the entries read so far
/// and the place the lines above it put them.
void readLine(std::string_view line, int lineNumber, Context& context,
              ParseResult& result) {
    std::vector<std::string> fields = splitFields(line, 8);
    const std::string& keywordField = fields.front();

    // `Boss` is not an entry: the node it names is in the nodelist already, and
    // the line is there to say what the points under it hang off.
    if (config::text::iequals(keywordField, "Boss")) {
        const auto boss =
            fields.size() > 1 ? domain::FtnAddress::parse(fields[1]) : std::nullopt;
        if (!boss || !boss->isValid()) {
            result.warnings.push_back(
                {lineNumber, "Boss does not name an address: '" +
                                 std::string(line.substr(0, 40)) +
                                 "' — the points under it are skipped"});
            context.pointMode = false;
            return;
        }
        context.boss = *boss;
        context.pointMode = true;
        result.pointList = true;
        return;
    }

    const auto keyword = parseKeyword(keywordField);
    if (!keyword) {
        result.warnings.push_back(
            {lineNumber, "'" + keywordField + "' is not a nodelist keyword"});
        return;
    }

    const auto number = fields.size() > 1 ? readNumber(fields[1], 65535) : std::nullopt;
    if (!number) {
        result.warnings.push_back({lineNumber, "the second field of '" +
                                                   std::string(line.substr(0, 40)) +
                                                   "' is not a node number"});
        return;
    }
    const auto value = static_cast<uint16_t>(*number);

    NodeEntry entry;
    entry.keyword = *keyword;

    switch (*keyword) {
        case NodeKeyword::Zone:
            // A zone's coordinator is zone:zone/0, and the lines under it are
            // in that net until a Region or a Host says otherwise.
            context.zone = value;
            context.net = value;
            context.node = 0;
            context.pointMode = false;
            entry.address = {context.zone, context.net, 0, 0, {}};
            break;
        case NodeKeyword::Region:
        case NodeKeyword::Host:
            context.net = value;
            context.node = 0;
            context.pointMode = false;
            entry.address = {context.zone, context.net, 0, 0, {}};
            break;
        case NodeKeyword::Hub:
            // A hub is a node of the net it stands in, and the node that the
            // `Point` lines under it — if any — belong to.
            context.node = value;
            context.pointMode = false;
            entry.address = {context.zone, context.net, value, 0, {}};
            break;
        case NodeKeyword::Point:
            entry.address = {context.zone, context.net, context.node, value, {}};
            break;
        case NodeKeyword::Node:
        case NodeKeyword::Pvt:
        case NodeKeyword::Hold:
        case NodeKeyword::Down:
            if (context.pointMode) {
                entry.address = {
                    context.boss.zone, context.boss.net, context.boss.node, value, {}};
            } else {
                context.node = value;
                entry.address = {context.zone, context.net, value, 0, {}};
            }
            break;
    }

    // No zone or no net means nothing above the line said where it is.
    // `isValid()` is not the question: a node number on its own satisfies that,
    // and is not enough to address anything.
    if (entry.address.zone == 0 || entry.address.net == 0) {
        result.warnings.push_back(
            {lineNumber,
             "no Zone, Region, Host or Boss line above it says where this entry is "
             "— it is skipped"});
        return;
    }

    if (fields.size() > 2) entry.system = spaced(fields[2]);
    if (fields.size() > 3) entry.location = spaced(fields[3]);
    if (fields.size() > 4) entry.sysop = spaced(fields[4]);
    if (fields.size() > 5) entry.phone = fields[5];
    if (fields.size() > 6) {
        // A baud rate that is not a number is the line's business and not ours
        // to refuse the node over: the field is kept at zero and the node stays
        // in the list, where its address and its sysop are what anybody was
        // looking for.
        if (const auto speed = readNumber(fields[6], 0xffffffffu)) {
            entry.speed = *speed;
        } else if (!fields[6].empty()) {
            result.warnings.push_back(
                {lineNumber, "'" + fields[6] + "' is not a baud rate"});
        }
    }
    if (fields.size() > 7) entry.flags = fields[7];

    if (entry.address.point != 0) ++result.pointCount;
    result.entries.push_back(std::move(entry));
}

}  // namespace

ParseResult parseNodelist(std::string_view text) {
    ParseResult result;
    Context context;

    const std::vector<std::string> lines = config::text::splitLines(text);
    for (size_t i = 0; i < lines.size(); ++i) {
        std::string_view raw = lines[i];
        const size_t end = raw.find(kDosEof);
        const bool ended = end != std::string_view::npos;
        if (ended) raw = raw.substr(0, end);

        const std::string_view line = config::text::trim(raw);
        // A comment, and the bare ';' that separates one block from the next.
        // Neither says anything about where the lines under it are.
        if (!line.empty() && line.front() != ';') {
            readLine(line, static_cast<int>(i) + 1, context, result);
        }
        if (ended) break;
    }

    return result;
}

}  // namespace amberedit::nodelist
