#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <tuple>

namespace amberedit::config {

/// A `member` line's pattern: an echotag with `*` and `?` in it, naming the
/// areas one `group ... endgroup` block covers.
///
/// `*` stands for any run of characters, the empty one included; `?` for
/// exactly one. Everything else stands for itself, the dots in a tag included —
/// they are ordinary characters here and not separators, so `esp.*` covers
/// `esp.argentina` and `esp.charla.libre` alike.
///
/// It lives in config/ rather than beside domain::AddressPattern because an
/// address is the domain's own type and "does this pattern match this address"
/// is a question worth asking with no config in sight. An echotag glob exists
/// only to pick a config setting, and nothing in domain/ will ever ask it.
class AreaTagPattern {
public:
    /// Whether the tag is one this pattern covers. Case is folded for ASCII and
    /// deliberately for nothing else — the reason text::asciiLower gives.
    [[nodiscard]] bool matches(std::string_view tag) const;

    /// How much of a tag the pattern pins down, for deciding between two that
    /// both cover an area. Three numbers, compared in order:
    ///
    /// - the literal characters before the first wildcard, so `esp.*` (4) beats
    ///   `*.libre` (0) — counted from the left for the reason
    ///   AddressPattern::depth() is, a tag being a hierarchy read left to right;
    /// - the literal characters in the whole pattern, which separates
    ///   `esp.*.libre` from `esp.*`;
    /// - whether it holds no wildcard at all, which settles `esp.argentina`
    ///   against `esp.argentina?` — the exact tag is the more particular thing
    ///   to have written.
    [[nodiscard]] std::tuple<int, int, bool> specificity() const;

    /// Whether some tag would match both patterns. What tells a real clash
    /// between two groups from two patterns that could never meet, and so what
    /// lets an ambiguous config be refused without knowing which areas the
    /// tosser happens to declare.
    [[nodiscard]] bool overlaps(const AreaTagPattern& other) const;

    /// The pattern as it was written, for the messages that name it.
    [[nodiscard]] const std::string& toString() const { return text_; }

    /// Returns nullopt for text that is no pattern at all — today, an empty one.
    [[nodiscard]] static std::optional<AreaTagPattern> parse(std::string_view text);

private:
    explicit AreaTagPattern(std::string text) : text_(std::move(text)) {}

    std::string text_;
};

}  // namespace amberedit::config
