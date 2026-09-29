#pragma once

#include "ui/app_state.hpp"
#include "ui/keys.hpp"

namespace amberedit::ui {

/// Whether the screen that is up can do that at all, as it stands.
///
/// One case, and it is the editor's: `external_editor` takes the internal
/// editor away entirely, so every command that edits the text of a message has
/// nothing left to act on. What edits a line rather than the message is not
/// among them — the ends of a line and the word keys act on a header field,
/// which is where the typing goes there.
///
/// Asked by the two places that write a key down for the user to press: the
/// hint bar, where the row is short and a key that does nothing is not worth a
/// column of it, and the help box, which would otherwise say what a key does
/// while the key does nothing. One question asked once, so the two cannot come
/// to different answers about the same keyboard.
[[nodiscard]] inline bool commandLive(const AppState& state, Command command) {
    if (!state.externalEditing()) return true;
    switch (command) {
        case Command::ComposeImport:
        case Command::ComposeDeleteLine:
        case Command::ComposeRestoreLine:
        case Command::ComposeDeleteQuote: return false;
        default: return true;
    }
}

}  // namespace amberedit::ui
