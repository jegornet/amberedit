#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace amberedit::domain {

/// An FTN address: zone:net/node.point[@domain].
struct FtnAddress {
    uint16_t zone{0};
    uint16_t net{0};
    uint16_t node{0};
    uint16_t point{0};
    std::string domain;

    [[nodiscard]] bool isValid() const { return zone != 0 || net != 0 || node != 0; }

    [[nodiscard]] std::string toString() const {
        std::string s =
            std::to_string(zone) + ':' + std::to_string(net) + '/' + std::to_string(node);
        if (point != 0) s += '.' + std::to_string(point);
        if (!domain.empty()) s += '@' + domain;
        return s;
    }

    /// Whether the two name the same node, the domain left out of it. What a
    /// message base holds is always 4D, so this is how an address out of one is
    /// compared with an address out of a config, which may be written 5D.
    [[nodiscard]] bool same4D(const FtnAddress& other) const {
        return zone == other.zone && net == other.net && node == other.node &&
               point == other.point;
    }

    friend bool operator==(const FtnAddress& a, const FtnAddress& b) {
        return a.zone == b.zone && a.net == b.net && a.node == b.node &&
               a.point == b.point && a.domain == b.domain;
    }

    /// Parses "2:5020/9999.1@fidonet". Missing parts default to zero.
    /// Returns nullopt if the string does not look like an address at all.
    static std::optional<FtnAddress> parse(std::string_view text);
};

}  // namespace amberedit::domain
