#include "ui/confirm_dialog.hpp"

#include <string>
#include <utility>

#include "i18n/i18n.hpp"
#include "ui/dialog_frame.hpp"
#include "ui/event_util.hpp"
#include "ui/theme.hpp"

namespace amberedit::ui::confirm_dialog {

using namespace term;

namespace {

/// What each confirmation asks. One dialog serves them all: three of these
/// would be three copies of the same buttons and the same hit-testing.
std::string question(AppState::Confirm confirm) {
    switch (confirm) {
        case AppState::Confirm::SaveMessage: return _("Save the message?");
        case AppState::Confirm::DropMessage: return _("Drop the message?");
        case AppState::Confirm::DeleteMessage: return _("Delete this message?");
        case AppState::Confirm::ChangeForeignMessage:
            return _("Change this message? It is not from you!");
        case AppState::Confirm::ChangeSentMessage:
            return _("Change this message? It has already been sent.");
        case AppState::Confirm::ProcessCopies: return _("XC and/or CC commands found.");
        case AppState::Confirm::Quit:
        case AppState::Confirm::None: break;
    }
    return _("Quit AmberEdit?");
}

/// What the two answers are called. Yes and No for a question that is one —
/// and for the one question that is not, the two things that can become of the
/// commands found in the message. The message is stored whichever is pressed,
/// so answering it "yes" would be answering a question nobody asked.
struct Answers {
    std::string yes;
    std::string no;
};
Answers answersTo(AppState::Confirm confirm) {
    if (confirm == AppState::Confirm::ProcessCopies) {
        return {_("Process"), _("Ignore")};
    }
    return {_("Yes"), _("No")};
}

/// Moves the selection to the other answer. There are two of them, so a step
/// either way is the same move, and ← and → both make it.
void step(AppState& state) {
    state.confirmChoice = state.confirmChoice == AppState::ConfirmChoice::Yes
                              ? AppState::ConfirmChoice::No
                              : AppState::ConfirmChoice::Yes;
}

}  // namespace

Element render(AppState& state, Element background) {
    const Answers answers = answersTo(state.confirm);
    const bool tall = state.dialogTallButtons();
    Elements buttons{
        // reflect() writes back where each button landed once the box has
        // been centred, which is what handleEvent() hit-tests a click on —
        // three rows of it where the buttons are framed, since the box a
        // button was drawn in is the box the click is measured against.
        dialog::button(answers.yes, state.confirmChoice == AppState::ConfirmChoice::Yes,
                       state.isPressed(AppState::Pressed::ConfirmYes), tall) |
            reflect(state.confirmYesBox),
        text("   "),
        dialog::button(answers.no, state.confirmChoice == AppState::ConfirmChoice::No,
                       state.isPressed(AppState::Pressed::ConfirmNo), tall) |
            reflect(state.confirmNoBox),
    };

    auto content = vbox({
        text(question(state.confirm)) | bold | color(theme::palette.dialogText) | center,
        text(""),
        hbox(std::move(buttons)) | center,
        text(""),
        // Esc is the second answer rather than a way out of the question where
        // the question has no way out: the commands are ignored and the message
        // is stored, which is what pressing Ignore does.
        text(state.confirm == AppState::Confirm::ProcessCopies
                 ? _("←→ choose · Enter confirm · y/n · Esc ignores")
                 : _("←→ choose · Enter confirm · y/n · Esc cancel")) |
            color(theme::palette.dialogHint),
    });

    // The frame is drawn round a padded box: without the margins the hint line
    // sets the width and ends up flush against the border.
    auto box = hbox({text("  "), std::move(content), text("  ")}) | border |
               color(theme::palette.dialogBorder);

    // dialog::surface() wipes the screen behind the box and lays the dialog's
    // own fill down in its place, so the area list neither shows through it nor
    // colors it.
    return dbox({std::move(background), dialog::surface(box) | center});
}

Outcome handleEvent(AppState& state, const Event& event) {
    // A click on a button answers with it, without going through selecting it
    // first: pointing at "No" and pressing is one gesture, not two.
    if (const auto click = leftClick(event)) {
        // The button is shown pressed before it answers — while the dialog is
        // still up, which is why the answer is given after the animation and
        // not before it.
        if (state.confirmYesBox.Contain(click->x, click->y)) {
            state.showClick(AppState::Pressed::ConfirmYes);
            return Outcome::Confirmed;
        }
        if (state.confirmNoBox.Contain(click->x, click->y)) {
            state.showClick(AppState::Pressed::ConfirmNo);
            state.confirm = AppState::Confirm::None;
            return Outcome::Dismissed;
        }
        // Anywhere else in the dialog, and outside it: the click is swallowed,
        // as every other event is while the dialog is modal.
        return Outcome::Ignored;
    }
    if (event == Event::Character('y') || event == Event::Character('Y')) {
        return Outcome::Confirmed;
    }
    if (event == Event::Character('n') || event == Event::Character('N') ||
        event == Event::Escape) {
        state.confirm = AppState::Confirm::None;
        return Outcome::Dismissed;
    }
    if (event == Event::ArrowRight || event == Event::ArrowLeft || event == Event::Tab ||
        event == Event::TabReverse) {
        step(state);
        return Outcome::Ignored;
    }
    if (event == Event::Return) {
        if (state.confirmChoice == AppState::ConfirmChoice::Yes) {
            return Outcome::Confirmed;
        }
        state.confirm = AppState::Confirm::None;
        return Outcome::Dismissed;
    }
    return Outcome::Ignored;
}

}  // namespace amberedit::ui::confirm_dialog
