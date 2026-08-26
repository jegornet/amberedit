#pragma once

#include <cstdint>

#include "ui/app_state.hpp"
#include "ui/term/element.hpp"
#include "ui/term/event.hpp"

/// The list of messages the reader puts up one side of itself in a window wide
/// enough for it — `reader_sidebar_threshold`, and `AppState::readerSidebarShown()`
/// is the whole of the question; which side is `reader_sidebar_position`, and
/// nothing but the order of the two columns turns on it. What a row holds is
/// `reader_sidebar_msglist_format`, laid out by `ui/msg_list_format.*` and drawn
/// by the same `drawLine()` the message list draws its rows with, so a message
/// reads the same in the panel as it does in the table.
///
/// **It is never what the keyboard is talking to.** There is no cursor of its
/// own and no focus to give it: the message the reader is showing is the one
/// the panel marks, every key on the screen is the reader's, and the panel
/// answers a click and the wheel over it and nothing else. Two lists of
/// messages on one screen, each with a cursor, is two places for the reading to
/// be, and the reading is in the reader.
namespace amberedit::ui::reader_sidebar {

/// The panel, `AppState::readerSidebarWidth()` columns of it, with the rule that
/// closes it off in the column beside — after it where the panel stands left of
/// the message, before it where it stands right — so what comes back is one
/// column wider than the panel and stands the whole height of the screen.
[[nodiscard]] term::Element render(AppState& state);

/// Scrolls the panel so that message `number` is on it: a step off either edge
/// moves it a row, and a jump to somewhere else altogether opens it around
/// where the jump landed. Called as each message is loaded, in a narrow window
/// as well as a wide one — the panel then has the right place to open at when
/// the window is dragged out to where it fits.
void follow(AppState& state, uint32_t number);

/// Which message a click landed on, counted from one, and zero where it landed
/// anywhere else — off the panel, on a blank row past the end of the area, or
/// on a row whose header has not been read yet. Any line of a row is that row:
/// the subject under a name is the same message as the name.
[[nodiscard]] uint32_t clickedMessage(const AppState& state, const term::Event& event);

/// Answers the wheel turned over the panel by scrolling it, the reader and the
/// message in it left exactly as they were. True where the event was that.
///
/// Through `list_wheel_throttle` like the lists, so a flick moves the panel as
/// far as it moves the text beside it. Scrolling is not choosing: what the
/// reader shows changes when a row is clicked and not before, and the mark
/// stays on the message that is on the screen.
bool wheeled(AppState& state, const term::Event& event);

}  // namespace amberedit::ui::reader_sidebar
