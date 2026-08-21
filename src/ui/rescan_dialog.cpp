#include "ui/rescan_dialog.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include "ui/text_layout.hpp"
#include "ui/theme.hpp"

namespace amberedit::ui::rescan_dialog {

using namespace term;

namespace {

/// How wide the box is inside its margins.
///
/// Fixed, rather than following the area being named: the line changes with
/// every base opened, and a box that changed width with it would jump about the
/// screen for the whole rescan. Long tags are cut instead, which loses the end
/// of a name that is on the screen for a fraction of a second.
constexpr int kLineWidth = 40;

}  // namespace

Element render(const AppState& state, Element background) {
    // Before the first area is reached there is no area to name: what is being
    // waited on then is the tosser config itself.
    const std::string doing = state.rescanArea.empty()
                                  ? std::string("reading the tosser config")
                                  : "reading " + state.rescanArea;

    const int width = std::max(1, std::min(kLineWidth, state.width - 6));
    auto content = vbox({
        text(truncateToWidth("Rescanning areas...", width)) | bold |
            color(theme::palette.text) | center,
        text(""),
        text(padRight(truncateToWidth(doing, width), width)) |
            color(theme::palette.footer),
    });

    // The same frame round the same margins as the confirmation: the two are the
    // only boxes drawn over a screen, and one of them looking like the other is
    // what makes either read as a box rather than as part of the list.
    auto dialog = hbox({text("  "), std::move(content), text("  ")}) | border |
                  color(theme::palette.separator);

    return dbox({std::move(background), std::move(dialog) | clear_under | center});
}

}  // namespace amberedit::ui::rescan_dialog
