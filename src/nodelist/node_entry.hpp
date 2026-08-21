#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "domain/ftn_address.hpp"

namespace amberedit::nodelist {

/// The first field of a nodelist line — what the entry it opens is.
///
/// The numbers are written into the compiled nodelist and are therefore part of
/// the format: a value is never renumbered, only added after the last one.
///
/// `Node` is the empty first field, which is what most lines have; `Point` is
/// the keyword FTS-5000 gives a point listed under its boss in a nodelist, and
/// a point out of a pointlist is stored under the keyword its own line carried
/// — usually `Node` or `Pvt`. What kind of address a record holds is asked of
/// the address, which says it exactly (`point != 0`), and not of the keyword,
/// which says what the entry is *for*.
enum class NodeKeyword : uint8_t {
    Node = 0,
    Zone = 1,
    Region = 2,
    Host = 3,
    Hub = 4,
    Pvt = 5,
    Hold = 6,
    Down = 7,
    Point = 8,
};

/// The keyword as it is written in a nodelist. The empty string for `Node`,
/// which is how that line is written — the field is there and holds nothing.
[[nodiscard]] std::string_view keywordName(NodeKeyword keyword);

/// The keyword a line opens with, case folded. Nullopt for a word that is not
/// one; the empty field is `Node` and not nullopt.
[[nodiscard]] std::optional<NodeKeyword> parseKeyword(std::string_view text);

/// One line of a nodelist, with the address the lines above it put it at.
///
/// The three fields a nodelist writes spaces in as underscores — the system
/// name, the location and the sysop — are held with the spaces back in them:
/// that is what they mean, it is what the screen and a search want, and
/// `toLine()` puts the underscores back for anyone who wants the line as the
/// nodelist had it. The phone and the flags are kept exactly as written, an
/// underscore in either being a character of its own.
struct NodeEntry {
    NodeKeyword keyword{NodeKeyword::Node};
    domain::FtnAddress address;
    std::string system;
    std::string location;
    std::string sysop;
    std::string phone;
    /// The baud rate of the seventh field. Zero where the line had none.
    uint32_t speed{0};
    /// Everything after the baud rate, joined back up with commas exactly as it
    /// was written: `CM,XX,INA:bbs.example.org,IBN`. Empty where a line ended at
    /// the baud rate, as a great many do.
    std::string flags;

    /// The line as a nodelist writes it, underscores and all. What `i` shows for
    /// a node, and what a search over "the node's line as a whole" reads.
    [[nodiscard]] std::string toLine() const;

    /// Whether the line carries this flag — `CM`, `IBN`, `INA:host`. Matched on
    /// the whole comma-separated word, or on the part before a ':' where the
    /// flag carries a value, so that `IBN` finds `IBN:24555` and `INA` does not
    /// find `BINKP`.
    [[nodiscard]] bool hasFlag(std::string_view flag) const;

    /// The value of a flag written `NAME:value`, or nullopt where the line
    /// carries the flag without one — or does not carry it at all.
    [[nodiscard]] std::optional<std::string> flagValue(std::string_view flag) const;
};

}  // namespace amberedit::nodelist
