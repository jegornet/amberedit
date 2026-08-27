#include "ui/mark_dialog.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "config/text_util.hpp"
#include "i18n/i18n.hpp"
#include "ui/dialog_frame.hpp"
#include "ui/event_util.hpp"
#include "ui/message_marks.hpp"
#include "ui/text_layout.hpp"
#include "ui/theme.hpp"

namespace amberedit::ui::mark_dialog {

using namespace term;

namespace {

using Action = AppState::MarkPicker::Action;

/// The five answers in the order they are drawn and stepped through, and the
/// letter each answers to outright — the initial of the English word, which is
/// what the hint line under the buttons prints.
///
/// The letters are not the labels': a translated button is still reached by the
/// letter written here, since that is the letter the box says to press.
struct Answer {
    Action action;
    char letter;
};

constexpr Answer kAnswers[] = {
    {Action::All, 'a'},   {Action::UnmarkAll, 'u'}, {Action::Toggle, 't'},
    {Action::Newer, 'n'}, {Action::Older, 'o'},
};
constexpr int kAnswerCount = 5;

/// The word on each button. Not a table beside the one above, because a `case`
/// is what a compiler checks against the enumeration: an answer added without a
/// word for it is a build that stops here.
const char* labelOf(Action action) {
    switch (action) {
        case Action::All: return _("All msgs");
        case Action::UnmarkAll: return _("Unmark all");
        case Action::Toggle: return _("Toggle marks");
        case Action::Newer: return _("New msgs >current");
        case Action::Older: return _("Old msgs <current");
    }
    return "";  // unreachable; an Action is one of the five
}

int indexOf(Action action) {
    for (int i = 0; i < kAnswerCount; ++i) {
        if (kAnswers[i].action == action) return i;
    }
    return 0;  // unreachable; an Action is one of the five
}

/// Moves the cursor down the column, stopping at neither end: five answers under
/// one another are a ring, and a user holding ↓ is asking for the next one.
void step(AppState::MarkPicker& picker, int delta) {
    const int next = (indexOf(picker.action) + delta + kAnswerCount) % kAnswerCount;
    picker.action = kAnswers[next].action;
}

/// The answer a letter names, or nothing. Read without regard to case, the way
/// the other boxes read their initials.
std::optional<Action> actionFor(const Event& event) {
    if (!event.is_character() || event.ctrl() || event.alt()) return std::nullopt;
    for (const Answer& answer : kAnswers) {
        if (event == Event::Character(answer.letter) ||
            event == Event::Character(config::text::asciiUpper(answer.letter))) {
            return answer.action;
        }
    }
    return std::nullopt;
}

/// The message the three relative answers are counted from: the one the reader
/// is showing. Zero where it is showing none, which is what keeps the box shut.
uint32_t currentMessage(const AppState& state) {
    return state.readHeader ? state.readHeader->number : 0;
}

}  // namespace

void open(AppState& state) {
    if (state.base == nullptr || state.messageCount == 0) return;
    if (currentMessage(state) == 0) return;
    state.markPicker = AppState::MarkPicker{};
}

void apply(AppState& state, Action action) {
    const uint32_t current = currentMessage(state);
    switch (action) {
        case Action::All: marks::markAll(state); break;
        case Action::UnmarkAll: marks::unmarkAll(state); break;
        case Action::Toggle: marks::toggleAll(state); break;
        case Action::Newer: marks::markNewer(state, current); break;
        case Action::Older: marks::markOlder(state, current); break;
    }
}

Element render(AppState& state, Element background) {
    AppState::MarkPicker& picker = *state.markPicker;

    // Every button the same width, the widest word deciding: a column of boxes
    // of different widths reads as a list of things of different weights, which
    // is exactly what these five are not.
    int widest = 0;
    for (const Answer& answer : kAnswers) {
        widest = std::max(widest, displayWidth(labelOf(answer.action)));
    }

    const bool tall = state.dialogTallButtons();
    // The boxes are written into as the frame is laid out, so the room is
    // reserved before any of them is drawn: a vector that grew under them would
    // leave the earlier ones pointing at freed memory.
    picker.boxes.assign(kAnswerCount, Box::Nowhere());

    Elements rows{
        text(_("Mark messages:")) | bold | color(theme::palette.dialogText) | center,
        text(""),
    };
    // reflect() writes back where the button landed once the box has been
    // centred, which is what handleEvent() hit-tests a click on.
    const auto answer = [&](int at) {
        const Action action = kAnswers[at].action;
        const bool pressed =
            state.isPressed(AppState::Pressed::MarkChoice, static_cast<uint32_t>(at));
        return dialog::button(padRight(labelOf(action), widest), picker.action == action,
                              pressed, tall) |
               reflect(picker.boxes[static_cast<size_t>(at)]);
    };
    for (int i = 0; i < kAnswerCount; ++i) rows.push_back(answer(i) | center);
    rows.push_back(text(""));
    rows.push_back(text(_("↑↓ choose · Enter confirm · a/u/t/n/o · Esc cancel")) |
                   color(theme::palette.dialogHint));

    // The frame is drawn round a padded box: without the margins the hint line
    // sets the width and ends up flush against the border.
    auto box = hbox({text("  "), vbox(std::move(rows)), text("  ")}) | border |
               color(theme::palette.dialogBorder);

    // dialog::surface() wipes the screen behind the box and lays the dialog's
    // own fill down in its place, so the message underneath neither shows
    // through it nor colors it.
    return dbox({std::move(background), dialog::surface(std::move(box)) | center});
}

Outcome handleEvent(AppState& state, const Event& event) {
    AppState::MarkPicker& picker = *state.markPicker;

    // A click answers with the button it landed on, without selecting it first:
    // pointing at Unmark all and pressing is one gesture, not two.
    if (const auto click = leftClick(event)) {
        for (int i = 0; i < kAnswerCount; ++i) {
            const auto at = static_cast<size_t>(i);
            if (at >= picker.boxes.size()) break;
            if (!picker.boxes[at].Contain(click->x, click->y)) continue;
            // Selected first and shown pressed after, so that the button the
            // pointer landed on is the current one for the length of the
            // animation before the box goes away.
            picker.action = kAnswers[i].action;
            state.showClick(AppState::Pressed::MarkChoice, static_cast<uint32_t>(i));
            return Outcome::Picked;
        }
        // Anywhere else, inside the box or outside it: swallowed, as every other
        // event is while the dialog is modal.
        return Outcome::Ignored;
    }

    // The initials answer outright, the way y and n answer a confirmation.
    if (const auto typed = actionFor(event)) {
        picker.action = *typed;
        return Outcome::Picked;
    }
    if (const int wheel = wheelDelta(event); wheel != 0) {
        step(picker, wheel);
        return Outcome::Ignored;
    }
    if (event == Event::ArrowDown || event == Event::Tab) {
        step(picker, 1);
        return Outcome::Ignored;
    }
    if (event == Event::ArrowUp || event == Event::TabReverse) {
        step(picker, -1);
        return Outcome::Ignored;
    }
    if (event == Event::Home) {
        picker.action = kAnswers[0].action;
        return Outcome::Ignored;
    }
    if (event == Event::End) {
        picker.action = kAnswers[kAnswerCount - 1].action;
        return Outcome::Ignored;
    }
    if (event == Event::Return) return Outcome::Picked;
    if (event == Event::Escape || event == Event::Backspace) {
        state.markPicker.reset();
        return Outcome::Dismissed;
    }
    return Outcome::Ignored;
}

}  // namespace amberedit::ui::mark_dialog
