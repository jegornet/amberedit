#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "ui/term/color.hpp"

/// The BBS color codes a message may be written with — "pipe codes", after the
/// character that opens them. Only the Renegade/Telegard dialect for now: a
/// pipe and two decimal digits, `|00` to `|31`, each naming a foreground or a
/// background out of the sixteen the DOS text mode had.
///
/// They are markup the reader takes out of the text rather than markers it
/// leaves standing, which is what tells them apart from the style codes in
/// `text_layout`: `*bold*` reads as itself in a terminal that cannot draw it,
/// while `|10` never meant anything but a color. Taking them out is also what
/// forces the parsing to happen **before** the body is wrapped — a code is
/// three bytes and no columns, and a line measured with them in it wraps
/// several columns early.
namespace amberedit::ui::bbs {

/// The color a code leaves in force: an index into the terminal's first
/// sixteen palette entries, or -1 for "whatever the line would have been drawn
/// in" — the theme's message, quote or trailer color, which is what a message
/// with no codes in it keeps.
///
/// Foreground and background are separate because the codes are: `|15|17` is
/// two of them, and each says nothing about the other.
struct Color {
    int fg{-1};
    int bg{-1};

    [[nodiscard]] bool plain() const { return fg < 0 && bg < 0; }
    bool operator==(const Color& other) const { return fg == other.fg && bg == other.bg; }
    bool operator!=(const Color& other) const { return !(*this == other); }
};

/// A color and the byte of a line it takes effect at, the runs of a line
/// standing in order.
struct ColorRun {
    size_t begin{0};
    Color color;
};

/// A line with its codes taken out: the text as it is to be drawn, and where
/// the color changes within it.
struct CodedLine {
    std::string text;
    std::vector<ColorRun> runs;
};

/// Takes the Renegade codes out of one line of a message.
///
/// **A line begins in no color of its own.** A code reaches to the end of the
/// line it stands on and no further: the line the message ends with a newline
/// is a line the reader colors from the theme again — its message, quote,
/// trailer or kludge color, whichever that line is. A terminal would carry the
/// attribute on, but a terminal has no quoting to keep intact and no wrapping
/// of its own, and a message that opens a color and never closes it would
/// otherwise repaint everything under it, the tearline and the origin included.
/// Wrapping is the other half of the same rule and is the one place the color
/// does carry — see `runsForRows()`.
///
/// A pipe that does not open a code — one at the end of the line, one followed
/// by anything but two digits, one whose number is past 31 — is text and stays
/// where it is. That is the whole of the error handling: a message writing
/// `a|b` or `|99` meant those characters, and a reader that swallowed them
/// would be losing text to guess at markup.
[[nodiscard]] CodedLine stripRenegade(std::string_view line);

/// The runs of `coded` cut up between `rows` — the pieces `wrapText` broke
/// `coded.text` into — with each row opening in the color in force where it
/// begins. One vector per row, and an empty one wherever the row is drawn in
/// the line's own color throughout.
///
/// This is where a color does cross a line ending: the rows are one line of the
/// message, broken to fit the window, and a break the window happened to fall
/// on must not change what the message looks like. A break the *message* wrote
/// is the other case, and `stripRenegade()` has it.
///
/// The rows are found in the text rather than tracked while it is wrapped:
/// `wrapText` hands back substrings of what it was given, in order, dropping
/// only the blanks a break falls on, so walking forward from the end of one row
/// to the start of the next lands on it. Doing it this way is what keeps the
/// wrapping itself unaware of colors — the layout of a message must not depend
/// on whether the config asked for them.
[[nodiscard]] std::vector<std::vector<ColorRun>> runsForRows(
    const CodedLine& coded, const std::vector<std::string>& rows);

/// The palette entry a code's color index names. Only ever 0-15, the sixteen
/// the terminal has of its own, so a theme is not involved and neither is the
/// 256-color half of the palette.
[[nodiscard]] term::Color paletteColor(int index);

}  // namespace amberedit::ui::bbs
