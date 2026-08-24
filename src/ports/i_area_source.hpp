#pragma once

#include <vector>

#include "domain/area.hpp"
#include "support/error.hpp"

namespace amberedit::ports {

/// Source of the area list — a tosser config in one format or another.
class IAreaConfigSource {
public:
    virtual ~IAreaConfigSource() = default;

    /// Reads and parses the config, or says why the file is unavailable;
    /// individual malformed lines are skipped silently.
    [[nodiscard]] virtual tl::expected<std::vector<domain::AreaConfig>, ErrorPtr>
    loadAreas() = 0;
};

}  // namespace amberedit::ports
