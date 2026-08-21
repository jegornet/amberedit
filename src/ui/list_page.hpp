#pragma once

#include <algorithm>

namespace amberedit::ui {

/// Where PageUp puts the cursor in a list showing `rows` rows from `offset`.
///
/// Two stops rather than one jump: a cursor partway down the window goes to
/// the top visible row first — the rows above it are on the screen already,
/// so that is a movement the eye can follow — and only from the top row does
/// the key turn the page. The page is a row short of the window so the row
/// that was on top stays in view at the bottom, which is what says the two
/// screenfuls join up.
///
/// The answer may run past the start of the list; the caller's clamping is
/// what stops it there, as it does for every other movement.
[[nodiscard]] inline int pageUpTarget(int cursor, int offset, int rows) {
    if (cursor > offset) return offset;
    return cursor - std::max(1, rows - 1);
}

/// Where PageDown puts the cursor — the same two stops the other way round:
/// the bottom visible row first, a page down from it. `total` is what says
/// which row is the bottom one when the list ends above the window's last row.
[[nodiscard]] inline int pageDownTarget(int cursor, int offset, int rows, int total) {
    const int bottom = std::min(offset + rows - 1, total - 1);
    if (cursor < bottom) return bottom;
    return cursor + std::max(1, rows - 1);
}

}  // namespace amberedit::ui
