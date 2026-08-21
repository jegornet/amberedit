#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace amberedit::config {

/// One line of an AmberEdit config or theme file: the key, the values written after
/// it, and where it came from so that a complaint can name the line.
///
/// The accessors below are what a setting is read through. They throw rather
/// than return a default: a line that is there was meant to do something, and a
/// line that cannot do it is a mistake worth stopping for.
struct CfgEntry {
    /// The first word of the line, lowercased — keys are case-insensitive, as
    /// they are in every tosser config AmberEdit reads beside its own.
    std::string key;
    std::vector<std::string> values;
    /// The file the line was read from, and its number in it.
    std::string origin;
    int line{0};

    /// Throws std::runtime_error with the file and line in front of `what`.
    [[noreturn]] void fail(std::string_view what) const;

    /// The values as one string, joined by single spaces: `name Vasya Pupkin`
    /// and `name "Vasya Pupkin"` are the same line. Quotes are what a value
    /// with several spaces in a row, or a leading one, needs — they come
    /// through as they were written.
    [[nodiscard]] std::string text() const;

    /// The single value of a key that takes exactly one — an address, a number,
    /// a charset name.
    [[nodiscard]] const std::string& one() const;

    /// The single value as a whole number, optionally within `min`..`max`.
    [[nodiscard]] long long number() const;
    [[nodiscard]] long long numberIn(long long min, long long max) const;

    /// The single value as `on` or `off`, and nothing else: a setting that
    /// says `1` or `yes` is more likely a misremembering than an intention.
    [[nodiscard]] bool flag() const;
};

/// Parses the AmberEdit config format — the same one the themes are written in.
///
/// One `key value...` per line. Values are separated by whitespace, and a value
/// containing spaces is written in double quotes; a `#` that begins a word
/// starts a comment running to the end of the line. Blank lines and comments
/// are dropped, and what is left is returned in the order it was written, so a
/// key that may be repeated (`aka`, `akamatch`) keeps its file order.
///
/// Throws std::runtime_error naming the file and line for an unterminated
/// quote, and for the two shapes a toml config left behind — a `[section]`
/// header and a `key = value` line.
[[nodiscard]] std::vector<CfgEntry> parseCfg(std::string_view text,
                                             const std::string& originName);

}  // namespace amberedit::config
