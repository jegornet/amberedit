#include "domain/ftn_address.hpp"

#include <cctype>
#include <charconv>
#include <cstdint>

namespace amberedit::domain {
namespace {

/// ASCII whitespace, rather than the locale's. An address is ASCII by
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

std::optional<FtnAddress> FtnAddress::parse(std::string_view text) {
    while (!text.empty() && asciiIsSpace(text.front())) text.remove_prefix(1);
    while (!text.empty() && asciiIsSpace(text.back())) text.remove_suffix(1);
    if (text.empty()) return std::nullopt;

    FtnAddress addr;
    size_t pos = 0;

    // zone:net/node is the mandatory part.
    if (!readNumber(text, pos, addr.zone)) return std::nullopt;
    if (pos >= text.size() || text[pos] != ':') return std::nullopt;
    ++pos;
    if (!readNumber(text, pos, addr.net)) return std::nullopt;
    if (pos >= text.size() || text[pos] != '/') return std::nullopt;
    ++pos;
    if (!readNumber(text, pos, addr.node)) return std::nullopt;

    if (pos < text.size() && text[pos] == '.') {
        ++pos;
        if (!readNumber(text, pos, addr.point)) return std::nullopt;
    }
    if (pos < text.size() && text[pos] == '@') {
        addr.domain = std::string(text.substr(pos + 1));
        pos = text.size();
    }
    if (pos != text.size()) return std::nullopt;
    return addr;
}

}  // namespace amberedit::domain
