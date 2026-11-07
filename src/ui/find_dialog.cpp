#include "ui/find_dialog.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "app/clipboard.hpp"
#include "app/message_search.hpp"
#include "config/text_util.hpp"
#include "i18n/i18n.hpp"
#include "ui/dialog_frame.hpp"
#include "ui/event_util.hpp"
#include "ui/input_field.hpp"
#include "ui/line_edit.hpp"
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

/// The words the box is written with.
///
/// Functions and not constants: what each of them says is the interface's own
/// language, and a catalog is read at startup rather than compiled in.
const char* title() {
    return _(" Find ");
}
const char* label() {
    return _(" Text: ");
}
const char* hint() {
    return _("Enter find · Tab move · Esc close");
}
/// What an Enter on an empty field is answered with. A search for nothing has
/// not been asked for, and running one would walk the whole area to land on the
/// message it started from.
const char* noQuery() {
    return _("Nothing to look for");
}

/// The two answers as they are written, and the button under them.
const char* bothLabel() {
    return _("Header + text");
}
const char* headerLabel() {
    return _("Header only");
}
/// The bare word: the spaces either side of it, or the frame in their place,
/// are `dialog::button()`'s.
std::string findLabel() {
    return _("Find");
}

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
        return dialog::framed(std::move(element) | theme::selectionBold |
                              color(theme::palette.selectionText) |
                              bgcolor(theme::palette.selection));
    }
    return dialog::framed(
        std::move(element) |
        color(chosen ? theme::palette.dialogText : theme::palette.dialogLabel));
}

/// The button that runs the search, centred on a row of the box's own width —
/// measured rather than centred by a filler, since every row here is as wide as
/// it is written and one narrower than the rest would take the frame in with it.
Element findButton(const AppState& state, Picker& picker, int inner) {
    const bool tall = state.dialogTallButtons();
    Element element =
        dialog::button(findLabel(), picker.focus == Focus::Button,
                       state.isPressed(AppState::Pressed::FindButton), tall);

    const int spare = std::max(0, inner - dialog::buttonWidth(findLabel(), tall));
    const int left = spare / 2;
    return dialog::framed(hbox({text(std::string(static_cast<size_t>(left), ' ')),
                                std::move(element) | reflect(picker.findBox),
                                text(std::string(static_cast<size_t>(spare - left), ' '))}),
                          dialog::buttonRows(tall));
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
        picker.error = noQuery();
        return Outcome::Ignored;
    }
    return Outcome::Search;
}

/// The words as a `line_edit::Line`, which is what edits them.
line_edit::Line wordsOf(Picker& picker) {
    return {picker.query, picker.cursor, picker.anchor};
}

/// Every word in the box selected, the cursor at the end of them — how the box
/// opens, and what the key that opens it does once it is up: the words are
/// there to be typed over or looked for again, and either is one keystroke.
void selectAll(Picker& picker) {
    picker.cursor = picker.query.size();
    picker.anchor.reset();
    if (!picker.query.empty()) picker.anchor = 0;
}

/// What is selected of the words, to the clipboard `clipboard` names — the
/// editor's copy exactly. `quiet` leaves the "Copied" box out, for the copy a
/// selection makes by itself.
void copySelected(AppState& state, Picker& picker, bool cut, bool quiet) {
    std::string text = line_edit::selectedText(wordsOf(picker));
    if (text.empty()) return;
    if (cut) {
        line_edit::deleteSelected(wordsOf(picker));
        picker.error.clear();
    }
    if (state.config.clipboardInEffect() == config::ClipboardMode::Internal) {
        state.clipboard = std::move(text);
        state.copiedNotice = state.config.clipboardCopyIndicate && !quiet;
    } else {
        state.clipboardCopyRequested = std::move(text);
        state.clipboardCopyQuiet = quiet;
    }
}

/// The byte of the words under column `x`, held to the field: a drag carried
/// past either end of it means that end, and a character over, which is what
/// scrolls words wider than the box along under the pointer.
size_t wordsAt(const Picker& picker, int x) {
    const Box& box = picker.queryBox;
    const int column = std::clamp(x, box.x_min, box.x_max + 1) - box.x_min;
    return offsetAtColumn(picker.query, picker.origin, column);
}

/// A press on the words: the cursor where it landed and the anchor beside it,
/// for a drag to carry the cursor away from — or, the second and third press
/// of a run, the word under it and then the whole of the words, as in the
/// editor's fields.
void pressWords(AppState& state, Picker& picker, const MouseEvent& click) {
    picker.focus = Focus::Query;
    const FieldSelection before =
        line_edit::selection(picker.query, picker.cursor, picker.anchor);
    picker.cursor = wordsAt(picker, click.x);
    picker.held.reset();
    picker.dragging = false;

    const int count = countClick(picker.clicks, click.x, click.y, state.monotonicMs());
    if (count > 1) {
        const auto [begin, end] = count == 2
                                      ? wordAround(picker.query, picker.cursor)
                                      : std::pair<size_t, size_t>{0, picker.query.size()};
        picker.anchor = begin;
        picker.cursor = end;
        if (state.config.clipboardCopyOnSelect != config::CopyOnSelect::Off) {
            copySelected(state, picker, false, /*quiet=*/true);
        }
        return;
    }
    picker.anchor = picker.cursor;
    picker.dragging = true;
    using Held = HeldSelection<size_t>;
    if (!before.empty() && Held::covers(before.begin, before.end, picker.cursor)) {
        picker.held = Held{before.begin, before.end, picker.cursor};
    }
}

/// The cursor carried to where the pointer has been dragged.
void dragWords(Picker& picker, int x) {
    picker.cursor = wordsAt(picker, x);
    if (picker.held) {
        const auto [anchor, cursor] = picker.held->endsFor(picker.cursor);
        picker.anchor = anchor;
        picker.cursor = cursor;
    }
}

/// The drag under way, if it is one: a movement carries it on, a release ends
/// it and copies what it selected where `clipboard_copy_on_select` asks, and a
/// press that only stands for a release nobody reported carries it on where it
/// lands on the words. Says whether the event was the drag's.
bool answerDrag(AppState& state, Picker& picker, const Event& event) {
    if (!picker.dragging) return false;
    const auto mouse = mouseOf(event);
    if (!mouse || mouse->button != MouseEvent::Button::Left) return false;
    switch (mouse->motion) {
        case MouseEvent::Motion::Moved: dragWords(picker, mouse->x); return true;
        case MouseEvent::Motion::Released:
            dragWords(picker, mouse->x);
            picker.dragging = false;
            picker.held.reset();
            if (state.config.clipboardCopyOnSelect != config::CopyOnSelect::Off) {
                copySelected(state, picker, false, /*quiet=*/true);
            }
            return true;
        case MouseEvent::Motion::Pressed:
            if (!mouse->resumed || !picker.queryBox.Contain(mouse->x, mouse->y)) {
                picker.dragging = false;
                picker.held.reset();
                return false;
            }
            dragWords(picker, mouse->x);
            return true;
    }
    return false;
}

}  // namespace

void open(AppState& state) {
    // Nothing to search: an empty area opens the reader on blank rows, and the
    // button for this is drawn dimmed there.
    if (state.base == nullptr || state.messageCount == 0) return;

    Picker picker;
    // Whatever was last looked for, all of it selected and the cursor at the
    // end of it: opening the box to look for the same thing again is the
    // commonest reason to open it, Enter does that, and the first character
    // typed writes over the old words rather than lengthening them.
    picker.query = state.lastFind.query;
    selectAll(picker);
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
    const int fieldWidth = std::max(1, inner - displayWidth(label()));
    Element field =
        inputField(picker.query, picker.cursor, fieldWidth, typing,
                   typing ? theme::palette.selectionText : theme::palette.dialogLabel,
                   fieldFiller(theme::palette.dialogHint), &picker.origin,
                   typing ? line_edit::selection(picker.query, picker.cursor, picker.anchor)
                          : FieldSelection{}) |
        bgcolor(typing ? theme::palette.selection : theme::palette.dialogField) |
        reflect(picker.queryBox);

    const bool onScope = picker.focus == Focus::Scope;
    picker.bothBox = Box::Nowhere();
    picker.headerBox = Box::Nowhere();
    picker.findBox = Box::Nowhere();

    Elements lines{
        dialog::titleBar(title(), inner),
        dialog::framed(
            hbox({text(label()) | color(theme::palette.dialogLabel), std::move(field)})),
        dialog::divider(inner),
        // The question the answers are to, so that neither of them has to say
        // "look in" as well as what it names.
        dialog::line(_(" Look in:"), inner, theme::palette.dialogLabel),
        radio(bothLabel(), picker.scope == Scope::HeaderAndText,
              onScope && picker.scope == Scope::HeaderAndText, inner) |
            reflect(picker.bothBox),
        radio(headerLabel(), picker.scope == Scope::Header,
              onScope && picker.scope == Scope::Header, inner) |
            reflect(picker.headerBox),
        dialog::divider(inner),
        findButton(state, picker, inner),
        dialog::bottomBar(hint(), picker.error, inner),
    };

    // dialog::surface() wipes the screen behind the box and lays the dialog's
    // own fill down in its place, so the message underneath neither shows
    // through it nor colors it.
    return dbox(
        {std::move(background), dialog::surface(vbox(std::move(lines))) | center});
}

Outcome handleEvent(AppState& state, const Event& event) {
    Picker& picker = *state.findPicker;

    if (answerDrag(state, picker, event)) return Outcome::Ignored;

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
            // offsetAtColumn() is measured against the scroll inputField()
            // settled on, so a click never lands inside a UTF-8 sequence.
            pressWords(state, picker, *click);
            return Outcome::Ignored;
        }
        // Anywhere else on the screen. A box one has thought better of is put
        // away by pointing away from it, and the reader underneath is not acted
        // on.
        state.findPicker.reset();
        return Outcome::Dismissed;
    }

    // The key that opened the box selects the words again, wherever the cursor
    // stood in it: the same key pressed twice means look for something else.
    if (state.keys.is(event, Command::ReaderFind)) {
        picker.focus = Focus::Query;
        selectAll(picker);
        return Outcome::Ignored;
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
        if (line_edit::typesText(event)) picker.focus = Focus::Query;
    }

    if (picker.focus != Focus::Query) return Outcome::Ignored;

    // The words are a field like any of the editor's, and answer the editor's
    // keys: copy, cut and paste, Shift and the arrows, the word keys.
    if (state.keys.is(event, Command::ComposeCopy)) {
        copySelected(state, picker, false, /*quiet=*/false);
        return Outcome::Ignored;
    }
    if (state.keys.is(event, Command::ComposeCut)) {
        copySelected(state, picker, true, /*quiet=*/false);
        return Outcome::Ignored;
    }
    if (state.keys.is(event, Command::ComposePaste)) {
        if (state.config.clipboardInEffect() == config::ClipboardMode::Internal) {
            pasteText(state, state.clipboard);
        } else {
            state.clipboardPasteRequested = true;
        }
        return Outcome::Ignored;
    }
    if (line_edit::extendsSelection(event)) {
        line_edit::extend(wordsOf(picker), event);
        if (state.config.clipboardCopyOnSelect == config::CopyOnSelect::Always) {
            copySelected(state, picker, false, /*quiet=*/true);
        }
        return Outcome::Ignored;
    }
    // ↓ steps down out of the field onto the answers, the box being read
    // downwards.
    if (event == Event::ArrowDown) {
        picker.anchor.reset();
        picker.focus = Focus::Scope;
        return Outcome::Ignored;
    }

    const std::string before = picker.query;
    static_cast<void>(line_edit::editKey(wordsOf(picker), event, state.keys));
    // What was said about the last Enter is about words that have since been
    // typed over.
    if (picker.query != before) picker.error.clear();
    return Outcome::Ignored;
}

void pasteText(AppState& state, std::string_view text) {
    if (!state.findPicker) return;
    Picker& picker = *state.findPicker;
    const std::vector<std::string> lines = app::pastedLines(text);
    if (lines.empty()) return;
    picker.focus = Focus::Query;
    line_edit::insert(wordsOf(picker), lines.front());
    picker.error.clear();
}

}  // namespace amberedit::ui::find_dialog
