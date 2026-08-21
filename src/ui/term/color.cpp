#include "ui/term/color.hpp"

#include <array>
#include <unordered_map>

#include "ui/term/ncurses.hpp"

namespace amberedit::ui::term {
namespace {

int colors = 0;

/// Whether the terminal was put into direct-color mode — `TERM=xterm-direct` and
/// its like. There a color number is read as a triple rather than as an index,
/// so an entry sent as it stands would paint whatever its number happens to
/// spell: 102 as #000066 instead of grey, quietly turning a whole theme blue.
/// Expanding it first is what keeps the theme the same on both sorts of terminal.
bool directColor = false;

/// The pairs handed out so far, keyed by the two colors that make them up.
/// Never emptied: the palette cannot change while the screen is open, so the set
/// of combinations is bounded by the theme and settles within a frame or two of
/// starting up.
std::unordered_map<uint32_t, int> pairs;
int nextPair = 1;  // 0 is the terminal's own pair and cannot be redefined

/// The six levels the 6x6x6 cube is built from. Not evenly spaced: the gap below
/// 95 is the one the original xterm chose.
constexpr std::array<int, 6> kCubeLevels{0, 95, 135, 175, 215, 255};

/// The sixteen ANSI colors as a terminal usually draws them. Only ever used to
/// judge which of them is nearest to a palette entry the terminal does not have;
/// what it actually paints is its own business, and its own configuration.
constexpr std::array<std::array<int, 3>, 16> kAnsiColors{{
    {0, 0, 0},        {205, 0, 0},     {0, 205, 0},     {205, 205, 0},
    {0, 0, 238},      {205, 0, 205},   {0, 205, 205},   {229, 229, 229},
    {127, 127, 127},  {255, 0, 0},     {0, 255, 0},     {255, 255, 0},
    {92, 92, 255},    {255, 0, 255},   {0, 255, 255},   {255, 255, 255},
}};

/// The three channels of a palette entry, for matching and for expanding.
std::array<int, 3> rgbOf(uint8_t index) {
    if (index < 16) return kAnsiColors[index];
    if (index < 232) {
        const int offset = index - 16;
        return {kCubeLevels[(offset / 36) % 6], kCubeLevels[(offset / 6) % 6],
                kCubeLevels[offset % 6]};
    }
    const int level = 8 + (index - 232) * 10;  // the 24-step grey ramp
    return {level, level, level};
}

}  // namespace

uint32_t paletteRgb(uint8_t index) {
    const auto rgb = rgbOf(index);
    return (uint32_t{static_cast<uint8_t>(rgb[0])} << 16u) |
           (uint32_t{static_cast<uint8_t>(rgb[1])} << 8u) |
           uint32_t{static_cast<uint8_t>(rgb[2])};
}

int nearestWithin(uint8_t index, int available) {
    // The usual case by far: the terminal has the entry, so it is used as the
    // theme wrote it and nothing is approximated at all.
    if (index < available) return index;
    if (available <= 0) return 0;

    const auto target = rgbOf(index);
    const int limit = available < 16 ? available : 16;

    int best = 0;
    int bestDistance = 1 << 30;
    for (int i = 0; i < limit; ++i) {
        const int dr = target[0] - kAnsiColors[i][0];
        const int dg = target[1] - kAnsiColors[i][1];
        const int db = target[2] - kAnsiColors[i][2];
        const int distance = dr * dr + dg * dg + db * db;
        if (distance < bestDistance) {
            bestDistance = distance;
            best = i;
        }
    }
    return best;
}

void initColors() {
    if (has_colors() == FALSE) {
        colors = 0;
        return;
    }

    start_color();
    // Lets -1 mean "leave this as the terminal had it", which is what a cleared
    // cell under a dialog needs. Failing is not fatal: it only means the default
    // color falls back to the pair-0 colors.
    use_default_colors();
    colors = COLORS;
    directColor = COLORS >= (1 << 24);
}

int paletteSize() { return colors; }

int pairFor(Color fg, Color bg) {
    if (colors <= 0) return 0;

    const uint32_t key = (uint32_t{fg.key()} << 16u) | uint32_t{bg.key()};
    if (const auto found = pairs.find(key); found != pairs.end()) return found->second;

    // Running out is not worth failing over: the interface stays readable in the
    // default pair, and the ceiling is high enough that only a theme far beyond
    // this one could reach it.
    if (nextPair >= COLOR_PAIRS) return 0;

    const auto resolve = [](Color color) {
        if (color.defaulted) return -1;
        if (directColor) return static_cast<int>(paletteRgb(color.index));
        return nearestWithin(color.index, colors);
    };

    const int pair = nextPair++;
    init_extended_pair(pair, resolve(fg), resolve(bg));
    pairs.emplace(key, pair);
    return pair;
}

}  // namespace amberedit::ui::term
