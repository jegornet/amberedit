#pragma once

#include <string>

#include "ui/app_state.hpp"
#include "ui/input_field.hpp"
#include "ui/term/event.hpp"

/// The reader's selection mode: `reader.select`, a cursor standing in the
/// message on screen and text selected to be copied out of it.
///
/// **It reads and never writes.** The cursor walks the rows as the reader has
/// laid them out and the header fields as the editor walks its own — the arrows,
/// Tab and Shift-Tab, the word and line keys the editor binds — and a selection
/// is made with Shift and the same keys, with the bare keys once Space has put
/// the anchor down, or by dragging the pointer. Nothing
/// typed reaches the message, and the reader's own commands are not answered
/// while the mode is on: a key either moves the cursor, copies, or leaves. The
/// three answers are the exception, and the reader takes them before this sees
/// them — a reply begun here quotes what is selected, and ends the mode.
///
/// Copying is the reader's copy key or Enter, and it leaves the mode, the work
/// being done; Esc and the mode's own key leave it with nothing copied. A
/// selection copies itself as `clipboard_copy_on_select` says, exactly as one in
/// the editor does, and that leaves nothing: the copy was not asked for.
///
/// The state is `AppState::readSelect`, and it lives as long as the message on
/// screen — `loadMessage()` ends the mode, whatever way another message is
/// reached.
namespace amberedit::ui::reader_select {

/// Begins the mode over the message on screen, the cursor at the start of the
/// first row the window shows. Nothing where there is no message.
void start(AppState& state);

/// Ends it, with whatever was selected dropped.
void stop(AppState& state);

/// Copies what is selected, where anything is, and ends it — the ✓ the menu
/// button turns into while the mode is on. A button pressed is asked to finish,
/// so it finishes with nothing selected too, where Enter would wait.
void finish(AppState& state);

/// Takes the rows the reader has just laid out again, the cursor held to them.
/// A selection is dropped: its ends were places in rows that are not there any
/// more.
void sync(AppState& state);

/// What a header field holds as the reader shows it, in `compose::Field`
/// numbering — the To address empty outside netmail, as the block leaves it.
[[nodiscard]] std::string fieldValue(const AppState& state, int field);

/// What of `field` is selected, or nothing where the cursor is elsewhere.
[[nodiscard]] FieldSelection fieldSelection(const AppState& state, int field);

/// What of row `row` of the reader's lines is selected, counted from its start.
/// A row whose break the selection takes in reaches a byte past its end, which
/// is how the rest of the row is drawn selected — `ui::inputField()`'s rule.
[[nodiscard]] FieldSelection rowSelection(const AppState& state, int row);

/// Whether anything at all is selected, in whichever half the cursor is in.
[[nodiscard]] bool hasSelection(const AppState& state);

/// What is selected, as it goes onto the clipboard: a row the window broke is
/// joined back onto the one below with what the break took out, and only a
/// break the message itself made is a line feed.
[[nodiscard]] std::string selectedText(const AppState& state);

/// A press and a drag of the pointer carried on — the movement and the release
/// after a press on a field or in the text began one. true where it was.
bool dragged(AppState& state, const term::Event& event);

/// Every other event while the mode is on. Answers true for all of them: what
/// the mode does not use, it swallows.
bool handleEvent(AppState& state, const term::Event& event);

}  // namespace amberedit::ui::reader_select
