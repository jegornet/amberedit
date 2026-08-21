#include "msgbase/jam_crc32.hpp"

#include <array>

namespace amberedit::msgbase {

namespace {

/// The reflected CRC-32 table, generated once on first use rather than written
/// out: 256 lines of hexadecimal say nothing the polynomial does not.
const std::array<uint32_t, 256>& crcTable() {
    static const std::array<uint32_t, 256> table = [] {
        constexpr uint32_t kPolynomial = 0xedb88320u;
        std::array<uint32_t, 256> out{};
        for (uint32_t i = 0; i < out.size(); ++i) {
            uint32_t value = i;
            for (int bit = 0; bit < 8; ++bit) {
                value = (value & 1u) != 0 ? (value >> 1) ^ kPolynomial : value >> 1;
            }
            out[i] = value;
        }
        return out;
    }();
    return table;
}

}  // namespace

uint32_t jamCrc32(std::string_view text) {
    uint32_t crc = 0xffffffffu;
    for (const char ch : text) {
        auto byte = static_cast<unsigned char>(ch);
        if (byte >= 'A' && byte <= 'Z') {
            byte = static_cast<unsigned char>(byte - 'A' + 'a');
        }
        crc = (crc >> 8) ^ crcTable()[(crc ^ byte) & 0xffu];
    }
    return crc;
}

}  // namespace amberedit::msgbase
