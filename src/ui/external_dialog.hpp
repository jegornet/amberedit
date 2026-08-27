#pragma once

#include "ui/app_state.hpp"
#include "ui/term/element.hpp"
#include "ui/term/event.hpp"

/// The box that asks what is to become of the message an editor of the user's
/// own has just handed back — what comes up over the compose screen every time
/// `external_editor`'s program is left having written something.
///
/// Four answers, where every other box here has two: **Save**, which stores the
/// message as `Ctrl-S` would; **Discard**, which leaves the editor with nothing
/// stored; **Continue**, which opens the same file in the same editor again;
/// and **Header**, which puts the typing into the block above the message.
///
/// The fourth is what makes the count four rather than three, and it is not a
/// luxury: with the writing done elsewhere there is no cursor on this screen to
/// walk up into the header with, so the way to the fields has to be a button
/// like the rest. Esc is that same answer — it is the one that drops nothing and
/// stores nothing, and leaves every other route still open underneath.
///
/// The message stands behind the box and is worth reading while it is up, so
/// `↑`, `↓`, the page keys and the wheel scroll it; `←` and `→` walk the
/// buttons.
namespace amberedit::ui::external_dialog {

/// What a key or a click did while the box was up.
enum class Outcome {
    Ignored,  ///< moved about inside the box, scrolled the message, or meant nothing
    Picked,   ///< an answer was given; the review's `answer` names it
};

/// Draws it over whatever the screen was showing. Not const: where the four
/// buttons landed is written back as they are laid out, so that a click is
/// tested against what was drawn.
[[nodiscard]] term::Element render(AppState& state, term::Element background);

/// Answers a key or a click while the box is up. Everything else is swallowed:
/// it is modal, as every other box over the editor is.
Outcome handleEvent(AppState& state, const term::Event& event);

}  // namespace amberedit::ui::external_dialog
