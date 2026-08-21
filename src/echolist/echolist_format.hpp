#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace amberedit::echolist {

/// The compiled echolist — what AmberEdit writes at startup and reads back.
///
/// It answers one question, and its whole shape follows from that: **"what does
/// this echo tag mean?"** The records are sorted by the folded tag and the index
/// over them is one `u32` per record, so a lookup is a binary search over a
/// fixed-width array and the area list can ask it once per area without
/// building anything at open time. There is no search by description and no
/// listing in any other order: nothing shows an echolist, only the descriptions
/// it carries.
///
/// Everything is little-endian, as the nodelist and the message base formats
/// are, and for the same reason: the file is written once and read on whatever
/// machine picks it up. The text in it is UTF-8 — the charset an echolist was
/// written in is settled while it is compiled and never survives into here.
///
/// ```
///   0  char[8]  magic "AMBERECH"
///   8  u16      format version
///  10  u16      header size, in bytes
///  12  u32      area count
///  16  u32      index offset            count * 4: u32 record offset
///  20  u32      records offset          the records, in folded-tag order
///  24  u32      records size
///  28  u32      source count
///  32  u32      source table offset     count * (u16 length + bytes) * 3,
///                                       then u64 modified, u64 size
///  36  u64      built at, seconds since the epoch
///  44  u32      reserved, zero
///
///   record: u16 tag length, u16 description length, then the tag and the
///           description
/// ```
namespace format {

inline constexpr char kMagic[8] = {'A', 'M', 'B', 'E', 'R', 'E', 'C', 'H'};

/// The version of the layout above, written into every file and checked by the
/// reader. It goes up whenever a file written today would be misread by the
/// code that reads it — never for a change that only adds to the end of the
/// header, since `headerSize` already says how far the header a writer knew
/// about reached.
inline constexpr uint16_t kVersion = 1;

inline constexpr size_t kHeaderSize = 48;
inline constexpr size_t kIndexEntrySize = 4;
/// The fixed part of a record, before the two strings it ends with.
inline constexpr size_t kRecordFixedSize = 4;

}  // namespace format

/// An echo tag as the index holds it and every lookup over it is done: ASCII
/// case folded, and the blanks round it taken off.
///
/// ASCII and deliberately nothing else, for the reason `config::text::asciiLower`
/// gives: the locale may be a single-byte Cyrillic one, and folding by it would
/// make two differently spelled tags compare equal. No echo tag has ever been
/// anything but ASCII — FTS-0004 area lines have no room for anything else.
[[nodiscard]] std::string foldTag(std::string_view tag);

}  // namespace amberedit::echolist
