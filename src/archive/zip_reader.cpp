#include "archive/zip_reader.hpp"
#include "config/text_util.hpp"

#include <zlib.h>

#include <cstring>
#include <fstream>
#include <iterator>

#include "msgbase/byte_order.hpp"

namespace amberedit::archive {
namespace {

using msgbase::bytes::readU16;
using msgbase::bytes::readU32;

constexpr uint32_t kEndOfDirectory = 0x06054b50;
constexpr uint32_t kDirectoryEntry = 0x02014b50;
constexpr uint32_t kLocalHeader = 0x04034b50;

constexpr size_t kEndOfDirectorySize = 22;
constexpr size_t kDirectoryEntrySize = 46;
constexpr size_t kLocalHeaderSize = 30;

/// The largest comment a zip may carry after its end record, and so the
/// furthest back the record itself may stand from the end of the file.
constexpr size_t kMaxComment = 0xffff;

constexpr uint16_t kMethodStore = 0;
constexpr uint16_t kMethodDeflate = 8;
/// Bit 0 of the general purpose flags: the entry is encrypted.
constexpr uint16_t kFlagEncrypted = 0x0001;

/// The DOS date and time a zip carries, as a time_t in local time — which is
/// what the format means by them, having no zone to say otherwise.
std::time_t dosTime(uint16_t date, uint16_t time) {
    std::tm parts{};
    parts.tm_mday = date & 0x1f;
    parts.tm_mon = ((date >> 5) & 0x0f) - 1;
    parts.tm_year = ((date >> 9) & 0x7f) + 80;
    parts.tm_hour = (time >> 11) & 0x1f;
    parts.tm_min = (time >> 5) & 0x3f;
    parts.tm_sec = (time & 0x1f) * 2;
    parts.tm_isdst = -1;
    const std::time_t stamp = std::mktime(&parts);
    return stamp == static_cast<std::time_t>(-1) ? 0 : stamp;
}

/// The complaint with the archive's name in front of it, ready to be returned:
/// `return fail(path, "is not a zip archive")`.
[[nodiscard]] tl::unexpected<ErrorPtr> fail(const std::string& path,
                                            const std::string& what) {
    return failure(path + ": " + what);
}

}  // namespace

std::string ZipEntry::baseName() const {
    const size_t cut = name.find_last_of("/\\");
    return cut == std::string::npos ? name : name.substr(cut + 1);
}

tl::expected<ZipArchive, ErrorPtr> ZipArchive::open(const std::string& path) {
    auto isFile = config::text::insistItIsAFile(path);
    if (!isFile) return tl::make_unexpected(std::move(isFile).error());

    std::ifstream in(path, std::ios::binary);
    if (!in) return failure("cannot read the archive: " + path);

    ZipArchive archive;
    archive.path_ = path;
    archive.data_.assign(std::istreambuf_iterator<char>(in),
                         std::istreambuf_iterator<char>());
    if (in.bad()) return failure("cannot read the archive: " + path);

    const auto* raw = archive.data_.data();
    const size_t size = archive.data_.size();
    if (size < kEndOfDirectorySize) return fail(path, "too short to be a zip archive");

    // The end record stands last but for the archive comment, so it is looked
    // for backwards from the end.
    size_t end = std::string::npos;
    const size_t lowest = size > kEndOfDirectorySize + kMaxComment
                              ? size - kEndOfDirectorySize - kMaxComment
                              : 0;
    for (size_t at = size - kEndOfDirectorySize;; --at) {
        if (readU32(raw + at) == kEndOfDirectory) {
            end = at;
            break;
        }
        if (at == lowest) break;
    }
    if (end == std::string::npos) return fail(path, "is not a zip archive");

    const uint16_t count = readU16(raw + end + 10);
    const uint32_t directorySize = readU32(raw + end + 12);
    const uint32_t directoryOffset = readU32(raw + end + 16);
    if (count == 0xffff || directoryOffset == 0xffffffffu ||
        directorySize == 0xffffffffu) {
        return fail(
            path,
            "is a zip64 archive, which AmberEdit does not read — a nodelist or an "
            "echolist archive is never one");
    }
    if (readU16(raw + end + 4) != 0 || readU16(raw + end + 6) != 0) {
        return fail(path, "is one part of a multi-part zip archive");
    }
    if (static_cast<uint64_t>(directoryOffset) + directorySize > size) {
        return fail(path, "is a truncated zip archive");
    }

    size_t at = directoryOffset;
    archive.entries_.reserve(count);
    for (uint16_t i = 0; i < count; ++i) {
        if (at + kDirectoryEntrySize > size || readU32(raw + at) != kDirectoryEntry) {
            return fail(path, "is a damaged zip archive");
        }
        const uint16_t flags = readU16(raw + at + 8);
        const uint16_t nameLength = readU16(raw + at + 28);
        const uint16_t extraLength = readU16(raw + at + 30);
        const uint16_t commentLength = readU16(raw + at + 32);
        if (at + kDirectoryEntrySize + nameLength + extraLength + commentLength > size) {
            return fail(path, "is a truncated zip archive");
        }

        ZipEntry entry;
        entry.name.assign(reinterpret_cast<const char*>(raw + at + kDirectoryEntrySize),
                          nameLength);
        entry.method = readU16(raw + at + 10);
        entry.modified = dosTime(readU16(raw + at + 14), readU16(raw + at + 12));
        entry.crc32 = readU32(raw + at + 16);
        entry.compressedSize = readU32(raw + at + 20);
        entry.uncompressedSize = readU32(raw + at + 24);
        entry.localHeaderOffset = readU32(raw + at + 42);

        // A directory entry is a name ending in a separator and no content;
        // an encrypted one is content nobody here can read. Neither is a file
        // to read, and both are left out rather than failing the archive.
        const bool isDirectory = !entry.name.empty() &&
                                 (entry.name.back() == '/' || entry.name.back() == '\\');
        if (!isDirectory && (flags & kFlagEncrypted) == 0) {
            archive.entries_.push_back(std::move(entry));
        }
        at += kDirectoryEntrySize + nameLength + extraLength + commentLength;
    }

    return archive;
}

tl::expected<std::string, ErrorPtr> ZipArchive::read(const ZipEntry& entry) const {
    const auto* raw = data_.data();
    const size_t size = data_.size();

    if (entry.localHeaderOffset + kLocalHeaderSize > size ||
        readU32(raw + entry.localHeaderOffset) != kLocalHeader) {
        return fail(path_,
                    "has no data where its directory says '" + entry.name + "' is");
    }
    // The local header's own name and extra fields are the only thing read from
    // it: its sizes may be zero where a data descriptor carries them instead,
    // and the central directory's are the ones that are always filled in.
    const uint16_t nameLength = readU16(raw + entry.localHeaderOffset + 26);
    const uint16_t extraLength = readU16(raw + entry.localHeaderOffset + 28);
    const uint64_t start = static_cast<uint64_t>(entry.localHeaderOffset) +
                           kLocalHeaderSize + nameLength + extraLength;
    if (start + entry.compressedSize > size) {
        return fail(path_, "is truncated inside '" + entry.name + "'");
    }

    std::string out;
    if (entry.method != kMethodStore && entry.method != kMethodDeflate) {
        return fail(path_, "packs '" + entry.name + "' with compression method " +
                               std::to_string(entry.method) +
                               ", which AmberEdit does not unpack");
    }

    if (entry.uncompressedSize == 0) {
        // Nothing to unpack, and nothing for zlib to be given a null buffer of.
    } else if (entry.method == kMethodStore) {
        if (entry.compressedSize != entry.uncompressedSize) {
            return fail(path_, "stores '" + entry.name + "' with two different sizes");
        }
        out.assign(reinterpret_cast<const char*>(raw + start), entry.compressedSize);
    } else {
        out.resize(entry.uncompressedSize);

        z_stream stream{};
        // A negative window size asks zlib for the raw deflate stream a zip
        // holds, without the zlib header a .gz or a .z would have in front of
        // it.
        if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
            return fail(path_, "cannot be unpacked: zlib would not start");
        }
        stream.next_in = const_cast<Bytef*>(raw + start);
        stream.avail_in = entry.compressedSize;
        stream.next_out = reinterpret_cast<Bytef*>(&out[0]);
        stream.avail_out = entry.uncompressedSize;
        const int status = inflate(&stream, Z_FINISH);
        const uLong written = stream.total_out;
        inflateEnd(&stream);
        if (status != Z_STREAM_END || written != entry.uncompressedSize) {
            return fail(path_, "is damaged inside '" + entry.name + "'");
        }
    }

    const uLong sum =
        ::crc32(::crc32(0, nullptr, 0), reinterpret_cast<const Bytef*>(out.data()),
                static_cast<uInt>(out.size()));
    if (static_cast<uint32_t>(sum) != entry.crc32) {
        return fail(path_,
                    "is damaged: '" + entry.name + "' does not match its checksum");
    }
    return out;
}

}  // namespace amberedit::archive
