#pragma once

#include <string>
#include <vector>

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
class AreasBbsParser final : public ports::IAreaConfigSource {
public:
    explicit AreasBbsParser(std::string path);

    [[nodiscard]] Result<std::vector<domain::AreaConfig>> loadAreas() override;

    /// Parsing from a string — the entry point for tests.
    static std::vector<domain::AreaConfig> parseText(const std::string& content);

private:
    std::string path_;
};

}  // namespace amberedit::config
