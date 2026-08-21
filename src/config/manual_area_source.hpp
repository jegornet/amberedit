#pragma once

#include <memory>
#include <string>
#include <vector>

#include "config/app_config.hpp"
#include "ports/i_area_source.hpp"

namespace amberedit::config {

/// The area list of a config that declares areas of its own: what the tosser's
/// config holds, and then the `area ... endarea` blocks.
///
/// A wrapper round the tosser's source rather than a second source beside it:
/// `AreaManager` knows one `IAreaConfigSource`, and joining two lists is the
/// business of whoever joins them. The tosser's source may be null, which is a
/// config that names no tosser at all — then the list is the blocks alone.
///
/// The tosser's areas come first and the blocks after them, each in the order
/// they were written. That order is only ever seen with `arealist_sort ""`;
/// anything else sorts the whole list anyway.
class ManualAreaSource final : public ports::IAreaConfigSource {
public:
    ManualAreaSource(std::vector<ManualArea> areas,
                     std::unique_ptr<ports::IAreaConfigSource> tosser);

    /// Throws std::runtime_error when a block declares a tag the tosser config
    /// declares too. The check is here and not where the config is read: the
    /// tosser's config has not been opened by then, and two answers to "where is
    /// the base for this tag" is not something to pick a winner for.
    [[nodiscard]] Result<std::vector<domain::AreaConfig>> loadAreas() override;

private:
    std::vector<ManualArea> areas_;
    std::unique_ptr<ports::IAreaConfigSource> tosser_;
};

}  // namespace amberedit::config
