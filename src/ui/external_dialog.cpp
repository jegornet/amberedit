#include "ui/external_dialog.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "i18n/i18n.hpp"
#include "ui/dialog_frame.hpp"
#include "ui/event_util.hpp"
#include "ui/screens/compose_screen.hpp"
#include "ui/theme.hpp"

namespace amberedit::ui::external_dialog {

using namespace term;

namespace {

using Answer = AppState::ExternalReview::Answer;

/// The four answers in the order they are drawn and stepped through: what the
/// user came for first, then the way out, then the way back into the editor,
/// and the header last — the one that is not about the text at all.
constexpr Answer kAnswers[] = {Answer::Save, Answer::Drop, Answer::Again,
                               Answer::Header};
constexpr int kAnswerCount = 4;

int indexOf(Answer answer) {
    for (int i = 0; i < kAnswerCount; ++i) {
        if (kAnswers[i] == answer) return i;
    }
    return 0;  // unreachable; an Answer is one of the four
}

/// One of the four, drawn as the forward box draws its three — the same fill
/// under whatever Enter would act on, and the same color under a click being
/// shown before it acts.
Element button(const std::string& label, bool selected, bool pressed) {
    auto element = text("  " + label + "  ");
    // Innermost, so that it is the color that lands: a parent paints its whole
    // box and the child paints over it.
    if (pressed) element = std::move(element) | color(theme::palette.dialogFlash);
    if (selected) {
        return std::move(element) | bold | color(theme::palette.selectionText) |
               bgcolor(theme::palette.selection);
    }
    return std::move(element) | color(theme::palette.dialogText);
}

/// Moves the selection along the row, stopping at neither end: four answers
/// side by side are a ring, and a user holding → is asking for the next one.
void step(AppState::ExternalReview& review, int delta) {
    const int next = (indexOf(review.answer) + delta + kAnswerCount) % kAnswerCount;
    review.answer = kAnswers[next];
}

/// The answer a letter names, or nothing. The initial of each, which is what
/// the hint line under the buttons prints.
std::optional<Answer> answerFor(const Event& event) {
    if (!event.is_character()) return std::nullopt;
    const std::string& typed = event.character();
    if (typed == "s" || typed == "S") return Answer::Save;
    if (typed == "d" || typed == "D") return Answer::Drop;
    if (typed == "c" || typed == "C") return Answer::Again;
    if (typed == "h" || typed == "H") return Answer::Header;
    return std::nullopt;
}

}  // namespace

Element render(AppState& state, Element background) {
    AppState::ExternalReview& review = *state.externalReview;

    const auto choice = [&](const std::string& label, Answer answer, term::Box& box) {
        // reflect() writes back where the button landed once the box has been
        // centred, which is what handleEvent() hit-tests a click on.
        return button(label, review.answer == answer,
                      state.isPressed(AppState::Pressed::ExternalChoice,
                                      static_cast<uint32_t>(indexOf(answer)))) |
               reflect(box);
    };

    Elements buttons{
        choice(_("Save"), Answer::Save, review.saveBox),
        text("  "),
        choice(_("Discard"), Answer::Drop, review.dropBox),
        text("  "),
        choice(_("Continue"), Answer::Again, review.againBox),
        text("  "),
        choice(_("Header"), Answer::Header, review.headerBox),
    };

    auto content = vbox({
        text(_("The message, as your editor left it:")) | bold |
            color(theme::palette.dialogText) | center,
        text(""),
        hbox(std::move(buttons)) | center,
        text(""),
        text(_("←→ choose · Enter confirm · s/d/c/h · ↑↓ scroll · Esc header")) |
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
    AppState::ExternalReview& review = *state.externalReview;

    // A click answers with the button it landed on, without selecting it first:
    // pointing at Discard and pressing is one gesture, not two.
    if (const auto click = leftClick(event)) {
        // Selected first and shown pressed after, so that the button the
        // pointer landed on is the current one for the length of the animation
        // before whatever it leads to takes the screen.
        const auto pick = [&](Answer answer) {
            review.answer = answer;
            state.showClick(AppState::Pressed::ExternalChoice,
                            static_cast<uint32_t>(indexOf(answer)));
            return Outcome::Picked;
        };
        if (review.saveBox.Contain(click->x, click->y)) return pick(Answer::Save);
        if (review.dropBox.Contain(click->x, click->y)) return pick(Answer::Drop);
        if (review.againBox.Contain(click->x, click->y)) return pick(Answer::Again);
        if (review.headerBox.Contain(click->x, click->y)) return pick(Answer::Header);
        // Anywhere else, inside the box or outside it: swallowed, as every
        // other event is while the box is modal.
        return Outcome::Ignored;
    }

    // The message behind the box, which is what the question is about: it can
    // be longer than the window, and there is no reading it otherwise — the box
    // takes every key while it is up.
    if (const int wheel = wheelDelta(event); wheel != 0) {
        screens::compose::scrollText(state, wheel);
        return Outcome::Ignored;
    }
    if (event == Event::ArrowUp) {
        screens::compose::scrollText(state, -1);
        return Outcome::Ignored;
    }
    if (event == Event::ArrowDown) {
        screens::compose::scrollText(state, 1);
        return Outcome::Ignored;
    }
    if (event == Event::PageUp || event == Event::PageDown) {
        const int rows = std::max(1, screens::compose::editorRows(state));
        screens::compose::scrollText(state, event == Event::PageDown ? rows : -rows);
        return Outcome::Ignored;
    }

    // The initials answer outright, the way y and n answer a confirmation.
    if (const auto typed = answerFor(event)) {
        review.answer = *typed;
        return Outcome::Picked;
    }
    if (event == Event::ArrowRight || event == Event::Tab) {
        step(review, 1);
        return Outcome::Ignored;
    }
    if (event == Event::ArrowLeft || event == Event::TabReverse) {
        step(review, -1);
        return Outcome::Ignored;
    }
    if (event == Event::Return) return Outcome::Picked;
    // Esc is the answer that does nothing to the message: the box goes and the
    // typing lands in the header, from where saving, dropping and going back
    // into the editor are all still one keystroke away. It is not Discard —
    // Escape must never be the key that throws away an hour's writing.
    if (event == Event::Escape) {
        review.answer = Answer::Header;
        return Outcome::Picked;
    }
    return Outcome::Ignored;
}

}  // namespace amberedit::ui::external_dialog
