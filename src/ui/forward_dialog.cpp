#include "ui/forward_dialog.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "i18n/i18n.hpp"
#include "ui/dialog_frame.hpp"
#include "ui/event_util.hpp"
#include "ui/theme.hpp"

namespace amberedit::ui::forward_dialog {

using namespace term;

namespace {

using Mode = AppState::ForwardPicker::Mode;

/// The three answers in the order they are drawn and stepped through: the one
/// that writes a message of one's own first, then the two that put this message
/// somewhere else — the destructive one of which is not the one the dialog opens
/// on.
constexpr Mode kModes[] = {Mode::Forward, Mode::Move, Mode::Copy};
constexpr int kModeCount = 3;

int indexOf(Mode mode) {
    for (int i = 0; i < kModeCount; ++i) {
        if (kModes[i] == mode) return i;
    }
    return 0;  // unreachable; a Mode is one of the three
}

/// Moves the selection along the row, stopping at neither end: three answers
/// side by side are a ring, and a user holding → is asking for the next one.
void step(AppState::ForwardPicker& picker, int delta) {
    const int next = (indexOf(picker.mode) + delta + kModeCount) % kModeCount;
    picker.mode = kModes[next];
}

/// The answer a letter names, or nothing. The initial of each, which is what the
/// hint line under the buttons prints.
std::optional<Mode> modeFor(const Event& event) {
    if (event == Event::Character('f') || event == Event::Character('F')) {
        return Mode::Forward;
    }
    if (event == Event::Character('m') || event == Event::Character('M')) {
        return Mode::Move;
    }
    if (event == Event::Character('c') || event == Event::Character('C')) {
        return Mode::Copy;
    }
    return std::nullopt;
}

}  // namespace

Element render(AppState& state, Element background) {
    AppState::ForwardPicker& picker = *state.forwardPicker;

    const bool tall = state.dialogTallButtons();
    const auto answer = [&](const std::string& label, Mode mode, term::Box& box) {
        // reflect() writes back where the button landed once the box has been
        // centred, which is what handleEvent() hit-tests a click on.
        return dialog::button(label, picker.mode == mode,
                              state.isPressed(AppState::Pressed::ForwardChoice,
                                              static_cast<uint32_t>(indexOf(mode))),
                              tall) |
               reflect(box);
    };

    Elements buttons{
        answer(_("Forward"), Mode::Forward, picker.forwardBox), text("   "),
        answer(_("Move"), Mode::Move, picker.moveBox),          text("   "),
        answer(_("Copy"), Mode::Copy, picker.copyBox),
    };

    auto content = vbox({
        text(_("Message actions:")) | bold | color(theme::palette.dialogText) |
            center,
        text(""),
        hbox(std::move(buttons)) | center,
        text(""),
        text(_("←→ choose · Enter confirm · f/m/c · Esc cancel")) |
            color(theme::palette.dialogHint),
    });

    // The frame is drawn round a padded box: without the margins the hint line
    // sets the width and ends up flush against the border.
    auto box = hbox({text("  "), std::move(content), text("  ")}) | border |
               color(theme::palette.dialogBorder);

    // dialog::surface() wipes the screen behind the box and lays the dialog's
    // own fill down in its place, so the message underneath neither shows
    // through it nor colors it.
    return dbox({std::move(background), dialog::surface(std::move(box)) | center});
}

Outcome handleEvent(AppState& state, const Event& event) {
    AppState::ForwardPicker& picker = *state.forwardPicker;

    // A click answers with the button it landed on, without selecting it first:
    // pointing at Copy and pressing is one gesture, not two.
    if (const auto click = leftClick(event)) {
        // Selected first and shown pressed after, so that the button the pointer
        // landed on is the current one for the length of the animation before
        // the area dialog takes the screen.
        const auto pick = [&](Mode mode) {
            picker.mode = mode;
            state.showClick(AppState::Pressed::ForwardChoice,
                            static_cast<uint32_t>(indexOf(mode)));
            return Outcome::Picked;
        };
        if (picker.forwardBox.Contain(click->x, click->y)) return pick(Mode::Forward);
        if (picker.moveBox.Contain(click->x, click->y)) return pick(Mode::Move);
        if (picker.copyBox.Contain(click->x, click->y)) return pick(Mode::Copy);
        // Anywhere else, inside the box or outside it: swallowed, as every other
        // event is while the dialog is modal.
        return Outcome::Ignored;
    }

    // The initials answer outright, the way y and n answer a confirmation.
    if (const auto typed = modeFor(event)) {
        picker.mode = *typed;
        return Outcome::Picked;
    }
    if (event == Event::ArrowRight || event == Event::Tab) {
        step(picker, 1);
        return Outcome::Ignored;
    }
    if (event == Event::ArrowLeft || event == Event::TabReverse) {
        step(picker, -1);
        return Outcome::Ignored;
    }
    if (event == Event::Return) return Outcome::Picked;
    if (event == Event::Escape || event == Event::Backspace) {
        state.forwardPicker.reset();
        return Outcome::Dismissed;
    }
    return Outcome::Ignored;
}

}  // namespace amberedit::ui::forward_dialog
