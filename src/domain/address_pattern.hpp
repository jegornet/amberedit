#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "domain/ftn_address.hpp"

namespace amberedit::domain {

/// An FTN address in which a component may be a wildcard: "192:*",
/// "2:382/736.*". Used to say which of our AKAs a message to a given address
/// is written from.
///
/// A `*` standing at the end of the text covers that component and every one
/// after it, so "192:*" is the whole of zone 192 and "2:382/736.*" is a node
/// with its points. A pattern that simply stops names a point of zero:
/// "2:382/736" is that node and not its points.
///
/// Four dimensions and no more: a "@domain" is refused rather than accepted and
/// ignored. Nothing AmberEdit reads carries one — an FTS-0001 header has no field
/// for it and the INTL/FMPT/TOPT kludges are 4D — so a pattern naming a domain
/// could only ever fail to match, which is the least useful way to be wrong.
struct AddressPattern {
    /// nullopt means "any value". A point of zero is a value like another:
    /// the pattern "2:382/736" matches the node alone.
    std::optional<uint16_t> zone;
    std::optional<uint16_t> net;
    std::optional<uint16_t> node;
    std::optional<uint16_t> point;

    [[nodiscard]] bool matches(const FtnAddress& addr) const;

    /// How much of an address the pattern pins down: the components it states,
    /// left to right, up to the first wildcard. 0 to 4.
    ///
    /// This is what decides between two patterns that both match — the one
    /// that says more about the address wins — so it has to count from the
    /// left: a zone is a wider thing than a node whatever else is stated.
    [[nodiscard]] int depth() const;

    /// The pattern written out again. Canonical rather than verbatim: a tail of
    /// wildcards collapses back into one `*` and a point of zero is left off.
    [[nodiscard]] std::string toString() const;

    /// Returns nullopt if the text is not a pattern at all.
    static std::optional<AddressPattern> parse(std::string_view text);
};

}  // namespace amberedit::domain
