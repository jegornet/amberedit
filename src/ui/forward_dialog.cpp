#include "ui/forward_dialog.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "config/text_util.hpp"
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

/// The two the box offers for a set of marked messages, in the order they are
/// drawn: **Forward is not among them**. A forward is a message of one's own
/// with another quoted inside it, and there is no one message a hundred of them
/// could be written into. Copy leads, being the one that takes nothing out of
/// the area.
constexpr Mode kMarkedModes[] = {Mode::Copy, Mode::Move};
constexpr int kMarkedModeCount = 2;

/// What this box is offering, and how many: the three, or the two a marked set
/// leaves.
const Mode* modesOf(const AppState::ForwardPicker& picker) {
    return picker.marked ? kMarkedModes : kModes;
}
int modeCountOf(const AppState::ForwardPicker& picker) {
    return picker.marked ? kMarkedModeCount : kModeCount;
}

/// Where the answer stands among the ones being offered — which is also what
/// `Pressed::ForwardChoice` numbers a button by, so the two rings cannot show a
/// click on the wrong box.
int indexOf(const AppState::ForwardPicker& picker, Mode mode) {
    const Mode* modes = modesOf(picker);
    for (int i = 0; i < modeCountOf(picker); ++i) {
        if (modes[i] == mode) return i;
    }
    return 0;  // unreachable; a Mode is one of the ones being offered
}

/// Moves the selection along the row, stopping at neither end: the answers side
/// by side are a ring, and a user holding → is asking for the next one.
void step(AppState::ForwardPicker& picker, int delta) {
    const int count = modeCountOf(picker);
    const int next = (indexOf(picker, picker.mode) + delta + count) % count;
    picker.mode = modesOf(picker)[next];
}

/// The answer a letter names, or nothing. The initial of each, which is what the
/// hint line under the buttons prints — and only of the ones being offered, so
/// `f` says nothing to a box that is not showing Forward.
std::optional<Mode> modeFor(const AppState::ForwardPicker& picker, const Event& event) {
    const auto named = [&event](char letter, Mode mode) -> std::optional<Mode> {
        if (event == Event::Character(letter) ||
            event == Event::Character(config::text::asciiUpper(letter))) {
            return mode;
        }
        return std::nullopt;
    };
    std::optional<Mode> typed = named('m', Mode::Move);
    if (!typed) typed = named('c', Mode::Copy);
    if (!typed && !picker.marked) typed = named('f', Mode::Forward);
    return typed;
}

}  // namespace

Element render(AppState& state, Element background) {
    AppState::ForwardPicker& picker = *state.forwardPicker;

    // Nowhere until the frame puts each of them somewhere: a default Box holds
    // the screen's top-left cell, so an answer this box is not offering would
    // otherwise take a click in the corner.
    picker.forwardBox = Box::Nowhere();
    picker.moveBox = Box::Nowhere();
    picker.copyBox = Box::Nowhere();

    const bool tall = state.dialogTallButtons();
    const auto answer = [&](const std::string& label, Mode mode, term::Box& box) {
        // reflect() writes back where the button landed once the box has been
        // centred, which is what handleEvent() hit-tests a click on.
        return dialog::button(
                   label, picker.mode == mode,
                   state.isPressed(AppState::Pressed::ForwardChoice,
                                   static_cast<uint32_t>(indexOf(picker, mode))),
                   tall) |
               reflect(box);
    };

    // The buttons the answers being offered come to, in their order. The
    // Forward box is left where it was — `Nowhere()`, which no click is inside —
    // rather than being drawn for a set that cannot be forwarded.
    Elements buttons;
    const Mode* modes = modesOf(picker);
    for (int i = 0; i < modeCountOf(picker); ++i) {
        if (i > 0) buttons.push_back(text("   "));
        switch (modes[i]) {
            case Mode::Forward:
                buttons.push_back(answer(_("Forward"), Mode::Forward, picker.forwardBox));
                break;
            case Mode::Move:
                buttons.push_back(answer(_("Move"), Mode::Move, picker.moveBox));
                break;
            case Mode::Copy:
                buttons.push_back(answer(_("Copy"), Mode::Copy, picker.copyBox));
                break;
        }
    }

    auto content = vbox({
        text(picker.marked ? _("Marked message actions:") : _("Message actions:")) |
            bold | color(theme::palette.dialogText) | center,
        text(""),
        hbox(std::move(buttons)) | center,
        text(""),
        text(picker.marked ? _("←→ choose · Enter confirm · c/m · Esc cancel")
                           : _("←→ choose · Enter confirm · f/m/c · Esc cancel")) |
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
                            static_cast<uint32_t>(indexOf(picker, mode)));
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
    if (const auto typed = modeFor(picker, event)) {
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
