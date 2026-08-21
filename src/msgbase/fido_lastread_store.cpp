#include "msgbase/fido_lastread_store.hpp"

#include <cstdint>
#include <filesystem>
#include <limits>

#include "msgbase/lastread_file.hpp"

namespace amberedit::msgbase {

namespace {
constexpr size_t kRecordSize = 2;  // one word per user
/// GoldED's FIDOLASTREAD defaults to this and the name is effectively fixed:
/// the setting takes a name and not a path, so the file always sits in the
/// area's own directory.
constexpr const char* kFileName = "lastread";
}  // namespace

std::string FidoLastReadStore::pathFor(const domain::AreaConfig& area) {
    return (std::filesystem::path(area.path) / kFileName).string();
}

uint32_t FidoLastReadStore::getLastRead(const domain::AreaConfig& area) {
    if (area.path.empty() || userNumber_ < 0) return 0;

    unsigned char record[kRecordSize]{};
    const uint64_t offset = static_cast<uint64_t>(userNumber_) * kRecordSize;
    if (!lastread_file::readBytes(pathFor(area), offset, record, sizeof(record))) {
        return 0;
    }
    return lastread_file::readU16(record);
}

void FidoLastReadStore::setLastRead(const domain::AreaConfig& area, uint32_t uid) {
    if (area.path.empty() || userNumber_ < 0) return;
    // Past what the format can say. Writing the low sixteen bits would point
    // every other reader at an unrelated message, so the mark stays as it was.
    if (uid > std::numeric_limits<uint16_t>::max()) return;

    unsigned char record[kRecordSize]{};
    lastread_file::writeU16(record, static_cast<uint16_t>(uid));
    const uint64_t offset = static_cast<uint64_t>(userNumber_) * kRecordSize;
    (void)lastread_file::writeBytes(pathFor(area), offset, record, sizeof(record));
}

}  // namespace amberedit::msgbase
