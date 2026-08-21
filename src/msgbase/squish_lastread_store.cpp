#include "msgbase/squish_lastread_store.hpp"

#include <cstdint>

#include "msgbase/lastread_file.hpp"

namespace amberedit::msgbase {

namespace {
constexpr size_t kRecordSize = 4;  // one dword per user
}

std::string SquishLastReadStore::pathFor(const domain::AreaConfig& area) {
    return area.path + ".sql";
}

uint32_t SquishLastReadStore::getLastRead(const domain::AreaConfig& area) {
    if (area.path.empty() || userNumber_ < 0) return 0;

    unsigned char record[kRecordSize]{};
    const uint64_t offset = static_cast<uint64_t>(userNumber_) * kRecordSize;
    if (!lastread_file::readBytes(pathFor(area), offset, record, sizeof(record))) {
        return 0;
    }
    const uint32_t uid = lastread_file::readU32(record);
    // A file grown past this user but never written for them reads as zero,
    // and ffffffffH is what a cleared record looks like. Both mean "unread".
    return uid == 0xffffffffu ? 0 : uid;
}

void SquishLastReadStore::setLastRead(const domain::AreaConfig& area, uint32_t uid) {
    if (area.path.empty() || userNumber_ < 0) return;

    unsigned char record[kRecordSize]{};
    lastread_file::writeU32(record, uid);
    const uint64_t offset = static_cast<uint64_t>(userNumber_) * kRecordSize;
    // Only this user's four bytes are touched, so a reader working in the same
    // base at the same time cannot lose its own mark to ours.
    (void)lastread_file::writeBytes(pathFor(area), offset, record, sizeof(record));
}

}  // namespace amberedit::msgbase
