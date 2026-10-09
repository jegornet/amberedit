#include "nodelist/nodelist_format.hpp"

#include <cstdlib>

#include "config/text_util.hpp"

namespace amberedit::nodelist {
namespace {

/// One number of an address as it is typed, and how far the text got. Nullopt
/// when what stands there is not a number at all.
struct Number {
    uint16_t value{0};
    size_t end{0};
};

std::optional<Number> readNumber(std::string_view text, size_t from) {
    uint32_t value = 0;
    size_t i = from;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
        value = (value * 10) + static_cast<uint32_t>(text[i] - '0');
        if (value > 65535) return std::nullopt;
        ++i;
    }
    if (i == from) return std::nullopt;
    return Number{static_cast<uint16_t>(value), i};
}

}  // namespace

std::string foldName(std::string_view name) {
    std::string out;
    out.reserve(name.size());
    bool pendingSpace = false;
    for (char raw : name) {
        const char c = raw == '_' ? ' ' : config::text::asciiLower(raw);
        if (c == ' ' || c == '\t') {
            // Held back rather than written: a name ending in blank must not
            // leave one at the end of the folded key, where it would stop a
            // search for the name itself from matching.
            pendingSpace = !out.empty();
            continue;
        }
        if (pendingSpace) {
            out += ' ';
            pendingSpace = false;
        }
        out += c;
    }
    return out;
}

std::optional<AddressPrefix> AddressPrefix::parse(std::string_view text,
                                                  std::optional<HomeNet> home) {
    const std::string_view trimmed = config::text::trim(text);
    if (trimmed.empty()) return std::nullopt;

    if (trimmed.front() == '/') {
        // A node of the home net: the same text with the zone and the net
        // written in front of it, and read as that.
        if (!home) return std::nullopt;
        const std::string whole = std::to_string(home->zone) + ':' +
                                  std::to_string(home->net) + std::string(trimmed);
        return parse(whole);
    }

    AddressPrefix prefix;
    const auto first = readNumber(trimmed, 0);
    if (!first) return std::nullopt;
    size_t at = first->end;
    // A number may go on where the text ends on it, unless a zero opens it:
    // nothing is written `07`, so `07` is 7 and not the beginning of 70.
    const auto mayGoOn = [&trimmed](const Number& number, size_t start) {
        return number.end == trimmed.size() && trimmed[start] != '0';
    };
    prefix.open = mayGoOn(*first, 0);
    int level = 0;
    if (at < trimmed.size() && trimmed[at] == '/') {
        // A first number followed by `/` is a net: the zone was left off.
        prefix.anyZone = true;
        prefix.net = first->value;
        prefix.depth = 2;
        level = 1;
    } else {
        prefix.zone = first->value;
        prefix.depth = 1;
    }

    // Each separator moves one field along, and text that runs out on a
    // separator is the prefix that stands before it: `2:` is `2` and `2:382/`
    // is `2:382`, which is what a search field holds halfway through being
    // typed. Anything else after the numbers is not an address.
    const char* separators = ":/.";
    for (; level < 3; ++level) {
        if (at >= trimmed.size()) return prefix;
        if (trimmed[at] != separators[level]) return std::nullopt;
        ++at;
        if (at >= trimmed.size()) return prefix;

        const auto number = readNumber(trimmed, at);
        if (!number) return std::nullopt;
        prefix.open = mayGoOn(*number, at);
        at = number->end;
        ++prefix.depth;
        if (level == 0) {
            prefix.net = number->value;
        } else if (level == 1) {
            prefix.node = number->value;
        } else {
            prefix.point = number->value;
        }
    }
    return at == trimmed.size() ? std::optional<AddressPrefix>(prefix) : std::nullopt;
}

uint64_t AddressPrefix::lowKey() const {
    return format::addressKey(zone, depth > 1 ? net : 0, depth > 2 ? node : 0,
                              depth > 3 ? point : 0);
}

uint64_t AddressPrefix::highKey() const {
    constexpr uint16_t kTop = 65535;
    return format::addressKey(zone, depth > 1 ? net : kTop, depth > 2 ? node : kTop,
                              depth > 3 ? point : kTop);
}

}  // namespace amberedit::nodelist
