#include "app/copy_commands.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "app/quoting.hpp"
#include "config/text_util.hpp"
#include "domain/message.hpp"

namespace amberedit::app {
namespace {

/// The three words a command line may begin with, and what each of them asks
/// for. `XP:` is `XC:` under another name: GoldED takes both, and a message
/// written under one habit has to work under the other.
struct CommandPrefix {
    std::string_view word;
    CopyKind kind;
};
constexpr CommandPrefix kPrefixes[] = {
    {"CC:", CopyKind::Carbon},
    {"XC:", CopyKind::Crosspost},
    {"XP:", CopyKind::Crosspost},
};

/// Which command the line begins with, or nothing where it begins with none.
/// The word has to stand at the very start of the line: a `CC:` further along
/// is text somebody wrote.
std::optional<CommandPrefix> prefixOf(std::string_view line) {
    for (const CommandPrefix& prefix : kPrefixes) {
        if (line.size() < prefix.word.size()) continue;
        if (config::text::iequals(line.substr(0, prefix.word.size()), prefix.word)) {
            return prefix;
        }
    }
    return std::nullopt;
}

/// Whether the line is one a command could stand on at all: the control lines,
/// the pair closing the message and anything carrying a quote prefix are none
/// of them. What was quoted is what somebody else wrote, and carrying out their
/// `CC:` would be sending copies they asked for and this writer did not.
bool scannable(const std::string& line) {
    if (!line.empty() && line.front() == '\x01') return false;
    if (domain::isTearline(line) || domain::isOriginLine(line)) return false;
    return parseQuotePrefix(line).level == 0;
}

/// Characters that separate one thing on a command line from the next.
///
/// A comma and nothing else for `CC:`, because a recipient's name holds spaces
/// and does not hold commas; a comma, a space or a tab for `XC:`, whose masks
/// hold none of the three.
std::string_view separatorsFor(CopyKind kind) {
    return kind == CopyKind::Carbon ? "," : ", \t";
}

/// The same inside a `@file`, where a line of the file separates two entries as
/// well.
std::string separatorsInFile(CopyKind kind) {
    return std::string(separatorsFor(kind)) + "\r\n";
}

std::vector<std::string> splitOn(std::string_view text, std::string_view separators) {
    std::vector<std::string> parts;
    std::string current;
    for (const char c : text) {
        if (separators.find(c) != std::string_view::npos) {
            parts.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    parts.push_back(current);
    return parts;
}

/// The domain a 5D address carries, taken off: `2:5020/1234@fidonet` names the
/// same node as `2:5020/1234`, and nothing in a message base holds a domain to
/// match it against.
///
/// Only where the text is an address at all, which is what the colon says. A
/// name is left alone, `@` or no `@`, since a `@file` is read before this is
/// ever reached and a name is not an address to trim.
std::string withoutDomain(const std::string& text) {
    if (text.find(':') == std::string::npos) return text;
    const size_t at = text.find('@');
    return at == std::string::npos ? text : text.substr(0, at);
}

/// One entry of a command line or of the file it names, as a token.
std::optional<CopyToken> makeToken(std::string_view raw, CopyKind kind) {
    std::string text(config::text::trim(raw));
    if (text.empty()) return std::nullopt;

    CopyToken token;
    if (text.front() == '#') {
        token.hidden = true;
        text = std::string(config::text::trim(std::string_view(text).substr(1)));
        if (text.empty()) return std::nullopt;
    }
    token.text = kind == CopyKind::Carbon ? withoutDomain(text) : text;
    return token;
}

/// The entries a `@file` holds, added to the line's own.
///
/// A `@` inside the file is passed over rather than followed: a list that could
/// name another list is a list that could name itself, and the whole of what
/// this is for is keeping the names one writes to in one place.
void readTokenFile(const std::string& name, const std::string& fileDir, CopyKind kind,
                   CopyCommand& command) {
    std::filesystem::path path(name);
    if (path.is_relative() && !fileDir.empty())
        path = std::filesystem::path(fileDir) / path;

    const auto bytes = config::text::readFile(path.string());
    if (!bytes) {
        command.error = bytes.error();
        return;
    }

    for (const auto& raw : splitOn(*bytes, separatorsInFile(kind))) {
        const std::string_view trimmed = config::text::trim(raw);
        if (trimmed.empty() || trimmed.front() == '@') continue;
        if (const auto token = makeToken(trimmed, kind)) command.tokens.push_back(*token);
    }
}

/// A number read off the front of the text, and where it ended.
struct Number {
    uint16_t value{0};
    size_t end{0};
};

std::optional<Number> readNumber(std::string_view text, size_t at) {
    const size_t start = at;
    unsigned value = 0;
    while (at < text.size() && std::isdigit(static_cast<unsigned char>(text[at])) != 0) {
        value = (value * 10) + static_cast<unsigned>(text[at] - '0');
        if (value > 65535) return std::nullopt;
        ++at;
    }
    if (at == start) return std::nullopt;
    return Number{static_cast<uint16_t>(value), at};
}

/// An address as it was written, which need not be the whole of one: which of
/// the four parts it stated, and what it stated them as.
struct PartialAddress {
    bool hasZone{false};
    bool hasNet{false};
    bool hasNode{false};
    bool hasPoint{false};
    uint16_t zone{0};
    uint16_t net{0};
    uint16_t node{0};
    uint16_t point{0};
};

/// Reads the shapes an address may be written in where the area it is written
/// in can finish it: the whole of one, the net and node, the node alone with a
/// `/` in front of it, and a point alone with a `.` in front of it.
///
/// A bare number is deliberately none of them: `1234` is as likely to be
/// somebody's name as a node, and a recipient guessed at is worse than one that
/// was not understood.
std::optional<PartialAddress> readPartial(std::string_view text) {
    text = config::text::trim(text);
    if (const size_t at = text.find('@'); at != std::string_view::npos) {
        text = text.substr(0, at);
    }
    if (text.empty()) return std::nullopt;

    PartialAddress out;
    size_t at = 0;

    if (text.front() == '.') {
        const auto point = readNumber(text, 1);
        if (!point || point->end != text.size()) return std::nullopt;
        out.hasPoint = true;
        out.point = point->value;
        return out;
    }

    if (text.front() == '/') {
        const auto node = readNumber(text, 1);
        if (!node) return std::nullopt;
        out.hasNode = true;
        out.node = node->value;
        at = node->end;
    } else {
        const auto first = readNumber(text, 0);
        if (!first) return std::nullopt;
        at = first->end;
        if (at < text.size() && text[at] == ':') {
            const auto net = readNumber(text, at + 1);
            if (!net) return std::nullopt;
            out.hasZone = true;
            out.zone = first->value;
            out.hasNet = true;
            out.net = net->value;
            at = net->end;
            if (at >= text.size() || text[at] != '/') return std::nullopt;
            const auto node = readNumber(text, at + 1);
            if (!node) return std::nullopt;
            out.hasNode = true;
            out.node = node->value;
            at = node->end;
        } else if (at < text.size() && text[at] == '/') {
            const auto node = readNumber(text, at + 1);
            if (!node) return std::nullopt;
            out.hasNet = true;
            out.net = first->value;
            out.hasNode = true;
            out.node = node->value;
            at = node->end;
        } else {
            return std::nullopt;  // a bare number is a word, not an address
        }
    }

    if (at < text.size() && text[at] == '.') {
        const auto point = readNumber(text, at + 1);
        if (!point) return std::nullopt;
        out.hasPoint = true;
        out.point = point->value;
        at = point->end;
    }
    return at == text.size() ? std::optional<PartialAddress>(out) : std::nullopt;
}

/// Characters rather than bytes, which is what a margin counts: a Cyrillic name
/// is two bytes here and one character in the message.
size_t charCount(std::string_view text) {
    size_t count = 0;
    for (const char c : text) {
        if ((static_cast<unsigned char>(c) & 0xC0u) != 0x80u) ++count;
    }
    return count;
}

/// The items after `lead`, separated by commas and wrapped at `margin`, the
/// lines under the first indented to stand under the first item.
std::vector<std::string> wrapList(const std::string& lead,
                                  const std::vector<std::string>& items, int margin) {
    if (items.empty()) return {};

    const std::string indent(charCount(lead), ' ');
    const auto width = static_cast<size_t>(std::max(1, margin));

    std::vector<std::string> out;
    out.reserve(items.size());
    std::string current = lead;
    for (size_t i = 0; i < items.size(); ++i) {
        std::string piece = items[i];
        if (i + 1 < items.size()) piece += ",";

        if (i != 0 && charCount(current) + 1 + charCount(piece) > width) {
            out.push_back(current);
            current = indent + piece;
            continue;
        }
        if (i != 0) current += ' ';
        current += piece;
    }
    out.push_back(current);
    return out;
}

constexpr const char* kCarbonLead = "* Carbon copied to ";
constexpr const char* kCrosspostLead = "* Crossposted in ";
constexpr const char* kOriginallyLead = "* Originally in ";

}  // namespace

bool isCopyCommand(std::string_view line) {
    return prefixOf(line).has_value();
}

std::vector<CopyCommand> findCopyCommands(const std::vector<std::string>& lines,
                                          const std::string& fileDir) {
    std::vector<CopyCommand> commands;
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        if (!scannable(line)) continue;
        const auto prefix = prefixOf(line);
        if (!prefix) continue;

        CopyCommand command;
        command.kind = prefix->kind;
        command.line = i;
        for (const auto& raw : splitOn(std::string_view(line).substr(prefix->word.size()),
                                       separatorsFor(prefix->kind))) {
            const std::string_view trimmed = config::text::trim(raw);
            if (trimmed.empty()) continue;
            if (trimmed.front() == '@') {
                readTokenFile(std::string(trimmed.substr(1)), fileDir, prefix->kind,
                              command);
                continue;
            }
            if (const auto token = makeToken(trimmed, prefix->kind)) {
                command.tokens.push_back(*token);
            }
        }
        commands.push_back(std::move(command));
    }
    return commands;
}

std::string disarmCopyCommand(const std::string& line) {
    return isCopyCommand(line) ? "!" + line : line;
}

void disarmCopyCommands(std::vector<std::string>& lines) {
    for (auto& line : lines) line = disarmCopyCommand(line);
}

bool looksLikeAddress(std::string_view text) {
    return readPartial(text).has_value();
}

WrittenRecipient readRecipient(std::string_view token) {
    const std::string_view trimmed = config::text::trim(token);
    if (trimmed.empty()) return {};
    if (looksLikeAddress(trimmed)) return {{}, std::string(trimmed)};

    const size_t space = trimmed.find_last_of(" \t");
    if (space == std::string_view::npos) return {std::string(trimmed), {}};

    const std::string_view tail = config::text::trim(trimmed.substr(space + 1));
    if (!looksLikeAddress(tail)) return {std::string(trimmed), {}};
    return {std::string(config::text::trim(trimmed.substr(0, space))), std::string(tail)};
}

std::optional<domain::FtnAddress> completeAddress(std::string_view text,
                                                  const domain::FtnAddress& area) {
    const auto written = readPartial(text);
    if (!written) return std::nullopt;
    // Whatever was left out is the area's to supply, and an area presented
    // under no address of its own cannot supply it.
    if (!written->hasZone && !area.isValid()) return std::nullopt;

    domain::FtnAddress address;
    address.zone = written->hasZone ? written->zone : area.zone;
    address.net = written->hasNet ? written->net : area.net;
    address.node = written->hasNode ? written->node : area.node;
    // A point is stated or it is nothing: `2:5020/1234` written under a point's
    // own AKA is that node and not this point of it.
    address.point = written->hasPoint ? written->point : 0;
    return address.isValid() ? std::optional<domain::FtnAddress>(address) : std::nullopt;
}

MaskResult addCrossposts(const CopyToken& mask,
                         const std::vector<domain::AreaConfig>& areas,
                         const domain::AreaConfig& current,
                         std::vector<Crosspost>& into) {
    MaskResult result;
    for (const auto& area : areas) {
        if (area.tag.empty()) continue;
        // A crosspost goes to an echo. Netmail is addressed to a node, and the
        // same message posted into it would be a message addressed to nobody.
        if (area.kind == domain::AreaKind::Netmail) continue;
        if (!config::text::globMatches(mask.text, area.tag)) continue;

        result.matched = true;
        if (config::text::iequals(area.tag, current.tag)) {
            result.current = true;
            continue;
        }
        const bool already =
            std::any_of(into.begin(), into.end(), [&area](const Crosspost& taken) {
                return config::text::iequals(taken.area.tag, area.tag);
            });
        if (already) continue;

        into.push_back(Crosspost{area, mask.hidden});
        ++result.added;
    }
    return result;
}

std::vector<std::string> carbonLines(const std::vector<CarbonCopy>& copies,
                                     config::CarbonList mode, int margin) {
    std::vector<std::string> out;
    out.reserve(copies.size());
    switch (mode) {
        // The command line stays where it was written, so there is nothing to
        // put in its place; the kludges are the message's own lines and not
        // text, and `remove` is the value that asks for nothing at all.
        case config::CarbonList::Keep:
        case config::CarbonList::Hidden:
        case config::CarbonList::Remove: return out;
        case config::CarbonList::Visible:
            for (const auto& copy : copies) {
                if (copy.hidden) continue;
                out.push_back(std::string(kCarbonLead) + copy.name + "  " +
                              copy.address.toString());
            }
            return out;
        case config::CarbonList::Names: break;
    }

    std::vector<std::string> names;
    names.reserve(copies.size());
    for (const auto& copy : copies) {
        if (!copy.hidden) names.push_back(copy.name);
    }
    return wrapList(kCarbonLead, names, margin);
}

std::vector<std::string> carbonKludges(const std::vector<CarbonCopy>& copies) {
    std::vector<std::string> out;
    out.reserve(copies.size());
    for (const auto& copy : copies) {
        if (copy.hidden) continue;
        out.push_back("CC: " + copy.name + " " + copy.address.toString());
    }
    return out;
}

std::vector<std::string> crosspostLines(const std::vector<Crosspost>& areas,
                                        const std::string& originally,
                                        config::CrosspostList mode, int margin) {
    std::vector<std::string> out;
    if (mode == config::CrosspostList::Raw || mode == config::CrosspostList::None) {
        return out;
    }
    if (!originally.empty()) out.push_back(std::string(kOriginallyLead) + originally);

    std::vector<std::string> tags;
    tags.reserve(areas.size());
    for (const auto& area : areas) {
        if (!area.hidden) tags.push_back(area.area.tag);
    }
    if (tags.empty()) return out;

    if (mode == config::CrosspostList::Yes) {
        out.reserve(out.size() + tags.size());
        for (const auto& tag : tags) out.push_back(std::string(kCrosspostLead) + tag);
        return out;
    }
    for (auto& line : wrapList(kCrosspostLead, tags, margin)) {
        out.push_back(std::move(line));
    }
    return out;
}

std::vector<std::string> rewriteCopyCommands(const std::vector<std::string>& lines,
                                             const std::vector<CopyCommand>& commands,
                                             const std::vector<size_t>& keep,
                                             const std::vector<std::string>& carbon,
                                             const std::vector<std::string>& crossposts) {
    const auto commandAt = [&commands](size_t line) -> const CopyCommand* {
        for (const auto& command : commands) {
            if (command.line == line) return &command;
        }
        return nullptr;
    };
    const auto kept = [&keep](size_t line) {
        return std::find(keep.begin(), keep.end(), line) != keep.end();
    };

    // A list whose command lines all stayed in the message has nowhere to
    // stand, and is left out: the line it belongs to is still there, saying
    // what was asked for.
    bool carbonPlaced = carbon.empty();
    bool crosspostPlaced = crossposts.empty();

    std::vector<std::string> out;
    out.reserve(lines.size() + carbon.size() + crossposts.size());
    for (size_t i = 0; i < lines.size(); ++i) {
        const CopyCommand* command = commandAt(i);
        if (command == nullptr || kept(i)) {
            out.push_back(lines[i]);
            continue;
        }
        if (command->kind == CopyKind::Carbon && !carbonPlaced) {
            out.insert(out.end(), carbon.begin(), carbon.end());
            carbonPlaced = true;
        }
        if (command->kind == CopyKind::Crosspost && !crosspostPlaced) {
            out.insert(out.end(), crossposts.begin(), crossposts.end());
            crosspostPlaced = true;
        }
    }
    return out;
}

std::vector<std::string> withoutTrailer(std::vector<std::string> lines) {
    while (!lines.empty() && config::text::trim(lines.back()).empty()) lines.pop_back();
    const size_t count = lines.size();
    if (count >= 2 && domain::isTearline(lines[count - 2]) &&
        domain::isOriginLine(lines[count - 1])) {
        lines.resize(count - 2);
    }
    return lines;
}

}  // namespace amberedit::app
