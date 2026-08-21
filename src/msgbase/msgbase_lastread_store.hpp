#pragma once

#include <cstdint>
#include <string>

#include "msgbase/fido_lastread_store.hpp"
#include "msgbase/jam_lastread_store.hpp"
#include "msgbase/squish_lastread_store.hpp"
#include "ports/i_lastread_store.hpp"

namespace amberedit::msgbase {

/// The lastread store the application uses: the three formats keep their marks
/// in three different files, so which one answers is decided per area, from
/// its base type.
///
/// The type comes from the tosser config where it states one and from what is
/// on disk where it does not (FtnMsgBase::probeType) — so an area the config
/// is vague about still gets its marks written to the right file rather than
/// silently to none.
class MsgBaseLastReadStore final : public ports::ILastReadStore {
public:
    MsgBaseLastReadStore(int userNumber, std::string userName)
        : squish_(userNumber), jam_(userNumber, std::move(userName)), fido_(userNumber) {}

    uint32_t getLastRead(const domain::AreaConfig& area) override;
    void setLastRead(const domain::AreaConfig& area, uint32_t uid) override;

private:
    /// The store for this area, or nullptr for a type that keeps no marks —
    /// a passthrough area, or one whose format could not be worked out.
    [[nodiscard]] ports::ILastReadStore* storeFor(const domain::AreaConfig& area);

    SquishLastReadStore squish_;
    JamLastReadStore jam_;
    FidoLastReadStore fido_;
};

}  // namespace amberedit::msgbase
