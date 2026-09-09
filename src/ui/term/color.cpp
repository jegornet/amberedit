#include "ui/term/color.hpp"

#include <algorithm>
#include <array>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ui/term/ncurses.hpp"

// PDCurses numbers the first eight colors in one of two orders, and which one it
// is was settled when the library itself was compiled: with PDC_RGB it is the
// ANSI order — 1 red, 4 blue — and without it the DOS order, 1 blue and 4 red.
// ncurses, every theme in themes/ and everything above this layer are written in
// the ANSI one, so a library holding the other would draw a blue theme in red
// and a yellow one in cyan while reporting nothing at all, since every number
// involved is valid.
//
// Nothing the compiler here can see settles it: the header only says how the
// COLOR_ macros in this file were spelled, not how the palette inside the
// library was built. So the library is asked at run time — PDCursesMod reports
// the flags it was compiled with — and where it holds the DOS order the numbers
// handed to it are turned round on the way out. See `cursesColor`.

namespace amberedit::ui::term {
namespace {

int colors = 0;

/// Whether the terminal was put into direct-color mode — `TERM=xterm-direct` and
/// its like. There a color number is read as a triple rather than as an index,
/// so an entry sent as it stands would paint whatever its number happens to
/// spell: 102 as #000066 instead of grey, quietly turning a whole theme blue.
/// Expanding it first is what keeps the theme the same on both sorts of terminal
/// — and it is the one place a theme's colors go out exactly as written.
bool directColor = false;

/// Whether the curses in use numbers the first eight colors the DOS way, which
/// only PDCursesMod ever does — ncurses has the ANSI order and nothing to ask.
bool bgrColors = false;

/// The pairs handed out so far, keyed by the two colors that make them up.
/// Never emptied: the palette cannot change while the screen is open, so the set
/// of combinations is bounded by the theme and settles within a frame or two of
/// starting up.
std::unordered_map<uint64_t, int> pairs;
int nextPair = 1;  // 0 is the terminal's own pair and cannot be redefined

/// The entries the theme draws with as numbers, so that none of them is lent to
/// a truecolor role. Set before the screen opens; see `reservePaletteEntries`.
std::array<bool, 256> spoken{};

/// What each color a theme asked for is drawn as, be that an entry lent to
/// hold it or the nearest entry there already. Keyed by the triple, so a color
/// several roles share costs one entry between them.
std::unordered_map<uint32_t, int> triples;

/// The entries lent, each with the triple it was set to: what `suspendTrueColors`
/// puts back and `resumeTrueColors` writes again. In the order they were lent,
/// which is the order the screen first asked for them.
std::vector<std::pair<int, uint32_t>> lent;

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

/// The three channels of a triple, the same way round.
std::array<int, 3> channelsOf(uint32_t rgb) {
    return {static_cast<int>((rgb >> 16u) & 0xffu), static_cast<int>((rgb >> 8u) & 0xffu),
            static_cast<int>(rgb & 0xffu)};
}

/// Sets a palette entry to a triple. curses takes each channel as a thousandth
/// rather than as a byte, and the terminal multiplies it back out: `initc` in
/// `xterm-256color` is `%p2%{255}%*%{1000}%/`, which **truncates**. So the
/// thousandth is rounded up rather than to nearest — the least one that comes
/// back out as the byte asked for. Rounding to nearest loses a step on the way
/// through wherever the division is not exact, and 0xf1 arrives as 0xf0.
void defineEntry(int entry, uint32_t rgb) {
    const auto channels = channelsOf(rgb);
    const auto perMille = [](int channel) { return (channel * 1000 + 254) / 255; };
    init_extended_color(entry, perMille(channels[0]), perMille(channels[1]),
                        perMille(channels[2]));
}

/// The entry from 16 up nearest the color `rgb`, out of those `taken` does not
/// rule out, or -1 where it rules out every one of them.
template <typename Taken>
int nearestEntryUnless(uint32_t rgb, Taken taken) {
    const auto target = channelsOf(rgb);

    int best = -1;
    int bestDistance = 1 << 30;
    for (int entry = 16; entry < 256; ++entry) {
        if (taken(entry)) continue;
        const auto candidate = rgbOf(static_cast<uint8_t>(entry));
        const int dr = target[0] - candidate[0];
        const int dg = target[1] - candidate[1];
        const int db = target[2] - candidate[2];
        const int distance = dr * dr + dg * dg + db * db;
        if (distance < bestDistance) {
            bestDistance = distance;
            best = entry;
        }
    }
    return best;
}

/// What the terminal is to draw a truecolor role with, on a terminal that reads
/// a color number as an index.
///
/// A terminal that will redefine its palette is lent an entry — never one the
/// theme draws with as a number, so that a theme mixing the two spellings does
/// not repaint its own roles.
///
/// **The entry lent is the one already nearest the color asked for**, and not
/// simply one nobody is using. A terminal that takes the redefinition draws the
/// color exactly either way, so the choice costs that terminal nothing — and one
/// that reports through terminfo that it will and then quietly ignores the
/// sequence is left drawing the nearest color it has, which is the same answer a
/// terminal that never claimed to could give. Lending from the top of the
/// palette down instead would leave that terminal drawing the whole interface in
/// the grey ramp entries 232-255 hold, which is what PuTTY does with `initc`.
/// Whether the redefinition was honoured is not something a terminal can be
/// asked, so the failure has to be made harmless rather than detected.
///
/// Where it will not redefine anything, or where the entries have run out, the
/// nearest entry it already has is drawn — the same theme quantised.
int trueColorEntry(uint32_t rgb) {
    if (const auto found = triples.find(rgb); found != triples.end()) return found->second;

    const int top = std::min(colors, 256);
    if (can_change_color() == TRUE) {
        const int entry = nearestEntryUnless(rgb, [top](int candidate) {
            return candidate >= top || spoken[static_cast<size_t>(candidate)];
        });
        if (entry >= 0) {
            spoken[static_cast<size_t>(entry)] = true;
            lent.emplace_back(entry, rgb);
            defineEntry(entry, rgb);
            triples.emplace(rgb, entry);
            return entry;
        }
    }

    const auto nearest = static_cast<uint8_t>(nearestPaletteEntry(rgb));
    const int drawn = cursesColor(nearestWithin(nearest, colors), bgrColors);
    triples.emplace(rgb, drawn);
    return drawn;
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

int nearestPaletteEntry(uint32_t rgb) {
    return nearestEntryUnless(rgb, [](int) { return false; });
}

int nearestPaletteEntry(uint32_t rgb, const std::vector<uint8_t>& skip) {
    const int entry = nearestEntryUnless(rgb, [&skip](int candidate) {
        return std::find(skip.begin(), skip.end(), candidate) != skip.end();
    });
    return entry;
}

int cursesColor(int index, bool bgr) {
    // Red and blue are bit 0 and bit 2, with green and the bright bit either
    // side of them staying where they are. Entries from 16 up are the
    // 256-color palette, which both orders number the same way.
    if (!bgr || index < 0 || index > 15) return index;
    return (index & 0x0a) | ((index & 0x01) << 2) | ((index & 0x04) >> 2);
}

void reservePaletteEntries(const std::vector<uint8_t>& used) {
    spoken.fill(false);
    for (const uint8_t entry : used) spoken[entry] = true;
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

#ifdef __PDCURSESMOD__
    // The half of the color order this side cannot see. A library built without
    // PDC_RGB numbers the first eight the DOS way whatever this file was
    // compiled with, and says so here.
    PDC_VERSION version;
    PDC_get_version(&version);
    bgrColors = (version.flags & PDC_VFLAG_RGB) == 0;
#endif
}

int paletteSize() { return colors; }

int pairFor(Color fg, Color bg) {
    if (colors <= 0) return 0;

    const uint64_t key = (uint64_t{fg.key()} << 32u) | uint64_t{bg.key()};
    if (const auto found = pairs.find(key); found != pairs.end()) return found->second;

    // Running out is not worth failing over: the interface stays readable in the
    // default pair, and the ceiling is high enough that only a theme far beyond
    // this one could reach it.
    if (nextPair >= COLOR_PAIRS) return 0;

    const auto resolve = [](Color color) {
        if (color.defaulted) return -1;
        if (color.trueColor) {
            return directColor ? static_cast<int>(color.value)
                               : trueColorEntry(color.value);
        }
        if (directColor) return static_cast<int>(paletteRgb(color.index()));
        return cursesColor(nearestWithin(color.index(), colors), bgrColors);
    };

    const int pair = nextPair++;
    init_extended_pair(pair, resolve(fg), resolve(bg));
    pairs.emplace(key, pair);
    return pair;
}

void suspendTrueColors() {
    for (const auto& borrowed : lent) {
        const int entry = borrowed.first;
        defineEntry(entry, paletteRgb(static_cast<uint8_t>(entry)));
    }
}

void resumeTrueColors() {
    for (const auto& [entry, rgb] : lent) defineEntry(entry, rgb);
}

}  // namespace amberedit::ui::term
