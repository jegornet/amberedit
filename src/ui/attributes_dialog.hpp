#pragma once

#include "ui/app_state.hpp"
#include "ui/term/element.hpp"
#include "ui/term/event.hpp"

/// The attributes of the message being written, set over its header.
///
/// The compose screen shows what the message carries — `[Uns Pvt Loc]`, under
/// the addresses, where the reader shows the same thing — and those attributes
/// are themselves the button that opens this: pointing at them, Enter or Space
/// with the typing on them, or `Ctrl-F` from anywhere on the screen. Here they
/// are changed, a checkbox each, turned over by pointing at one or by the chord
/// written beside it. It is modal while it is up, and every chord is its own
/// while it is — which is what lets Ctrl-C be Crash here.
namespace amberedit::ui::attributes_dialog {

/// Opens it on the message in `state`.
void open(AppState& state);

/// Draws it over `background`.
[[nodiscard]] term::Element render(AppState& state, term::Element background);

/// Answers a key or a click. The dialog closes itself — `state.attributePicker`
/// is empty afterwards — so there is nothing for the caller to do but stop
/// sending it events.
void handleEvent(AppState& state, const term::Event& event);

}  // namespace amberedit::ui::attributes_dialog
