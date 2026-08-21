#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace amberedit::ui {

/// Text layout helpers for the table screens.
///
/// They all measure in **terminal columns**, not in bytes and not in code
/// points. Bytes are obvious: Cyrillic would throw every column off. Code
/// points are the subtler trap — a CJK ideograph is one code point and two
/// columns, a combining accent is one code point and none — and the renderer
/// is what settles the argument, so the measuring here is the renderer's own
/// `term::stringWidth()`. Anything else and the widths we budget for are not
/// the widths that get drawn.
int displayWidth(std::string_view s);

/// The part of `s` from column `start`, `columns` columns wide. A double-width
/// glyph that would be cut in half is left out rather than half-drawn.
std::string substrByWidth(std::string_view s, int start, int columns);

/// Truncates to `columns` columns, appending an ellipsis (itself one column).
std::string truncateToWidth(std::string_view s, int columns);

/// Pads to `width` columns; a string already that wide is returned unchanged.
std::string padRight(std::string_view s, int width);
std::string padLeft(std::string_view s, int width);

/// Width of a number's decimal representation — for the number column.
int digitWidth(int64_t n);

/// A run of box-drawing horizontals `width` characters long: the rule the
/// screens use to separate their sections.
std::string horizontalRule(int width);

/// How deeply a body line is quoted, counted in '>' markers, or 0 when the
/// line is not a quote at all.
///
/// A quote opens with an optional one or two spaces, then up to six letters of
/// the quoted author's initials, then an optional '-', then the markers, then a
/// space. That last space is what tells `> like this` apart from text that
/// merely starts with a greater-than sign, so it is required. The '-' is there
/// for `-> like this`, which is how QWK gateways quote. Initials may be
/// Cyrillic: Russian echoes write them that way as a matter of course.
int quoteDepth(std::string_view line);

/// Breaks text to a given width, keeping line breaks and not splitting words.
/// Words longer than width are split by force.
std::vector<std::string> wrapText(std::string_view text, int width);

/// Where each row of `line` begins, in bytes, when the line is shown `width`
/// columns wide: `{0}` for a line that fits, one offset per row for one that
/// does not.
///
/// This is `wrapText` for a line that is being written rather than read. The
/// offsets divide the line and nothing else — every byte of it stands on
/// exactly one row — so a cursor can be found among them by counting, which is
/// what an editor needs and a reader does not. That is also why the blanks a
/// break falls on stay at the end of the row they close: they draw nothing
/// against the right edge, and dropping them the way `wrapText` does would
/// leave bytes with no row to be on. A word longer than the width is cut where
/// the width falls, having nowhere else to break.
std::vector<size_t> softWrapOffsets(std::string_view line, int width);

/// Byte ranges, [begin, end), of the links in a line, in the order they appear.
///
/// Only `http://`, `https://` and `ftp://` count. A bare `www.` or a domain on
/// its own is left alone: guessing there means coloring ordinary words, and a
/// message that discusses `google.com` is not offering a link. A link runs to
/// the first space, less any trailing punctuation that reads as the sentence's
/// rather than the link's — a closing bracket is kept when the link opened one
/// itself, since those belong to the address.
std::vector<std::pair<size_t, size_t>> findLinks(std::string_view line);

/// One emphasised phrase in a body line: the byte range it covers, its markers
/// included, and the marker that opened it — `_` for underlined, `*` for bold,
/// `/` for italic, `#` for inverted.
struct StyleSpan {
    size_t begin{0};
    size_t end{0};
    char marker{'\0'};
};

/// The emphasised phrases in a line, in the order they appear — the style
/// codes FTN messages have always been written with, `*like this*`.
///
/// A marker only opens a phrase where a word could start: at the beginning of
/// the line or after a space or an opening bracket, and with a non-space after
/// it. It only closes one after a non-space and before a space, a closing
/// bracket, sentence punctuation or the end of the line. That is what keeps
/// `2*3*4`, `/usr/local/bin`, `snake_case_names` and `#include <stdio.h>` out
/// of it — the markers in those stand inside words or never close, and text
/// that merely contains one is not marked up. A phrase may hold spaces, so
/// several words can be emphasised together.
///
/// The spans never overlap and are never nested: the first marker to open wins
/// and the rest of its phrase is taken as written, so `*_both_*` comes out
/// bold with the underscores plainly in it.
std::vector<StyleSpan> findStyleSpans(std::string_view line);

}  // namespace amberedit::ui
