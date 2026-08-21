#include "config/manual_area_source.hpp"

#include <algorithm>
#include <utility>

#include "config/text_util.hpp"

namespace amberedit::config {

using domain::AreaConfig;

ManualAreaSource::ManualAreaSource(std::vector<ManualArea> areas,
                                   std::unique_ptr<ports::IAreaConfigSource> tosser)
    : areas_(std::move(areas)), tosser_(std::move(tosser)) {}

Result<std::vector<AreaConfig>> ManualAreaSource::loadAreas() {
    // An unreadable tosser config comes back out of here exactly as it did
    // before there were blocks: it is the thing the config points at, and a list
    // silently short of every echo would be worse than a startup that stops.
    std::vector<AreaConfig> areas;
    if (tosser_) {
        auto loaded = tosser_->loadAreas();
        if (!loaded) return tl::make_unexpected(loaded.error());
        areas = std::move(*loaded);
    }

    for (const auto& manual : areas_) {
        const auto clash =
            std::find_if(areas.begin(), areas.end(), [&manual](const AreaConfig& area) {
                return text::iequals(area.tag, manual.area.tag);
            });
        if (clash != areas.end()) {
            return failure(
                "area '" + manual.area.tag + "' declared at line " +
                std::to_string(manual.line) +
                " of the config is declared by the tosser config as well — take one "
                "of the two out");
        }
        areas.push_back(manual.area);
    }
    return areas;
}

}  // namespace amberedit::config
