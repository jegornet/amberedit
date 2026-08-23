#pragma once

#include <optional>
#include <string>

#include "ui/app_state.hpp"
#include "ui/term/element.hpp"
#include "ui/term/event.hpp"

/// The last row of the screen: the commands of whichever screen is up, each
/// behind the key that runs it — `q reply  n reply elsewhere  e new`. There is no help
/// screen, and this quiet row is what stands in for one.
///
/// **The keys come from the layout**, never from a letter written here: a
/// command the layout leaves unbound is left out of the row entirely, and one
/// with several keys is shown under the shortest of them
/// (`KeyMap::preferredKey`). A row that named a key nothing runs would be worse
/// than no row at all. The key is written in the case the row is —
/// `hint_bar_capitalize`, by way of `hintSpellingOf()` — and the word beside it
/// is the one the menu writes on a button for the same command,
/// `config::Commands` answering for both. Where the hints stand in the row is
/// `hint_bar_align`.
///
/// Which commands each screen offers is the config's — `arealist_hints`,
/// `msglist_hints`, `reader_hints`, `compose_hints` — and each of them starts as
/// a reminder of what there is to do rather than as every binding the screen
/// has, which would be a page rather than a line. The message list starts with
/// none, every key on it moving the cursor, and its row is the rule alone rather
/// than given back, so that the screens on either side of it are not a row
/// taller.
namespace amberedit::ui::hint_bar {

/// The row's text, without the padding it is drawn with: `text(state)` is what
/// a test reads and what `render()` puts on the screen.
[[nodiscard]] std::string text(const AppState& state);

/// The row itself: the hints in the hint bar's own color, set into a rule that
/// runs to the edge of the screen in `separator` — the rule closes the bottom of
/// the interface the way the one under a screen's headings closes its top, and
/// the hints are a label in it. Which side of them it runs along is
/// `hint_bar_align`. A screen with nothing to say leaves it whole.
///
/// `AppState::hintBarShown()` decides whether it is drawn at all, and `runApp()`
/// takes the row off the screens' height before they lay themselves out in what
/// is left.
///
/// Where each hint landed is left in `AppState::hintSpots` on the way, which is
/// what `clicked()` answers a click against.
[[nodiscard]] term::Element render(AppState& state);

/// What a click on the row asks for: the key the hint under the pointer is
/// written under, or nothing where the click landed on no hint — including
/// every click that is not on this row at all.
///
/// A hint is pressed like any other button, animation and all, and then answers
/// with its key rather than reaching into the screen itself: what the row says
/// about a command is which key runs it, and the click does exactly that key.
/// The caller hands what comes back to the screen that is up.
[[nodiscard]] std::optional<term::Event> clicked(AppState& state,
                                                 const term::Event& event);

}  // namespace amberedit::ui::hint_bar
