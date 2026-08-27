#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "ui/term/event.hpp"

/// The number typed to be taken to a message, which both screens showing an
/// area take: the reader, where it stands in the columns `12/44` had, and the
/// message list, where it stands in the columns of its own pair. The field has
/// no state beyond the digits typed into it — `AppState::readGoto` and
/// `AppState::listGoto` — and this is what the two of them share, so that a
/// number is typed the same way wherever it is typed.
namespace amberedit::ui::goto_field {

/// How many digits the field takes. Nine, which is more than the messages of
/// any base ever written, and few enough that what has been typed cannot
/// overflow the `uint32_t` a message number is — a base holding 999999999
/// messages is not the case being guarded against here.
constexpr size_t kDigits = 9;

/// The digit an event types into the field, if it types one. Digits and
/// nothing else: the field is a message number, and every other key means what
/// it means on the screen it was typed at.
[[nodiscard]] inline std::optional<char> digitOf(const term::Event& event) {
    if (!event.is_character() || event.input().size() != 1) return std::nullopt;
    if (event.ctrl() || event.alt()) return std::nullopt;
    const char c = event.input()[0];
    if (c < '0' || c > '9') return std::nullopt;
    return c;
}

/// The number the digits come to. An empty field comes to nought, which names
/// no message and is answered as any other number naming none is.
[[nodiscard]] inline uint32_t numberOf(const std::string& digits) {
    uint32_t number = 0;
    for (const char digit : digits) {
        number = number * 10 + static_cast<uint32_t>(digit - '0');
    }
    return number;
}

}  // namespace amberedit::ui::goto_field
