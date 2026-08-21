#include "echolist/echolist_area_source.hpp"

#include <utility>
#include <vector>

#include "echolist/echolist_db.hpp"

namespace amberedit::echolist {

EcholistAreaSource::EcholistAreaSource(std::unique_ptr<ports::IAreaConfigSource> inner,
                                       std::string dbPath,
                                       config::DescriptionPriority priority)
    : inner_(std::move(inner)), dbPath_(std::move(dbPath)), priority_(priority) {}

Result<std::vector<domain::AreaConfig>> EcholistAreaSource::loadAreas() {
    auto loaded = inner_->loadAreas();
    if (!loaded) return tl::make_unexpected(loaded.error());
    std::vector<domain::AreaConfig> areas = std::move(*loaded);
    if (dbPath_.empty()) return areas;

    // Opening it is the whole of what can go wrong here, and going wrong means
    // the areas keep the descriptions they came with. A compiled echolist that
    // is missing or was written by another version of the format is what a start
    // compiles again; a start that could not compile it has already said so.
    auto db = EcholistDb::open(dbPath_);
    if (!db || db->empty()) return areas;

    for (auto& area : areas) {
        // Only a description that says something counts, on either side: the
        // preferred one steps aside for the other where it is empty, so an echo
        // the tosser config says nothing about is described by the echolist
        // whichever way round the setting stands.
        if (priority_ == config::DescriptionPriority::Area && !area.description.empty()) {
            continue;
        }
        if (const auto found = db->descriptionOf(area.tag)) area.description = *found;
    }
    return areas;
}

std::unique_ptr<ports::IAreaConfigSource> withEcholistDescriptions(
    std::unique_ptr<ports::IAreaConfigSource> inner, const config::AppConfig& cfg) {
    if (cfg.echolistDbPath.empty()) return inner;
    return std::make_unique<EcholistAreaSource>(std::move(inner), cfg.echolistDbPath,
                                                cfg.areaDescriptionPriority);
}

}  // namespace amberedit::echolist
