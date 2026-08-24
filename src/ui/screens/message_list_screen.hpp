#pragma once

#include "ui/term/event.hpp"
#include "ui/term/element.hpp"

#include <cstdint>

#include "ui/app_state.hpp"

/// The message list screen for one area. What a row holds is `msglist_format`'s
/// — by default the number, the names and the stamp, with the subject beside them
/// in a wide window and on a line of its own in a narrow one.
namespace amberedit::ui::screens::message_list {

/// The table, laid out by `msglist_format` — non-const because the offset is
/// settled here: how many messages a screen holds depends on how tall a row is,
/// and that changes with the window.
term::Element render(AppState& state);
bool handleEvent(AppState& state, const term::Event& event);

/// Loads a window of headers around the current position if it went stale.
/// render() calls this too, hence the non-const state.
void ensureHeaders(AppState& state);

/// The same, for a run of `count` messages beginning at index `first` — what
/// the reader asks for, the list being scrolled somewhere else entirely while
/// it walks. There is one window of headers and whoever asks last has it: the
/// two screens are never both in front of the user, and each asks for its own
/// as it draws.
void ensureHeaders(AppState& state, int first, int count);

/// Puts the current message about halfway down the list rather than wherever
/// the previous scrolling position leaves it. For the moments the list is
/// arrived at rather than moved about in — opening an area, and bringing the
/// list up on the message being read — where the lastread mark otherwise lands
/// on the bottom row with the new messages, the ones there to be read, below
/// the screen. Moving within the list scrolls a row at a time as before.
///
/// The header window is read from the offset, so ensureHeaders() belongs after
/// this rather than before it.
void centerCursor(AppState& state);

/// Opens an area and moves the navigator into it — to the reader, positioned
/// at the lastread message, or to this list when the area is empty. A failure
/// says why the base did not open and leaves the navigator where it was.
[[nodiscard]] Result<void> enterArea(AppState& state, const domain::AreaConfig& area);

/// Closes the current area and returns to the area list, dropping both the
/// loaded headers and whatever the reader was showing.
void leaveArea(AppState& state);

/// The header of message msgNumber (1-based) from the loaded window, or
/// nullptr if it is not in the window.
const domain::MessageHeader* headerAt(const AppState& state, uint32_t msgNumber);

}  // namespace amberedit::ui::screens::message_list
