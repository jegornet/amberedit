#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "ui/input_field.hpp"
#include "ui/keys.hpp"
#include "ui/term/event.hpp"

/// The keys a one-line field is edited with, and the selection in it.
///
/// The compose screen's header fields and the find box's words are edited by
/// this and by nothing of their own, so a field anywhere answers the word keys,
/// Shift and the arrows, Backspace over a selection and a paste the same way.
/// What stands around a field — walking to the next one, crossing onto the
/// field beside it at an edge — is the screen's, and is answered before it asks
/// here.
///
/// The keys are the editor's: `compose.word_left`, `compose.word_right`,
/// `compose.delete_word`, `compose.delete_next_word`, `compose.line_start` and
/// `compose.line_end`, read wherever the field stands, as the reader's
/// selection mode reads them.
namespace amberedit::ui::line_edit {

/// The field being edited: its text, the cursor in it and the other end of the
/// selection, if one is down. `limit` is how many characters it takes, zero for
/// no limit — counted in characters, as the compose screen counts a name.
struct Line {
    std::string& value;
    size_t& cursor;
    std::optional<size_t>& anchor;
    size_t limit{0};
};

/// What of the field is selected — nothing where no anchor is down or it
/// stands on the cursor. Both ends are held to the text as it now stands.
[[nodiscard]] FieldSelection selection(const std::string& value, size_t cursor,
                                       const std::optional<size_t>& anchor);

/// Whether a key is one a selection is made with: Shift with an arrow, Home or
/// End.
[[nodiscard]] bool extendsSelection(const term::Event& event);

/// Whether a key types into the field — the keys that, over a selection, write
/// over it rather than beside it.
[[nodiscard]] bool typesText(const term::Event& event);

/// Shift with a key that moves: the cursor moves inside the field and the
/// anchor is put down where it stood, if none is. A field is one line, so Up
/// and Down are its two ends.
void extend(Line line, const term::Event& event);

/// The selected text, or nothing.
[[nodiscard]] std::string selectedText(const Line& line);

/// Takes the selection out, the cursor left where it began, and drops the
/// anchor either way. Says whether there was anything to take out.
bool deleteSelected(Line line);

/// Answers an editing key: the word keys, ← and →, Home and End, Backspace and
/// Delete, and the characters typed. Over a selection the keys that delete and
/// the keys that type write over it; every other key drops it. A key that
/// extends the selection is answered here as well.
///
/// false is a key that is none of them — a chord nothing here binds, a control
/// byte — for the caller to answer or pass on. A character the field has no
/// room for is answered and swallowed.
[[nodiscard]] bool editKey(Line line, const term::Event& event, const KeyMap& keys);

/// Puts `text` in at the cursor, in place of the selection: its first line
/// alone, a field holding one, and as much of it as `limit` leaves room for.
/// `text` has been through `app::pastedLines()` already.
void insert(Line line, std::string_view text);

}  // namespace amberedit::ui::line_edit
