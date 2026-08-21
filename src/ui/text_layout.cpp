#include "ui/text_layout.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <sstream>

#include "ui/term/utf8.hpp"

namespace amberedit::ui {
namespace {

/// Whether a code point can stand in the initials before a quote marker.
bool isInitialLetter(char32_t code) {
    if (code < 128) return std::isalpha(static_cast<int>(code)) != 0;
    return code >= 0x0400 && code <= 0x04FF;  // Cyrillic
}

bool isBlank(char c) { return c == ' ' || c == '\t'; }

/// A line cut into alternating runs of blanks and of non-blanks.
///
/// Wrapping walks these rather than extracting words with `>>`: a stream drops
/// whatever whitespace it skips over, which is how a wrapped line used to lose
/// the space it began with and the columns inside it. Spaces are ASCII, so
/// cutting on bytes cannot land inside a UTF-8 sequence.
std::vector<std::string_view> splitRuns(std::string_view line) {
    std::vector<std::string_view> runs;
    size_t pos = 0;
    while (pos < line.size()) {
        const bool blank = isBlank(line[pos]);
        size_t end = pos + 1;
        while (end < line.size() && isBlank(line[end]) == blank) ++end;
        runs.push_back(line.substr(pos, end - pos));
        pos = end;
    }
    return runs;
}

}  // namespace

int quoteDepth(std::string_view line) {
    constexpr size_t kMaxLeadingSpaces = 2;
    constexpr int kMaxInitials = 6;

    size_t pos = 0;
    while (pos < kMaxLeadingSpaces && pos < line.size() && line[pos] == ' ') ++pos;

    for (int letters = 0; letters < kMaxInitials; ++letters) {
        size_t next = pos;
        const char32_t code = term::decodeUtf8(line, next);
        if (code == 0 || !isInitialLetter(code)) break;
        pos = next;
    }

    // A single '-' may stand before the markers: QWK gateways quote with "-> "
    // and messages carrying it come through the echoes as they are.
    if (pos < line.size() && line[pos] == '-') ++pos;

    int markers = 0;
    while (pos < line.size() && line[pos] == '>') {
        ++markers;
        ++pos;
    }
    if (markers == 0) return 0;

    // No space after the markers means this is not a quote, whatever it looks
    // like — ">8 lines later" and the like stay ordinary text.
    if (pos >= line.size() || line[pos] != ' ') return 0;
    return markers;
}

int displayWidth(std::string_view s) { return term::stringWidth(s); }

std::string substrByWidth(std::string_view s, int start, int columns) {
    if (columns <= 0) return {};

    // toGlyphs hands back one entry per cell: a double-width glyph comes as
    // itself followed by an empty string holding the second cell, and combining
    // marks are already attached to the glyph they modify. Walking it means the
    // budget is spent in the same units the renderer draws in.
    std::string out;
    int column = 0;
    for (const auto& glyph : term::toGlyphs(s)) {
        const int width = term::stringWidth(glyph);
        if (column + width > start + columns) break;  // a wide glyph must not straddle
        if (column >= start) out += glyph;
        column += width;
    }
    return out;
}

std::string truncateToWidth(std::string_view s, int columns) {
    if (columns <= 0) return {};
    if (displayWidth(s) <= columns) return std::string(s);
    if (columns == 1) return "…";
    return substrByWidth(s, 0, columns - 1) + "…";
}

std::string padRight(std::string_view s, int width) {
    const int length = displayWidth(s);
    if (length >= width) return std::string(s);
    return std::string(s) + std::string(static_cast<size_t>(width - length), ' ');
}

std::string padLeft(std::string_view s, int width) {
    const int length = displayWidth(s);
    if (length >= width) return std::string(s);
    return std::string(static_cast<size_t>(width - length), ' ') + std::string(s);
}

int digitWidth(int64_t n) {
    if (n < 10) return 1;
    int width = 0;
    while (n > 0) {
        n /= 10;
        width++;
    }
    return width;
}

std::string horizontalRule(int width) {
    std::string rule;
    for (int i = 0; i < std::max(1, width); ++i) rule += "─";
    return rule;
}

std::vector<std::string> wrapText(std::string_view text, int width) {
    std::vector<std::string> lines;
    if (width <= 0) return lines;
    const auto limit = static_cast<size_t>(width);

    std::istringstream paragraphs{std::string(text)};
    std::string paragraph;
    while (std::getline(paragraphs, paragraph, '\n')) {
        if (paragraph.empty()) {
            lines.emplace_back();
            continue;
        }
        // The line fits as it is — hand it back untouched. This matters for
        // Fidonet mail: indentation, "AB>" quoting and ASCII art must not be
        // rearranged by whitespace normalisation.
        if (displayWidth(paragraph) <= limit) {
            lines.push_back(paragraph);
            continue;
        }

        std::string current;
        size_t currentWidth = 0;
        // Blanks between two words are held back until it is known whether the
        // line goes on: kept where it does, dropped at a break, where they would
        // only push the text of the next line off the left margin.
        std::string pending;
        size_t pendingWidth = 0;
        bool atStart = true;

        const auto flush = [&] {
            lines.push_back(current);
            current.clear();
            currentWidth = 0;
            pending.clear();
            pendingWidth = 0;
        };

        for (const auto run : splitRuns(paragraph)) {
            const auto runWidth = static_cast<size_t>(displayWidth(run));
            if (isBlank(run.front())) {
                // The blanks a line opens with are its indentation and belong to
                // it, so they are laid down rather than held back.
                if (atStart) {
                    current.append(run);
                    currentWidth += runWidth;
                } else {
                    pending.append(run);
                    pendingWidth += runWidth;
                }
                continue;
            }
            atStart = false;

            if (!current.empty() && currentWidth + pendingWidth + runWidth > limit) flush();
            current += pending;
            currentWidth += pendingWidth;
            pending.clear();
            pendingWidth = 0;

            if (currentWidth + runWidth <= limit) {
                current.append(run);
                currentWidth += runWidth;
                continue;
            }

            // A word too long for a line of its own (ASCII art, a long URL) is
            // cut to width. Cell by cell, so that a double-width glyph is not
            // halved and none of it is dropped at the seam.
            for (const auto& glyph : term::toGlyphs(run)) {
                const auto glyphWidth = static_cast<size_t>(term::stringWidth(glyph));
                if (!current.empty() && currentWidth + glyphWidth > limit) flush();
                current += glyph;
                currentWidth += glyphWidth;
            }
        }
        // Trailing blanks are not carried over: they would be invisible anyway.
        if (!current.empty()) lines.push_back(current);
    }
    return lines;
}

std::vector<size_t> softWrapOffsets(std::string_view line, int width) {
    std::vector<size_t> starts{0};
    if (width <= 0) return starts;
    // The lines of a message mostly fit, and this is asked of every one of them
    // on every frame. A line of no more bytes than the width fits whatever is in
    // it — UTF-8 never spends fewer bytes than columns — and one measured to fit
    // is done with before a single cell has been cut out of it.
    if (line.size() <= static_cast<size_t>(width)) return starts;
    if (displayWidth(line) <= width) return starts;

    size_t rowStart = 0;
    size_t breakAt = 0;  // where the word being laid down began
    int used = 0;        // columns laid down since rowStart
    size_t pos = 0;
    bool afterBlank = false;

    for (const auto& glyph : term::toGlyphs(line)) {
        const int cells = term::stringWidth(glyph);
        const bool blank = glyph.size() == 1 && isBlank(glyph[0]);
        // Only a character that is to be seen can push a row past the edge.
        // Blanks run past it instead of breaking there: a row broken before a
        // space would open the next one with it, and a space at the right edge
        // draws nothing worth a row of its own.
        if (!blank) {
            if (afterBlank) breakAt = pos;
            if (used + cells > width && pos > rowStart) {
                // Back to where the word began, unless the word is the whole
                // row — one longer than the width has nowhere to break and is
                // cut where the width falls.
                const size_t next = breakAt > rowStart ? breakAt : pos;
                starts.push_back(next);
                rowStart = next;
                used = displayWidth(line.substr(next, pos - next));
            }
        }
        used += cells;
        pos += glyph.size();
        afterBlank = blank;
    }
    return starts;
}

std::vector<std::pair<size_t, size_t>> findLinks(std::string_view line) {
    static constexpr std::array<std::string_view, 3> kSchemes{"http://", "https://",
                                                              "ftp://"};
    // Trailing characters that end a sentence rather than an address. The
    // closing bracket is conditional: `(see http://x)` ends a parenthesis,
    // while a wiki link may carry a balanced pair of its own.
    static constexpr std::string_view kSentenceTail = ".,;:!?\"'";

    const auto isWordChar = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    };

    std::vector<std::pair<size_t, size_t>> links;
    for (size_t i = 0; i < line.size();) {
        // A scheme has to start a word, or "xhttp://" would read as a link.
        const bool atWordStart = i == 0 || !isWordChar(line[i - 1]);
        size_t schemeLength = 0;
        if (atWordStart) {
            for (const auto scheme : kSchemes) {
                if (line.compare(i, scheme.size(), scheme) == 0) {
                    schemeLength = std::max(schemeLength, scheme.size());
                }
            }
        }
        if (schemeLength == 0) {
            ++i;
            continue;
        }

        size_t end = line.find_first_of(" \t", i);
        if (end == std::string_view::npos) end = line.size();

        while (end > i + schemeLength) {
            const char last = line[end - 1];
            if (kSentenceTail.find(last) != std::string_view::npos) {
                --end;
                continue;
            }
            if (last == ')' &&
                line.substr(i, end - i).find('(') == std::string_view::npos) {
                --end;
                continue;
            }
            break;
        }

        // The scheme alone is not an address.
        if (end > i + schemeLength) links.emplace_back(i, end);
        i = std::max(end, i + schemeLength);
    }
    return links;
}

std::vector<StyleSpan> findStyleSpans(std::string_view line) {
    static constexpr std::string_view kMarkers = "_*/#";
    /// What may stand before an opening marker: nothing, a space, or something
    /// a phrase can be written inside of.
    static constexpr std::string_view kBeforeOpen = " \t([{<\"'";
    /// What may stand after a closing marker: a space, punctuation ending the
    /// sentence the phrase is in, or the end of the line.
    static constexpr std::string_view kAfterClose = " \t.,;:!?)]}>\"'";

    const auto isSpace = [](char c) { return c == ' ' || c == '\t'; };

    std::vector<StyleSpan> spans;
    for (size_t i = 0; i < line.size(); ++i) {
        const char marker = line[i];
        if (kMarkers.find(marker) == std::string_view::npos) continue;
        if (i > 0 && kBeforeOpen.find(line[i - 1]) == std::string_view::npos) continue;
        // Nothing to emphasise: the marker is on its own, or opens on a space.
        if (i + 1 >= line.size() || isSpace(line[i + 1]) || line[i + 1] == marker)
            continue;

        // The first closing marker that ends a word — `*a* b*` closes at the
        // first one, leaving the second to open a phrase of its own or not.
        size_t close = std::string_view::npos;
        for (size_t j = i + 2; j < line.size(); ++j) {
            if (line[j] != marker || isSpace(line[j - 1])) continue;
            if (j + 1 < line.size() &&
                kAfterClose.find(line[j + 1]) == std::string_view::npos)
                continue;
            close = j;
            break;
        }
        if (close == std::string_view::npos) continue;

        spans.push_back({i, close + 1, marker});
        // Whatever else the phrase holds is part of it, markers included.
        i = close;
    }
    return spans;
}

}  // namespace amberedit::ui
