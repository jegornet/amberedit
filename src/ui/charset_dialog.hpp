#pragma once

#include "ui/app_state.hpp"
#include "ui/term/element.hpp"
#include "ui/term/event.hpp"

/// The dialog that asks which charset the message on screen is to be read in —
/// what the reader's Alt-E, and the `charset` button in its menu, put up.
///
/// It says what the message is being read in and where that came from — its own
/// CHRS kludge, the area's `default_charset`, or an earlier answer to this same
/// box — and asks for a name in the field under it. Enter reads the message
/// again in what was typed; the **Read** button under the field does the same
/// thing, and is there for the pointer.
///
/// **The name is checked here and the message is read there.** A name iconv
/// cannot open leaves the box standing with the name still in it and the reason
/// in its bottom rule — the find box's habit, and for the same reason: a box
/// that vanished would leave the user to open it again and type the name
/// afresh. What comes back to the shell is a charset iconv has already been
/// asked about, under the name iconv knows it by, and the reader does the
/// reading.
///
/// What it asks for lives as long as the message is on the screen: see
/// `AppState::readCharset`.
namespace amberedit::ui::charset_dialog {

/// What a key or a click did while the dialog was up.
enum class Outcome {
    Ignored,    ///< moved about inside the dialog, or meant nothing here
    Read,       ///< read the message in what the field holds
    Dismissed,  ///< the dialog is gone and nothing was asked for
};

/// Puts it up, holding the charset the message on screen is being read in.
/// Does nothing where there is no message to read — an empty area has none, and
/// the button for this is drawn dimmed there.
void open(AppState& state);

/// Draws it over whatever the screen was showing. Not const: where the field
/// and the button landed is written back as they are laid out, so that a click
/// is tested against what was drawn.
[[nodiscard]] term::Element render(AppState& state, term::Element background);

/// Answers a key or a click while the dialog is up. Everything else is
/// swallowed: the dialog is modal, as every other one here is.
///
/// `Outcome::Read` leaves the field holding the charset **under iconv's name for
/// it** — what was typed, resolved the way a `default_charset` line is resolved,
/// so that `+7_FIDO` and `866` come back as CP866 — which is what the shell
/// takes off the box and hands to the reader.
Outcome handleEvent(AppState& state, const term::Event& event);

}  // namespace amberedit::ui::charset_dialog
