#pragma once

#include "ui/app_state.hpp"
#include "ui/term/element.hpp"
#include "ui/term/event.hpp"

/// The box that asks **which messages a key means** where anything in the area is
/// marked: the marked ones, the one on screen, or neither.
///
/// With nothing marked no key is ambiguous — there is only the message in front
/// of the user — and this box is never opened: `reader.delete` asks its ordinary
/// yes/no confirmation, and `reader.forward` and `reader.export` put their own
/// boxes up as they always have. Marks are what raise the question, so they are
/// what opens it.
///
/// Three keys ask it and the purpose is the whole of the difference: `d` deletes
/// what is answered, `m` sends it elsewhere and `w` writes it out. The wording of
/// the question follows the purpose; everything else about the box is the same
/// whichever asked, which is what makes it one box and not three.
///
/// **For `d` it is the confirmation as well as the question** — Cancel stands
/// where No would, and an answer of either of the other two deletes there and
/// then. For `m` it is the first of three boxes, the way the forward picker has
/// always been the first of two. For `w` it stands between the question about the
/// files the message carries and the box that picks where to write.
namespace amberedit::ui::scope_dialog {

/// What a key or a click did while the box was up.
enum class Outcome {
    Ignored,    ///< moved about inside it, or meant nothing here
    Picked,     ///< an answer was chosen; `AppState::ScopePicker::mode` says which
    Dismissed,  ///< the box is gone and nothing is to be done
};

/// Puts it up over the reader, on the marks as they stand. Does nothing where
/// there is no message on screen: neither key has anything to ask about then.
void open(AppState& state, AppState::ScopePicker::For purpose);

/// Draws it over whatever the screen was showing. Not const: where each button
/// landed is written back as they are laid out, so that a click is tested
/// against what was drawn.
[[nodiscard]] term::Element render(AppState& state, term::Element background);

/// Answers a key or a click while it is up. Modal, as every other box is.
Outcome handleEvent(AppState& state, const term::Event& event);

}  // namespace amberedit::ui::scope_dialog
