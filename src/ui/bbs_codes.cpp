#include "ui/bbs_codes.hpp"

#include <array>
#include <cstdint>

namespace amberedit::ui::bbs {
namespace {

/// The DOS color order the codes are numbered in, as the terminal's own
/// entries. They are not the same order: the codes count black, blue, green,
/// cyan, red, magenta, brown, white — the CGA attribute nibble — while the
/// terminal counts black, red, green, yellow, blue, magenta, cyan, white. Blue
/// and red change places, as do cyan and yellow, so a table says it once rather
/// than every reader of the numbers working it out again.
constexpr std::array<int, 8> kDosToTerminal{0, 4, 2, 6, 1, 5, 3, 7};

/// The color code `value` — 00 to 31 — laid over `current`.
///
/// 00-15 are a foreground, the high eight of them the bright half of the
/// palette. 16-23 are a background. 24-31 are the ones DOS drew either as a
/// bright background or as a blinking foreground, depending on how the adapter
/// was set: taken here as the bright background, since that is what the code is
/// named after and what a terminal can show without making a message flash.
Color apply(Color current, int value) {
    if (value < 16) {
        current.fg =
            kDosToTerminal[static_cast<size_t>(value & 7)] + (value >= 8 ? 8 : 0);
        return current;
    }
    const int slot = (value - 16) & 7;
    current.bg = kDosToTerminal[static_cast<size_t>(slot)] + (value >= 24 ? 8 : 0);
    return current;
}

bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

/// Records `color` as taking effect at `at`, or corrects the run already
/// standing there — two codes side by side (`|15|17`, the usual way of naming
/// both halves) are one change as far as the drawing is concerned.
void noteRun(std::vector<ColorRun>& runs, size_t at, Color color) {
    if (!runs.empty() && runs.back().begin == at) {
        runs.back().color = color;
        return;
    }
    runs.push_back({at, color});
}

}  // namespace

term::Color paletteColor(int index) {
    return term::Color{static_cast<uint8_t>(index & 15)};
}

CodedLine stripRenegade(std::string_view line) {
    CodedLine coded;
    coded.text.reserve(line.size());
    Color inForce;

    for (size_t i = 0; i < line.size();) {
        if (line[i] == '|' && i + 2 < line.size() && isDigit(line[i + 1]) &&
            isDigit(line[i + 2])) {
            const int value = ((line[i + 1] - '0') * 10) + (line[i + 2] - '0');
            if (value <= 31) {
                inForce = apply(inForce, value);
                noteRun(coded.runs, coded.text.size(), inForce);
                i += 3;
                continue;
            }
        }
        coded.text += line[i];
        ++i;
    }

    // A code at the very end of a line colors nothing: the next line begins in
    // the theme's colors whatever this one closed with.
    if (!coded.runs.empty() && coded.runs.back().begin == coded.text.size())
        coded.runs.pop_back();
    return coded;
}

std::vector<std::vector<ColorRun>> runsForRows(const CodedLine& coded,
                                               const std::vector<std::string>& rows) {
    std::vector<std::vector<ColorRun>> perRow(rows.size());
    if (coded.runs.empty()) return perRow;

    size_t at = 0;    // where in the text the search for the next row starts
    size_t next = 0;  // the first run not yet spent
    Color inForce;    // what is in force where the row being cut begins

    for (size_t row = 0; row < rows.size(); ++row) {
        const size_t begin = coded.text.find(rows[row], at);
        // Cannot happen — the rows are the text's own substrings — but a row
        // that is somehow not in it is drawn plainly rather than colored from
        // whatever offset a wrong answer would have given.
        if (begin == std::string::npos) break;
        const size_t end = begin + rows[row].size();

        while (next < coded.runs.size() && coded.runs[next].begin <= begin) {
            inForce = coded.runs[next].color;
            ++next;
        }
        if (!inForce.plain()) perRow[row].push_back({0, inForce});
        for (size_t i = next; i < coded.runs.size() && coded.runs[i].begin < end; ++i) {
            perRow[row].push_back({coded.runs[i].begin - begin, coded.runs[i].color});
        }
        at = end;
    }
    return perRow;
}

}  // namespace amberedit::ui::bbs
