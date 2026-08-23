#pragma once

#include <cstddef>
#include <string>

#include "ui/term/box.hpp"
#include "ui/term/element.hpp"
#include "ui/term/event.hpp"
#include "ui/theme.hpp"

namespace amberedit::ui {

/// A line of text that is typed into, with everything it takes to draw one and
/// to answer a key or a click in it.
///
/// `inputField()` draws a field and works out where a click in one landed; this
/// is the state that goes with it — what has been typed, where the cursor is,
/// how far the text has scrolled and where the field was last drawn — and the
/// two keystroke and click rules that every field in AmberEdit answers by.
///
/// It is here because the setup wizard has six of them. The compose screen, the
/// import dialog and the find box each keep their own copy of this arithmetic
/// and are not moved onto it in the same breath: they work, and a wizard is no
/// reason to reopen them.
struct TextField {
    std::string value;
    size_t cursor{0};
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
};

/// Puts a value in as though it had always been there — the cursor at the end of
/// it and nobody having typed.
void setFieldValue(TextField& field, std::string value);

/// Answers a key in the field: the characters, Backspace and Delete, the arrows,
/// Home and End. True where the key was one of them, so the caller can go on to
/// whatever else it does with the keys that are left.
[[nodiscard]] bool handleFieldKey(TextField& field, const term::Event& event);

/// Puts the cursor where the click landed, `x` being the column it was in.
void clickField(TextField& field, int x);

/// The field, drawn the way every box that is typed into is drawn: the fill of a
/// field at rest or of the selection when it is the one being typed in, with the
/// room it has left underscored.
[[nodiscard]] term::Element renderField(TextField& field, int width, bool active);

}  // namespace amberedit::ui
