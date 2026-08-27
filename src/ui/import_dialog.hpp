#pragma once

#include "ui/app_state.hpp"
#include "ui/term/element.hpp"
#include "ui/term/event.hpp"

/// The dialog that reads a file into the message being written — what the
/// editor's Ctrl-O and F3, and the `import` button beside it, put on the screen.
///
/// One box asks the whole question: which file, and how it is to go in. The
/// path is typed into, the files under it are the directory it names — walked
/// with the arrows and searched by typing a name the way the area list is — and
/// the mode below them is Text or UUE. Tab walks the three, and Enter acts
/// wherever the typing is, since by then everything has been said.
///
/// **There is no charset to choose.** A text file is decoded out of the one the
/// locale names, which is what a file on this machine is written in — the
/// terminal is being read in it, and a box asking again would be one more
/// question with one answer.
///
/// The reading happens here rather than in the caller, because this is what is
/// still on the screen when it fails: a file that will not open or a charset
/// iconv does not know is a line inside the box, and the dialog stays up on the
/// answer that has just been corrected.
namespace amberedit::ui::import_dialog {

/// What a key or a click did while the dialog was up.
enum class Outcome {
    Ignored,    ///< moved about inside the dialog, or meant nothing here
    Imported,   ///< a file was read; `ImportPicker::lines` is what it came to
    Dismissed,  ///< the dialog is gone and nothing was read
};

/// Opens it over the message being written.
void open(AppState& state);

/// The dialog drawn over `background`, its rows and controls recorded as they
/// land so that a click can be answered on the next event.
[[nodiscard]] term::Element render(AppState& state, term::Element background);

/// Answers a key or a click while the dialog is up. Everything else is
/// swallowed: the dialog is modal, as every other one over the editor is.
Outcome handleEvent(AppState& state, const term::Event& event);

}  // namespace amberedit::ui::import_dialog
