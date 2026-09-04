#pragma once

#include "ui/term/element.hpp"
#include "ui/term/event.hpp"

#include <utility>

#include "ui/event_util.hpp"
#include "ui/theme.hpp"

/// The way from one message to the next without a keyboard: the three columns
/// down either side of the message text answer a click by going to the previous
/// or the next message, exactly as ← and → do. `ui.reader_side_taps` decides
/// whether they answer at all; the reader checks that itself.
///
/// **Nothing is drawn there until it is pressed.** The columns are the message's
/// own, text and all — they are given up for the length of a click and taken
/// back with the frame after it. What stands there for those milliseconds is the
/// pictogram below, laid over the text in the color the back button and the menu
/// button light up in, so that a click on the side of a message reads as a
/// button being pressed and not as the message flickering.
///
/// The zone is the three columns whether or not the scrollbar is in the last of
/// them: what a reader aims at is the edge of the message, and where the bar
/// happens to be drawn is not something to have to aim around.
namespace amberedit::ui::reader_side_tap {

/// Which side was pressed, and so which way the reader is being sent.
enum class Side {
    None,      ///< neither side — the click was somewhere else entirely
    Previous,  ///< the left columns: the message before this one, as ← asks for
    Next,      ///< the right columns: the message after it, as → asks for
};

/// The columns down each side that answer a click. Three, on both sides, which
/// is what the editor's delete-line button asks for as well: it is the narrowest
/// strip a finger finds without looking.
constexpr int kZone = 3;
/// The columns the pictogram takes, which is wider than the zone that raises it:
/// the glyph has a box around it and the box has a side either hand.
constexpr int kWidth = 5;
/// The rows it stands on — the two sides of the box and the glyph between them.
constexpr int kHeight = 3;

/// Which side of the text `event` landed on, given the reader's own columns
/// [`left`, `right`) and the rows of the message body — `rows` of them from
/// `top` down. `Side::None` for everything else, a click over the header block
/// or the title included: this is the text and nothing but the text.
///
/// Only the press acts, for the reason every other button here takes the press:
/// the release would arrive with the next message already loaded.
///
/// In a window too narrow for two zones side by side the left one answers, a
/// message with no room to read it in being no place to be sent onwards from.
[[nodiscard]] inline Side hit(const term::Event& event, int left, int right, int top,
                             int rows) {
    const auto click = leftClick(event);
    if (!click) return Side::None;
    if (click->y < top || click->y >= top + rows) return Side::None;
    if (click->x >= left && click->x < left + kZone) return Side::Previous;
    if (click->x < right && click->x >= right - kZone) return Side::Next;
    return Side::None;
}

/// The pictogram itself: the arrow the pressed side stands for, in a box.
///
/// Drawn whole in `animated_button_text`, the color a press on the back button
/// or on the menu button turns those into — one color for every button of the
/// interface that is being pressed, so that what the color means is the same
/// wherever it appears.
[[nodiscard]] inline term::Element icon(Side side) {
    const auto tint = term::color(theme::palette.animatedButtonText);
    return term::vbox({
        term::text("┌───┐") | tint,
        term::text(side == Side::Next ? "│ ▶ │" : "│ ◀ │") | tint,
        term::text("└───┘") | tint,
    });
}

/// `body` with the pictogram over it, against the side that was pressed and
/// halfway down the text: the message is what the reader is looking at, so the
/// answer to a click on it is shown in the middle of it rather than at a corner.
/// `Side::None` is the body untouched, which is every frame but the one a click
/// is being shown on.
///
/// It is laid over the text rather than drawn beside it — the reader gives up no
/// column for a button that is not there — and the frame after the click puts
/// back whatever it covered, whether the reader moved to another message or
/// `reader_edge stay` left it standing on this one.
[[nodiscard]] inline term::Element over(term::Element body, Side side) {
    if (side == Side::None) return body;

    term::Elements row;
    if (side == Side::Next) row.push_back(term::filler());
    row.push_back(icon(side));
    if (side == Side::Previous) row.push_back(term::filler());

    // Fillers rather than an offset worked out here: a filler paints nothing, so
    // what the pictogram does not cover is the text underneath as it was drawn.
    return term::dbox({std::move(body), term::vbox({
                                            term::filler(),
                                            term::hbox(std::move(row)),
                                            term::filler(),
                                        })});
}

}  // namespace amberedit::ui::reader_side_tap
