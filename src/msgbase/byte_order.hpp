#pragma once

#include <cstdint>

/// The little-endian numbers every message base format is built from.
///
/// Squish, JAM and the Fido *.msg header all fix their byte order on disk, so
/// the numbers are assembled byte by byte rather than by copying a struct over
/// — a big-endian machine must not write a base that a little-endian one
/// cannot read back, and no format has an alignment rule that a struct would
/// reproduce anyway.
namespace amberedit::msgbase::bytes {

[[nodiscard]] inline uint16_t readU16(const unsigned char* bytes) {
    return static_cast<uint16_t>(static_cast<uint16_t>(bytes[0]) |
                                 static_cast<uint16_t>(bytes[1]) << 8);
}

[[nodiscard]] inline uint32_t readU32(const unsigned char* bytes) {
    return static_cast<uint32_t>(bytes[0]) | static_cast<uint32_t>(bytes[1]) << 8 |
           static_cast<uint32_t>(bytes[2]) << 16 | static_cast<uint32_t>(bytes[3]) << 24;
}

/// A 16-bit field that carries a sign — the Squish time-zone offset, and the
/// node numbers of the special addresses written 2:2/-1.
[[nodiscard]] inline int16_t readI16(const unsigned char* bytes) {
    return static_cast<int16_t>(readU16(bytes));
}

inline void writeU16(unsigned char* bytes, uint16_t value) {
    bytes[0] = static_cast<unsigned char>(value & 0xffu);
    bytes[1] = static_cast<unsigned char>((value >> 8) & 0xffu);
}

inline void writeU32(unsigned char* bytes, uint32_t value) {
    bytes[0] = static_cast<unsigned char>(value & 0xffu);
    bytes[1] = static_cast<unsigned char>((value >> 8) & 0xffu);
    bytes[2] = static_cast<unsigned char>((value >> 16) & 0xffu);
    bytes[3] = static_cast<unsigned char>((value >> 24) & 0xffu);
}

}  // namespace amberedit::msgbase::bytes
