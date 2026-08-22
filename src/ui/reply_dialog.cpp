#include "ui/reply_dialog.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>

#include "ui/dialog_frame.hpp"
#include "ui/event_util.hpp"
#include "ui/text_layout.hpp"
#include "ui/theme.hpp"

namespace amberedit::ui::reply_dialog {

using namespace term;

namespace {

/// The columns are as wide as the widest entry, so that the numbers, the names
/// and the addresses line up down the list — which is what makes it readable
/// as a list rather than as a stack of sentences.
struct Widths {
    int number{1};
    int name{1};
    int address{1};
};

Widths widthsOf(const std::vector<AppState::ReplyChoice>& choices) {
    Widths widths;
    for (const auto& choice : choices) {
        widths.number =
            std::max(widths.number, digitWidth(static_cast<int64_t>(choice.number)));
        widths.name = std::max(widths.name, displayWidth(choice.from));
        widths.address = std::max(widths.address, displayWidth(choice.address));
    }
    return widths;
}

std::string rowText(const AppState::ReplyChoice& choice, const Widths& widths) {
    return "  + " + padLeft(std::to_string(choice.number), widths.number) + " : " +
           padRight(choice.from, widths.name) + "   (" +
           padRight(choice.address + ")", widths.address + 1) + "   " + choice.date + " ";
}

Element rowOf(const std::string& text_, bool selected) {
    auto element = text(text_);
    if (selected) {
        // The same fill the lists put under the current row, so that whatever
        // Enter would act on looks the same wherever the user is.
        return std::move(element) | bold | color(theme::palette.selectionText) |
               bgcolor(theme::palette.selection);
    }
    return std::move(element) | color(theme::palette.dialogText);
}

/// The top of the frame, with the title in the middle of it.
Element titleBar(int width) {
    const std::string label = " Replies ";
    const int left = std::max(0, (width - displayWidth(label)) / 2);
    const int right = std::max(0, width - left - displayWidth(label));
    return hbox({text("╭" + horizontalRule(left)) | color(theme::palette.dialogBorder),
                 text(label) | color(theme::palette.dialogTitle),
                 text(horizontalRule(right) + "╮") | color(theme::palette.dialogBorder)});
}

}  // namespace

Element render(AppState& state, Element background) {
    const Widths widths = widthsOf(state.replyChoices);

    // Every row the same width, so that the frame round them is a rectangle
    // and the fill under the current one runs the whole way across.
    std::vector<std::string> texts;
    int inner = 0;
    for (const auto& choice : state.replyChoices) {
        texts.push_back(rowText(choice, widths));
        inner = std::max(inner, displayWidth(texts.back()));
    }

    // The frame is drawn by hand rather than with border(), which has no room
    // for a title: this one belongs in the middle of the top side.
    const auto side = [] { return text("│") | color(theme::palette.dialogBorder); };
    Elements lines{titleBar(inner)};
    for (size_t i = 0; i < texts.size(); ++i) {
        auto& choice = state.replyChoices[i];
        lines.push_back(hbox(
            {side(),
             rowOf(padRight(texts[i], inner), static_cast<int>(i) == state.replyChoice) |
                 reflect(choice.box),
             side()}));
    }
    lines.push_back(text("╰" + horizontalRule(inner) + "╯") |
                    color(theme::palette.dialogBorder));

    // dialog::surface() wipes the screen behind the box and lays the dialog's
    // own fill down in its place, so the message underneath neither shows
    // through it nor colors it.
    return dbox(
        {std::move(background), dialog::surface(vbox(std::move(lines))) | center});
}

Outcome handleEvent(AppState& state, const Event& event) {
    const int count = static_cast<int>(state.replyChoices.size());
    if (count == 0) return Outcome::Dismissed;

    // A click picks the row it landed on, without selecting it first: pointing
    // at an answer and pressing is one gesture, not two.
    if (const auto click = leftClick(event)) {
        for (int i = 0; i < count; ++i) {
            if (!state.replyChoices[static_cast<size_t>(i)].box.Contain(click->x,
                                                                        click->y)) {
                continue;
            }
            // Selected first and opened after: the row the pointer landed on
            // is on screen as the current one for the length of the click
            // animation, so the click can be seen to have landed where it did.
            state.replyChoice = i;
            state.showClick();
            return Outcome::Picked;
        }
        // Anywhere else, inside the box or outside it: swallowed, as every
        // other event is while the dialog is modal.
        return Outcome::Ignored;
    }

    if (event == Event::ArrowUp) {
        state.replyChoice = std::max(0, state.replyChoice - 1);
        return Outcome::Ignored;
    }
    if (event == Event::ArrowDown) {
        state.replyChoice = std::min(count - 1, state.replyChoice + 1);
        return Outcome::Ignored;
    }
    if (event == Event::Home) {
        state.replyChoice = 0;
        return Outcome::Ignored;
    }
    if (event == Event::End) {
        state.replyChoice = count - 1;
        return Outcome::Ignored;
    }
    if (event == Event::Return) {
        state.replyChoice = std::clamp(state.replyChoice, 0, count - 1);
        return Outcome::Picked;
    }
    if (event == Event::Escape || event == Event::Backspace) {
        state.replyChoices.clear();
        return Outcome::Dismissed;
    }
    return Outcome::Ignored;
}

}  // namespace amberedit::ui::reply_dialog
