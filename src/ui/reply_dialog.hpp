#pragma once

#include "ui/term/event.hpp"
#include "ui/term/element.hpp"

#include "ui/app_state.hpp"

/// The list of messages answering the one being read, shown when `+` has more
/// than one of them to choose between. One answer needs no dialog, and none
/// needs no list.
namespace amberedit::ui::reply_dialog {

enum class Outcome {
    Ignored,    ///< the key means nothing here, and the dialog stays up
    Dismissed,  ///< the user backed out; the dialog is already closed
    Picked,     ///< the row under state.replyChoice is the answer
};

/// Draws the list over whatever the screen was showing. Not const: where each
/// row lands is written back as it is laid out, so that a click is tested
/// against what was drawn.
term::Element render(AppState& state, term::Element background);

/// Handles a key while the list is up. It is modal, so every key is consumed —
/// the outcome only says what it meant.
Outcome handleEvent(AppState& state, const term::Event& event);

}  // namespace amberedit::ui::reply_dialog
