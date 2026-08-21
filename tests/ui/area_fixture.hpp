#pragma once

#include <doctest/doctest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "app/area_manager.hpp"
#include "config/app_config.hpp"
#include "ports/i_area_source.hpp"
#include "ports/i_lastread_store.hpp"
#include "temp_squish_base.hpp"
#include "ui/app_state.hpp"

/// One area on a real Squish base, with the state the screens work on — what
/// both the message list's and the reader's tests are driven through, since
/// the two screens hand over to each other and share every field.
namespace amberedit::test {

class SingleAreaSource final : public ports::IAreaConfigSource {
public:
    explicit SingleAreaSource(domain::AreaConfig area) : area_(std::move(area)) {}
    amberedit::Result<std::vector<domain::AreaConfig>> loadAreas() override {
        return std::vector<domain::AreaConfig>{area_};
    }

private:
    domain::AreaConfig area_;
};

/// The mark another reader would have left behind, set by hand: where an area
/// opens is exactly this, and no lastread file is written for it.
class StubLastReadStore final : public ports::ILastReadStore {
public:
    uint32_t getLastRead(const domain::AreaConfig&) override { return uid_; }
    void setLastRead(const domain::AreaConfig&, uint32_t uid) override { uid_ = uid; }

    void set(uint32_t uid) { uid_ = uid; }

private:
    uint32_t uid_{0};
};

inline domain::AreaConfig squishArea(const std::string& path) {
    domain::AreaConfig area;
    area.tag = "localnet";
    area.path = path;
    area.type = domain::MsgBaseType::Squish;
    return area;
}

/// The state and everything it refers to, kept alive together: ui::AppState
/// holds references to both the manager and the config.
struct AreaFixture {
    /// The config is taken here rather than set afterwards for the settings the
    /// manager reads: it keeps a copy of its own, made when it is built, so a
    /// later `fixture.config.x = …` reaches the screens and not it. Settings the
    /// screens read — `edgeExit` and the rest — can still be set either way.
    explicit AreaFixture(const std::string& path, config::AppConfig cfg = {})
        : area(squishArea(path)),
          config(std::move(cfg)),
          lastRead(new StubLastReadStore),
          manager(std::make_unique<SingleAreaSource>(area),
                  std::unique_ptr<StubLastReadStore>(lastRead), config),
          state(manager, config) {
        static_cast<void>(manager.reload());
    }

    /// Where the cursor sits within the window on screen, as a row number.
    [[nodiscard]] int cursorRow() const {
        return state.messageCursor - state.messageOffset;
    }

    [[nodiscard]] uint32_t total() const { return manager.areas()[0].total; }

    domain::AreaConfig area;
    config::AppConfig config;
    /// Owned by the manager; kept here so the test can place the mark.
    StubLastReadStore* lastRead;
    app::AreaManager manager;
    ui::AppState state;
};

/// The UID of a message, which is what a lastread mark holds.
inline uint32_t uidAt(AreaFixture& fixture, uint32_t number) {
    ports::IMsgBase* base = fixture.manager.openArea(fixture.area);
    REQUIRE(base != nullptr);
    const uint32_t uid = base->uidOf(number);
    fixture.manager.closeCurrentArea();
    return uid;
}

}  // namespace amberedit::test
