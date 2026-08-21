#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "app/area_manager.hpp"
#include "config/text_util.hpp"

namespace amberedit::ui {

/// Whether `text` begins with `prefix`, ignoring case for ASCII only.
///
/// The same rule the rest of the program compares names by: a Cyrillic tag
/// matches only when it is spelled the same way, which is the safe way round —
/// the alternative would need a locale, and the tags are ASCII in practice.
[[nodiscard]] inline bool startsWithIgnoreCase(std::string_view text,
                                               std::string_view prefix) {
    if (prefix.size() > text.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (config::text::asciiLower(text[i]) != config::text::asciiLower(prefix[i]))
            return false;
    }
    return true;
}

/// Which area a quick-search query points at: the first one in list order
/// whose tag begins with it.
///
/// nullopt when nothing matches, and for an empty query too — an empty query
/// means the search is not up, and it should leave the cursor where the user
/// put it rather than pulling it back to the top.
[[nodiscard]] inline std::optional<int> findAreaByPrefix(
    const std::vector<app::AreaEntry>& areas, std::string_view query) {
    if (query.empty()) return std::nullopt;
    for (size_t i = 0; i < areas.size(); ++i) {
        if (startsWithIgnoreCase(areas[i].config.tag, query))
            return static_cast<int>(i);
    }
    return std::nullopt;
}

}  // namespace amberedit::ui
