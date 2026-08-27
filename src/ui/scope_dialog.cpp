#include "ui/scope_dialog.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "i18n/i18n.hpp"
#include "ui/dialog_frame.hpp"
#include "ui/event_util.hpp"
#include "ui/theme.hpp"

namespace amberedit::ui::scope_dialog {

using namespace term;

namespace {

using Mode = AppState::ScopePicker::Mode;
using For = AppState::ScopePicker::For;

/// The three answers in the order they are drawn and stepped through: the set
/// that raised the question, the message the key has always meant, and the way
/// out of both.
constexpr Mode kModes[] = {Mode::Marked, Mode::Current, Mode::Cancel};
constexpr int kModeCount = 3;

int indexOf(Mode mode) {
    for (int i = 0; i < kModeCount; ++i) {
        if (kModes[i] == mode) return i;
    }
    return 0;  // unreachable; a Mode is one of the three
}

/// Moves the selection along the row, stopping at neither end: three answers
/// side by side are a ring, and a user holding → is asking for the next one.
void step(AppState::ScopePicker& picker, int delta) {
    const int next = (indexOf(picker.mode) + delta + kModeCount) % kModeCount;
    picker.mode = kModes[next];
}

/// The answer a letter names, or nothing — the initials of the two that act.
/// Cancel has none: Esc is the key that means it everywhere, and a letter for
/// it beside two that change the base is a letter to press by mistake.
std::optional<Mode> modeFor(const Event& event) {
    if (event == Event::Character('m') || event == Event::Character('M')) {
        return Mode::Marked;
    }
    if (event == Event::Character('c') || event == Event::Character('C')) {
        return Mode::Current;
    }
    return std::nullopt;
}

}  // namespace

/// The question, in the words the purpose asks it in. The rest of the box says
/// the same thing either way.
const char* question(For purpose) {
    switch (purpose) {
        case For::Delete: return _("Which messages are to be deleted?");
        case For::Forward: return _("Which messages are to go elsewhere?");
        case For::Export: return _("Which messages are to be written out?");
    }
    return "";  // unreachable; a For is one of the three
}

void open(AppState& state, For purpose) {
    if (state.base == nullptr || !state.readHeader) return;

    AppState::ScopePicker picker;
    picker.purpose = purpose;
    picker.marked = state.marks.size();
    // Nowhere until the frame puts each of them somewhere: a default Box holds
    // the screen's top-left cell, which is a click nobody meant for a button.
    picker.markedBox = Box::Nowhere();
    picker.currentBox = Box::Nowhere();
    picker.cancelBox = Box::Nowhere();
    state.scopePicker = picker;
}

Element render(AppState& state, Element background) {
    AppState::ScopePicker& picker = *state.scopePicker;

    const bool tall = state.dialogTallButtons();
    const auto answer = [&](const std::string& label, Mode mode, term::Box& box) {
        // reflect() writes back where the button landed once the box has been
        // centred, which is what handleEvent() hit-tests a click on.
        return dialog::button(label, picker.mode == mode,
                              state.isPressed(AppState::Pressed::ScopeChoice,
                                              static_cast<uint32_t>(indexOf(mode))),
                              tall) |
               reflect(box);
    };

    Elements buttons{
        answer(_("Marked"), Mode::Marked, picker.markedBox),    text("   "),
        answer(_("Current"), Mode::Current, picker.currentBox), text("   "),
        answer(_("Cancel"), Mode::Cancel, picker.cancelBox),
    };

    // How many are marked, under the question. It is the one thing the screen
    // behind the box cannot be read for — the marks are spread down an area that
    // does not fit on it — and what follows an answer here is not undoable.
    const std::string count = i18n::format(
        i18n::plural("{0} message marked", "{0} messages marked", picker.marked),
        {std::to_string(picker.marked)});

    auto content = vbox({
        text(question(picker.purpose)) | bold | color(theme::palette.dialogText) | center,
        text(count) | color(theme::palette.dialogHint) | center,
        text(""),
        hbox(std::move(buttons)) | center,
        text(""),
        text(_("←→ choose · Enter confirm · m/c · Esc cancel")) |
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
    AppState::ScopePicker& picker = *state.scopePicker;

    // A click answers with the button it landed on, without selecting it first:
    // pointing at Cancel and pressing is one gesture, not two.
    if (const auto click = leftClick(event)) {
        const auto pick = [&](Mode mode) {
            // Selected first and shown pressed after, so that the button the
            // pointer landed on is the current one for the length of the
            // animation before the box goes away.
            picker.mode = mode;
            state.showClick(AppState::Pressed::ScopeChoice,
                            static_cast<uint32_t>(indexOf(mode)));
            return Outcome::Picked;
        };
        if (picker.markedBox.Contain(click->x, click->y)) return pick(Mode::Marked);
        if (picker.currentBox.Contain(click->x, click->y)) return pick(Mode::Current);
        if (picker.cancelBox.Contain(click->x, click->y)) return pick(Mode::Cancel);
        // Anywhere else, inside the box or outside it: swallowed, as every other
        // event is while the dialog is modal. Not dismissed — a click that
        // missed a button is not an answer to a question about deleting.
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
        state.scopePicker.reset();
        return Outcome::Dismissed;
    }
    return Outcome::Ignored;
}

}  // namespace amberedit::ui::scope_dialog
