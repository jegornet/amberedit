#pragma once

#include <optional>

#include "ui/term/event.hpp"

namespace amberedit::ui {

/// The mouse behind an event, if it is one.
[[nodiscard]] inline std::optional<term::MouseEvent> mouseOf(const term::Event& event) {
    if (!event.is_mouse()) return std::nullopt;
    return event.mouse();
}

/// Whether the event is Ctrl held with the given letter.
///
/// One test where there used to be two. A terminal reports a Ctrl chord either
/// as the C0 control byte it has meant since ASCII — Ctrl-S is 0x13 — or, once
/// modified-key reporting is on, as a `CSI <codepoint> ; 5 u` sequence, and
/// which of them arrives depends on the terminal rather than on the key. Both
/// are recognised as they are read, so by the time an event reaches here it is
/// simply a letter with Ctrl held.
[[nodiscard]] inline bool isCtrl(const term::Event& event, char letter) {
    return event.ctrl() && event.is_character() && event.character().size() == 1 &&
           event.character()[0] == letter;
}

/// Whether the event is Alt held with the given letter.
///
/// Alt reaches an application in more shapes than Ctrl does — as an ESC in front
/// of the letter, as xterm's modifier 3 inside a CSI sequence, as the 9 some
/// terminals send in its place, or as kitty's CSI u. All of them are settled in
/// the input layer, and what arrives here is the letter and the modifier.
[[nodiscard]] inline bool isAlt(const term::Event& event, char letter) {
    return event.alt() && event.is_character() && event.character().size() == 1 &&
           event.character()[0] == letter;
}

/// Where a left-button click landed, or nothing when the event is not one.
///
/// The press alone counts. A release would arrive after whatever the press did
/// — a screen opened, a dialog closed — and act a second time on whatever has
/// since moved under the pointer.
[[nodiscard]] inline std::optional<term::MouseEvent> leftClick(const term::Event& event) {
    const auto mouse = mouseOf(event);
    if (!mouse || mouse->button != term::MouseEvent::Button::Left ||
        mouse->motion != term::MouseEvent::Motion::Pressed)
        return std::nullopt;
    return mouse;
}

/// Which way a wheel event points: -1 up, +1 down, 0 when the event is not a
/// wheel press.
///
/// Every screen moves by one line per notch, but what moves differs — the
/// reader scrolls its body, the lists move their cursor — so the decision is
/// left to the caller. Only the press counts: a terminal that also reported
/// the release would otherwise move two lines per notch.
[[nodiscard]] inline int wheelDelta(const term::Event& event) {
    const auto mouse = mouseOf(event);
    if (!mouse || mouse->motion != term::MouseEvent::Motion::Pressed) return 0;
    if (mouse->button == term::MouseEvent::Button::WheelUp) return -1;
    if (mouse->button == term::MouseEvent::Button::WheelDown) return 1;
    return 0;
}

}  // namespace amberedit::ui
