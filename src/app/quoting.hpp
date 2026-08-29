#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace amberedit::app {

/// A quote prefix as found at the head of a line: " AB> ", ">> ", "  ABC>>> ".
struct QuotePrefix {
    /// The initials between the leading spaces and the markers, empty when
    /// the line carries none.
    std::string initials;
    /// How many '>' the line carries, 0 when it is not a quote at all.
    int level{0};
    /// Bytes the prefix occupies, the space after the markers included.
    size_t length{0};
};

/// Reads the quote prefix off a line, by the same rules ui::quoteDepth()
/// recognises one: up to two leading spaces, up to six letters of initials, an
/// optional '-' for the `-> ` that QWK gateways quote with, the markers, then a
/// mandatory space. Anything else means level 0 — what we write has to be what
/// our own reader reads back as a quote.
[[nodiscard]] QuotePrefix parseQuotePrefix(std::string_view line);

/// The prefix a line of `author`'s message is quoted with, from the configured
/// quote string: F is the first letter of the first name, L of the last, and M
/// stands for the letters of every name between them. "The Lord of the Rings"
/// with " FL> " gives " TR> " and with " FML> " gives " TLotR> ".
///
/// The leading spaces are the quote string's own, and exactly one space closes
/// the prefix whether the quote string spells it out or not.
[[nodiscard]] std::string quotePrefixFor(std::string_view quoteString,
                                         std::string_view author);

/// The prefix for a line that is a quote already: its own initials, one more
/// marker, and the leading spaces the quote string asks for.
[[nodiscard]] std::string deeperQuotePrefix(std::string_view quoteString,
                                            const QuotePrefix& prefix);

/// The body of a message being answered, quoted.
///
/// A line that is a quote already gains a level and keeps whose initials it
/// carries; anything else is prefixed with the author's. A line that would run
/// past `margin` characters, prefix and all, is wrapped onto further lines
/// under the same prefix.
///
/// A line carrying nothing — empty, all spaces, or a quote prefix and no more
/// — comes out empty rather than quoted: there is nothing there to answer, and
/// an empty quote reads worse than a break in the text. The break itself is
/// kept, because it is how the answered message was paragraphed; a run of
/// several such lines comes out as one.
///
/// Measured in characters rather than terminal columns: the margin is about
/// how long the line is in the message, and in the single-byte charsets FTN
/// messages are stored in that is one byte per character.
///
/// With `unwrap` set, the lines a paragraph was hard-wrapped into are joined
/// back together before any of that happens, so that the quote is wrapped at
/// `margin` rather than at whatever margin it was written to. The lines joined
/// are the ones standing together at one quote level under one set of initials,
/// where the line above had no room left for the next line's first word — the
/// width to measure that against being the longest line of the run. Nothing
/// indented, tabbed, spaced into columns or beginning a bullet or a numbered
/// point is joined: those breaks are the writer's, not the wrap's.
[[nodiscard]] std::vector<std::string> quoteLines(const std::vector<std::string>& lines,
                                                  std::string_view author,
                                                  std::string_view quoteString,
                                                  int margin, bool unwrap = false);

}  // namespace amberedit::app
