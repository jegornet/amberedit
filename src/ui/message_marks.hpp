#pragma once

#include <cstdint>

namespace amberedit::ui {

struct AppState;

/// The messages the user has picked out of the area being read, and the whole of
/// what can be done to that set.
///
/// A mark is the user saying "this one, and I will say later what for": it is
/// drawn as a star in the message list's `m` column and beside the numbers in
/// the reader's title, and it is nothing the message base knows about — the set
/// lives in `AppState::marks` and is emptied when the area is left.
///
/// **Everything here speaks message numbers and stores UIDs**, so nothing above
/// has to know which of the two it is holding. The conversion wants the base
/// open, which is why it is here and not on `AppState`: a mark made in an area
/// that has since been closed would be a number with nothing behind it.
namespace marks {

/// Whether that message is marked. False for a number naming no message, and
/// for every number at all while no area is open.
[[nodiscard]] bool isMarked(const AppState& state, uint32_t number);

/// Marks it where it is not and unmarks it where it is — the whole of what a key
/// on either screen does. A number naming no message does nothing.
void toggle(AppState& state, uint32_t number);

/// Every message in the area, marked.
void markAll(AppState& state);

/// Every mark taken off. The one operation that needs no base: a set emptied is
/// a set emptied whatever the area holds.
void unmarkAll(AppState& state);

/// Marked becomes unmarked and unmarked marked, message by message. Not the same
/// as emptying the set and filling it again: a mark on a message that is no
/// longer in the area is dropped here, exactly as it is dropped by every other
/// pass over the area.
void toggleAll(AppState& state);

/// Every message *after* `number`, marked — the rest of the area from where the
/// reader stands. What is already marked stays marked: this adds to the set
/// rather than replacing it, so two of these one after the other are two runs
/// marked and not the second alone.
void markNewer(AppState& state, uint32_t number);

/// Every message *before* `number`, marked, on the same terms.
void markOlder(AppState& state, uint32_t number);

}  // namespace marks
}  // namespace amberedit::ui
