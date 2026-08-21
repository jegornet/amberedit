#include <catch2/catch.hpp>

#include <memory>
#include <vector>

#include "msgbase/null_lastread_store.hpp"
#include "ui/app_state.hpp"

using amberedit::app::AreaManager;
using amberedit::config::AppConfig;
using amberedit::domain::AreaConfig;
using amberedit::ui::AppState;

namespace {

/// Hands back nothing: these tests are about the state, not the areas.
class EmptyAreaSource final : public amberedit::ports::IAreaConfigSource {
public:
    std::vector<AreaConfig> loadAreas() override { return {}; }
};

/// Keeps the manager the state refers to alive for as long as the state is.
struct StateWithManager {
    explicit StateWithManager(const std::string& userName)
        : manager(std::make_unique<EmptyAreaSource>(),
                  std::make_unique<amberedit::msgbase::NullLastReadStore>(),
                  config(userName)),
          state(manager, config(userName)) {}

    static AppConfig config(const std::string& userName) {
        AppConfig cfg;
        cfg.userName = userName;
        return cfg;
    }

    AreaManager manager;
    AppState state;
};

/// A state built on a config that outlives it, for the settings the state reads
/// at construction. The config is the first member, so it is there before the
/// manager and the state are built on it.
struct StateWithConfig {
    explicit StateWithConfig(int clickAnimationMs)
        : config(configWith(clickAnimationMs)),
          manager(std::make_unique<EmptyAreaSource>(),
                  std::make_unique<amberedit::msgbase::NullLastReadStore>(), config),
          state(manager, config) {}

    static AppConfig configWith(int clickAnimationMs) {
        AppConfig cfg;
        cfg.clickAnimationMs = clickAnimationMs;
        return cfg;
    }

    AppConfig config;
    AreaManager manager;
    AppState state;
};

}  // namespace

TEST_CASE("isOwnName recognises the user's own name", "[appstate]") {
    StateWithManager owner("Yegor Gluhov");
    CHECK(owner.state.isOwnName("Yegor Gluhov"));
    CHECK_FALSE(owner.state.isOwnName("Areafix robot"));
    // A prefix is somebody else.
    CHECK_FALSE(owner.state.isOwnName("Yegor"));
    CHECK_FALSE(owner.state.isOwnName(""));
}

TEST_CASE("isOwnName ignores case in ASCII", "[appstate]") {
    StateWithManager owner("Yegor Gluhov");
    CHECK(owner.state.isOwnName("YEGOR GLUHOV"));
    CHECK(owner.state.isOwnName("yegor gluhov"));
}

TEST_CASE("isOwnName matches a Cyrillic name exactly", "[appstate]") {
    // Case folding is ASCII-only, so a Cyrillic name has to be spelled the same
    // way. That is the safe direction: the worst that happens is the name stays
    // in the ordinary color.
    StateWithManager owner("Егор Глухов");
    CHECK(owner.state.isOwnName("Егор Глухов"));
    CHECK_FALSE(owner.state.isOwnName("ЕГОР ГЛУХОВ"));
}

TEST_CASE("isOwnName matches nothing when the config names nobody", "[appstate]") {
    StateWithManager anonymous("");
    CHECK_FALSE(anonymous.state.isOwnName(""));
    CHECK_FALSE(anonymous.state.isOwnName("Yegor Gluhov"));
}

TEST_CASE("a click is shown on the button before it acts", "[appstate]") {
    StateWithConfig clicked(200);

    // What the shell would draw: the frame is held while the button is pressed,
    // so the state as the animation sees it is what is recorded here.
    int frames = 0;
    AppState::Pressed drawn = AppState::Pressed::None;
    clicked.state.holdFrame = [&] {
        ++frames;
        drawn = clicked.state.pressed;
        CHECK(clicked.state.isPressed(AppState::Pressed::Back));
    };

    clicked.state.showClick(AppState::Pressed::Back);

    CHECK(frames == 1);
    CHECK(drawn == AppState::Pressed::Back);
    // And nothing is left pressed afterwards: it says what the pointer is doing
    // now, not what the interface holds.
    CHECK(clicked.state.pressed == AppState::Pressed::None);
    CHECK_FALSE(clicked.state.isPressed(AppState::Pressed::Back));
}

TEST_CASE("a thread marker is pressed by the message it names", "[appstate]") {
    StateWithConfig clicked(200);
    clicked.state.holdFrame = [&] {
        CHECK(clicked.state.isPressed(AppState::Pressed::ThreadLink, 42));
        // The other markers beside it are drawn as they were.
        CHECK_FALSE(clicked.state.isPressed(AppState::Pressed::ThreadLink, 41));
        CHECK_FALSE(clicked.state.isPressed(AppState::Pressed::Back));
    };
    clicked.state.showClick(AppState::Pressed::ThreadLink, 42);
    CHECK(clicked.state.pressedLink == 0);
}

TEST_CASE("a click in a list holds the frame with no button pressed", "[appstate]") {
    // What the lists ask for: the cursor has been moved onto the row that was
    // clicked, and the animation is that row being on screen as the current one
    // before it opens. There is no button to invert.
    StateWithConfig clicked(200);
    int frames = 0;
    clicked.state.holdFrame = [&] {
        ++frames;
        CHECK(clicked.state.pressed == AppState::Pressed::None);
    };
    clicked.state.showClick();
    CHECK(frames == 1);
}

TEST_CASE("click_animation_ms 0 turns the animation off", "[appstate]") {
    StateWithConfig instant(0);
    CHECK(instant.state.clickAnimationMs == 0);

    int frames = 0;
    instant.state.holdFrame = [&] { ++frames; };
    instant.state.showClick(AppState::Pressed::ConfirmYes);
    instant.state.showClick();

    // No frame held and no pause: the click acts the moment it is read.
    CHECK(frames == 0);
    CHECK(instant.state.pressed == AppState::Pressed::None);
}

TEST_CASE("showClick does nothing without a terminal to draw on", "[appstate]") {
    // Which is how every test but these ones runs: the screens can be driven
    // through a click without a shell underneath them.
    StateWithConfig headless(200);
    headless.state.showClick(AppState::Pressed::Back);
    CHECK(headless.state.pressed == AppState::Pressed::None);
}
