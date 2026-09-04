#pragma once

#include <string>
#include <vector>

#include "config/path_map.hpp"
#include "ports/i_area_source.hpp"

namespace amberedit::config {

/// Parser for the line-based areas.bbs:
///
///   $/var/spool/ftn/mail/ru.linux ru.linux 2:5020/9999.1 2:5020/715
///   !/var/spool/ftn/mail/715.pvt  715.pvt  2:5020/715
///   /var/spool/ftn/mail/ru.today  ru.today 2:5020/715
///   P     su.general               2:5020/715
///
/// The path field is `[prefix]path`, where the prefix names the base type:
/// `$` for Squish, `!` for JAM, no prefix for Fido *.msg. A field of just `P`
/// marks a passthrough area (no base on disk). Lines starting with `;` are
/// comments.
///
/// The path, once the prefix is off it, comes back through the `map_path` rules
/// the parser was built with.
class AreasBbsParser final : public ports::IAreaConfigSource {
public:
    explicit AreasBbsParser(std::string path, PathMap paths = {});

    [[nodiscard]] tl::expected<std::vector<domain::AreaConfig>, ErrorPtr> loadAreas()
        override;

    /// Parsing from a string — the entry point for tests.
    static std::vector<domain::AreaConfig> parseText(const std::string& content,
                                                     const PathMap& paths = {});

private:
    std::string path_;
    PathMap paths_;
};

}  // namespace amberedit::config
