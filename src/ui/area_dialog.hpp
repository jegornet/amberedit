#pragma once

#include "ui/app_state.hpp"
#include "ui/term/element.hpp"
#include "ui/term/event.hpp"

/// The dialog that asks which area a message is to be written into — what the
/// reader's `n` and `m`, and the `reply_to` and `forward` buttons, put on the
/// screen. It shows the areas by name and nothing else: the counts and the
/// groups are the area list's business, and what is being chosen here is a
/// destination. Which of the two asked is `AreaPicker::purpose`, and the title
/// says it.
///
/// The list is the manager's own, drawn in its order, so the areas stand here
/// exactly as they do on the area list screen — `arealist_sort` orders both,
/// there being only one list.
namespace amberedit::ui::area_dialog {

/// What a key or a click did while the dialog was up.
enum class Outcome {
    Ignored,    ///< moved about inside the dialog, or meant nothing here
    Picked,     ///< an area was chosen; the picker's cursor names it
    Dismissed,  ///< the dialog is gone and no area was picked
};

/// The picker drawn over `background`, and the rows recorded as they land so
/// that a click can be answered on the next event.
[[nodiscard]] term::Element render(AppState& state, term::Element background);

/// Answers a key or a click while the dialog is up. Everything else is
/// swallowed: the dialog is modal, as the list of replies is.
Outcome handleEvent(AppState& state, const term::Event& event);

}  // namespace amberedit::ui::area_dialog
