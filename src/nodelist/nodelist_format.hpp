#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace amberedit::nodelist {

/// The compiled nodelist — what AmberEdit writes at startup and reads back.
///
/// The file answers two questions, and its whole shape follows from them.
///
/// **"What is at this address, or under this part of one?"** Every node is
/// keyed by one 64-bit number, zone:net/node.point packed in that order, so
/// that the numeric order of the keys is the order an address is read in. An
/// exact address is then one binary search, and *every* partial address is a
/// contiguous run: `2` is every key from 2:0/0 to 2:65535/65535.65535, `2:382`
/// is the run inside it, and so on down to the point. Nothing about a prefix
/// search is a special case — it is the same search with a shorter key.
///
/// **"Which nodes has this sysop?"** The sysop names are folded (ASCII case
/// and the underscores a nodelist writes spaces as) into one pool, and the name
/// index is a suffix array over it: every position of every name that starts a
/// word or continues one, sorted by the text that follows it. A search for any
/// part of a name — the whole of it, a surname, three letters out of the middle
/// of one — is again a binary search for a run, this time of every place that
/// text appears. Matching in the middle of a word is what a suffix array buys
/// over a sorted list of names, and it is what "partial match" is usually taken
/// to mean.
///
/// Both indexes are fixed-width arrays sorted on disk, so neither is built at
/// open time: the reader holds the file and searches it where it lies.
///
/// Everything is little-endian, as the message base formats are, and for the
/// same reason: the file is written once and read on whatever machine picks it
/// up.
///
/// ```
///   0  char[8]  magic "AMBERNDL"
///   8  u16      format version
///  10  u16      header size, in bytes
///  12  u32      node count
///  16  u32      records offset          the node records, in key order
///  20  u32      records size
///  24  u32      address index offset    node count * 12: u64 key, u32 record
///  28  u32      name index count
///  32  u32      name index offset       count * 8: u32 pool offset, u32 node
///  36  u32      name pool offset        NUL-terminated folded sysop names
///  40  u32      name pool size
///  44  u32      source count
///  48  u32      source table offset     count * (the line, the charset it
///                                     stated and the file it named, each a
///                                     u16 length and its bytes, then u64
///                                     modified and u64 size)
///  52  u64      built at, seconds since the epoch
///  60  u32      reserved, zero
///
///   record: u8 keyword, u8 source, u16 zone, u16 net, u16 node, u16 point,
///           u32 speed, u16 * 5 field lengths, then the five fields —
///           system, location, sysop, phone, flags
/// ```
namespace format {

inline constexpr char kMagic[8] = {'A', 'M', 'B', 'E', 'R', 'N', 'D', 'L'};

/// The version of the layout above, written into every file and checked by the
/// reader. It goes up whenever a file written today would be misread by the
/// code that reads it — never for a change that only adds to the end of the
/// header, since `headerSize` already says how far the header a writer knew
/// about reached.
inline constexpr uint16_t kVersion = 2;

inline constexpr size_t kHeaderSize = 64;
inline constexpr size_t kAddressEntrySize = 12;
inline constexpr size_t kNameEntrySize = 8;
/// The fixed part of a record, before the five fields it ends with.
inline constexpr size_t kRecordFixedSize = 24;

/// A source index is one byte, which is what caps the number of nodelists one
/// compiled file may be made of.
inline constexpr size_t kMaxSources = 255;

/// zone:net/node.point as one number, ordered the way an address is read.
[[nodiscard]] constexpr uint64_t addressKey(uint16_t zone, uint16_t net, uint16_t node,
                                            uint16_t point) {
    return static_cast<uint64_t>(zone) << 48 | static_cast<uint64_t>(net) << 32 |
           static_cast<uint64_t>(node) << 16 | static_cast<uint64_t>(point);
}

}  // namespace format

/// A sysop name as the name pool and every search over it hold it: ASCII case
/// folded, the underscores a nodelist writes for spaces turned back into spaces,
/// and runs of blank collapsed to one so that a name written with two of them
/// is found by a query written with one.
///
/// ASCII and deliberately nothing else, for the reason `config::text::asciiLower`
/// gives: the locale may be a single-byte Cyrillic one, and folding by it would
/// make two different names compare equal.
[[nodiscard]] std::string foldName(std::string_view name);

/// As much of an address as somebody typed: `2`, `2:382`, `2:382/736`,
/// `2:382/736.1`. What the address search takes, and the reason a partial
/// address needs no code of its own — it is a key range like any other.
struct AddressPrefix {
    uint16_t zone{0};
    uint16_t net{0};
    uint16_t node{0};
    uint16_t point{0};
    /// How many of the four were written: 1 for `2`, 4 for a full address with
    /// a point on it.
    int depth{0};

    /// Parses what was typed. A trailing separator is allowed and says nothing
    /// — `2:` is `2` — so that a search field can be read while it is still
    /// being typed. Nullopt when the text is not the beginning of an address at
    /// all.
    [[nodiscard]] static std::optional<AddressPrefix> parse(std::string_view text);

    /// The first and the last key the prefix covers, both included. The last is
    /// computed by filling the unwritten parts with their highest value rather
    /// than by adding one to the written part, which is what keeps `65535` from
    /// running off the end of the arithmetic.
    [[nodiscard]] uint64_t lowKey() const;
    [[nodiscard]] uint64_t highKey() const;
};

}  // namespace amberedit::nodelist
