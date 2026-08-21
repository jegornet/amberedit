#pragma once

#include <cstdint>
#include <string>

/// Reading and writing the fixed-width little-endian records the three
/// lastread formats are made of.
///
/// Every one of them is an array of numbers on disk — Squish's .sql and the
/// Fido *.msg `lastread` file indexed by user number, JAM's .jlr searched for
/// the user's record — so the whole of the I/O comes down to "read a record at
/// an offset" and "write one there, growing the file if it is short".
///
/// The numbers are read and written byte by byte rather than by copying a
/// struct over: the formats fix their byte order, and a big-endian machine
/// must not write a base that a little-endian one cannot read back. The Fido
/// file is the one place where this is a deliberate departure — GoldED writes
/// it in the machine's own order — but every platform that runs this reader is
/// little-endian, so the two agree in practice and the file stays portable.
namespace amberedit::msgbase::lastread_file {

/// Reads `count` bytes at `offset`. Returns false when the file is missing,
/// unreadable, or too short to hold the record — all of which mean "this user
/// has no mark here" rather than an error worth reporting.
[[nodiscard]] bool readBytes(const std::string& path, uint64_t offset, void* out,
                             size_t count);

/// Writes `count` bytes at `offset`, creating the file if it does not exist
/// and zero-filling the gap if the offset is past its end. False means the
/// mark could not be stored — a read-only base, most likely.
[[nodiscard]] bool writeBytes(const std::string& path, uint64_t offset, const void* data,
                              size_t count);

/// Size of the file in bytes, or 0 if it cannot be stat'ed.
[[nodiscard]] uint64_t fileSize(const std::string& path);

/// The little-endian encodings the records are built from.
[[nodiscard]] uint32_t readU32(const unsigned char* bytes);
[[nodiscard]] uint16_t readU16(const unsigned char* bytes);
void writeU32(unsigned char* bytes, uint32_t value);
void writeU16(unsigned char* bytes, uint16_t value);

/// CRC-32 of a name, as JAM keys its lastread records: the reflected
/// polynomial edb88320H seeded with ffffffffH and *not* inverted at the end,
/// over the name in lower case. Only A-Z are lowered — the JAM specification
/// requires exactly that and no more, so a Cyrillic name hashes as written and
/// every implementation agrees on the result.
[[nodiscard]] uint32_t nameCrc32(const std::string& name);

}  // namespace amberedit::msgbase::lastread_file
