#pragma once

#include <cstdint>

/// The colors the interface draws with: a number in the terminal's own
/// 256-color palette, and nothing else.
namespace amberedit::ui::term {

/// A palette entry, or the terminal's own color.
///
/// The default-constructed Color is not black: it is "whatever this terminal
/// uses when nothing is asked for", which is what a cleared cell must be. Only
/// `clear_under` produces those, so that a dialog does not inherit the theme's
/// background — see `element.cpp`.
struct Color {
    uint8_t index{0};
    bool defaulted{true};

    constexpr Color() = default;
    constexpr explicit Color(uint8_t palette_index)
        : index(palette_index), defaulted(false) {}

    [[nodiscard]] constexpr bool operator==(const Color& other) const {
        if (defaulted || other.defaulted) return defaulted == other.defaulted;
        return index == other.index;
    }
    [[nodiscard]] constexpr bool operator!=(const Color& other) const {
        return !(*this == other);
    }

    /// A stable key for the pair cache. The terminal's own color has to fall
    /// outside the palette's range so it cannot collide with a real entry.
    [[nodiscard]] constexpr uint16_t key() const { return defaulted ? 256 : index; }
};

/// Looks at how many colors the terminal admits to. Must be called after the
/// screen has been opened, since it is terminfo that answers.
void initColors();

/// How many the terminal reported. Below 256 a theme's numbers cannot be used as
/// they stand and are approximated; see `pairFor`.
[[nodiscard]] int paletteSize();

/// The ncurses color pair painting `fg` on `bg`, allocated the first time that
/// combination is asked for. A palette holds twenty roles over a dozen colors,
/// so the handful of combinations that actually occur stays far below what any
/// terminal offers.
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

/// What a palette entry looks like, as 0xRRGGBB. Needed for a terminal in
/// direct-color mode, which reads a color number as a triple rather than as an
/// index — see `pairFor`. Entries 0-15 are the terminal's own and only
/// approximated here.
[[nodiscard]] uint32_t paletteRgb(uint8_t index);

}  // namespace amberedit::ui::term
