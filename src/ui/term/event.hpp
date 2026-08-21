#pragma once

#include <cstdint>
#include <string>

/// What arrived from the keyboard or the mouse.
///
/// The shape follows what AmberEdit's screens were already written against, so that
/// moving off FTXUI did not mean rewriting every handler. One thing did change,
/// and for the better: a modifier is now a flag rather than the raw bytes the
/// terminal happened to send. A binding used to be spelled as four different
/// escape sequences depending on which protocol the terminal had settled on —
/// see the `isAlt` this replaced — and is now spelled once.
namespace amberedit::ui::term {

/// Where the pointer was and what it did. Only presses are acted on anywhere in
/// amberedit: a release would arrive after whatever the press did — a screen opened,
/// a dialog closed — and act a second time on whatever has since moved under it.
struct MouseEvent {
    enum class Button : uint8_t { None, Left, Middle, Right, WheelUp, WheelDown };
    enum class Motion : uint8_t { Pressed, Released, Moved };

    Button button{Button::None};
    Motion motion{Motion::Pressed};
    int x{0};
    int y{0};
};

class Event {
public:
    /// Which of the four sorts of thing this is.
    enum class Kind : uint8_t {
        /// Nothing happened — a poll that timed out.
        None,
        /// A named key: an arrow, Home, F2.
        Key,
        /// Text. `character()` holds it, as UTF-8.
        Character,
        Mouse,
        /// The window changed size. Not a keystroke, and deliberately not
        /// treated as one: the frame has already been drawn to the new size,
        /// and passing it to a screen would act on whatever it resembles.
        Resize,
    };

    /// The named keys AmberEdit binds. Only these: a key that nothing listens for
    /// never becomes an Event at all.
    enum class Name : uint8_t {
        None,
        Escape,
        Return,
        Tab,
        TabReverse,
        Backspace,
        Delete,
        ArrowUp,
        ArrowDown,
        ArrowLeft,
        ArrowRight,
        Home,
        End,
        PageUp,
        PageDown,
        F1,
        F2,
        F3,
        F4,
        F5,
        F6,
        F7,
        F8,
        F9,
        F10,
        F11,
        F12,
    };

    Event() = default;

    static const Event Escape;
    static const Event Return;
    static const Event Tab;
    static const Event TabReverse;
    static const Event Backspace;
    static const Event Delete;
    static const Event ArrowUp;
    static const Event ArrowDown;
    static const Event ArrowLeft;
    static const Event ArrowRight;
    static const Event Home;
    static const Event End;
    static const Event PageUp;
    static const Event PageDown;
    static const Event F1;
    static const Event F2;
    static const Event F3;
    static const Event F4;
    static const Event F5;
    static const Event F6;
    static const Event F7;
    static const Event F8;
    static const Event F9;
    static const Event F10;
    static const Event F11;
    static const Event F12;
    static const Event Resize;

    [[nodiscard]] static Event Named(Name name, bool ctrl = false, bool alt = false,
                                     bool shift = false);
    [[nodiscard]] static Event Character(std::string text, bool ctrl = false,
                                         bool alt = false, bool shift = false);
    [[nodiscard]] static Event Character(char c);
    [[nodiscard]] static Event Mouse(MouseEvent mouse);

    [[nodiscard]] Kind kind() const { return kind_; }
    [[nodiscard]] Name name() const { return name_; }

    [[nodiscard]] bool is_character() const { return kind_ == Kind::Character; }
    [[nodiscard]] bool is_mouse() const { return kind_ == Kind::Mouse; }

    /// The text of a Character event, as UTF-8. Empty for anything else.
    [[nodiscard]] const std::string& character() const { return character_; }

    /// The bytes behind the event. For text this is the text itself, which is
    /// what the screens that inspect it — the incremental search — expect.
    [[nodiscard]] const std::string& input() const { return character_; }

    [[nodiscard]] const MouseEvent& mouse() const { return mouse_; }

    [[nodiscard]] bool ctrl() const { return ctrl_; }
    [[nodiscard]] bool alt() const { return alt_; }
    [[nodiscard]] bool shift() const { return shift_; }

    /// Equality takes the modifiers into account, so `Event::ArrowLeft` is a
    /// plain arrow and not an Alt-arrow. That is what the screens assume: they
    /// used to compare whole escape sequences, which differed for exactly this
    /// reason, and an else-if chain that tested the unmodified key first would
    /// otherwise swallow the modified one.
    [[nodiscard]] bool operator==(const Event& other) const;
    [[nodiscard]] bool operator!=(const Event& other) const { return !(*this == other); }

private:
    Kind kind_{Kind::None};
    Name name_{Name::None};
    std::string character_;
    MouseEvent mouse_{};
    bool ctrl_{false};
    bool alt_{false};
    bool shift_{false};
};

}  // namespace amberedit::ui::term
