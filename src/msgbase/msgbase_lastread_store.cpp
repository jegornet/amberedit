#include "msgbase/msgbase_lastread_store.hpp"

#include <cstdint>

#include "msgbase/ftn_msgbase.hpp"

namespace amberedit::msgbase {

ports::ILastReadStore* MsgBaseLastReadStore::storeFor(const domain::AreaConfig& area) {
    if (area.isPassthrough()) return nullptr;

    domain::MsgBaseType type = area.type;
    // The same fallback FtnMsgBase::open() makes: areas.bbs states the type
    // by a path prefix and fidoconfig may omit -b altogether, so without this
    // the marks of a perfectly ordinary area would go nowhere.
    if (type == domain::MsgBaseType::Unknown) type = FtnMsgBase::probeType(area.path);

    switch (type) {
        case domain::MsgBaseType::Squish: return &squish_;
        case domain::MsgBaseType::Jam: return &jam_;
        case domain::MsgBaseType::Sdm: return &fido_;
        case domain::MsgBaseType::Passthrough:
        case domain::MsgBaseType::Unknown: break;
    }
    return nullptr;
}

uint32_t MsgBaseLastReadStore::getLastRead(const domain::AreaConfig& area) {
    ports::ILastReadStore* store = storeFor(area);
    return store == nullptr ? 0 : store->getLastRead(area);
}

void MsgBaseLastReadStore::setLastRead(const domain::AreaConfig& area, uint32_t uid) {
    if (ports::ILastReadStore* store = storeFor(area)) store->setLastRead(area, uid);
}

}  // namespace amberedit::msgbase
