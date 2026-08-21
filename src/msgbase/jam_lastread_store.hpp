#pragma once

#include <cstdint>
#include <string>
#include <utility>

#include "ports/i_lastread_store.hpp"

namespace amberedit::msgbase {

/// JAM lastread marks: the `<base>.jlr` file beside the .jhr, .jdt and .jdx.
///
/// Unlike the other two formats it is not indexed by user number. It holds a
/// record per user — UserCRC, UserID, LastReadMsg, HighReadMsg, four 32-bit
/// little-endian numbers — in no particular order, and a user's record has to
/// be searched for by the CRC-32 of their name (see specs/JAM.txt, ".JLR file"
/// section). The number stored is the absolute JAM message number, which is
/// what the JAM driver hands back as the UID.
///
/// The user number therefore only fills in the record's UserID field, where
/// JAM wants something unique per user and nothing reads it back. Without a
/// name in the config there is no key to search on, and the store does nothing
/// at all rather than claim the record whose CRC happens to be that of the
/// empty string.
class JamLastReadStore final : public ports::ILastReadStore {
public:
    JamLastReadStore(int userNumber, std::string userName)
        : userNumber_(userNumber), userName_(std::move(userName)) {}

    uint32_t getLastRead(const domain::AreaConfig& area) override;
    void setLastRead(const domain::AreaConfig& area, uint32_t uid) override;

    /// The file the records live in, for the sake of tests and error messages.
    [[nodiscard]] static std::string pathFor(const domain::AreaConfig& area);

private:
    /// Where this user's record sits, counted in records; -1 when the file has
    /// none for them yet, in which case a new one goes at the end.
    [[nodiscard]] long findRecord(const std::string& path, uint32_t crc,
                                  uint32_t* lastRead, uint32_t* highRead) const;

    int userNumber_{0};
    std::string userName_;
};

}  // namespace amberedit::msgbase
