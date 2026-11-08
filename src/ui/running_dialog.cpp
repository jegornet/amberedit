#include "ui/running_dialog.hpp"

#include <algorithm>
#include <utility>

#include "i18n/i18n.hpp"
#include "ui/dialog_frame.hpp"
#include "ui/text_layout.hpp"
#include "ui/theme.hpp"

namespace amberedit::ui::running_dialog {

using namespace term;

namespace {

/// How wide the box is inside its margins: the rescan modal's width, so the two
/// boxes that stand over a blocked loop are one shape.
constexpr int kLineWidth = 40;

}  // namespace

Element render(const AppState& state, Element background) {
    const int width = std::max(1, std::min(kLineWidth, state.width - 6));
    auto content = vbox({
        text(truncateToWidth(_("Running..."), width)) | bold |
            color(theme::palette.dialogText) | center,
        text(""),
        text(padRight(truncateToWidth(state.externUtilRunning, width), width)) |
            color(theme::palette.dialogHint),
    });

    auto box = hbox({text("  "), std::move(content), text("  ")}) | border |
               color(theme::palette.dialogBorder);

    return dbox({std::move(background), dialog::surface(std::move(box)) | center});
}

}  // namespace amberedit::ui::running_dialog
