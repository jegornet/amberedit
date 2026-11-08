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
/// It shows and does not ask, as the info and help boxes do, and the one button
/// it carries is there to say so: what is in it was said by somebody else's
/// program, and OK is the user having read it. The arrows, the page keys, Home,
/// End and the wheel scroll it where it is longer than the box; Enter, Esc, `o`
/// and the button put it away, and so does a click outside it.
namespace amberedit::ui::output_dialog {

/// Puts the box up over whatever screen ran the utility, called `title`. Empty
/// `lines` are said to be empty rather than shown as a blank box, and `cut` says
/// at the foot that only the front of what was written is here.
void open(AppState& state, std::string title, std::vector<std::string> lines, bool cut);

/// Draws it over `background`.
[[nodiscard]] term::Element render(AppState& state, term::Element background);

/// Answers a key or a click. The box closes itself — `state.outputView` is
/// empty afterwards.
void handleEvent(AppState& state, const term::Event& event);

}  // namespace amberedit::ui::output_dialog
