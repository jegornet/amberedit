#include "ui/output_dialog.hpp"

#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <vector>

#include "msgbase/null_lastread_store.hpp"
#include "ui/app_state.hpp"
#include "ui/term/element.hpp"
#include "ui/term/event.hpp"
#include "ui/term/screen.hpp"

using amberedit::app::AreaManager;
using amberedit::config::AppConfig;
using amberedit::domain::AreaConfig;
using amberedit::ui::AppState;
using amberedit::ui::term::Event;

namespace output_dialog = amberedit::ui::output_dialog;
namespace term = amberedit::ui::term;

namespace {

class EmptyAreaSource final : public amberedit::ports::IAreaConfigSource {
public:
    tl::expected<std::vector<AreaConfig>, amberedit::ErrorPtr> loadAreas() override {
        return {};
    }
};

struct Fixture {
    Fixture()
        : manager(std::make_unique<EmptyAreaSource>(),
                  std::make_unique<amberedit::msgbase::NullLastReadStore>(), config),
          state(manager, config) {
        state.width = 100;
        state.height = 20;
    }

    AppConfig config;
    AreaManager manager;
    AppState state;
};

std::vector<std::string> rowsOf(AppState& state) {
    term::Screen screen(state.width, state.height);
    term::render(screen, output_dialog::render(state, term::text("")));
    std::vector<std::string> rows;
    for (int y = 0; y < state.height; ++y) {
        std::string row;
        for (int x = 0; x < state.width; ++x) row += screen.at(x, y).glyph;
        rows.push_back(row);
    }
    return rows;
}

bool anyRowHas(const std::vector<std::string>& rows, const std::string& what) {
    for (const auto& row : rows) {
        if (row.find(what) != std::string::npos) return true;
    }
    return false;
}

term::MouseEvent leftPressAt(int x, int y) {
    term::MouseEvent mouse;
    mouse.x = x;
    mouse.y = y;
    mouse.button = term::MouseEvent::Button::Left;
    mouse.motion = term::MouseEvent::Motion::Pressed;
    return mouse;
}

std::vector<std::string> numbered(int count) {
    std::vector<std::string> lines;
    for (int i = 1; i <= count; ++i) lines.push_back("line " + std::to_string(i));
    return lines;
}

}  // namespace

TEST_CASE("A utility's output is shown under its title with an OK button [output]") {
    Fixture fixture;
    output_dialog::open(fixture.state, "Scan", {"scanned 12", "no outbound"}, false);
    REQUIRE(fixture.state.outputView.has_value());
    const std::vector<std::string> rows = rowsOf(fixture.state);
    CHECK(anyRowHas(rows, "Scan"));
    CHECK(anyRowHas(rows, "scanned 12"));
    CHECK(anyRowHas(rows, "no outbound"));
    CHECK(anyRowHas(rows, "OK"));
    // Everything fits, so there is nothing to scroll and no bar saying so.
    CHECK_FALSE(fixture.state.outputView->scrollbar);
}

TEST_CASE("A utility that wrote nothing is said to have [output]") {
    Fixture fixture;
    output_dialog::open(fixture.state, "Scan", {}, false);
    CHECK(anyRowHas(rowsOf(fixture.state), "the program wrote nothing"));
}

TEST_CASE("Output longer than the box scrolls [output]") {
    Fixture fixture;
    output_dialog::open(fixture.state, "Log", numbered(100), true);
    std::vector<std::string> rows = rowsOf(fixture.state);
    AppState::OutputView& view = *fixture.state.outputView;
    CHECK(view.scrollbar);
    CHECK(view.visible < 100);
    CHECK(anyRowHas(rows, "line 1 "));
    CHECK_FALSE(anyRowHas(rows, "line 100"));
    // Cut output says so at the foot.
    CHECK(anyRowHas(rows, "only the first megabyte"));

    output_dialog::handleEvent(fixture.state, Event::ArrowDown);
    CHECK(fixture.state.outputView->scroll == 1);
    output_dialog::handleEvent(fixture.state, Event::End);
    rows = rowsOf(fixture.state);
    CHECK(anyRowHas(rows, "line 100"));
    output_dialog::handleEvent(fixture.state, Event::PageDown);
    CHECK(fixture.state.outputView->scroll == 100 - fixture.state.outputView->visible);
    output_dialog::handleEvent(fixture.state, Event::Home);
    CHECK(fixture.state.outputView->scroll == 0);
    output_dialog::handleEvent(fixture.state, Event::ArrowUp);
    CHECK(fixture.state.outputView->scroll == 0);
}

TEST_CASE("A long line is wrapped to the box rather than cut [output]") {
    Fixture fixture;
    fixture.state.width = 40;
    output_dialog::open(fixture.state, "Wide",
                        {"alpha beta gamma delta epsilon zeta eta theta iota kappa"},
                        false);
    const std::vector<std::string> rows = rowsOf(fixture.state);
    CHECK(fixture.state.outputView->rows.size() > 1);
    CHECK(anyRowHas(rows, "kappa"));
}

TEST_CASE("The output box is put away by its keys and its button [output]") {
    for (const Event& key : {Event::Return, Event::Escape, Event::Character('o')}) {
        Fixture fixture;
        output_dialog::open(fixture.state, "Scan", {"done"}, false);
        output_dialog::handleEvent(fixture.state, key);
        CHECK_FALSE(fixture.state.outputView.has_value());
    }

    Fixture fixture;
    output_dialog::open(fixture.state, "Scan", {"done"}, false);
    static_cast<void>(rowsOf(fixture.state));
    const term::Box ok = fixture.state.outputView->okBox;
    const term::Box box = fixture.state.outputView->box;
    // A click on the text inside the box is nothing.
    output_dialog::handleEvent(fixture.state,
                               Event::Mouse(leftPressAt(box.x_min + 2, box.y_min + 1)));
    CHECK(fixture.state.outputView.has_value());
    output_dialog::handleEvent(
        fixture.state,
        Event::Mouse(leftPressAt((ok.x_min + ok.x_max) / 2, (ok.y_min + ok.y_max) / 2)));
    CHECK_FALSE(fixture.state.outputView.has_value());

    // And a click outside it.
    output_dialog::open(fixture.state, "Scan", {"done"}, false);
    static_cast<void>(rowsOf(fixture.state));
    output_dialog::handleEvent(fixture.state, Event::Mouse(leftPressAt(0, 0)));
    CHECK_FALSE(fixture.state.outputView.has_value());
}
