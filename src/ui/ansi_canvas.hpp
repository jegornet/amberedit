#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "ui/bbs_codes.hpp"

/// BBS ANSI graphics: the escape sequences a message drew a picture with on the
/// terminal the caller dialled in on, replayed here onto a canvas the reader can
/// scroll.
///
/// **A message written this way is not text with colors in it — it is a
/// sequence of drawing operations**, and that is what separates this from
/// `bbs_codes`. A pipe code says "the rest of this line is green" and leaves the
/// text where it stood; `ESC[12;30H` says "carry on drawing at row 12, column
/// 30", and the bytes that follow it belong nowhere in the message's own line
/// order. So nothing here can be done a line at a time the way `stripRenegade()`
/// works: the whole visible body is one stream, it is played out onto a grid of
/// cells, and only the grid — after every move, overwrite and erase the message
/// asked for — is a picture with lines in it.
///
/// The grid is what `render()` hands back, as the `bbs::CodedLine` the reader
/// already knows how to draw: one line per row, the glyphs in it, and where the
/// color changes along it. Everything downstream of that is the ordinary body
/// path.
///
/// Two things do not survive the replay and cannot:
///
/// - **Nothing wraps.** A row of a picture is as wide as it is; broken to fit a
///   narrow window it would stop being the picture. `render()` is given the
///   window's width and cuts each row there instead, so a window narrower than
///   the art shows the left of it and the rest is simply not on screen.
/// - **No other markup applies.** `reader_stylecodes` and `bbs_codes_renegade`
///   are off inside a canvas however the config has them set: a `*` or a `|07`
///   in a picture is a glyph somebody drew with, and reading it as markup would
///   take it out of the art.
namespace amberedit::ui::ansi {

/// The width the canvas is drawn on, and the column output wraps at.
///
/// 80 is not a default here, it is the format: BBS art was composed on an
/// 80-column terminal and the files say so, printing straight through the
/// right-hand edge and expecting the next glyph on the next row. Art that runs
/// to column 80 and carries on — which is most of it, since that is where a
/// border goes — comes out as one long row and nothing under it unless the wrap
/// happens exactly here.
inline constexpr int kColumns = 80;

/// How tall the canvas may grow. Only a guard: a message is free to say
/// `ESC[999999B` and a reader that believed it would allocate for it. No
/// picture — and no message — reaches this.
inline constexpr int kMaxRows = 4096;

/// Whether the text holds an escape sequence at all: a CSI (`ESC [ … final`),
/// an OSC (`ESC ] … BEL` or `… ESC \`), one of the single-character ESC
/// commands, or the DEC cursor save and restore.
///
/// This is what decides, per message and not per area, whether the canvas is
/// used: the option says an echo *may* carry ANSI, and most of the messages in
/// such an echo still do not. One that does not is read the ordinary way, with
/// its quoting, its style codes and its wrapping intact.
[[nodiscard]] bool containsCodes(std::string_view text);

/// The bytes of one escape sequence beginning at `at`, or 0 where what stands
/// there does not complete one — a lone ESC at the end of the message, an ESC
/// before an ordinary character, an unterminated OSC.
///
/// Public because this is what `containsCodes()` reads a message for: a whole
/// sequence is the evidence that somebody drew with ANSI here, and half of one
/// is not. The replay steps over a little more than this — the opening of a CSI
/// that never finishes — because there the question is whether a byte is a
/// glyph, and the digits of a code nobody can act on are not one.
[[nodiscard]] size_t escapeLength(std::string_view text, size_t at);

/// Plays `stream` out onto the canvas and hands back its rows, each cut to
/// `columns` columns.
///
/// `stream` is the visible body with its line breaks in it, and a break is read
/// as the terminal read it: back to column one and down a row. That is what the
/// art was drawn against — the files are written as chunks that undo the
/// newline with an `ESC[A` and carry on — so it cannot be a line-per-line walk
/// however much it looks like one.
///
/// Trailing blank rows and trailing blank cells come off: a picture that
/// finished by moving the cursor below itself has nothing there, and a row
/// padded to 80 would paint the theme over with blanks. A blank cell carrying a
/// background is not blank and stays.
[[nodiscard]] std::vector<bbs::CodedLine> render(std::string_view stream, int columns);

}  // namespace amberedit::ui::ansi
