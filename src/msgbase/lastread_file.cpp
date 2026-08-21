#include "msgbase/lastread_file.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <vector>

namespace amberedit::msgbase::lastread_file {

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

bool readBytes(const std::string& path, uint64_t offset, void* out, size_t count) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) return false;

    bool ok = std::fseek(file, static_cast<long>(offset), SEEK_SET) == 0 &&
              std::fread(out, 1, count, file) == count;
    std::fclose(file);
    return ok;
}

bool writeBytes(const std::string& path, uint64_t offset, const void* data,
                size_t count) {
    // "r+b" so that an existing file keeps the records of every other user;
    // only when there is none is the file created.
    std::FILE* file = std::fopen(path.c_str(), "r+b");
    if (file == nullptr) file = std::fopen(path.c_str(), "w+b");
    if (file == nullptr) return false;

    // Seeking past the end and writing leaves a hole that reads back as zeros,
    // which is exactly what "these users have read nothing" looks like — this
    // is how the format grows to reach a user number it has never seen.
    bool ok = std::fseek(file, static_cast<long>(offset), SEEK_SET) == 0 &&
              std::fwrite(data, 1, count, file) == count;
    ok = (std::fclose(file) == 0) && ok;
    return ok;
}

uint64_t fileSize(const std::string& path) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) return 0;
    return static_cast<uint64_t>(size);
}

uint32_t readU32(const unsigned char* bytes) {
    return static_cast<uint32_t>(bytes[0]) | static_cast<uint32_t>(bytes[1]) << 8 |
           static_cast<uint32_t>(bytes[2]) << 16 | static_cast<uint32_t>(bytes[3]) << 24;
}

uint16_t readU16(const unsigned char* bytes) {
    return static_cast<uint16_t>(static_cast<uint16_t>(bytes[0]) |
                                 static_cast<uint16_t>(bytes[1]) << 8);
}

void writeU32(unsigned char* bytes, uint32_t value) {
    bytes[0] = static_cast<unsigned char>(value & 0xffu);
    bytes[1] = static_cast<unsigned char>((value >> 8) & 0xffu);
    bytes[2] = static_cast<unsigned char>((value >> 16) & 0xffu);
    bytes[3] = static_cast<unsigned char>((value >> 24) & 0xffu);
}

void writeU16(unsigned char* bytes, uint16_t value) {
    bytes[0] = static_cast<unsigned char>(value & 0xffu);
    bytes[1] = static_cast<unsigned char>((value >> 8) & 0xffu);
}

uint32_t nameCrc32(const std::string& name) {
    uint32_t crc = 0xffffffffu;
    for (const char ch : name) {
        auto byte = static_cast<unsigned char>(ch);
        if (byte >= 'A' && byte <= 'Z') byte = static_cast<unsigned char>(byte - 'A' + 'a');
        crc = (crc >> 8) ^ crcTable()[(crc ^ byte) & 0xffu];
    }
    return crc;
}

}  // namespace amberedit::msgbase::lastread_file
