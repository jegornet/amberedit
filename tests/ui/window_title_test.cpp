#include "ui/window_title.hpp"

#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <vector>

#include "msgbase/null_lastread_store.hpp"
#include "ports/i_area_source.hpp"
#include "ui/app_state.hpp"

using amberedit::app::AreaManager;
using amberedit::app::ScreenId;
using amberedit::config::AppConfig;
using amberedit::domain::AreaConfig;
using amberedit::ui::AppState;
using amberedit::ui::windowTitle;

namespace {

class EmptyAreaSource final : public amberedit::ports::IAreaConfigSource {
public:
    tl::expected<std::vector<AreaConfig>, amberedit::ErrorPtr> loadAreas() override {
        return {};
    }
};

/// A state standing on `screen` in an area called `tag`: the title is made of
/// the two and nothing a base holds.
struct Fixture {
    Fixture(ScreenId screen, const std::string& tag)
        : manager(std::make_unique<EmptyAreaSource>(),
                  std::make_unique<amberedit::msgbase::NullLastReadStore>(), config),
          state(manager, config) {
        AreaConfig area;
        area.tag = tag;
        state.setCurrentArea(area);
        if (screen != ScreenId::AreaList) state.navigator.push(screen);
    }

    AppConfig config;
    AreaManager manager;
    AppState state;
};

}  // namespace

TEST_CASE("The window title names the screen and its area [windowtitle]") {
    CHECK(windowTitle(Fixture(ScreenId::AreaList, "ru.fidonet.today").state) ==
          "Areas — AmberEdit");
    CHECK(windowTitle(Fixture(ScreenId::MessageList, "ru.fidonet.today").state) ==
          "Messages in ru.fidonet.today — AmberEdit");
    CHECK(windowTitle(Fixture(ScreenId::MessageRead, "ru.fidonet.today").state) ==
          "ru.fidonet.today — AmberEdit");
    CHECK(windowTitle(Fixture(ScreenId::Compose, "ru.fidonet.today").state) ==
          "Writing to ru.fidonet.today — AmberEdit");
}

TEST_CASE("A forward is titled by the area it goes into [windowtitle]") {
    Fixture fixture(ScreenId::Compose, "ru.fidonet.today");
    fixture.state.compose.forward = true;
    fixture.state.targetArea.tag = "netmail";
    CHECK(windowTitle(fixture.state) == "Writing to netmail — AmberEdit");
}

TEST_CASE("A box over a screen leaves the title alone [windowtitle]") {
    Fixture fixture(ScreenId::MessageRead, "localnet");
    const std::string before = windowTitle(fixture.state);
    fixture.state.errorMessage = "something went wrong";
    CHECK(windowTitle(fixture.state) == before);
}
