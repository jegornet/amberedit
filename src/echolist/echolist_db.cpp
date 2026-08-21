#include "echolist/echolist_db.hpp"

#include <cstring>
#include <fstream>
#include <utility>

#include "echolist/echolist_format.hpp"
#include "msgbase/byte_order.hpp"

namespace amberedit::echolist {
namespace {

using msgbase::bytes::readU16;
using msgbase::bytes::readU32;

[[nodiscard]] uint64_t readU64(const unsigned char* bytes) {
    return static_cast<uint64_t>(readU32(bytes)) |
           static_cast<uint64_t>(readU32(bytes + 4)) << 32;
}

/// Whether a run of `count` bytes at `offset` is inside a file of `size` bytes,
/// asked in 64-bit arithmetic so that a length near the top of the range cannot
/// wrap around into looking valid.
[[nodiscard]] bool within(uint64_t offset, uint64_t count, uint64_t size) {
    return offset <= size && count <= size - offset;
}

}  // namespace

Result<EcholistDb> EcholistDb::open(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return failure("compiled echolist not found: " + path);

    EcholistDb db;
    db.path_ = path;
    db.data_.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (in.bad()) return failure("cannot read the compiled echolist: " + path);

    const auto* raw = db.data_.data();
    const uint64_t size = db.data_.size();
    if (size < format::kHeaderSize ||
        std::memcmp(raw, format::kMagic, sizeof(format::kMagic)) != 0) {
        return failure(path +
                       " is not a compiled echolist — echolist_db names the "
                       "file AmberEdit compiles them into, not an echolist "
                       "itself");
    }

    const uint16_t version = readU16(raw + 8);
    if (version != format::kVersion) {
        return failure(path + " was compiled as format version " +
                       std::to_string(version) + ", and this AmberEdit reads version " +
                       std::to_string(format::kVersion) +
                       " — amberedit --compile writes it again");
    }

    db.areaCount_ = readU32(raw + 12);
    db.indexOffset_ = readU32(raw + 16);
    db.recordsOffset_ = readU32(raw + 20);
    db.recordsSize_ = readU32(raw + 24);
    const uint32_t sourceCount = readU32(raw + 28);
    const uint32_t sourceOffset = readU32(raw + 32);
    db.builtAt_ = static_cast<std::time_t>(readU64(raw + 36));

    // Everything the lookup will walk is checked here and nowhere else: a
    // truncated file must fail at open, where the path can be named, rather than
    // reading past the end of what was read in the middle of a search.
    const bool sane =
        within(db.indexOffset_,
               static_cast<uint64_t>(db.areaCount_) * format::kIndexEntrySize, size) &&
        within(db.recordsOffset_, db.recordsSize_, size) && within(sourceOffset, 0, size);
    if (!sane) {
        return failure("the compiled echolist is truncated or damaged: " + path +
                       " — amberedit --compile writes it again");
    }

    uint64_t at = sourceOffset;
    db.sources_.reserve(sourceCount);
    // Answers whether it could read, the complaint being the same one for every
    // way it could not: a lambda cannot return out of open() on its behalf.
    const auto readString = [&](std::string& out) {
        if (!within(at, 2, size)) return false;
        const uint16_t length = readU16(raw + at);
        at += 2;
        if (!within(at, length, size)) return false;
        out.assign(reinterpret_cast<const char*>(raw + at), length);
        at += length;
        return true;
    };
    for (uint32_t i = 0; i < sourceCount; ++i) {
        SourceState state;
        if (!readString(state.spec) || !readString(state.charset) ||
            !readString(state.path) || !within(at, 16, size)) {
            return failure("the compiled echolist is truncated: " + path);
        }
        state.modified = readU64(raw + at);
        state.size = readU64(raw + at + 8);
        at += 16;
        db.sources_.push_back(std::move(state));
    }

    // Every record is reached through the index, so an index entry pointing past
    // the records is the one damage a lookup could not survive. Checked once
    // here rather than at every search, the array being small and read whole.
    for (size_t i = 0; i < db.areaCount_; ++i) {
        const uint32_t offset =
            readU32(raw + db.indexOffset_ + (i * format::kIndexEntrySize));
        if (offset > db.recordsSize_ ||
            db.recordsSize_ - offset < format::kRecordFixedSize) {
            return failure("the compiled echolist is damaged: " + path +
                           " — amberedit --compile writes it again");
        }
        const unsigned char* record = raw + db.recordsOffset_ + offset;
        const uint64_t length = static_cast<uint64_t>(format::kRecordFixedSize) +
                                readU16(record) + readU16(record + 2);
        if (length > db.recordsSize_ - offset) {
            return failure("the compiled echolist is damaged: " + path +
                           " — amberedit --compile writes it again");
        }
    }

    return db;
}

uint32_t EcholistDb::recordOffsetAt(size_t index) const {
    return readU32(data_.data() + indexOffset_ + (index * format::kIndexEntrySize));
}

std::string EcholistDb::tagAt(size_t index) const {
    const unsigned char* record = data_.data() + recordsOffset_ + recordOffsetAt(index);
    return std::string(reinterpret_cast<const char*>(record + format::kRecordFixedSize),
                       readU16(record));
}

std::string EcholistDb::descriptionAt(size_t index) const {
    const unsigned char* record = data_.data() + recordsOffset_ + recordOffsetAt(index);
    const uint16_t tagLength = readU16(record);
    return std::string(
        reinterpret_cast<const char*>(record + format::kRecordFixedSize + tagLength),
        readU16(record + 2));
}

std::string EcholistDb::keyAt(size_t index) const {
    return foldTag(tagAt(index));
}

size_t EcholistDb::lowerBound(const std::string& key) const {
    size_t low = 0;
    size_t high = areaCount_;
    while (low < high) {
        const size_t middle = low + ((high - low) / 2);
        if (keyAt(middle) < key) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    return low;
}

std::optional<std::string> EcholistDb::descriptionOf(std::string_view tag) const {
    const std::string key = foldTag(tag);
    if (key.empty() || areaCount_ == 0) return std::nullopt;
    const size_t at = lowerBound(key);
    if (at >= areaCount_ || keyAt(at) != key) return std::nullopt;
    return descriptionAt(at);
}

}  // namespace amberedit::echolist
