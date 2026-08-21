#pragma once

#include <vector>

#include "domain/area.hpp"

namespace amberedit::ports {

/// Source of the area list — a tosser config in one format or another.
class IAreaConfigSource {
public:
    virtual ~IAreaConfigSource() = default;

    /// Reads and parses the config. Throws std::runtime_error if the file is
    /// unavailable; individual malformed lines are skipped silently.
    virtual std::vector<domain::AreaConfig> loadAreas() = 0;
};

}  // namespace amberedit::ports
