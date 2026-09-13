#include "ui/charset_dialog.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>

#include "config/text_util.hpp"
#include "encoding/charset_detector.hpp"
#include "encoding/iconv_recoder.hpp"
#include "i18n/i18n.hpp"
#include "ui/dialog_frame.hpp"
#include "ui/event_util.hpp"
#include "ui/input_field.hpp"
#include "ui/text_layout.hpp"
#include "ui/theme.hpp"

namespace amberedit::ui::charset_dialog {

using namespace term;

namespace {

using Picker = AppState::CharsetPicker;
using Focus = AppState::CharsetPicker::Focus;
using From = AppState::CharsetPicker::From;

/// The box's width, and the narrowest it will be squeezed to. As wide as the
/// find box: a field, a line saying what the message is being read in, and a
/// button — and that line is the longest thing in it.
constexpr int kInnerWidth = 48;
constexpr int kMinInner = 30;
/// The frame itself, a column on each side.
constexpr int kFrame = 2;

/// The words the box is written with.
///
/// Functions and not constants: what each of them says is the interface's own
/// language, and a catalog is read at startup rather than compiled in.
const char* title() {
    return _(" Charset ");
}
const char* label() {
    return _(" Charset: ");
}
const char* hint() {
    return _("Enter read · Esc close");
}
/// What an Enter on an empty field is answered with. There is nothing to read
/// the message in, and the charset it is being read in is on the line above.
const char* noCharset() {
    return _("Name a charset");
}
/// The bare word on the button: the spaces either side of it, or the frame in
/// their place, are `dialog::button()`'s.
std::string readLabel() {
    return _("Read");
}

/// The line above the field: what the message is being read in, and which of
/// the three things said so.
///
/// Three sentences rather than one with a word swapped into it: what stands
/// where the charset does is a name and not a word of the language, so a
/// translation has the whole sentence to move it about in.
///
/// Each of them has to fit the box's own width — `dialog::line()` truncates,
/// and a sentence cut off at `it declares no…` says less than a shorter one
/// that fits. That is what keeps the third of them to the four words that
/// matter: a charset the *area* answered for is one the message did not.
std::string currentLine(const Picker& picker) {
    switch (picker.from) {
        case From::Kludge:
            return i18n::format(_(" Read as {0}, which its CHRS kludge names."),
                                {picker.current});
        case From::Override:
            return i18n::format(_(" Read as {0}, which you asked for."),
                                {picker.current});
        case From::Default: break;
    }
    return i18n::format(_(" Read as {0}, the area's default."), {picker.current});
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

/// What an event types into the field. A charset name is an ASCII word — the
/// letters, the digits and the handful of characters the Fidonet spellings use
/// — but nothing is filtered out here: a name iconv cannot open is refused on
/// Enter with the reason said out loud, which is a better answer than a key
/// that silently does nothing while somebody is typing.
std::optional<std::string> typedText(const Event& event) {
    if (!event.is_character() || event.ctrl() || event.alt()) return std::nullopt;
    const std::string& input = event.character();
    if (input.empty()) return std::nullopt;
    if (input.size() == 1 && static_cast<unsigned char>(input[0]) < 0x20) {
        return std::nullopt;
    }
    return input;
}

/// The button that reads the message again, centred on a row of the box's own
/// width — measured rather than centred by a filler, since every row here is as
/// wide as it is written and one narrower than the rest would take the frame in
/// with it.
Element readButton(const AppState& state, Picker& picker, int inner) {
    const bool tall = state.dialogTallButtons();
    Element element =
        dialog::button(readLabel(), picker.focus == Focus::Button,
                       state.isPressed(AppState::Pressed::CharsetButton), tall);

    const int spare = std::max(0, inner - dialog::buttonWidth(readLabel(), tall));
    const int left = spare / 2;
    return dialog::framed(hbox({text(std::string(static_cast<size_t>(left), ' ')),
                                std::move(element) | reflect(picker.applyBox),
                                text(std::string(static_cast<size_t>(spare - left), ' '))}),
                          dialog::buttonRows(tall));
}

/// What Enter and the button both come to: the name resolved and checked, or
/// the reason there is nothing to read the message in.
///
/// Resolved the way a `default_charset` line is resolved, so that the Fidonet
/// spellings a reader of FTN mail has to hand — `+7_FIDO`, a bare `866` — are
/// names this box takes. `IBMPC` names no encoding in particular and is refused
/// here rather than quietly falling back on the area's default: it is the
/// answer the message already gave, and somebody typing it has asked for
/// nothing.
///
/// Then iconv is asked, because a name it cannot open would otherwise hand the
/// undecoded bytes back and draw CP866 one byte at a time as Latin-1. The
/// checked name is left in the field, which is what the shell reads back.
Outcome runRead(Picker& picker) {
    picker.error.clear();

    const std::string typed{config::text::trim(picker.charset)};
    if (typed.empty()) {
        picker.error = noCharset();
        return Outcome::Ignored;
    }

    const std::string resolved = encoding::CharsetDetector::normalize(typed);
    if (resolved.empty()) {
        picker.error = i18n::format(_("{0} names no charset in particular"), {typed});
        return Outcome::Ignored;
    }
    if (const auto known = encoding::checkCharset(resolved); !known) {
        picker.error = known.error()->message();
        return Outcome::Ignored;
    }

    picker.charset = resolved;
    picker.cursor = picker.charset.size();
    return Outcome::Read;
}

}  // namespace

void open(AppState& state) {
    // An empty area has no message to read, and neither has one whose body
    // could not be read at all. The button for this is drawn dimmed there.
    if (!state.readBody) return;

    Picker picker;
    picker.current = state.readBody->charset;
    picker.from = !state.readCharset.empty() ? From::Override
                  : state.readBody->charsetDeclared ? From::Kludge
                                                    : From::Default;
    // Holding the charset it is being read in, with the cursor at the end of
    // it: the commonest answer is a small edit of that name — KOI8-R for a
    // message the kludge called CP866 — and typing over what is there is one
    // keystroke either way.
    picker.charset = picker.current;
    picker.cursor = picker.charset.size();

    state.charsetPicker = std::move(picker);
    fitBox(state, *state.charsetPicker);
}

Element render(AppState& state, Element background) {
    Picker& picker = *state.charsetPicker;
    fitBox(state, picker);
    picker.cursor = std::min(picker.cursor, picker.charset.size());

    const int inner = picker.inner;
    const bool typing = picker.focus == Focus::Charset;

    picker.charsetBox = Box::Nowhere();
    picker.applyBox = Box::Nowhere();

    const int fieldWidth = std::max(1, inner - displayWidth(label()));
    Element field =
        inputField(picker.charset, picker.cursor, fieldWidth, typing,
                   typing ? theme::palette.selectionText : theme::palette.dialogLabel,
                   fieldFiller(theme::palette.dialogHint), &picker.origin) |
        bgcolor(typing ? theme::palette.selection : theme::palette.dialogField) |
        reflect(picker.charsetBox);

    Elements lines{
        dialog::titleBar(title(), inner),
        // What the message is being read in now, which is the whole reason the
        // box says anything before it asks anything: a charset to correct is
        // one the user has to be told first.
        dialog::line(currentLine(picker), inner, theme::palette.dialogLabel),
        dialog::divider(inner),
        dialog::framed(
            hbox({text(label()) | color(theme::palette.dialogLabel), std::move(field)})),
        dialog::divider(inner),
        readButton(state, picker, inner),
        dialog::bottomBar(hint(), picker.error, inner),
    };

    // dialog::surface() wipes the screen behind the box and lays the dialog's
    // own fill down in its place, so the message underneath neither shows
    // through it nor colors it.
    return dbox(
        {std::move(background), dialog::surface(vbox(std::move(lines))) | center});
}

Outcome handleEvent(AppState& state, const Event& event) {
    Picker& picker = *state.charsetPicker;

    if (const auto click = leftClick(event)) {
        if (picker.applyBox.Contain(click->x, click->y)) {
            picker.focus = Focus::Button;
            state.showClick(AppState::Pressed::CharsetButton);
            return runRead(picker);
        }
        if (picker.charsetBox.Contain(click->x, click->y)) {
            picker.focus = Focus::Charset;
            // offsetAtColumn() is measured against the scroll inputField()
            // settled on, so a click never lands inside a UTF-8 sequence.
            picker.cursor = offsetAtColumn(picker.charset, picker.origin,
                                           click->x - picker.charsetBox.x_min);
            return Outcome::Ignored;
        }
        // Anywhere else on the screen. A box one has thought better of is put
        // away by pointing away from it, and the message underneath is left
        // being read as it was.
        state.charsetPicker.reset();
        return Outcome::Dismissed;
    }

    if (event == Event::Tab) {
        picker.focus = picker.focus == Focus::Charset ? Focus::Button : Focus::Charset;
        return Outcome::Ignored;
    }
    if (event == Event::TabReverse) {
        picker.focus = picker.focus == Focus::Button ? Focus::Charset : Focus::Button;
        return Outcome::Ignored;
    }
    // Enter reads wherever the cursor is: the button is where the ring comes to
    // rest, not the only place the box can be answered from.
    if (event == Event::Return) return runRead(picker);
    if (event == Event::Escape) {
        state.charsetPicker.reset();
        return Outcome::Dismissed;
    }

    if (picker.focus == Focus::Button) {
        // ↑ goes back to the field above it, which is where the button was
        // stepped down from.
        if (event == Event::ArrowUp) {
            picker.focus = Focus::Charset;
            return Outcome::Ignored;
        }
        if (event == Event::Character(' ')) return runRead(picker);
        // Anything typed while the button has the cursor goes back into the
        // field: the name is what this box is about, and a letter meant for it
        // is not a mistake worth swallowing.
        if (typedText(event)) picker.focus = Focus::Charset;
    }

    if (picker.focus != Focus::Charset) return Outcome::Ignored;

    if (const auto typed = typedText(event)) {
        picker.charset.insert(picker.cursor, *typed);
        picker.cursor += typed->size();
        // What was said about the last Enter is about a name that has since
        // been typed over.
        picker.error.clear();
        return Outcome::Ignored;
    }
    if (event == Event::Backspace) {
        if (picker.cursor > 0) {
            const size_t from = prevChar(picker.charset, picker.cursor);
            picker.charset.erase(from, picker.cursor - from);
            picker.cursor = from;
            picker.error.clear();
        }
        return Outcome::Ignored;
    }
    if (event == Event::Delete) {
        if (picker.cursor < picker.charset.size()) {
            picker.charset.erase(picker.cursor, charLen(picker.charset, picker.cursor));
            picker.error.clear();
        }
        return Outcome::Ignored;
    }
    if (event == Event::ArrowLeft) {
        picker.cursor = prevChar(picker.charset, picker.cursor);
        return Outcome::Ignored;
    }
    if (event == Event::ArrowRight) {
        if (picker.cursor < picker.charset.size()) {
            picker.cursor += charLen(picker.charset, picker.cursor);
        }
        return Outcome::Ignored;
    }
    // ↓ steps down out of the field onto the button, the box being read
    // downwards.
    if (event == Event::ArrowDown) {
        picker.focus = Focus::Button;
        return Outcome::Ignored;
    }
    if (event == Event::Home) {
        picker.cursor = 0;
        return Outcome::Ignored;
    }
    if (event == Event::End) {
        picker.cursor = picker.charset.size();
        return Outcome::Ignored;
    }
    return Outcome::Ignored;
}

}  // namespace amberedit::ui::charset_dialog
