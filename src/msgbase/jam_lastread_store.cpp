#include "msgbase/jam_lastread_store.hpp"

#include <algorithm>
#include <cstdint>

#include "msgbase/lastread_file.hpp"

namespace amberedit::msgbase {

namespace {

/// UserCRC, UserID, LastReadMsg, HighReadMsg — four dwords.
constexpr size_t kRecordSize = 16;
constexpr size_t kUserIdOffset = 4;
constexpr size_t kLastReadOffset = 8;
constexpr size_t kHighReadOffset = 12;
/// A deleted record has both UserCRC and UserID set to ffffffffH.
constexpr uint32_t kDeleted = 0xffffffffu;

}  // namespace

std::string JamLastReadStore::pathFor(const domain::AreaConfig& area) {
    return area.path + ".jlr";
}

long JamLastReadStore::findRecord(const std::string& path, uint32_t crc,
                                  uint32_t* lastRead, uint32_t* highRead) const {
    const uint64_t size = lastread_file::fileSize(path);
    const auto records = static_cast<long>(size / kRecordSize);

    for (long i = 0; i < records; ++i) {
        unsigned char record[kRecordSize]{};
        if (!lastread_file::readBytes(path, static_cast<uint64_t>(i) * kRecordSize,
                                      record, sizeof(record))) {
            break;
        }
        const uint32_t userCrc = lastread_file::readU32(record);
        if (userCrc == kDeleted || userCrc != crc) continue;

        if (lastRead != nullptr) {
            *lastRead = lastread_file::readU32(record + kLastReadOffset);
        }
        if (highRead != nullptr) {
            *highRead = lastread_file::readU32(record + kHighReadOffset);
        }
        return i;
    }
    return -1;
}

uint32_t JamLastReadStore::getLastRead(const domain::AreaConfig& area) {
    if (area.path.empty() || userName_.empty()) return 0;

    uint32_t lastRead = 0;
    const std::string path = pathFor(area);
    if (findRecord(path, lastread_file::nameCrc32(userName_), &lastRead, nullptr) < 0) {
        return 0;
    }
    return lastRead;
}

void JamLastReadStore::setLastRead(const domain::AreaConfig& area, uint32_t uid) {
    if (area.path.empty() || userName_.empty()) return;

    const std::string path = pathFor(area);
    const uint32_t crc = lastread_file::nameCrc32(userName_);

    uint32_t highRead = 0;
    long index = findRecord(path, crc, nullptr, &highRead);
    if (index < 0) {
        // No record yet: ours goes on the end. Records are in no particular
        // order, so appending is as correct as anywhere else.
        index = static_cast<long>(lastread_file::fileSize(path) / kRecordSize);
    }

    unsigned char record[kRecordSize]{};
    lastread_file::writeU32(record, crc);
    lastread_file::writeU32(record + kUserIdOffset, static_cast<uint32_t>(userNumber_));
    lastread_file::writeU32(record + kLastReadOffset, uid);
    // HighReadMsg is the furthest the reading has ever got, so re-reading an
    // older message moves LastReadMsg back but leaves this where it was.
    lastread_file::writeU32(record + kHighReadOffset, std::max(highRead, uid));

    (void)lastread_file::writeBytes(path, static_cast<uint64_t>(index) * kRecordSize,
                                    record, sizeof(record));
}

}  // namespace amberedit::msgbase
