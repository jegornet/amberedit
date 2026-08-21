#pragma once

#include <vector>

#include "app/export_file.hpp"
#include "ui/app_state.hpp"
#include "ui/term/element.hpp"
#include "ui/term/event.hpp"

/// The dialog that asks what an export of *this* message is to be — what w, and
/// the `export` button beside it, put up where the message carries uuencoded
/// files. Two answers: **Files**, the files taken back out of the message and
/// written whole, and **Text**, the message written out as it is read, which is
/// what an export has always been.
///
/// It asks what and not where, and the export dialog follows it either way: the
/// two boxes are one question in two halves, and Esc on either leaves the reader
/// as it was. Where the message carries no file there is no question to ask and
/// this box never comes up.
///
/// The names are listed and cannot be typed over. They are the message's, not
/// the user's — a decoded file under a name of somebody else's choosing is what
/// was sent, and a box offering to change them would be offering to rename a
/// thing whose name is part of it. Where there are more than the box shows, the
/// last row says how many are left.
namespace amberedit::ui::export_mode_dialog {

/// What a key or a click did while the dialog was up.
enum class Outcome {
    Ignored,    ///< moved about inside the dialog, or meant nothing here
    Picked,     ///< an answer was given; the picker's mode names it
    Dismissed,  ///< the dialog is gone and nothing was chosen
};

/// Opens it over the message on screen, on the files already decoded out of it.
/// Does nothing where there are none: the box would be asking about nothing.
void open(AppState& state, std::vector<app::UueFile> files);

/// Draws it over whatever the screen was showing. Not const: where the two
/// buttons landed is written back as they are laid out, so that a click is
/// tested against what was drawn.
[[nodiscard]] term::Element render(AppState& state, term::Element background);

/// Answers a key or a click while the dialog is up. Everything else is
/// swallowed: the dialog is modal, as the export dialog after it is.
Outcome handleEvent(AppState& state, const term::Event& event);

}  // namespace amberedit::ui::export_mode_dialog
