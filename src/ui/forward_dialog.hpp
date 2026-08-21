#pragma once

#include "ui/app_state.hpp"
#include "ui/term/element.hpp"
#include "ui/term/event.hpp"

/// The dialog that asks what is to become of the message on screen — what the
/// reader's `m`, and the `forward` button beside it, put up first. Three
/// answers: **Forward**, a new message of one's own carrying this one;
/// **Move**, this very message in another area and gone from here; and
/// **Copy**, this very message in another area and standing here still.
///
/// It asks what and not where. Whichever answer is given, the area dialog
/// follows it, and picking an area there is what acts — so the two boxes read as
/// one question in two halves, and Esc on either leaves the reader as it was.
namespace amberedit::ui::forward_dialog {

/// What a key or a click did while the dialog was up.
enum class Outcome {
    Ignored,    ///< moved about inside the dialog, or meant nothing here
    Picked,     ///< an answer was given; the picker's mode names it
    Dismissed,  ///< the dialog is gone and nothing was chosen
};

/// Draws the dialog over whatever the screen was showing. Not const: where the
/// three buttons landed is written back as they are laid out, so that a click is
/// tested against what was drawn.
[[nodiscard]] term::Element render(AppState& state, term::Element background);

/// Answers a key or a click while the dialog is up. Everything else is
/// swallowed: the dialog is modal, as the area picker after it is.
Outcome handleEvent(AppState& state, const term::Event& event);

}  // namespace amberedit::ui::forward_dialog
