#include "ui/progress_dialog.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include "i18n/i18n.hpp"
#include "ui/dialog_frame.hpp"
#include "ui/text_layout.hpp"
#include "ui/theme.hpp"

namespace amberedit::ui::progress_dialog {

using namespace term;

namespace {

using Doing = AppState::Progress::Doing;

/// How wide the box is inside its margins.
///
/// Fixed, and for the rescan box's reason: the counter changes ten times a
/// second, and a box that followed the width of its own numbers would grow
/// sideways under the user as the thousands column filled.
constexpr int kLineWidth = 40;

/// What the pass being counted is doing, as the box says it.
///
/// A `case` rather than a table, so that a pass added without a word for it is a
/// build that stops here. Copying and moving are two words for what is nearly
/// one pass because the user asked for one of the two and is owed the word they
/// used.
const char* headingOf(Doing doing) {
    switch (doing) {
        case Doing::Copy: return _("Copying messages...");
        case Doing::Move: return _("Moving messages...");
        case Doing::Delete: return _("Deleting messages...");
    }
    return "";  // unreachable; a Doing is one of the three
}

}  // namespace

Element render(const AppState& state, Element background) {
    const AppState::Progress& progress = *state.progress;

    const int width = std::max(1, std::min(kLineWidth, state.width - 6));
    // Which message of how many, in the one line the whole box is for. Both
    // numbers, rather than a share of the whole: a run is over messages the user
    // marked one by one, and how many are left is the thing they can act on.
    const std::string counted =
        i18n::format(_("message {0} of {1}"),
                     {std::to_string(progress.done), std::to_string(progress.total)});

    // The one key the box answers to, where the pass it is counting answers to
    // it: the half of a move that takes the messages out of this area once they
    // are written into the other one goes through whatever is pressed, and a
    // line offering a key that does nothing would be worse than no line. The row
    // stays, blank, so that the box does not change height under the user
    // between one pass and the next.
    const std::string keys = progress.breakable ? _("Esc cancel") : "";

    auto content = vbox({
        text(truncateToWidth(headingOf(progress.doing), width)) | bold |
            color(theme::palette.dialogText) | center,
        text(""),
        text(padRight(truncateToWidth(counted, width), width)) |
            color(theme::palette.dialogHint),
        text(""),
        text(padRight(truncateToWidth(keys, width), width)) |
            color(theme::palette.dialogHint),
    });

    // The same frame round the same margins as the rescan box and the
    // confirmation: three boxes that stand over a screen, and one of them
    // looking like the others is what makes any of them read as a box.
    auto box = hbox({text("  "), std::move(content), text("  ")}) | border |
               color(theme::palette.dialogBorder);

    return dbox({std::move(background), dialog::surface(std::move(box)) | center});
}

}  // namespace amberedit::ui::progress_dialog
