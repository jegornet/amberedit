#pragma once

#include <string>
#include <vector>

#include "ui/app_state.hpp"
#include "ui/term/element.hpp"
#include "ui/term/event.hpp"

/// What an `extern_utilN -printresult` utility wrote, in a box of its own once
/// it has ended — its standard output and its standard error together, as a
/// terminal would have shown them.
///
/// It shows and does not ask, as the info and help boxes do. Of its two buttons
/// OK is the one Enter is on as it opens — what is in it was said by somebody
/// else's program, and OK is the user having read it — and Copy left of it puts
/// the output on the clipboard `clipboard` names and leaves the box standing,
/// as Ctrl-C does from anywhere in it. ← → and Tab move Enter between the two;
/// ↑ ↓, the page keys, Home, End and the wheel scroll the output where it is
/// longer than the box; Esc, `o` and OK put it away, and so does a click
/// outside it.
namespace amberedit::ui::output_dialog {

/// Puts the box up over whatever screen ran the utility, called `title`. Empty
/// `lines` are said to be empty rather than shown as a blank box, and `cut` says
/// at the foot that only the front of what was written is here.
void open(AppState& state, std::string title, std::vector<std::string> lines, bool cut);

/// Draws it over `background`.
[[nodiscard]] term::Element render(AppState& state, term::Element background);

/// What Copy puts on the clipboard: the lines as the program wrote them, one
/// to a line feed, and not as the box wrapped them. Empty where the program
/// wrote nothing.
[[nodiscard]] std::string copiedText(const AppState::OutputView& view);

/// Answers a key or a click. The box closes itself — `state.outputView` is
/// empty afterwards.
void handleEvent(AppState& state, const term::Event& event);

}  // namespace amberedit::ui::output_dialog
