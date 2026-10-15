#pragma once

#include "ui/app_state.hpp"
#include "ui/term/element.hpp"

/// The two boxes a clipboard puts over the screen. Neither answers a key or
/// carries a button: each is the frame `runApp()` draws while it is busy with
/// something the user has to be told about, and is taken down when that ends.
namespace amberedit::ui::clipboard_dialog {

/// How long the box after a copy stays up, in milliseconds. Long enough to be
/// read as a word and short enough not to be waited on; not a setting, there
/// being nothing behind the number a user would want to choose — the one choice
/// worth offering is whether it is shown at all, `clipboard_copy_indicate`.
inline constexpr int kCopiedMs = 500;

/// "Copied", over whichever screen asked. `runApp()` draws one frame with it,
/// holds that frame for `kCopiedMs` and takes it down. A key pressed meanwhile
/// is not lost — the terminal keeps it, and it is read once the box has gone,
/// against the screen it was aimed at.
term::Element renderCopied(const AppState& state, term::Element background);

/// The box standing while the terminal is asked for its clipboard under
/// `clipboard terminal`: the answer may be a person's click away — iTerm2 asks
/// every time unless told not to — and the screen would otherwise sit frozen
/// with nothing saying why or how to get out of it, which is Escape.
term::Element renderWaiting(const AppState& state, term::Element background);

}  // namespace amberedit::ui::clipboard_dialog
