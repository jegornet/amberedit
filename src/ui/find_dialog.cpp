#include "ui/find_dialog.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "app/message_search.hpp"
#include "config/text_util.hpp"
#include "ui/dialog_frame.hpp"
#include "ui/event_util.hpp"
#include "ui/input_field.hpp"
#include "ui/text_layout.hpp"
#include "ui/theme.hpp"

namespace amberedit::ui::find_dialog {

using namespace term;

namespace {

using Picker = AppState::FindPicker;
using Focus = AppState::FindPicker::Focus;
using Scope = app::SearchScope;

/// The box's width, and the narrowest it will be squeezed to. Narrower than the
/// import and export boxes because it has no listing in it: a field, two
/// answers and a button.
constexpr int kInnerWidth = 48;
constexpr int kMinInner = 30;
/// The frame itself, a column on each side.
constexpr int kFrame = 2;

constexpr const char* kTitle = " Find ";
constexpr const char* kLabel = " Text: ";
constexpr const char* kHint = "Enter find · Tab move · Esc close";
/// What an Enter on an empty field is answered with. A search for nothing has
/// not been asked for, and running one would walk the whole area to land on the
/// message it started from.
constexpr const char* kNoQuery = "Nothing to look for";

/// The two answers as they are written, and the button under them.
constexpr const char* kBothLabel = "Header + text";
constexpr const char* kHeaderLabel = "Header only";
constexpr const char* kFindLabel = "  Find  ";

/// The two answers in the order they are drawn and stepped through.
constexpr Scope kScopes[] = {Scope::HeaderAndText, Scope::Header};
constexpr int kScopeCount = 2;

int indexOf(Scope scope) {
    return scope == Scope::Header ? 1 : 0;
}

/// Settles how wide the box is, once — and again only where the window has
/// changed size, which is the habit every modal here keeps. A box measured
/// against what is typed into it would be a different size on every keystroke.
void fitBox(const AppState& state, Picker& picker) {
    if (picker.layoutWidth == state.width && picker.layoutHeight == state.height) return;
    picker.layoutWidth = state.width;
    picker.layoutHeight = state.height;
    picker.inner = std::min(kInnerWidth, std::max(kMinInner, state.width - kFrame));
}

/// What an event types into the field: anything at all, since a message may be
/// about anything — Cyrillic, spaces and punctuation included.
std::optional<std::string> typedText(const Event& event) {
    if (!event.is_character() || event.ctrl() || event.alt()) return std::nullopt;
    const std::string& input = event.character();
    if (input.empty()) return std::nullopt;
    if (input.size() == 1 && static_cast<unsigned char>(input[0]) < 0x20) {
        return std::nullopt;
    }
    return input;
}

/// One of the two answers, as a radio button: the mark says which is chosen,
/// and the fill — the same one every list gives the row Enter would act on —
/// says where the cursor is. Two things worth seeing separately, since the
/// cursor may be somewhere else in the box while an answer stands chosen.
Element radio(const std::string& label, bool chosen, bool current, int inner) {
    const std::string mark = chosen ? "(•) " : "( ) ";
    const std::string line = "  " + mark + label;
    const int room = std::max(0, inner - displayWidth(line));

    auto element = text(line + std::string(static_cast<size_t>(room), ' '));
    if (current) {
        return dialog::framed(std::move(element) | bold |
                              color(theme::palette.selectionText) |
                              bgcolor(theme::palette.selection));
    }
    return dialog::framed(std::move(element) |
                          color(chosen ? theme::palette.text : theme::palette.header));
}

/// The button that runs the search, centred on a row of the box's own width —
/// measured rather than centred by a filler, since every row here is as wide as
/// it is written and one narrower than the rest would take the frame in with it.
Element findButton(const AppState& state, Picker& picker, int inner) {
    auto element = text(kFindLabel);
    // Innermost, so that it is the color that lands: a parent paints its whole
    // box and the child paints over it.
    if (state.isPressed(AppState::Pressed::FindButton)) {
        element = std::move(element) | color(theme::palette.animatedButtonText);
    }
    element = picker.focus == Focus::Button
                  ? std::move(element) | bold | color(theme::palette.selectionText) |
                        bgcolor(theme::palette.selection)
                  : std::move(element) | color(theme::palette.text);

    const int spare = std::max(0, inner - displayWidth(kFindLabel));
    const int left = spare / 2;
    return dialog::framed(
        hbox({text(std::string(static_cast<size_t>(left), ' ')),
              std::move(element) | reflect(picker.findBox),
              text(std::string(static_cast<size_t>(spare - left), ' '))}));
}

/// Moves the answer along, stopping at neither end: two answers one under the
/// other are a ring, as every other column of choices here is.
void stepScope(Picker& picker, int delta) {
    const int next = (indexOf(picker.scope) + delta + kScopeCount) % kScopeCount;
    picker.scope = kScopes[next];
}

/// The next stop of the box, `step` forwards or back.
void focusAfter(Picker& picker, int step) {
    constexpr Focus kRing[] = {Focus::Query, Focus::Scope, Focus::Button};
    constexpr int kStops = 3;

    const auto* at = std::find(std::begin(kRing), std::end(kRing), picker.focus);
    const auto index =
        static_cast<int>(at == std::end(kRing) ? 0 : at - std::begin(kRing));
    picker.focus =
        kRing[static_cast<size_t>(((index + step) % kStops + kStops) % kStops)];
}

/// What Enter and the button both come to: the words as they stand, or the
/// reason there was nothing to look for. Trimmed, since a query is words rather
/// than a line — but only at the ends, a search for two words with a space
/// between them being ordinary.
Outcome runFind(Picker& picker) {
    picker.error.clear();
    if (config::text::trim(picker.query).empty()) {
        picker.error = kNoQuery;
        return Outcome::Ignored;
    }
    return Outcome::Search;
}

}  // namespace

void open(AppState& state) {
    // Nothing to search: an empty area opens the reader on blank rows, and the
    // button for this is drawn dimmed there.
    if (state.base == nullptr || state.messageCount == 0) return;

    Picker picker;
    // Whatever was last looked for, with the cursor at the end of it: opening
    // the box to look for the same thing again is the commonest reason to open
    // it, and typing over what is there is one keystroke either way.
    picker.query = state.lastFind.query;
    picker.cursor = picker.query.size();
    picker.scope = state.lastFind.scope;
    state.findPicker = std::move(picker);
    fitBox(state, *state.findPicker);
}

Element render(AppState& state, Element background) {
    Picker& picker = *state.findPicker;
    fitBox(state, picker);
    picker.cursor = std::min(picker.cursor, picker.query.size());

    const int inner = picker.inner;
    const bool typing = picker.focus == Focus::Query;

    picker.queryBox = Box::Nowhere();
    const int fieldWidth = std::max(1, inner - displayWidth(kLabel));
    Element field =
        inputField(picker.query, picker.cursor, fieldWidth, typing,
                   typing ? theme::palette.selectionText : theme::palette.header,
                   &picker.origin) |
        bgcolor(typing ? theme::palette.selection : theme::palette.inputField) |
        reflect(picker.queryBox);

    const bool onScope = picker.focus == Focus::Scope;
    picker.bothBox = Box::Nowhere();
    picker.headerBox = Box::Nowhere();
    picker.findBox = Box::Nowhere();

    Elements lines{
        dialog::titleBar(kTitle, inner),
        dialog::framed(
            hbox({text(kLabel) | color(theme::palette.header), std::move(field)})),
        dialog::divider(inner),
        // The question the answers are to, so that neither of them has to say
        // "look in" as well as what it names.
        dialog::line(" Look in:", inner, theme::palette.header),
        radio(kBothLabel, picker.scope == Scope::HeaderAndText,
              onScope && picker.scope == Scope::HeaderAndText, inner) |
            reflect(picker.bothBox),
        radio(kHeaderLabel, picker.scope == Scope::Header,
              onScope && picker.scope == Scope::Header, inner) |
            reflect(picker.headerBox),
        dialog::divider(inner),
        findButton(state, picker, inner),
        dialog::bottomBar(kHint, picker.error, inner),
    };

    // clear_under wipes the screen behind the box, so the message underneath
    // does not show through it.
    return dbox({std::move(background), vbox(std::move(lines)) | clear_under | center});
}

Outcome handleEvent(AppState& state, const Event& event) {
    Picker& picker = *state.findPicker;

    if (const auto click = leftClick(event)) {
        // A radio button is turned over by pointing at it and nothing else: it
        // says what the search will read rather than doing anything, and the
        // button below is what acts.
        const auto choose = [&](Scope scope) {
            picker.scope = scope;
            picker.focus = Focus::Scope;
            state.showClick(AppState::Pressed::FindScope,
                            static_cast<uint32_t>(indexOf(scope)));
            return Outcome::Ignored;
        };
        if (picker.bothBox.Contain(click->x, click->y))
            return choose(Scope::HeaderAndText);
        if (picker.headerBox.Contain(click->x, click->y)) return choose(Scope::Header);
        if (picker.findBox.Contain(click->x, click->y)) {
            picker.focus = Focus::Button;
            state.showClick(AppState::Pressed::FindButton);
            return runFind(picker);
        }
        if (picker.queryBox.Contain(click->x, click->y)) {
            picker.focus = Focus::Query;
            // offsetAtColumn() is measured against the scroll inputField()
            // settled on, so a click never lands inside a UTF-8 sequence.
            picker.cursor = offsetAtColumn(picker.query, picker.origin,
                                           click->x - picker.queryBox.x_min);
            return Outcome::Ignored;
        }
        // Anywhere else on the screen. A box one has thought better of is put
        // away by pointing away from it, and the reader underneath is not acted
        // on.
        state.findPicker.reset();
        return Outcome::Dismissed;
    }

    if (event == Event::Tab) {
        focusAfter(picker, 1);
        return Outcome::Ignored;
    }
    if (event == Event::TabReverse) {
        focusAfter(picker, -1);
        return Outcome::Ignored;
    }
    // Enter searches wherever the cursor is: the button is where the ring comes
    // to rest, not the only place the box can be answered from.
    if (event == Event::Return) return runFind(picker);
    if (event == Event::Escape) {
        state.findPicker.reset();
        return Outcome::Dismissed;
    }

    if (picker.focus == Focus::Button) {
        // ↑ goes back to the answers above it, which is where the button was
        // stepped down from.
        if (event == Event::ArrowUp) {
            picker.focus = Focus::Scope;
            return Outcome::Ignored;
        }
        if (event == Event::Character(' ')) return runFind(picker);
    }

    if (picker.focus == Focus::Scope) {
        if (event == Event::ArrowUp || event == Event::ArrowLeft) {
            stepScope(picker, -1);
            return Outcome::Ignored;
        }
        // Space turns the pair over as it does a checkbox, which is the same
        // step down the column ↓ makes.
        if (event == Event::ArrowDown || event == Event::ArrowRight ||
            event == Event::Character(' ')) {
            stepScope(picker, 1);
            return Outcome::Ignored;
        }
        // Anything typed while the answers have the cursor goes back into the
        // field: the words are what this box is about, and a letter meant for
        // them is not a mistake worth swallowing.
        if (typedText(event)) picker.focus = Focus::Query;
    }

    if (picker.focus != Focus::Query) return Outcome::Ignored;

    if (const auto typed = typedText(event)) {
        picker.query.insert(picker.cursor, *typed);
        picker.cursor += typed->size();
        // What was said about the last Enter is about words that have since been
        // typed over.
        picker.error.clear();
        return Outcome::Ignored;
    }
    if (event == Event::Backspace) {
        if (picker.cursor > 0) {
            const size_t from = prevChar(picker.query, picker.cursor);
            picker.query.erase(from, picker.cursor - from);
            picker.cursor = from;
            picker.error.clear();
        }
        return Outcome::Ignored;
    }
    if (event == Event::Delete) {
        if (picker.cursor < picker.query.size()) {
            picker.query.erase(picker.cursor, charLen(picker.query, picker.cursor));
            picker.error.clear();
        }
        return Outcome::Ignored;
    }
    if (event == Event::ArrowLeft) {
        picker.cursor = prevChar(picker.query, picker.cursor);
        return Outcome::Ignored;
    }
    if (event == Event::ArrowRight) {
        if (picker.cursor < picker.query.size()) {
            picker.cursor += charLen(picker.query, picker.cursor);
        }
        return Outcome::Ignored;
    }
    // ↓ steps down out of the field onto the answers, the box being read
    // downwards.
    if (event == Event::ArrowDown) {
        picker.focus = Focus::Scope;
        return Outcome::Ignored;
    }
    if (event == Event::Home) {
        picker.cursor = 0;
        return Outcome::Ignored;
    }
    if (event == Event::End) {
        picker.cursor = picker.query.size();
        return Outcome::Ignored;
    }
    return Outcome::Ignored;
}

}  // namespace amberedit::ui::find_dialog
