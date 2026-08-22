#include "ui/error_dialog.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include "ui/dialog_frame.hpp"
#include "ui/event_util.hpp"
#include "ui/text_layout.hpp"
#include "ui/theme.hpp"

namespace amberedit::ui::error_dialog {

using namespace term;

namespace {

/// How wide the box is inside its margins, at most. A message naming a path is
/// as long as the path, and a box as wide as the window would read as a screen
/// rather than as something standing over one; a narrower window still wins.
constexpr int kLineWidth = 56;

/// The button, drawn selected because it is the only thing here to press —
/// there is no choice to make, only something to acknowledge. `pressed` is a
/// click on it being shown before it is acted on, exactly as the confirmation
/// shows one.
Element button(bool pressed) {
    auto element = text("  OK  ");
    if (pressed) element = std::move(element) | color(theme::palette.dialogFlash);
    return std::move(element) | bold | color(theme::palette.selectionText) |
           bgcolor(theme::palette.selection);
}

}  // namespace

Element render(AppState& state, Element background) {
    const int width = std::max(1, std::min(kLineWidth, state.width - 6));

    // Wrapped rather than cut: what went wrong is usually a path with a reason
    // behind it, and the reason is the half that would be lost off the right
    // edge. The box grows downwards instead, which the screen has room for.
    Elements content;
    for (const auto& line : wrapText(state.errorMessage, width)) {
        content.push_back(text(padRight(line, width)) | color(theme::palette.error));
    }
    content.push_back(text(""));
    Element ok = button(state.isPressed(AppState::Pressed::ErrorOk));
    content.push_back(std::move(ok) | reflect(state.errorOkBox) | center);
    content.push_back(text(""));
    content.push_back(text("Enter · Esc") | color(theme::palette.dialogHint) | center);

    // The same frame round the same margins as the other two boxes: one of them
    // looking like the others is what makes any of them read as a box.
    auto box = hbox({text("  "), vbox(std::move(content)), text("  ")}) | border |
               color(theme::palette.dialogBorder);

    return dbox({std::move(background), dialog::surface(std::move(box)) | center});
}

void handleEvent(AppState& state, const Event& event) {
    // Acknowledged, whichever way round. Where it leaves the user is
    // `errorEndsScreen`: the area list where the box stands in place of a
    // screen that did not open, since the stack has nothing on it worth coming
    // back to, and the screen underneath where that screen is still there and
    // the box only had something to report about it.
    const auto acknowledge = [&state] {
        state.errorMessage.clear();
        if (state.errorEndsScreen) state.navigator.reset();
        state.errorEndsScreen = true;
    };

    if (const auto click = leftClick(event)) {
        if (state.errorOkBox.Contain(click->x, click->y)) {
            state.showClick(AppState::Pressed::ErrorOk);
            acknowledge();
        }
        // Anywhere else, inside the box or outside it: swallowed, as every
        // other event is while the box is modal.
        return;
    }
    if (event == Event::Return || event == Event::Escape ||
        event == Event::Character(' ') || event == Event::Character('o') ||
        event == Event::Character('O')) {
        acknowledge();
    }
}

}  // namespace amberedit::ui::error_dialog
