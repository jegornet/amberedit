#include <doctest/doctest.h>

#include <optional>
#include <string>

#include "ui/keys.hpp"
#include "ui/term/event.hpp"
#include "ui/text_field.hpp"

using amberedit::config::CopyOnSelect;
using amberedit::ui::FieldContext;
using amberedit::ui::KeyMap;
using amberedit::ui::Millis;
using amberedit::ui::TextField;
using amberedit::ui::term::Event;
using amberedit::ui::term::MouseEvent;

namespace {

/// A field and what it is answered with: the default layout, a clock the test
/// moves, and a clipboard of its own.
struct FieldFixture {
    KeyMap keys = KeyMap::defaults();
    TextField field;
    std::string clipboard;
    Millis now{0};

    FieldContext context() {
        FieldContext answer{keys};
        answer.now = now;
        answer.copyOnSelect = CopyOnSelect::Mouse;
        answer.copy = [this](std::string text, bool) { clipboard = std::move(text); };
        answer.paste = [this]() -> std::optional<std::string> { return clipboard; };
        return answer;
    }
    bool key(const Event& event) {
        return amberedit::ui::handleFieldKey(field, event, context());
    }
    void type(const std::string& text) {
        for (const char c : text) static_cast<void>(key(Event::Character(c)));
    }
    bool pointer(int column, MouseEvent::Motion motion) {
        MouseEvent mouse;
        mouse.button = MouseEvent::Button::Left;
        mouse.motion = motion;
        mouse.x = field.box.x_min + column;
        mouse.y = field.box.y_min;
        return amberedit::ui::handleFieldMouse(field, Event::Mouse(mouse), context());
    }
};

Event shifted(Event::Name name) {
    return Event::Named(name, false, false, true);
}

}  // namespace

TEST_CASE("A dialog's field answers the editor's word keys [text_field]") {
    FieldFixture fixture;
    fixture.type("one two three");
    CHECK(fixture.key(Event::Character("w", true)));
    CHECK(fixture.field.value == "one two ");
    CHECK(fixture.key(Event::Character("b", false, true)));
    CHECK(fixture.field.cursor == 4);
    CHECK(fixture.field.touched);
    // A chord nothing binds is not typed as its letter.
    CHECK_FALSE(fixture.key(Event::Character("q", true)));
    CHECK(fixture.field.value == "one two ");
}

TEST_CASE("Shift and the arrows select, and copy, cut and paste use it [text_field]") {
    FieldFixture fixture;
    fixture.type("alpha beta");
    CHECK(fixture.key(shifted(Event::Name::Home)));
    CHECK(fixture.key(Event::Character("x", true)));
    CHECK(fixture.field.value.empty());
    CHECK(fixture.clipboard == "alpha beta");

    // A paste takes the first line, in place of what is selected.
    fixture.clipboard = std::string("gamma") + '\n' + "delta";
    CHECK(fixture.key(Event::Character("v", true)));
    CHECK(fixture.field.value == "gamma");
    CHECK(fixture.key(shifted(Event::Name::ArrowLeft)));
    CHECK(fixture.key(Event::Character("c", true)));
    CHECK(fixture.clipboard == "a");
    CHECK(fixture.key(Event::Character('o')));
    CHECK(fixture.field.value == "gammo");
    // ↑ and ↓ are the box's, and leave nothing selected.
    CHECK(fixture.key(shifted(Event::Name::Home)));
    CHECK_FALSE(fixture.key(Event::ArrowDown));
    CHECK_FALSE(fixture.field.anchor);
}

TEST_CASE("The pointer drags, double and triple clicks in a field [text_field]") {
    FieldFixture fixture;
    fixture.field.value = "alpha beta";
    fixture.field.box = {10, 29, 5, 5};

    CHECK(fixture.pointer(1, MouseEvent::Motion::Pressed));
    CHECK(fixture.pointer(5, MouseEvent::Motion::Moved));
    CHECK(fixture.pointer(5, MouseEvent::Motion::Released));
    CHECK(fixture.clipboard == "lpha");

    fixture.now += 1000;
    CHECK(fixture.pointer(7, MouseEvent::Motion::Pressed));
    CHECK(fixture.pointer(7, MouseEvent::Motion::Pressed));
    CHECK(fixture.clipboard == "beta");
    CHECK(fixture.pointer(7, MouseEvent::Motion::Pressed));
    CHECK(fixture.clipboard == "alpha beta");

    // A press somewhere else is not the field's.
    fixture.now += 1000;
    CHECK_FALSE(fixture.pointer(40, MouseEvent::Motion::Pressed));
}
