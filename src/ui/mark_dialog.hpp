#pragma once

#include "ui/app_state.hpp"
#include "ui/term/element.hpp"
#include "ui/term/event.hpp"

/// The mark box the reader's `s` opens: the five things that can be done to a
/// whole area's worth of marks at once, one button each.
///
/// It is the only way to any of them. Marking one message is a key —
/// `reader.mark_toggle` and `msglist.mark_toggle` — and there is nothing else to
/// press it for; marking a hundred is a thing to be asked about, and this is
/// where the asking is.
///
/// Three of the five read the message the reader is standing on, so the box is
/// the reader's alone: "after this one" is a sentence with nothing behind it on a
/// screen showing no message.
namespace amberedit::ui::mark_dialog {

/// What a key or a click did while the box was up.
enum class Outcome {
    Ignored,    ///< moved about inside it, or meant nothing here
    Picked,     ///< an answer was chosen; `AppState::MarkPicker::action` says which
    Dismissed,  ///< the box is gone and nothing was chosen
};

/// Puts it up over the reader. Does nothing where there is no message to count
/// from — an empty area has nothing to mark and nowhere to mark from.
void open(AppState& state);

/// Carries out whichever answer was picked, on the area being read. The shell
/// puts the box away first: what these do is done to the state the box was
/// standing over.
void apply(AppState& state, AppState::MarkPicker::Action action);

/// Draws it over whatever the screen was showing. Not const: where each button
/// landed is written back as they are laid out, so that a click is tested
/// against what was drawn.
[[nodiscard]] term::Element render(AppState& state, term::Element background);

/// Answers a key or a click while it is up. Modal, as every other box is.
Outcome handleEvent(AppState& state, const term::Event& event);

}  // namespace amberedit::ui::mark_dialog
