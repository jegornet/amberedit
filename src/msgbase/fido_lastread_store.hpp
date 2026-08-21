#pragma once

#include <cstdint>
#include <string>

#include "ports/i_lastread_store.hpp"

namespace amberedit::msgbase {

/// Fido *.msg (FTS-0001) lastread marks: a file named `lastread` in the area's
/// own directory, the area being a directory of N.msg files rather than a base
/// with an extension.
///
/// It is an array of 16-bit numbers indexed by user number — see
/// specs/FIDO-LASTREAD.md — holding the absolute number of the last message
/// read, which is the N in its N.msg and what the driver hands back as the UID.
///
/// Sixteen bits is the format's own limit, not ours: an area that has ever
/// held more than 65535 messages cannot be marked past that point by anybody,
/// and a mark that would not fit is dropped rather than written back as some
/// wrapped-around message the reader never opened.
class FidoLastReadStore final : public ports::ILastReadStore {
public:
    explicit FidoLastReadStore(int userNumber) : userNumber_(userNumber) {}

    uint32_t getLastRead(const domain::AreaConfig& area) override;
    void setLastRead(const domain::AreaConfig& area, uint32_t uid) override;

    /// The file the marks live in, for the sake of tests and error messages.
    [[nodiscard]] static std::string pathFor(const domain::AreaConfig& area);

private:
    int userNumber_{0};
};

}  // namespace amberedit::msgbase
