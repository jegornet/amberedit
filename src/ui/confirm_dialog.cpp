#include "ui/confirm_dialog.hpp"

#include <string>
#include <utility>

#include "ui/event_util.hpp"
#include "ui/theme.hpp"

namespace amberedit::ui::confirm_dialog {

using namespace term;

namespace {

/// One of the two answers. `pressed` is a click on it being shown before it is
/// acted on — the label in the theme's `animated_button_text` for the length of
/// the click animation, whichever of the two it is and whether or not it was
/// the selected one.
Element button(const std::string& label, bool selected, bool pressed) {
    auto element = text("  " + label + "  ");
    // Innermost, so that it is the color that lands: a parent paints its whole
    // box and the child paints over it, which is what the fill below relies on.
    if (pressed) element = std::move(element) | color(theme::palette.animatedButtonText);
    if (selected) {
        // The same fill as the current row in the lists: one color for
        // whatever Enter would act on, wherever the user is.
        return std::move(element) | bold | color(theme::palette.selectionText) |
               bgcolor(theme::palette.selection);
    }
    return std::move(element) | color(theme::palette.text);
}

/// What each confirmation asks. One dialog serves them all: three of these
/// would be three copies of the same buttons and the same hit-testing.
std::string question(AppState::Confirm confirm) {
    switch (confirm) {
        case AppState::Confirm::SaveMessage: return "Save the message?";
        case AppState::Confirm::DropMessage: return "Drop the message?";
        case AppState::Confirm::DeleteMessage: return "Delete this message?";
        case AppState::Confirm::ChangeForeignMessage:
            return "Change this message? It is not from you!";
        case AppState::Confirm::ChangeSentMessage:
            return "Change this message? It has already been sent.";
        case AppState::Confirm::ProcessCopies: return "XC and/or CC commands found.";
        case AppState::Confirm::Quit:
        case AppState::Confirm::None: break;
    }
    return "Quit AmberEdit?";
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
    if (confirm == AppState::Confirm::ProcessCopies) return {"Process", "Ignore"};
    return {"Yes", "No"};
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
    Elements buttons{
        // reflect() writes back where each button landed once the box has
        // been centred, which is what handleEvent() hit-tests a click on.
        button(answers.yes, state.confirmChoice == AppState::ConfirmChoice::Yes,
               state.isPressed(AppState::Pressed::ConfirmYes)) |
            reflect(state.confirmYesBox),
        text("   "),
        button(answers.no, state.confirmChoice == AppState::ConfirmChoice::No,
               state.isPressed(AppState::Pressed::ConfirmNo)) |
            reflect(state.confirmNoBox),
    };

    auto content = vbox({
        text(question(state.confirm)) | bold | color(theme::palette.text) | center,
        text(""),
        hbox(std::move(buttons)) | center,
        text(""),
        // Esc is the second answer rather than a way out of the question where
        // the question has no way out: the commands are ignored and the message
        // is stored, which is what pressing Ignore does.
        text(state.confirm == AppState::Confirm::ProcessCopies
                 ? "←→ choose · Enter confirm · y/n · Esc ignores"
                 : "←→ choose · Enter confirm · y/n · Esc cancel") |
            color(theme::palette.footer),
    });

    // The frame is drawn round a padded box: without the margins the hint line
    // sets the width and ends up flush against the border.
    auto dialog = hbox({text("  "), std::move(content), text("  ")}) | border |
                  color(theme::palette.separator);

    // clear_under wipes the screen behind the box, so the area list does not
    // show through the dialog.
    return dbox({std::move(background), dialog | clear_under | center});
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
    if (event == Event::ArrowRight || event == Event::ArrowLeft ||
        event == Event::Tab || event == Event::TabReverse) {
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
