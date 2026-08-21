#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "nodelist/node_entry.hpp"

namespace amberedit::nodelist {

/// A line the parser could not use, and why. Nothing is ever guessed at: a line
/// that does not parse is left out and named here, so that a nodelist segment
/// somebody mangled shows up as a warning against a line number rather than as
/// a node quietly missing.
struct ParseWarning {
    int line{0};
    std::string message;
};

struct ParseResult {
    /// In the order the file wrote them, which is the order a nodelist is
    /// arranged in and not the order the compiled file keeps.
    std::vector<NodeEntry> entries;
    std::vector<ParseWarning> warnings;
    /// How many of the entries are points — those with a non-zero point number.
    size_t pointCount{0};
    /// Whether the file carried `Boss` lines, which is what makes it a
    /// pointlist. Nothing is decided by it: the same parser reads both kinds and
    /// a `Boss` line simply says what the lines under it are addressed from. It
    /// is here so that the compiler can say which kind of file it read.
    bool pointList{false};
};

/// Reads a nodelist or a pointlist — FTS-5000 lines, or the `Boss` blocks a
/// pointlist is made of — and answers with the entries and the addresses the
/// file's own structure puts them at.
///
/// The two kinds are told apart by the lines themselves rather than by being
/// declared: `Zone`, `Region` and `Host` set the net that the lines under them
/// belong to, and `Boss` sets the node that the lines under *it* are points of.
/// A file holding both reads correctly for the same reason — each line is
/// addressed by whatever last said where it is.
[[nodiscard]] ParseResult parseNodelist(std::string_view text);

}  // namespace amberedit::nodelist
