#pragma once

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "echolist/echolist_source.hpp"

namespace amberedit::echolist {

/// The compiled echolist, opened for reading.
///
/// An echo is named by its index — its place in folded-tag order, which is the
/// order the records are written in and the only order there is. Nothing lists
/// them: the one question asked of this file is what an area tag means, and
/// `descriptionOf` is the whole of the interface that matters. The rest is for
/// saying what a compiled file holds.
///
/// The whole file is held in memory. Every echolist published anywhere compiles
/// to a few hundred kilobytes, the lookup is a binary search over what was read,
/// and a reader that held a descriptor instead would be paying a system call per
/// area on every rescan.
class EcholistDb {
public:
    /// Opens a compiled echolist. Throws std::runtime_error naming the file for
    /// one that is not there, is not a compiled echolist, or was written by
    /// another version of the format. `echolistNeedsCompiling` reads every one
    /// of those as "compile it again", which is the whole of the answer to them.
    [[nodiscard]] static EcholistDb open(const std::string& path);

    [[nodiscard]] size_t size() const { return areaCount_; }
    [[nodiscard]] bool empty() const { return areaCount_ == 0; }

    /// When the file was compiled, and out of what — one entry per `echolist`
    /// line, in the order the config wrote them, each with the file it named and
    /// what that file was. Comparing them against `stateOf()` is how a start
    /// finds out whether anything needs compiling again.
    [[nodiscard]] std::time_t builtAt() const { return builtAt_; }
    [[nodiscard]] const std::vector<SourceState>& sources() const { return sources_; }

    /// The tag and the description at an index, both as the echolist spelled
    /// them. Throws std::runtime_error if the file is damaged in a way the
    /// header could not show.
    [[nodiscard]] std::string tagAt(size_t index) const;
    [[nodiscard]] std::string descriptionAt(size_t index) const;

    /// What the echolists say this echo is, or nullopt where none of them says
    /// anything. The tag is folded the way the file's own are, so an area the
    /// tosser config spells `RU.LINUX` finds the echolist's `Ru.Linux`.
    ///
    /// Never an empty string: an echo with nothing said about it is not written
    /// into the compiled file at all, so "there is an answer" and "the answer is
    /// worth having" are the same question here.
    [[nodiscard]] std::optional<std::string> descriptionOf(std::string_view tag) const;

private:
    /// Where a record starts, and the folded tag it is sorted by.
    [[nodiscard]] uint32_t recordOffsetAt(size_t index) const;
    [[nodiscard]] std::string keyAt(size_t index) const;
    /// The first index whose folded tag is not below `key`.
    [[nodiscard]] size_t lowerBound(const std::string& key) const;

    std::vector<unsigned char> data_;
    std::vector<SourceState> sources_;
    size_t areaCount_{0};
    uint32_t indexOffset_{0};
    uint32_t recordsOffset_{0};
    uint32_t recordsSize_{0};
    std::time_t builtAt_{0};
    std::string path_;
};

}  // namespace amberedit::echolist
