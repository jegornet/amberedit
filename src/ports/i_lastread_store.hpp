#pragma once

#include <cstdint>

#include "domain/area.hpp"

namespace amberedit::ports {

/// Storage for "read up to here" marks.
///
/// The mark is a message **UID**, not its position in the area: that is what
/// every one of the three formats keeps on disk, and it is the only thing that
/// survives the base being packed or renumbered. Squish stores the UMSGID, JAM
/// the absolute JAM message number, Fido *.msg the number in `N.msg`; smapi
/// calls all three the UID and converts to and from the 1-based position
/// (IMsgBase::uidOf / indexOfUid). Zero means nothing has been read.
class ILastReadStore {
public:
    virtual ~ILastReadStore() = default;

    /// The stored UID, or 0 when the user has no mark in this area — which
    /// covers a missing lastread file, an unreadable one and an area whose
    /// format keeps no marks.
    virtual uint32_t getLastRead(const domain::AreaConfig& area) = 0;

    /// Stores the mark. Failure is silent by design: a read-only message base
    /// is a perfectly ordinary thing to be pointed at, and losing the mark is
    /// not worth interrupting reading over.
    virtual void setLastRead(const domain::AreaConfig& area, uint32_t uid) = 0;
};

}  // namespace amberedit::ports
