#pragma once

#include <cstdint>
#include <vector>

/// The colors the interface draws with: a number in the terminal's own
/// 256-color palette, or a 24-bit color a theme wrote out in six hex digits.
namespace amberedit::ui::term {

/// A palette entry, a truecolor triple, or the terminal's own color.
///
/// The default-constructed Color is not black: it is "whatever this terminal
/// uses when nothing is asked for", which is what a cleared cell must be. Only
/// `clear_under` produces those — and nothing the user looks at is left in one:
/// the screen is painted in the theme's `background`, and a dialog paints its
/// own `dialog_background` over the box it has just cleared. See `element.cpp`
/// and `ui/dialog_frame.cpp`.
///
/// The two forms are told apart by `trueColor` and never by the number: a triple
/// and an entry share every value from 0 to 255, so `Color{33}` — the blue of
/// the built-in palette — and `Color::rgb(0x000021)` are different colors that
/// hold the same `value`.
struct Color {
    /// A palette entry from 0 to 255, or 0xRRGGBB where `trueColor` is set.
    uint32_t value{0};
    bool trueColor{false};
    bool defaulted{true};

    constexpr Color() = default;
    constexpr explicit Color(uint8_t palette_index)
        : value(palette_index), defaulted(false) {}

    /// A theme's own color. What reaches the terminal is `pairFor`'s business:
    /// a triple is only ever drawn as written where the terminal has 24-bit
    /// color, and nothing can ask a terminal whether it has.
    [[nodiscard]] static constexpr Color rgb(uint32_t triple) {
        Color color;
        color.value = triple & 0xffffffu;
        color.trueColor = true;
        color.defaulted = false;
        return color;
    }

    /// The palette entry. Only meaningful where `trueColor` is not set.
    [[nodiscard]] constexpr uint8_t index() const { return static_cast<uint8_t>(value); }

    [[nodiscard]] constexpr bool operator==(const Color& other) const {
        if (defaulted || other.defaulted) return defaulted == other.defaulted;
        return trueColor == other.trueColor && value == other.value;
    }
    [[nodiscard]] constexpr bool operator!=(const Color& other) const {
        return !(*this == other);
    }

    /// A stable key for the pair cache. The terminal's own color and a triple
    /// have to fall outside the palette's range so that neither can collide with
    /// a real entry.
    [[nodiscard]] constexpr uint32_t key() const {
        if (defaulted) return 1u << 25u;
        return trueColor ? ((1u << 24u) | value) : value;
    }
};

/// Looks at how many colors the terminal admits to. Must be called after the
/// screen has been opened, since it is terminfo that answers.
void initColors();

/// How many the terminal reported. Below 256 a theme's numbers cannot be used as
/// they stand and are approximated; see `pairFor`.
[[nodiscard]] int paletteSize();

/// The palette entries the theme in force draws with as numbers, which a
/// truecolor role may therefore not be lent — see `pairFor`. Said before the
/// screen opens, because it is a fact about the theme and not about the
/// terminal; a palette written entirely in numbers reserves everything it uses
/// and asks for nothing back.
void reservePaletteEntries(const std::vector<uint8_t>& used);

/// The ncurses color pair painting `fg` on `bg`, allocated the first time that
/// combination is asked for. A palette holds forty roles over a dozen or two
/// colors, so the handful of combinations that actually occur stays far below
/// what any terminal offers.
///
/// The number is returned rather than a shifted attribute because a pair beyond
/// 256 does not fit in one: those reach the terminal through setcchar's `opts`
/// argument, which takes the number as it stands.
///
/// Returns 0 — the default pair — when the terminal has no color at all, which
/// is the one case where the interface simply goes monochrome.
[[nodiscard]] int pairFor(Color fg, Color bg);

/// The nearest color a terminal with only `available` of them can show. An index
/// the terminal already has is returned untouched, so a theme written in the
/// sixteen ANSI colors reaches a sixteen-color terminal exactly as written.
///
/// Exposed for the tests, which are the only thing that can check an
/// approximation without eyes on a terminal.
[[nodiscard]] int nearestWithin(uint8_t index, int available);

/// The palette entry nearest the triple `rgb`.
///
/// It is both what a truecolor role falls back to where the terminal will not
/// lend an entry to hold one, and — with `skip` naming the entries already spoken
/// for — the entry that *is* lent. Lending the nearest rather than any free one
/// is what leaves a terminal that ignores the redefinition drawing the theme
/// quantised instead of drawing it in whatever those entries held; see `pairFor`.
/// Answers -1 where `skip` rules out every entry.
///
/// Only the entries above the ANSI sixteen are looked at — the 6x6x6 cube and
/// the grey ramp, which every terminal numbers the same way. What the sixteen
/// below them look like is the terminal's own configuration, so matching a
/// triple against an assumed value for one would pick a color by a name it may
/// not answer to.
///
/// Exposed for the tests, as `nearestWithin` is.
[[nodiscard]] int nearestPaletteEntry(uint32_t rgb);
[[nodiscard]] int nearestPaletteEntry(uint32_t rgb, const std::vector<uint8_t>& skip);

/// The number curses itself uses for the ANSI palette entry `index`.
///
/// PDCurses numbers the first eight colors in the DOS order — 1 blue, 4 red —
/// unless the library was built with PDC_RGB, and how it was built is not
/// something the code including its header can see. Themes are written in the
/// ANSI order, so where `bgr` says the library holds the other one, red and blue
/// swap and everything else stays: green, the bright bit, and every entry from
/// 16 up, which is the 256-color palette both orders share.
///
/// `bgr` is a parameter rather than read from the library here so that the swap
/// can be checked without one; `pairFor` passes what `initColors` was told.
[[nodiscard]] int cursesColor(int index, bool bgr);

/// What a palette entry looks like, as 0xRRGGBB. Needed for a terminal in
/// direct-color mode, which reads a color number as a triple rather than as an
/// index — see `pairFor`. Entries 0-15 are the terminal's own and only
/// approximated here.
[[nodiscard]] uint32_t paletteRgb(uint8_t index);

/// Puts back the palette entries `pairFor` lent to a truecolor theme, setting
/// each to the color its number stands for everywhere else, and defines them
/// again. What is lent is the terminal's, not this program's: it outlives the
/// screen, and a shell handed it back with the top of its palette repainted
/// would draw everything else wrong for the rest of the session.
///
/// Both are nothing at all where no entry was lent, which is every theme written
/// in palette numbers and every terminal that took the triples as written.
void suspendTrueColors();
void resumeTrueColors();

}  // namespace amberedit::ui::term
