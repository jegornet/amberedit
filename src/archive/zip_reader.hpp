#pragma once

#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

#include "support/error.hpp"

namespace amberedit::archive {

/// One file in a zip archive, as the central directory describes it.
struct ZipEntry {
    /// The name as the archive stores it, directories and all. What is done
    /// about those is the caller's: `baseName()` is the whole of what unpacking
    /// a nodelist or an echolist archive wants.
    std::string name;
    uint16_t method{0};
    uint32_t crc32{0};
    uint32_t compressedSize{0};
    uint32_t uncompressedSize{0};
    uint32_t localHeaderOffset{0};
    /// The stamp the archive carries for the file, which is local time in the
    /// zip format and is taken as local time here.
    std::time_t modified{0};

    /// The name with every directory in front of it taken off — the file as it
    /// is unpacked when it is unpacked without paths. Both separators are cut
    /// on, since an archive made on either system may be read on either.
    [[nodiscard]] std::string baseName() const;
};

/// A zip archive, opened for reading.
///
/// Enough of the format to unpack a nodelist or an echolist: stored and
/// deflated entries, no encryption, no zip64, no multi-part archives. Anything
/// else is refused by name rather than half-read — an FTN distribution has been
/// a plain single-part zip for thirty years, and a file that is not one is a
/// file this was not pointed at on purpose.
class ZipArchive {
public:
    /// The archive at `path`, or why it is not a readable zip — named, since
    /// the caller was pointed at it by a config line and that is where the
    /// answer is.
    [[nodiscard]] static tl::expected<ZipArchive, ErrorPtr> open(const std::string& path);

    [[nodiscard]] const std::vector<ZipEntry>& entries() const { return entries_; }
    [[nodiscard]] const std::string& path() const { return path_; }

    /// The contents of one entry, checked against the CRC the archive states,
    /// or why a damaged or unsupported entry could not be unpacked.
    [[nodiscard]] tl::expected<std::string, ErrorPtr> read(const ZipEntry& entry) const;

private:
    std::vector<unsigned char> data_;
    std::vector<ZipEntry> entries_;
    std::string path_;
};

}  // namespace amberedit::archive
