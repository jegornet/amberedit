#include "ui/clipboard_dialog.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include "i18n/i18n.hpp"
#include "ui/dialog_frame.hpp"
#include "ui/text_layout.hpp"
#include "ui/theme.hpp"

namespace amberedit::ui::clipboard_dialog {

using namespace term;

namespace {

/// The rescan box's frame and margins round what the box says: these say what
/// is being done rather than ask anything, and a box that looked like a
/// question would be read as one.
Element boxed(Element content, Element background) {
    auto box = hbox({text("  "), std::move(content), text("  ")}) | border |
               color(theme::palette.dialogBorder);
    return dbox({std::move(background), dialog::surface(std::move(box)) | center});
}

}  // namespace

Element renderCopied(const AppState& state, Element background) {
    const int width = std::max(1, state.width - 6);
    return boxed(text(truncateToWidth(_("Copied"), width)) | bold |
                     color(theme::palette.dialogText),
                 std::move(background));
}

Element renderWaiting(const AppState& state, Element background) {
    const int width = std::max(1, state.width - 6);
    const std::string asking = _("Waiting for the terminal's clipboard...");
    const std::string hint = _("Esc cancel");
    const int inner = std::min(width, std::max(displayWidth(asking), displayWidth(hint)));
    return boxed(vbox({
                     text(truncateToWidth(asking, inner)) | bold |
                         color(theme::palette.dialogText) | center,
                     text(""),
                     text(truncateToWidth(hint, inner)) |
                         color(theme::palette.dialogHint) | center,
                 }),
                 std::move(background));
}

}  // namespace amberedit::ui::clipboard_dialog
