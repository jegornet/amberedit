#pragma once

#include <vector>

#include "app/export_file.hpp"
#include "ui/app_state.hpp"
#include "ui/term/element.hpp"
#include "ui/term/event.hpp"

/// The dialog that writes the message being read out to a text file — what the
/// reader's w, and the `export` button beside it, put on the screen.
///
/// It is the import dialog's box with the answers the other way round. The path
/// is typed into, the listing under it holds **directories only** — what is
/// being picked there is somewhere to write, and a file in the way would only be
/// something to point at by mistake — and the row below is the name to write
/// under. Tab walks the three, Enter writes wherever the typing is except on a
/// directory row, which is walked into, and Esc closes.
///
/// **There is no charset and no format to choose.** A text file, in the charset
/// the locale names: the terminal is being read in it, and a file written beside
/// it will be read on the same machine.
///
/// **A run of marked messages is the same box asked the same way**, and what
/// differs is only how many messages go into the file the name picks: they are
/// written in the order they stand in the area, the first under whatever the
/// question below was answered with and every one after it appended, since each
/// writing afresh would leave the file holding the last of them alone.
///
/// **A file already standing under the name is a question**, put in a box of its
/// own over this one: **Overwrite**, **Append**, or Esc for neither, which leaves
/// the dialog exactly as it was with the name there to be typed over. It used to
/// append without asking, which is right for collecting one message after another
/// into a digest and quietly wrong for every other reason a name is typed twice —
/// and writing over is wrong exactly the other way round. Neither is a default
/// worth having, so neither is one.
///
/// **The files a message carries are the same box asked the same way**, once
/// `ui/export_mode_dialog.*` has been answered with them: the path and the
/// listing stand exactly as they do for a text export, and the row that would
/// hold the name to type holds the names the message gave the files instead —
/// a block that is stepped onto and pressed like a button rather than typed
/// into. There is one thing the two modes do not share: a decoded file is
/// **never written over one already there**, since those names are not the
/// user's and nothing here can change them.
namespace amberedit::ui::export_dialog {

/// What a key or a click did while the dialog was up.
enum class Outcome {
    Ignored,    ///< moved about inside the dialog, or meant nothing here
    Written,    ///< the message is in the file and the dialog is done
    Dismissed,  ///< the dialog is gone and nothing was written
};

/// Opens it over the message on screen. Does nothing where there is none — an
/// empty area has nothing to export, and the button for it is drawn dimmed.
///
/// `files` is what the message carries uuencoded, where that was asked about and
/// answered with them; empty is the ordinary text export, which is what a
/// message carrying no file has for an answer without being asked.
///
/// `marked` is the scope box's answer carried in: the marked messages are written
/// into the one file the box names, one after another in the order they stand in
/// the area, each under the rule that keeps two of them apart. It is only ever
/// true for a text export — the files were decoded out of the message on screen,
/// and there is no set of them to write.
void open(AppState& state, std::vector<app::UueFile> files = {}, bool marked = false);

/// The dialog drawn over `background`, its rows and controls recorded as they
/// land so that a click can be answered on the next event.
[[nodiscard]] term::Element render(AppState& state, term::Element background);

/// Answers a key or a click while the dialog is up. Everything else is
/// swallowed: the dialog is modal, as every other one is.
Outcome handleEvent(AppState& state, const term::Event& event);

}  // namespace amberedit::ui::export_dialog
