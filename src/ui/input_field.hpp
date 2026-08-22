#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "ui/term/element.hpp"
#include "ui/theme.hpp"

/// One line of text that is typed into, and the UTF-8 arithmetic a cursor
/// standing in it needs.
///
/// There is one of these because there is one way a field behaves: the cursor
/// is drawn as an inverted character — the terminal's own is hidden for the
/// whole application — text wider than the column scrolls sideways to keep it
/// on screen, and where a click lands is worked out from the same scroll that
/// drew the row. The compose screen's header fields and the import dialog's
/// path and charset boxes all come through here, so the hit test cannot drift
/// away from what was drawn.
namespace amberedit::ui {

/// Where the character before `at` starts. Continuation bytes are stepped over
/// so that Backspace and ← move by characters: a Cyrillic letter is two bytes,
/// and deleting one of them would leave the text invalid.
[[nodiscard]] size_t prevChar(const std::string& text, size_t at);

/// How many bytes the character at `at` occupies.
[[nodiscard]] size_t charLen(const std::string& text, size_t at);

/// How many characters `text` holds. Continuation bytes are not characters of
/// their own, which is the whole difference between this and `size()` — and the
/// difference between a Cyrillic name of 35 letters fitting and being refused
/// halfway.
[[nodiscard]] size_t charCount(std::string_view text);

/// The byte the character standing at display column `column` starts at, in a
/// field whose drawn text begins at byte `origin`. A column past the end of what
/// is written answers with the end of it, which is where a click on the blank
/// part of a field puts the cursor.
[[nodiscard]] size_t offsetAtColumn(const std::string& value, size_t origin, int column);

/// The field itself, `width` columns wide and written in `tint`.
///
/// The active one carries the cursor and is the only one that scrolls: an
/// inactive field shows what fits from its start. A cursor past the end of the
/// text stands at the end of it. `origin`, where one is passed,
/// comes back with the byte the leftmost column shows — which is what a click
/// has to be answered against, and is known only here, this being where the
/// sideways scroll is decided.
///
/// `filler` is the color the columns nothing has been typed into yet are
/// underscored in — the room the field has left, said the way a paper form says
/// it. It is optional because this draws two different things: a box asking for
/// something, which wants them, and a row of the message text under the cursor,
/// which is edited the same way and is not a box at all. Passing no color pads
/// with blanks, as everything did before there were any.
[[nodiscard]] term::Element inputField(const std::string& value, size_t cursor, int width,
                                       bool active, theme::Color tint,
                                       std::optional<theme::Color> filler = std::nullopt,
                                       size_t* origin = nullptr);

/// `color` where the theme asks for the underscores and nothing where it does
/// not — `input_filler_show`. What a field passes as its `filler`: whether it
/// wants them is the field's answer, whether the theme draws them is the
/// theme's, and this is where the two meet, so that no screen has to ask about
/// the setting itself.
[[nodiscard]] std::optional<theme::Color> fieldFiller(theme::Color color);

}  // namespace amberedit::ui
