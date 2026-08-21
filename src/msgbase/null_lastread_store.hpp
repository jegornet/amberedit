#pragma once

#include <cstdint>

#include "ports/i_lastread_store.hpp"

namespace amberedit::msgbase {

/// An ILastReadStore that keeps no marks at all: every area reads as entirely
/// unread and nothing is written to disk.
///
/// The application uses MsgBaseLastReadStore; this one is for tests that are
/// about something else and have no business touching a message base, and for
/// anywhere marks are deliberately not wanted.
class NullLastReadStore final : public ports::ILastReadStore {
public:
    uint32_t getLastRead(const domain::AreaConfig& /*area*/) override { return 0; }
    void setLastRead(const domain::AreaConfig& /*area*/, uint32_t /*msgNum*/) override {}
};

}  // namespace amberedit::msgbase
