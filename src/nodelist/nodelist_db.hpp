#pragma once

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "domain/ftn_address.hpp"
#include "nodelist/node_entry.hpp"
#include "nodelist/nodelist_format.hpp"
#include "nodelist/nodelist_source.hpp"

namespace amberedit::nodelist {

/// The compiled nodelist, opened for reading.
///
/// A node is named by its index — its place in address order, which is the
/// order every search here answers in and the order the list is drawn in. The
/// index is where the two searches meet: an address search hands back a run of
/// them, a sysop search hands back a set of them, and `entry()` turns one into
/// the line it stands for.
///
/// The whole file is held in memory. A worldwide nodelist and every pointlist
/// beside it compile to a few megabytes, the searches are binary searches over
/// what was read, and a reader that held a descriptor instead would be paying
/// a system call for every row it draws.
class NodelistDb {
public:
    /// Opens a compiled nodelist. Throws std::runtime_error naming the file for
    /// one that is not there, is not a compiled nodelist, or was written by
    /// another version of the format. `nodelistNeedsCompiling` reads every one
    /// of those as "compile it again", which is the whole of the answer to them.
    [[nodiscard]] static NodelistDb open(const std::string& path);

    [[nodiscard]] size_t size() const { return nodeCount_; }
    [[nodiscard]] bool empty() const { return nodeCount_ == 0; }

    /// When the file was compiled, and out of what — one entry per `nodelist`
    /// line, in the order the config wrote them, each with the file it named and
    /// what that file was. Comparing them against `stateOf()` is how a start
    /// finds out whether anything needs compiling again.
    [[nodiscard]] std::time_t builtAt() const { return builtAt_; }
    [[nodiscard]] const std::vector<SourceState>& sources() const { return sources_; }

    /// The address of a node, without reading its line. What the address column
    /// of a list is drawn from, and cheap enough to ask for every visible row.
    [[nodiscard]] domain::FtnAddress addressAt(size_t index) const;

    /// The whole line. Throws std::runtime_error if the file is damaged in a way
    /// the header could not show.
    [[nodiscard]] NodeEntry entry(size_t index) const;

    /// Which nodelist the node came from, as an index into `sources()`.
    [[nodiscard]] size_t sourceAt(size_t index) const;

    /// The node at exactly this address, point and all.
    [[nodiscard]] std::optional<size_t> find(const domain::FtnAddress& address) const;

    /// The node that answers for this address: the node itself, or — for a point
    /// no nodelist here lists — the node it hangs off.
    ///
    /// A pointlist is a separate file and a great many systems never compile
    /// one, so a point is the address most likely to be missing and the boss is
    /// the one thing that is certainly known about it: a point is that node's
    /// own client, reached through it and standing where it stands. Falling back
    /// says something true and useful where an exact search would say nothing at
    /// all.
    ///
    /// Nothing falls back the other way, and nothing falls back from a node: an
    /// address with no point on it is either listed or it is not.
    [[nodiscard]] std::optional<size_t> findOrBoss(
        const domain::FtnAddress& address) const;

    /// Every node under as much of an address as was typed, as the half-open
    /// range of indexes `[first, last)`. `2` answers with the whole of zone 2,
    /// `2:382` with the net, and a full address with the one node or with
    /// nothing.
    [[nodiscard]] std::pair<size_t, size_t> findRange(const AddressPrefix& prefix) const;

    /// What order `findBySysop` answers in.
    enum class SysopOrder {
        /// The order the nodelist itself is in, which is the order a list of
        /// nodes is read in and the order a browser shows them in.
        Address,
        /// The closest match first: the whole name before the name it begins,
        /// a name it begins before a surname inside it, and a surname before
        /// three letters out of the middle of a word. Names of equal closeness
        /// stand shortest first — the query is then more of the name — and
        /// after that in address order, so that the answer never depends on
        /// which of two equals a sort happened to see first.
        ///
        /// It is what the compose screen asks for: somebody who typed a name
        /// wants the node of the person they named, not the first node in
        /// address order that happens to hold those letters.
        Relevance,
    };

    /// Every node whose sysop's name holds `query` anywhere in it. The query is
    /// folded the way the names are, so `nick andre`, `Nick_Andre` and `ANDRE`
    /// all find the same node, and `ndr` finds it too.
    ///
    /// `limit` of zero is no limit, and where there is one it is applied after
    /// the order — the best `limit` of them, not the first `limit` found. An
    /// empty query answers with nothing rather than with everything: it is what
    /// a search field holds before anything has been typed into it.
    [[nodiscard]] std::vector<size_t> findBySysop(
        std::string_view query, size_t limit = 0,
        SysopOrder order = SysopOrder::Address) const;

private:
    [[nodiscard]] uint64_t keyAt(size_t index) const;
    [[nodiscard]] uint32_t recordOffsetAt(size_t index) const;
    /// The first index whose key is not below `key`.
    [[nodiscard]] size_t lowerBound(uint64_t key) const;

    std::vector<unsigned char> data_;
    std::vector<SourceState> sources_;
    size_t nodeCount_{0};
    size_t nameCount_{0};
    uint32_t recordsOffset_{0};
    uint32_t recordsSize_{0};
    uint32_t addressIndexOffset_{0};
    uint32_t nameIndexOffset_{0};
    uint32_t namePoolOffset_{0};
    uint32_t namePoolSize_{0};
    std::time_t builtAt_{0};
    std::string path_;
};

}  // namespace amberedit::nodelist
