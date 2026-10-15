#include "encoding/base64.hpp"

#include <cstdint>

namespace amberedit::encoding {
namespace {

constexpr std::string_view kAlphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/// The six bits a character stands for, or -1 for one outside the alphabet.
int valueOf(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

}  // namespace

std::string toBase64(std::string_view bytes) {
    std::string out;
    out.reserve(((bytes.size() + 2) / 3) * 4);
    size_t i = 0;
    for (; i + 3 <= bytes.size(); i += 3) {
        const uint32_t group =
            (static_cast<uint32_t>(static_cast<unsigned char>(bytes[i])) << 16) |
            (static_cast<uint32_t>(static_cast<unsigned char>(bytes[i + 1])) << 8) |
            static_cast<uint32_t>(static_cast<unsigned char>(bytes[i + 2]));
        out += kAlphabet[(group >> 18) & 0x3F];
        out += kAlphabet[(group >> 12) & 0x3F];
        out += kAlphabet[(group >> 6) & 0x3F];
        out += kAlphabet[group & 0x3F];
    }
    const size_t left = bytes.size() - i;
    if (left == 0) return out;

    uint32_t group = static_cast<uint32_t>(static_cast<unsigned char>(bytes[i])) << 16;
    if (left == 2)
        group |= static_cast<uint32_t>(static_cast<unsigned char>(bytes[i + 1])) << 8;
    out += kAlphabet[(group >> 18) & 0x3F];
    out += kAlphabet[(group >> 12) & 0x3F];
    out += left == 2 ? kAlphabet[(group >> 6) & 0x3F] : '=';
    out += '=';
    return out;
}

std::optional<std::string> fromBase64(std::string_view text) {
    // The padding at the end, and only there: what is left must be a run of
    // the alphabet whose length leaves a whole number of bytes.
    size_t length = text.size();
    size_t padding = 0;
    while (length > 0 && text[length - 1] == '=' && padding < 2) {
        --length;
        ++padding;
    }
    if (padding > 0 && text.size() % 4 != 0) return std::nullopt;
    if (length % 4 == 1) return std::nullopt;

    std::string out;
    out.reserve(((length / 4) * 3) + 2);
    uint32_t bits = 0;
    int count = 0;
    for (size_t i = 0; i < length; ++i) {
        const int value = valueOf(text[i]);
        if (value < 0) return std::nullopt;
        bits = (bits << 6) | static_cast<uint32_t>(value);
        count += 6;
        if (count >= 8) {
            count -= 8;
            out += static_cast<char>((bits >> count) & 0xFF);
        }
    }
    return out;
}

}  // namespace amberedit::encoding
