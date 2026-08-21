#pragma once

#include "ui/app_state.hpp"
#include "ui/term/element.hpp"
#include "ui/term/event.hpp"

/// What the base holds about the message being read, over the reader: the
/// stored header field by field, the records naming it, and a hexdump of the
/// bytes those records are made of — what `i` opens and what the `info`
/// menu command runs.
///
/// It is a window onto the storage and nothing else: nothing in it can be
/// changed, and the report is read once, when the box opens. Eighty columns
/// wide at most, because that is what a hexdump of sixteen bytes to the row
/// takes, and narrower where the window is — the dump then carries eight bytes
/// to the row, or four, rather than running off the edge.
namespace amberedit::ui::info_dialog {

/// Opens it on the message the reader is showing. Does nothing where there is
/// none, which is what an empty area leaves.
void open(AppState& state);

/// Draws it over `background`.
[[nodiscard]] term::Element render(AppState& state, term::Element background);

/// Answers a key or a click. The box closes itself — `state.infoView` is empty
/// afterwards — so there is nothing for the caller to do but stop sending it
/// events.
void handleEvent(AppState& state, const term::Event& event);

}  // namespace amberedit::ui::info_dialog
