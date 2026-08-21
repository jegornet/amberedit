#include "ui/term/event.hpp"

#include <utility>

namespace amberedit::ui::term {

Event Event::Named(Name name, bool ctrl, bool alt, bool shift) {
    Event event;
    event.kind_ = Kind::Key;
    event.name_ = name;
    event.ctrl_ = ctrl;
    event.alt_ = alt;
    event.shift_ = shift;
    return event;
}

Event Event::Character(std::string text, bool ctrl, bool alt, bool shift) {
    Event event;
    event.kind_ = Kind::Character;
    event.character_ = std::move(text);
    event.ctrl_ = ctrl;
    event.alt_ = alt;
    event.shift_ = shift;
    return event;
}

Event Event::Character(char c) { return Character(std::string(1, c)); }

Event Event::Mouse(MouseEvent mouse) {
    Event event;
    event.kind_ = Kind::Mouse;
    event.mouse_ = mouse;
    return event;
}

bool Event::operator==(const Event& other) const {
    if (kind_ != other.kind_) return false;
    if (ctrl_ != other.ctrl_ || alt_ != other.alt_ || shift_ != other.shift_) return false;
    switch (kind_) {
        case Kind::Key: return name_ == other.name_;
        case Kind::Character: return character_ == other.character_;
        // Two mouse reports are never compared for equality anywhere in AmberEdit —
        // a click is asked where it landed, not whether it is some other click.
        case Kind::Mouse:
        case Kind::Resize:
        case Kind::None: return true;
    }
    return false;
}

const Event Event::Escape = Event::Named(Event::Name::Escape);
const Event Event::Return = Event::Named(Event::Name::Return);
const Event Event::Tab = Event::Named(Event::Name::Tab);
const Event Event::TabReverse = Event::Named(Event::Name::TabReverse);
const Event Event::Backspace = Event::Named(Event::Name::Backspace);
const Event Event::Delete = Event::Named(Event::Name::Delete);
const Event Event::ArrowUp = Event::Named(Event::Name::ArrowUp);
const Event Event::ArrowDown = Event::Named(Event::Name::ArrowDown);
const Event Event::ArrowLeft = Event::Named(Event::Name::ArrowLeft);
const Event Event::ArrowRight = Event::Named(Event::Name::ArrowRight);
const Event Event::Home = Event::Named(Event::Name::Home);
const Event Event::End = Event::Named(Event::Name::End);
const Event Event::PageUp = Event::Named(Event::Name::PageUp);
const Event Event::PageDown = Event::Named(Event::Name::PageDown);
const Event Event::F1 = Event::Named(Event::Name::F1);
const Event Event::F2 = Event::Named(Event::Name::F2);
const Event Event::F3 = Event::Named(Event::Name::F3);
const Event Event::F4 = Event::Named(Event::Name::F4);
const Event Event::F5 = Event::Named(Event::Name::F5);
const Event Event::F6 = Event::Named(Event::Name::F6);
const Event Event::F7 = Event::Named(Event::Name::F7);
const Event Event::F8 = Event::Named(Event::Name::F8);
const Event Event::F9 = Event::Named(Event::Name::F9);
const Event Event::F10 = Event::Named(Event::Name::F10);
const Event Event::F11 = Event::Named(Event::Name::F11);
const Event Event::F12 = Event::Named(Event::Name::F12);

// Built here rather than through a factory: Resize is the one kind with nothing
// else to carry. The initializer of a static member is inside the class's scope,
// so it may set the private field directly.
const Event Event::Resize = [] {
    Event event;
    event.kind_ = Kind::Resize;
    return event;
}();

}  // namespace amberedit::ui::term
