#include "config/area_pattern.hpp"

#include <vector>

#include "config/text_util.hpp"

namespace amberedit::config {
namespace {

[[nodiscard]] bool isWildcard(char c) {
    return c == '*' || c == '?';
}

/// Whether one character of a pattern can stand for one character of another.
/// `?` takes anything a single character can be, and two literals have to be
/// the same letter — folded for ASCII, as matching itself is.
[[nodiscard]] bool charsCanAgree(char a, char b) {
    if (a == '?' || b == '?') return true;
    return text::asciiLower(a) == text::asciiLower(b);
}

}  // namespace

bool AreaTagPattern::matches(std::string_view tag) const {
    // The glob itself is `text::globMatches`, which the `twit` lines match
    // names and subjects with as well: a `*` means the same thing wherever the
    // config holds one, and two copies of a matcher are two chances to drift.
    return text::globMatches(text_, tag);
}

std::tuple<int, int, bool> AreaTagPattern::specificity() const {
    int leading = 0;
    int literals = 0;
    bool exact = true;
    bool counting = true;

    for (char c : text_) {
        if (isWildcard(c)) {
            exact = false;
            counting = false;
            continue;
        }
        ++literals;
        if (counting) ++leading;
    }
    return {leading, literals, exact};
}

bool AreaTagPattern::overlaps(const AreaTagPattern& other) const {
    const std::string_view a(text_);
    const std::string_view b(other.text_);
    const size_t na = a.size();
    const size_t nb = b.size();

    // can[i][j]: whether the tails a[i..] and b[j..] have some string both of
    // them match. Filled from the far end, where two exhausted patterns agree on
    // the empty string. A `*` on either side either swallows a character the
    // other side has yet to account for or gives up and lets the tail past it
    // stand; anything else has to agree character for character, which is what
    // charsCanAgree() decides.
    std::vector<char> can((na + 1) * (nb + 1), 0);
    const auto at = [nb](size_t i, size_t j) { return (i * (nb + 1)) + j; };

    for (size_t i = na + 1; i-- > 0;) {
        for (size_t j = nb + 1; j-- > 0;) {
            if (i == na && j == nb) {
                can[at(i, j)] = 1;
            } else if (i == na) {
                can[at(i, j)] = static_cast<char>(b[j] == '*' && can[at(i, j + 1)]);
            } else if (j == nb) {
                can[at(i, j)] = static_cast<char>(a[i] == '*' && can[at(i + 1, j)]);
            } else if (a[i] == '*') {
                can[at(i, j)] = static_cast<char>(can[at(i + 1, j)] || can[at(i, j + 1)]);
            } else if (b[j] == '*') {
                can[at(i, j)] = static_cast<char>(can[at(i, j + 1)] || can[at(i + 1, j)]);
            } else {
                can[at(i, j)] =
                    static_cast<char>(charsCanAgree(a[i], b[j]) && can[at(i + 1, j + 1)]);
            }
        }
    }
    return can[at(0, 0)] != 0;
}

std::optional<AreaTagPattern> AreaTagPattern::parse(std::string_view text) {
    // An empty pattern would match the empty tag and nothing else, which no area
    // has. Everything else is a pattern, `*` included.
    if (text.empty()) return std::nullopt;
    return AreaTagPattern(std::string(text));
}

}  // namespace amberedit::config
