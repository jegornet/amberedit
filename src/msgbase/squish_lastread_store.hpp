#pragma once

#include <cstdint>
#include <string>

#include "ports/i_lastread_store.hpp"

namespace amberedit::msgbase {

/// Squish lastread marks: the `<base>.sql` file beside the .sqd and .sqi.
///
/// It is an array of 32-bit little-endian numbers, one per user, indexed by
/// the user number — see specs/Squish.txt, "Squish lastread file format". The
/// number stored is the UMSGID of the last message read, which is what smapi
/// calls the UID and what survives the base being packed. The file is optional
/// and belongs to the editors rather than to the tosser, so a missing one
/// simply means nobody has read this area yet.
class SquishLastReadStore final : public ports::ILastReadStore {
public:
    explicit SquishLastReadStore(int userNumber) : userNumber_(userNumber) {}

    uint32_t getLastRead(const domain::AreaConfig& area) override;
    void setLastRead(const domain::AreaConfig& area, uint32_t uid) override;

    /// The file the marks live in, for the sake of tests and error messages.
    [[nodiscard]] static std::string pathFor(const domain::AreaConfig& area);

private:
    int userNumber_{0};
};

}  // namespace amberedit::msgbase
