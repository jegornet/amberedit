#include "domain/address_pattern.hpp"

#include <array>
#include <cctype>
#include <charconv>
#include <cstdint>

namespace amberedit::domain {
namespace {

/// ASCII whitespace, rather than the locale's. A pattern is ASCII by
/// definition, and the terminal layer may have installed a single-byte locale
/// under which isspace() would also answer for bytes that are half of a
/// character rather than a separator.
constexpr bool asciiIsSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

/// Reads an unsigned number starting at pos and advances it.
/// Returns false when there are no digits there.
bool readNumber(std::string_view s, size_t& pos, uint16_t& out) {
    size_t start = pos;
    while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) ++pos;
    if (pos == start) return false;
    unsigned value = 0;
    auto [ptr, ec] = std::from_chars(s.data() + start, s.data() + pos, value);
    if (ec != std::errc{}) return false;
    out = static_cast<uint16_t>(value);
    return true;
}

}  // namespace

bool AddressPattern::matches(const FtnAddress& addr) const {
    if (zone && *zone != addr.zone) return false;
    if (net && *net != addr.net) return false;
    if (node && *node != addr.node) return false;
    if (point && *point != addr.point) return false;
    return true;
}

int AddressPattern::depth() const {
    if (!zone) return 0;
    if (!net) return 1;
    if (!node) return 2;
    if (!point) return 3;
    return 4;
}

std::string AddressPattern::toString() const {
    const std::array<std::optional<uint16_t>, 4> fields{zone, net, node, point};
    constexpr std::array<char, 4> kSeparators{'\0', ':', '/', '.'};

    std::string s;
    for (size_t i = 0; i < fields.size(); ++i) {
        bool tailIsAny = true;
        for (size_t j = i; j < fields.size(); ++j) tailIsAny = tailIsAny && !fields[j];

        if (i != 0) {
            // A stated point of zero is what an address without a point means,
            // so it is written the same way: left off.
            if (!tailIsAny && i == 3 && *fields[i] == 0) break;
            s += kSeparators[i];
        }
        if (tailIsAny) {
            s += '*';
            break;
        }
        s += fields[i] ? std::to_string(*fields[i]) : "*";
    }
    return s;
}

std::optional<AddressPattern> AddressPattern::parse(std::string_view text) {
    while (!text.empty() && asciiIsSpace(text.front())) text.remove_prefix(1);
    while (!text.empty() && asciiIsSpace(text.back())) text.remove_suffix(1);
    if (text.empty()) return std::nullopt;
    // 4D and no more; the header says why a domain is refused rather than
    // quietly dropped.
    if (text.find('@') != std::string_view::npos) return std::nullopt;

    AddressPattern pattern;
    size_t pos = 0;
    bool tail = false;  // a '*' at the end stands for every component left

    const auto readField = [&](std::optional<uint16_t>& out) {
        if (pos < text.size() && text[pos] == '*') {
            ++pos;
            out = std::nullopt;
            tail = pos == text.size();
            return true;
        }
        uint16_t value = 0;
        if (!readNumber(text, pos, value)) return false;
        out = value;
        return true;
    };

    if (!readField(pattern.zone)) return std::nullopt;
    if (tail) return pattern;
    if (pos >= text.size() || text[pos] != ':') return std::nullopt;
    ++pos;

    if (!readField(pattern.net)) return std::nullopt;
    if (tail) return pattern;
    if (pos >= text.size() || text[pos] != '/') return std::nullopt;
    ++pos;

    if (!readField(pattern.node)) return std::nullopt;
    if (tail) return pattern;

    if (pos < text.size() && text[pos] == '.') {
        ++pos;
        if (!readField(pattern.point)) return std::nullopt;
        if (tail) return pattern;
    } else {
        pattern.point = 0;
    }

    if (pos != text.size()) return std::nullopt;
    return pattern;
}

}  // namespace amberedit::domain
