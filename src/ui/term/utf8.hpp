#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

/// UTF-8 as the terminal cares about it: how many columns a string occupies,
/// and how it divides into the cells it will be drawn in.
///
/// This lives beside the renderer rather than in text_layout because it is a
/// property of the terminal, not of the text: the same string is two columns
/// wide or four depending on what the terminal does with a wide glyph. Every
/// measurement in the interface goes through here, so that layout is computed
/// in exactly the units it is drawn in.
namespace amberedit::ui::term {

/// Length of a UTF-8 sequence, judged by its first byte.
[[nodiscard]] size_t sequenceLength(unsigned char first);

/// Decodes the code point at `pos` and advances past it. Returns 0 for a
/// malformed byte, having still advanced by one so callers cannot spin.
[[nodiscard]] char32_t decodeUtf8(std::string_view text, size_t& pos);

/// How many columns one code point takes: 0 for a combining mark, 2 for a
/// double-width glyph, 1 for everything else.
///
/// This asks the C library, which answers from the locale — which is why
/// `ensureUtf8Locale()` has to have run first. A code point the library will
/// not judge is counted as one column rather than dropped: an unknown character
/// drawn in the wrong width costs one misaligned row, while counting it as -1
/// would pull the whole layout apart.
[[nodiscard]] int codepointWidth(char32_t code);

/// The cells a string will occupy, one entry each.
///
/// A double-width glyph comes back as itself followed by an empty string
/// standing for the column it also covers; combining marks are attached to the
/// glyph they modify rather than given a cell of their own. Walking this is what
/// lets a budget be spent in the units the renderer draws in.
[[nodiscard]] std::vector<std::string> toGlyphs(std::string_view text);

/// The width of a string in terminal columns.
[[nodiscard]] int stringWidth(std::string_view text);

/// One code point as UTF-8. The inverse of `decodeUtf8`, needed because the
/// terminal hands a keystroke over as a code point while everything above it
/// works in UTF-8 strings.
[[nodiscard]] std::string encodeUtf8(char32_t code);

/// Makes sure LC_CTYPE names an encoding the terminal layer can work in, and
/// returns the codeset that ended up in force.
///
/// This has to be done, and done before ncurses starts: ncursesw encodes what it
/// writes with wcrtomb(), so under the C locale every non-ASCII character would
/// be dropped on the way out — and wcwidth() would refuse to measure them, which
/// would take the layout with it. A VPS with no locale set is the normal case
/// rather than an unusual one, so the environment's own setting is tried first
/// and a UTF-8 locale is only imposed when it turns out to be unusable.
///
/// A locale the user *did* choose is left alone, including a single-byte one
/// like KOI8-R: that is the whole reason the interface can now be drawn on a
/// terminal that is not UTF-8.
///
/// Idempotent, and safe to call from a test that has no terminal at all.
const std::string& ensureUtf8Locale();

}  // namespace amberedit::ui::term
