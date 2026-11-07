#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "config/app_config.hpp"
#include "ui/click_count.hpp"
#include "ui/input_field.hpp"
#include "ui/keys.hpp"
#include "ui/term/box.hpp"
#include "ui/term/element.hpp"
#include "ui/term/event.hpp"
#include "ui/text_editor.hpp"

namespace amberedit::ui {

/// A line of text that is typed into in a dialog, with everything it takes to
/// draw one and to answer a key or the pointer in it.
///
/// `inputField()` draws a field and works out where a click in one landed;
/// `ui/line_edit.hpp` is what edits one. This is the state that goes with them
/// — what has been typed, where the cursor is, the selection, how far the text
/// has scrolled and where the field was last drawn — and the rules every box
/// that is typed into answers by: the editor's word keys, Shift and the
/// arrows, a drag, a double click for a word and a triple one for the whole,
/// copy, cut and paste. The compose screen's header fields are edited by
/// `line_edit` directly, their selection being part of the editor's.
struct TextField {
    std::string value;
    size_t cursor{0};
    /// The other end of what is selected, the cursor being this end.
    std::optional<size_t> anchor;
    /// The byte the leftmost drawn column shows, written back by `inputField`.
    size_t origin{0};
    /// Where it was last drawn, for the click that comes after.
    term::Box box{term::Box::Nowhere()};
    /// Whether anybody has typed in it.
    ///
    /// A wizard puts a guess in a field and must know not to put it there
    /// again: going back to correct an address re-guesses the charset it
    /// suggested, but only while the user has left that guess alone.
    bool touched{false};

    /// A drag of the pointer across the field, from the press that began it to
    /// the release; and the selection a press landed inside, held for the drag
    /// to grow.
    bool dragging{false};
    std::optional<HeldSelection<size_t>> held;
    /// The presses on the field so far, for a double and a triple click.
    ClickRun clicks;
};

/// What a field is answered with from around it: the layout its keys are read
/// from, the clock a double click is counted by, and where a copy goes and a
/// paste comes from. Built afresh for each event by whoever owns the field —
/// `dialog_field::contextOf()` for a dialog over a screen, the wizard for its
/// own.
struct FieldContext {
    const KeyMap& keys;
    Millis now{0};
    config::CopyOnSelect copyOnSelect{config::CopyOnSelect::Mouse};
    /// Takes what is copied, and whether the copy is one a selection made by
    /// itself and nobody asked to be told of.
    std::function<void(std::string text, bool quiet)> copy;
    /// What a paste brings, here and now, or nothing where it arrives later —
    /// a clipboard the shell asks for, which then calls `pasteIntoField()`.
    std::function<std::optional<std::string>()> paste;
    /// How many characters the field takes, zero for no limit.
    size_t limit{0};
};

/// Puts a value in as though it had always been there — the cursor at the end of
/// it, nothing selected and nobody having typed.
void setFieldValue(TextField& field, std::string value);

/// Selects the whole of what the field holds, the cursor at the end of it.
void selectAllOf(TextField& field);

/// What of the field is selected, or nothing.
[[nodiscard]] FieldSelection fieldSelection(const TextField& field);

/// Answers a key in the field: the characters, Backspace and Delete, the
/// arrows, Home and End, the word keys, Shift and the arrows, copy, cut and
/// paste. True where the key was one of them, so the caller can go on to
/// whatever else it does with the keys that are left. ↑ and ↓ are not among
/// them: a box walks its stops by them.
[[nodiscard]] bool handleFieldKey(TextField& field, const term::Event& event,
                                  const FieldContext& context);

/// Answers the pointer in the field: a press on it puts the cursor there and
/// begins a drag, or selects the word and then the whole on a second and a
/// third; a movement carries the drag on and a release ends it. True where the
/// event was the field's — a press inside its box, or any part of a drag under
/// way.
[[nodiscard]] bool handleFieldMouse(TextField& field, const term::Event& event,
                                    const FieldContext& context);

/// Puts what a paste brought in at the cursor, in place of the selection: its
/// first line, a field holding one, made safe to carry by `app::pastedLines()`.
void pasteIntoField(TextField& field, std::string_view text, size_t limit = 0);

/// The field, drawn the way every box that is typed into is drawn: `dialog_field`
/// at rest and `dialog_focused_field` while it is the one being typed in, with
/// the room it has left underscored and what is selected of it in `selection`.
[[nodiscard]] term::Element renderField(TextField& field, int width, bool active);

}  // namespace amberedit::ui
