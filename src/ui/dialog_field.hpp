#pragma once

#include <cstddef>
#include <string_view>

#include "ui/app_state.hpp"
#include "ui/text_field.hpp"

/// The fields of the boxes that stand over a screen — find, charset, import,
/// export — and what they are answered with from `AppState`: the layout, the
/// clock, and the clipboard `clipboard` names.
namespace amberedit::ui::dialog_field {

/// The context a field in a box over a screen is answered with. A copy goes to
/// AmberEdit's own clipboard here and now and to any other by way of the shell,
/// as the editor's does; so does a paste, which then comes back through
/// `paste()` below.
[[nodiscard]] FieldContext contextOf(AppState& state, size_t limit = 0);

/// The field the typing is in, in whichever of those boxes is up, or nothing
/// where none is or the typing is elsewhere in it.
[[nodiscard]] TextField* focused(AppState& state);

/// What a paste the shell fetched brought, into that field.
void paste(AppState& state, std::string_view text);

}  // namespace amberedit::ui::dialog_field
